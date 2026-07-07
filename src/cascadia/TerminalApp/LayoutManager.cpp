// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "LayoutManager.h"
#include <json/json.h>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <ShlObj.h>
#include <combaseapi.h>

namespace ClickTerminal
{
    static std::wstring NarrowToWide(const std::string& s)
    {
        if (s.empty()) return {};
        const int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
        std::wstring result(static_cast<size_t>(len) - 1, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, result.data(), len);
        return result;
    }

    static std::string WideToNarrow(const std::wstring& ws)
    {
        if (ws.empty()) return {};
        const int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string result(static_cast<size_t>(len) - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, result.data(), len, nullptr, nullptr);
        return result;
    }

    LayoutManager::LayoutManager(std::wstring configPath)
        : _configPath(std::move(configPath))
    {
    }

    bool LayoutManager::LoadLayouts()
    {
        if (_configPath.empty())
            _configPath = GetDefaultConfigPath();

        if (!std::filesystem::exists(_configPath))
        {
            _layouts.clear();
            _loaded = true;
            return true;
        }

        std::ifstream file(_configPath);
        if (!file.is_open())
            return false;

        std::ostringstream ss;
        ss << file.rdbuf();

        if (!DeserializeFromJson(ss.str()))
            return false;

        _loaded = true;
        return true;
    }

    bool LayoutManager::SaveLayouts()
    {
        if (_configPath.empty())
            _configPath = GetDefaultConfigPath();

        std::filesystem::create_directories(std::filesystem::path(_configPath).parent_path());

        if (!WriteFileAtomic(_configPath, SerializeToJson()))
            return false;

        if (OnLayoutsChanged)
            OnLayoutsChanged();

        return true;
    }

    Layout LayoutManager::AddLayout(Layout layout)
    {
        if (layout.Id.empty())
            layout.Id = GenerateLayoutId();

        // Timestamp
        SYSTEMTIME st;
        GetSystemTime(&st);
        wchar_t buf[32];
        swprintf_s(buf, L"%04d-%02d-%02dT%02d:%02d:%02dZ",
                   st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        if (layout.CreatedAt.empty())
            layout.CreatedAt = buf;
        layout.LastUsedAt = buf;

        // New layouts go last: Order = max existing Order + 1
        uint32_t maxOrder = 0;
        for (const auto& l : _layouts)
            maxOrder = (std::max)(maxOrder, l.Order);
        layout.Order = _layouts.empty() ? 0 : maxOrder + 1;

        _layouts.push_back(layout);
        return layout;
    }

    bool LayoutManager::UpdateLayout(const Layout& layout)
    {
        for (auto& l : _layouts)
        {
            if (l.Id == layout.Id)
            {
                l = layout;
                return true;
            }
        }
        return false;
    }

    bool LayoutManager::RemoveLayout(const std::wstring& id)
    {
        auto it = std::find_if(_layouts.begin(), _layouts.end(),
                               [&id](const Layout& l) { return l.Id == id; });
        if (it == _layouts.end())
            return false;
        _layouts.erase(it);
        return true;
    }

    std::optional<Layout> LayoutManager::GetLayoutById(const std::wstring& id) const
    {
        for (const auto& l : _layouts)
            if (l.Id == id) return l;
        return std::nullopt;
    }

    std::vector<Layout> LayoutManager::GetAllLayouts() const
    {
        // Return sorted by Order ascending (user-defined display order)
        auto sorted = _layouts;
        std::stable_sort(sorted.begin(), sorted.end(), [](const Layout& a, const Layout& b) {
            return a.Order < b.Order;
        });
        return sorted;
    }

    void LayoutManager::ReorderLayouts(const std::vector<std::wstring>& orderedIds)
    {
        uint32_t next = 0;

        // 1) Assign Order following the given id sequence
        std::vector<bool> assigned(_layouts.size(), false);
        for (const auto& id : orderedIds)
        {
            for (size_t i = 0; i < _layouts.size(); i++)
            {
                if (!assigned[i] && _layouts[i].Id == id)
                {
                    _layouts[i].Order = next++;
                    assigned[i] = true;
                    break;
                }
            }
        }

        // 2) Layouts not in the list keep their relative order and go after
        std::vector<size_t> rest;
        for (size_t i = 0; i < _layouts.size(); i++)
            if (!assigned[i])
                rest.push_back(i);
        std::stable_sort(rest.begin(), rest.end(), [this](size_t a, size_t b) {
            return _layouts[a].Order < _layouts[b].Order;
        });
        for (size_t i : rest)
            _layouts[i].Order = next++;
    }

    bool LayoutManager::TouchLayout(const std::wstring& id)
    {
        for (auto& l : _layouts)
        {
            if (l.Id == id)
            {
                SYSTEMTIME st;
                GetSystemTime(&st);
                wchar_t buf[32];
                swprintf_s(buf, L"%04d-%02d-%02dT%02d:%02d:%02dZ",
                           st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
                l.LastUsedAt = buf;
                return true;
            }
        }
        return false;
    }

    bool LayoutManager::DeserializeFromJson(const std::string& json)
    {
        Json::Value root;
        Json::CharReaderBuilder builder;
        std::string errs;
        std::istringstream ss(json);

        if (!Json::parseFromStream(builder, ss, &root, &errs))
            return false;

        _layouts.clear();

        bool anyOrderKey = false;

        const auto& layouts = root["layouts"];
        for (const auto& lj : layouts)
        {
            Layout layout;
            layout.Id         = NarrowToWide(lj["id"].asString());
            layout.Name       = NarrowToWide(lj.get("name", "").asString());
            layout.Rows       = static_cast<uint32_t>(lj.get("rows", 1).asInt());
            layout.Cols       = static_cast<uint32_t>(lj.get("cols", 1).asInt());
            layout.CreatedAt  = NarrowToWide(lj.get("createdAt", "").asString());
            layout.LastUsedAt = NarrowToWide(lj.get("lastUsedAt", "").asString());
            if (lj.isMember("order"))
                anyOrderKey = true;
            layout.Order      = static_cast<uint32_t>(lj.get("order", 0).asUInt());

            // Clamp to valid range
            layout.Rows = std::clamp(layout.Rows, 1u, 3u);
            layout.Cols = std::clamp(layout.Cols, 1u, 3u);

            for (const auto& sj : lj["slots"])
            {
                LayoutSlot slot;
                slot.Row       = static_cast<uint32_t>(sj.get("row", 0).asInt());
                slot.Col       = static_cast<uint32_t>(sj.get("col", 0).asInt());
                slot.ProjectId = NarrowToWide(sj.get("projectId", "").asString());
                layout.Slots.push_back(slot);
            }

            _layouts.push_back(std::move(layout));
        }

        // Migration: files written before the "order" field have no order key at all.
        // Assign Order 0..N by LastUsedAt descending (most recent first).
        // It will be persisted on the next SaveLayouts().
        if (!anyOrderKey && !_layouts.empty())
        {
            std::vector<size_t> idx(_layouts.size());
            for (size_t i = 0; i < idx.size(); i++)
                idx[i] = i;
            std::stable_sort(idx.begin(), idx.end(), [this](size_t a, size_t b) {
                return _layouts[a].LastUsedAt > _layouts[b].LastUsedAt;
            });
            uint32_t next = 0;
            for (size_t i : idx)
                _layouts[i].Order = next++;
        }

        return true;
    }

    std::string LayoutManager::SerializeToJson() const
    {
        Json::Value root;
        root["$version"] = 1;

        Json::Value layouts(Json::arrayValue);
        for (const auto& l : _layouts)
        {
            Json::Value lj;
            lj["id"]         = WideToNarrow(l.Id);
            lj["name"]       = WideToNarrow(l.Name);
            lj["rows"]       = static_cast<int>(l.Rows);
            lj["cols"]       = static_cast<int>(l.Cols);
            lj["createdAt"]  = WideToNarrow(l.CreatedAt);
            lj["lastUsedAt"] = WideToNarrow(l.LastUsedAt);
            lj["order"]      = static_cast<Json::UInt>(l.Order);

            Json::Value slots(Json::arrayValue);
            for (const auto& s : l.Slots)
            {
                Json::Value sj;
                sj["row"]       = static_cast<int>(s.Row);
                sj["col"]       = static_cast<int>(s.Col);
                sj["projectId"] = WideToNarrow(s.ProjectId);
                slots.append(sj);
            }
            lj["slots"] = slots;
            layouts.append(lj);
        }

        root["layouts"] = layouts;

        Json::StreamWriterBuilder writerBuilder;
        writerBuilder["indentation"] = "    ";
        return Json::writeString(writerBuilder, root);
    }

    bool LayoutManager::WriteFileAtomic(const std::wstring& path, const std::string& content)
    {
        const std::wstring tempPath = path + L".tmp";
        std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) return false;
        file.write(content.c_str(), static_cast<std::streamsize>(content.size()));
        file.close();
        return MoveFileExW(tempPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
    }

    std::wstring LayoutManager::GenerateLayoutId()
    {
        GUID guid{};
        CoCreateGuid(&guid);
        wchar_t buf[48];
        swprintf_s(buf, L"layout-%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                   guid.Data1, guid.Data2, guid.Data3,
                   guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
                   guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
        return buf;
    }

    std::wstring LayoutManager::GetDefaultConfigPath()
    {
        wchar_t profile[MAX_PATH] = {};
        DWORD len = GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
        if (len > 0 && len < MAX_PATH)
        {
            std::wstring dir = std::wstring(profile, len) + L"\\AppData\\Local\\ClickTerminal";
            std::filesystem::create_directories(dir);
            return dir + L"\\layouts.json";
        }
        wchar_t* appDataRaw = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &appDataRaw)))
        {
            CoTaskMemFree(appDataRaw);
            return L"";
        }
        std::wstring path(appDataRaw);
        CoTaskMemFree(appDataRaw);
        path += L"\\ClickTerminal";
        std::filesystem::create_directories(path);
        return path + L"\\layouts.json";
    }

} // namespace ClickTerminal
