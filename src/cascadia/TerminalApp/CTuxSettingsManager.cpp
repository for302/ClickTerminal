// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "CTuxSettings.h"
#include <json/json.h>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <ShlObj.h>
#include <combaseapi.h>
#include <winrt/Windows.Storage.h>

namespace ClickTerminal
{
    // -----------------------------------------------------------------------
    // String helpers
    // -----------------------------------------------------------------------
    static std::wstring NarrowToWide(const std::string& s)
    {
        if (s.empty()) return {};
        int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
        std::wstring r(len - 1, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, r.data(), len);
        return r;
    }

    static std::string WideToNarrow(const std::wstring& ws)
    {
        if (ws.empty()) return {};
        int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string r(len - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, r.data(), len, nullptr, nullptr);
        return r;
    }

    // -----------------------------------------------------------------------
    // Built-in theme definitions
    // -----------------------------------------------------------------------
    std::vector<CTuxTheme> GetBuiltInThemes()
    {
        return {
            {
                L"CTux Dark", true, false,
                { L"#252535", L"#1A1A2A", L"#CDD6F4", L"#7C7F93",
                  L"#1A1A2A", L"#2A2A3A", L"#1F1F2F", L"CTux Dark" }
            },
            {
                L"CTux Light", true, true,
                { L"#C4A882", L"#B5956A", L"#2C1D0E", L"#7A5C3A",
                  L"#B5956A", L"#F5EDDF", L"#EDE0CD", L"CTux Light" }
            },
            {
                L"Mocha", true, false,
                { L"#2D1F16", L"#1E1208", L"#F5DEB3", L"#A08060",
                  L"#1E1208", L"#352515", L"#2A1A0D", L"CTux Mocha" }
            },
            {
                L"Ocean", true, false,
                { L"#1A2535", L"#0F1A28", L"#B8D4F0", L"#6890B8",
                  L"#0F1A28", L"#1E2D40", L"#162233", L"Campbell" }
            },
            {
                L"Forest", true, false,
                { L"#1A2518", L"#0F1A0D", L"#C8E6C0", L"#6A9060",
                  L"#0F1A0D", L"#1E2D1C", L"#162314", L"Solarized Dark" }
            },
        };
    }

    // -----------------------------------------------------------------------
    // CTuxSettings helpers
    // -----------------------------------------------------------------------
    CTuxTheme CTuxSettings::GetActiveTheme() const
    {
        auto builtins = GetBuiltInThemes();
        for (auto& t : builtins)
            if (t.Name == SelectedThemeName) return t;
        for (auto& t : CustomThemes)
            if (t.Name == SelectedThemeName) return t;
        return builtins[0]; // fallback: CTux Dark
    }

    std::wstring CTuxSettings::ThemeString() const
    {
        return GetActiveTheme().IsLightMode ? L"light" : L"dark";
    }

    // -----------------------------------------------------------------------
    // Persist helpers for custom themes
    // -----------------------------------------------------------------------
    static CTuxThemeColors ParseThemeColors(const Json::Value& c)
    {
        CTuxThemeColors tc;
        tc.SidebarBg        = NarrowToWide(c.get("sidebarBg",        "").asString());
        tc.SidebarHeaderBg  = NarrowToWide(c.get("sidebarHeaderBg",  "").asString());
        tc.SidebarText      = NarrowToWide(c.get("sidebarText",      "").asString());
        tc.SidebarTextMuted = NarrowToWide(c.get("sidebarTextMuted", "").asString());
        tc.TabBarBg         = NarrowToWide(c.get("tabBarBg",         "").asString());
        tc.DialogBg         = NarrowToWide(c.get("dialogBg",         "").asString());
        tc.DialogNavBg      = NarrowToWide(c.get("dialogNavBg",      "").asString());
        tc.TerminalScheme   = NarrowToWide(c.get("terminalScheme",   "").asString());
        return tc;
    }

    static Json::Value SerializeThemeColors(const CTuxThemeColors& c)
    {
        Json::Value v;
        v["sidebarBg"]        = WideToNarrow(c.SidebarBg);
        v["sidebarHeaderBg"]  = WideToNarrow(c.SidebarHeaderBg);
        v["sidebarText"]      = WideToNarrow(c.SidebarText);
        v["sidebarTextMuted"] = WideToNarrow(c.SidebarTextMuted);
        v["tabBarBg"]         = WideToNarrow(c.TabBarBg);
        v["dialogBg"]         = WideToNarrow(c.DialogBg);
        v["dialogNavBg"]      = WideToNarrow(c.DialogNavBg);
        v["terminalScheme"]   = WideToNarrow(c.TerminalScheme);
        return v;
    }

    // -----------------------------------------------------------------------
    // Schema versioning
    //   1 — original (theme: "dark"/"light", projectsConfigPath)
    //   2 — added selectedThemeName, customThemes, pluginsFolder, plugins toggles
    //   3 — current: explicit schemaVersion field, color fallbacks in custom themes
    // -----------------------------------------------------------------------
    static constexpr int kCurrentSchemaVersion = 3;

    // Fill any empty color fields in a custom theme with CTux Dark defaults
    static void FillMissingColors(CTuxThemeColors& c)
    {
        const CTuxThemeColors def{}; // default-initialized = CTux Dark values
        if (c.SidebarBg.empty())        c.SidebarBg        = def.SidebarBg;
        if (c.SidebarHeaderBg.empty())  c.SidebarHeaderBg  = def.SidebarHeaderBg;
        if (c.SidebarText.empty())      c.SidebarText      = def.SidebarText;
        if (c.SidebarTextMuted.empty()) c.SidebarTextMuted = def.SidebarTextMuted;
        if (c.TabBarBg.empty())         c.TabBarBg         = def.TabBarBg;
        if (c.DialogBg.empty())         c.DialogBg         = def.DialogBg;
        if (c.DialogNavBg.empty())      c.DialogNavBg      = def.DialogNavBg;
        if (c.TerminalScheme.empty())   c.TerminalScheme   = def.TerminalScheme;
    }

    // -----------------------------------------------------------------------
    // Load / Save
    // -----------------------------------------------------------------------
    std::wstring CTuxSettings::GetDefaultDir()
    {
        // Use USERPROFILE to get real (non-virtualized) AppData path.
        // FOLDERID_LocalAppData under MSIX returns a package-container path
        // that is wiped on clean reinstall, losing user settings.
        wchar_t profile[MAX_PATH] = {};
        DWORD len = GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
        if (len > 0 && len < MAX_PATH)
        {
            std::wstring p = std::wstring(profile, len) + L"\\AppData\\Local\\ClickTerminal";
            std::filesystem::create_directories(p);
            return p;
        }
        // Fallback: virtualized path (may not survive reinstall)
        wchar_t* raw = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw)))
        {
            CoTaskMemFree(raw);
            return L"";
        }
        std::wstring p(raw);
        CoTaskMemFree(raw);
        p += L"\\ClickTerminal";
        std::filesystem::create_directories(p);
        return p;
    }

    CTuxSettings CTuxSettings::Load()
    {
        CTuxSettings s;

        auto path = GetDefaultDir() + L"\\ctux-settings.json";
        std::ifstream file(path);
        if (!file.is_open()) return s; // first run — return defaults

        std::ostringstream ss;
        ss << file.rdbuf();

        Json::Value root;
        Json::CharReaderBuilder builder;
        std::string errs;
        std::istringstream iss(ss.str());
        if (!Json::parseFromStream(builder, iss, &root, &errs)) return s;

        int fileVersion = root.get("schemaVersion", 1).asInt();

        // ── v1 → v2: "theme" string became "selectedThemeName" ──────────────
        s.ProjectsConfigPath = NarrowToWide(root.get("projectsConfigPath", "").asString());
        s.PluginsFolder      = NarrowToWide(root.get("pluginsFolder", "").asString());
        s.GitPluginEnabled   = root.get("gitPluginEnabled", false).asBool();
        s.PortPluginEnabled  = root.get("portPluginEnabled", false).asBool();

        if (root.isMember("selectedThemeName"))
            s.SelectedThemeName = NarrowToWide(root["selectedThemeName"].asString());
        else
        {
            // v1 migration: map "dark"/"light" string to theme name
            auto legacy = root.get("theme", "dark").asString();
            s.SelectedThemeName = (legacy == "light") ? L"CTux Light" : L"CTux Dark";
        }

        // ── v2 → v3: fill missing color fields in custom themes ──────────────
        const auto& ct = root["customThemes"];
        if (ct.isArray())
        {
            for (const auto& entry : ct)
            {
                CTuxTheme t;
                t.Name        = NarrowToWide(entry.get("name", "").asString());
                t.IsBuiltIn   = false;
                t.IsLightMode = entry.get("isLightMode", false).asBool();
                t.Colors      = ParseThemeColors(entry["colors"]);
                FillMissingColors(t.Colors); // ensure no empty fields
                if (!t.Name.empty()) s.CustomThemes.push_back(std::move(t));
            }
        }

        // If file was an older version, save immediately to upgrade it
        if (fileVersion < kCurrentSchemaVersion)
            s.Save();

        return s;
    }

    void CTuxSettings::Save() const
    {
        Json::Value root;
        root["schemaVersion"]      = kCurrentSchemaVersion;
        root["projectsConfigPath"] = WideToNarrow(ProjectsConfigPath);
        root["selectedThemeName"]  = WideToNarrow(SelectedThemeName);
        root["pluginsFolder"]      = WideToNarrow(PluginsFolder);
        root["gitPluginEnabled"]   = GitPluginEnabled;
        root["portPluginEnabled"]  = PortPluginEnabled;

        Json::Value ct(Json::arrayValue);
        for (const auto& t : CustomThemes)
        {
            Json::Value entry;
            entry["name"]        = WideToNarrow(t.Name);
            entry["isLightMode"] = t.IsLightMode;
            entry["colors"]      = SerializeThemeColors(t.Colors);
            ct.append(entry);
        }
        root["customThemes"] = ct;

        Json::StreamWriterBuilder wb;
        wb["indentation"] = "    ";
        std::string json = Json::writeString(wb, root);

        auto path = GetDefaultDir() + L"\\ctux-settings.json";
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (f.is_open()) f.write(json.c_str(), static_cast<std::streamsize>(json.size()));
    }

    // -----------------------------------------------------------------------
    // ApplyTerminalTheme — injects CTux schemes + updates settings.json default
    // -----------------------------------------------------------------------
    void CTuxSettings::ApplyTerminalTheme() const
    {
        std::wstring settingsPath;
        try
        {
            auto folder = winrt::Windows::Storage::ApplicationData::Current().LocalFolder();
            settingsPath = std::wstring{ folder.Path() } + L"\\settings.json";
        }
        catch (...) { return; }

        if (!std::filesystem::exists(settingsPath)) return;

        std::ifstream fin(settingsPath);
        if (!fin.is_open()) return;
        std::string content((std::istreambuf_iterator<char>(fin)), {});
        fin.close();

        Json::Value root;
        Json::CharReaderBuilder rb;
        rb["allowComments"] = true;
        std::string errs;
        std::istringstream iss(content);
        if (!Json::parseFromStream(rb, iss, &root, &errs)) return;

        auto activeTheme = GetActiveTheme();
        const std::string schemeName = WideToNarrow(activeTheme.Colors.TerminalScheme);

        auto makeDark = []() {
            Json::Value s;
            s["name"]                = "CTux Dark";
            s["background"]          = "#1E1E2E";
            s["foreground"]          = "#CDD6F4";
            s["black"]               = "#45475A";
            s["red"]                 = "#F38BA8";
            s["green"]               = "#A6E3A1";
            s["yellow"]              = "#F9E2AF";
            s["blue"]                = "#89B4FA";
            s["purple"]              = "#CBA6F7";
            s["cyan"]                = "#89DCEB";
            s["white"]               = "#BAC2DE";
            s["brightBlack"]         = "#585B70";
            s["brightRed"]           = "#F38BA8";
            s["brightGreen"]         = "#A6E3A1";
            s["brightYellow"]        = "#F9E2AF";
            s["brightBlue"]          = "#89B4FA";
            s["brightPurple"]        = "#CBA6F7";
            s["brightCyan"]          = "#89DCEB";
            s["brightWhite"]         = "#A6ADC8";
            s["cursorColor"]         = "#F5E0DC";
            s["selectionBackground"] = "#585B70";
            return s;
        };
        auto makeLight = []() {
            Json::Value s;
            s["name"]                = "CTux Light";
            s["background"]          = "#FDF6EC";
            s["foreground"]          = "#2C1D10";
            s["black"]               = "#2C1D10";
            s["red"]                 = "#B84630";
            s["green"]               = "#2D7B3C";
            s["yellow"]              = "#A06020";
            s["blue"]                = "#1A5FA0";
            s["purple"]              = "#7B2D8B";
            s["cyan"]                = "#1A7B7B";
            s["white"]               = "#F0E8D8";
            s["brightBlack"]         = "#5C4030";
            s["brightRed"]           = "#D85040";
            s["brightGreen"]         = "#3D9B4C";
            s["brightYellow"]        = "#C08030";
            s["brightBlue"]          = "#2A7FD0";
            s["brightPurple"]        = "#9B3DAB";
            s["brightCyan"]          = "#2A9B9B";
            s["brightWhite"]         = "#FFFFFF";
            s["cursorColor"]         = "#DA7756";
            s["selectionBackground"] = "#E8D5C4";
            return s;
        };

        auto makeMocha = []() {
            Json::Value s;
            s["name"]                = "CTux Mocha";
            s["background"]          = "#2D1F16";
            s["foreground"]          = "#F5DEB3";
            s["black"]               = "#3D2B1E";
            s["brightBlack"]         = "#5C4033";
            s["red"]                 = "#C44535";
            s["brightRed"]           = "#E05040";
            s["green"]               = "#7C9C4F";
            s["brightGreen"]         = "#96B865";
            s["yellow"]              = "#C8943A";
            s["brightYellow"]        = "#E8A84A";
            s["blue"]                = "#6A8FAE";
            s["brightBlue"]          = "#84AAC8";
            s["purple"]              = "#9A6B8F";
            s["brightPurple"]        = "#B585A9";
            s["cyan"]                = "#6A9A8A";
            s["brightCyan"]          = "#84B4A4";
            s["white"]               = "#D4B896";
            s["brightWhite"]         = "#EDD8BA";
            s["cursorColor"]         = "#F5DEB3";
            s["selectionBackground"] = "#5C4033";
            return s;
        };

        auto& schemes = root["schemes"];
        bool hasDark = false, hasLight = false, hasMocha = false;
        if (schemes.isArray())
        {
            for (const auto& s : schemes)
            {
                auto n = s.get("name", "").asString();
                if (n == "CTux Dark")  hasDark  = true;
                if (n == "CTux Light") hasLight = true;
                if (n == "CTux Mocha") hasMocha = true;
            }
        }
        if (!hasDark)  schemes.append(makeDark());
        if (!hasLight) schemes.append(makeLight());
        if (!hasMocha) schemes.append(makeMocha());

        root["profiles"]["defaults"]["colorScheme"] = schemeName;
        if (schemeName == "CTux Mocha")
        {
            root["profiles"]["defaults"]["font"]["face"]  = "Cascadia Mono";
            root["profiles"]["defaults"]["font"]["style"] = "Italic";
        }

        Json::StreamWriterBuilder wb;
        wb["indentation"] = "    ";
        std::string newJson = Json::writeString(wb, root);

        std::wstring tmpPath = settingsPath + L".tmp";
        {
            std::ofstream fout(tmpPath, std::ios::binary | std::ios::trunc);
            if (!fout.is_open()) return;
            fout.write(newJson.c_str(), static_cast<std::streamsize>(newJson.size()));
        }
        MoveFileExW(tmpPath.c_str(), settingsPath.c_str(), MOVEFILE_REPLACE_EXISTING);
    }

} // namespace ClickTerminal
