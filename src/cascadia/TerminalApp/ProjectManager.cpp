// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "ProjectManager.h"
#include <json/json.h>
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

    ProjectManager::ProjectManager(std::wstring configPath)
        : _configPath(std::move(configPath))
    {
    }

    ProjectResult<bool> ProjectManager::LoadProjects()
    {
        if (_configPath.empty())
        {
            _configPath = GetDefaultConfigPath();
        }

        if (!std::filesystem::exists(_configPath))
        {
            _projects.clear();
            _loaded = true;
            return { true };
        }

        std::ifstream file(_configPath);
        if (!file.is_open())
        {
            return { false, ProjectManagerError::FileNotFound, L"Cannot open config file" };
        }

        std::ostringstream ss;
        ss << file.rdbuf();

        if (!DeserializeFromJson(ss.str()))
        {
            return { false, ProjectManagerError::ParseError, L"Failed to parse clickterminal.json" };
        }

        _loaded = true;
        return { true };
    }

    ProjectResult<bool> ProjectManager::SaveProjects()
    {
        if (_configPath.empty())
        {
            _configPath = GetDefaultConfigPath();
        }

        // Ensure parent directory exists
        std::filesystem::create_directories(std::filesystem::path(_configPath).parent_path());

        const auto json = SerializeToJson();
        if (!WriteFileAtomic(_configPath, json))
        {
            return { false, ProjectManagerError::WriteError, L"Failed to write config file" };
        }

        if (OnProjectsChanged)
        {
            OnProjectsChanged();
        }
        return { true };
    }

    ProjectResult<bool> ProjectManager::ReloadProjects()
    {
        _loaded = false;
        return LoadProjects();
    }

    ProjectResult<Project> ProjectManager::AddProject(Project project)
    {
        if (project.Id.empty())
        {
            project.Id = GenerateProjectId();
        }

        for (const auto& p : _projects)
        {
            if (p.Id == project.Id)
            {
                return { {}, ProjectManagerError::Duplicate, L"Project ID already exists" };
            }
        }

        _projects.push_back(project);
        return { project };
    }

    ProjectResult<Project> ProjectManager::UpdateProject(const Project& project)
    {
        for (auto& p : _projects)
        {
            if (p.Id == project.Id)
            {
                p = project;
                return { project };
            }
        }
        return { {}, ProjectManagerError::InvalidId, L"Project not found" };
    }

    ProjectResult<bool> ProjectManager::RemoveProject(const std::wstring& id)
    {
        auto it = std::find_if(_projects.begin(), _projects.end(),
                               [&id](const Project& p) { return p.Id == id; });
        if (it == _projects.end())
        {
            return { false, ProjectManagerError::InvalidId, L"Project not found" };
        }
        _projects.erase(it);
        return { true };
    }

    std::optional<Project> ProjectManager::GetProjectById(const std::wstring& id) const
    {
        for (const auto& p : _projects)
        {
            if (p.Id == id) return p;
        }
        return std::nullopt;
    }

    std::optional<Project> ProjectManager::GetProjectByPath(const std::wstring& folderPath) const
    {
        for (const auto& p : _projects)
        {
            if (_wcsicmp(p.FolderPath.c_str(), folderPath.c_str()) == 0) return p;
        }
        return std::nullopt;
    }

    std::vector<Project> ProjectManager::GetAllProjects() const
    {
        return _projects;
    }

    std::vector<Project> ProjectManager::GetProjectsByType(ProjectType type) const
    {
        std::vector<Project> result;
        for (const auto& p : _projects)
        {
            if (p.Type == type) result.push_back(p);
        }
        return result;
    }

    ProjectResult<bool> ProjectManager::TouchProject(const std::wstring& id)
    {
        for (auto& p : _projects)
        {
            if (p.Id == id)
            {
                // Update lastOpenedAt to current ISO 8601 time
                SYSTEMTIME st;
                GetSystemTime(&st);
                wchar_t buf[32];
                swprintf_s(buf, L"%04d-%02d-%02dT%02d:%02d:%02dZ",
                           st.wYear, st.wMonth, st.wDay,
                           st.wHour, st.wMinute, st.wSecond);
                p.LastOpenedAt = buf;
                return { true };
            }
        }
        return { false, ProjectManagerError::InvalidId, L"Project not found" };
    }

    std::vector<Project> ProjectManager::AutoDetectProjects(
        const std::vector<std::wstring>& searchPaths,
        uint32_t maxDepth) const
    {
        std::vector<Project> detected;

        std::function<void(const std::filesystem::path&, uint32_t)> scan;
        scan = [&](const std::filesystem::path& dir, uint32_t depth) {
            if (depth > maxDepth) return;
            if (!std::filesystem::is_directory(dir)) return;

            bool hasGit = std::filesystem::exists(dir / L".git");
            bool hasPkg = std::filesystem::exists(dir / L"package.json");
            bool hasSln = false;
            for (const auto& entry : std::filesystem::directory_iterator(dir))
            {
                if (entry.path().extension() == L".sln")
                {
                    hasSln = true;
                    break;
                }
            }

            if (hasGit || hasPkg || hasSln)
            {
                Project p;
                p.Id = GenerateProjectId();
                p.Name = dir.filename().wstring();
                p.FolderPath = dir.wstring();
                p.Type = hasPkg ? ProjectType::Web : ProjectType::App;
                detected.push_back(p);
                return; // Don't recurse into detected project directories
            }

            if (depth < maxDepth)
            {
                for (const auto& entry : std::filesystem::directory_iterator(dir))
                {
                    if (entry.is_directory())
                    {
                        scan(entry.path(), depth + 1);
                    }
                }
            }
        };

        for (const auto& path : searchPaths)
        {
            scan(path, 0);
        }

        return detected;
    }

    std::vector<std::pair<std::wstring, std::wstring>> ProjectManager::ResolveEnv(const Project& project) const
    {
        std::vector<std::pair<std::wstring, std::wstring>> resolved;
        for (const auto& [key, val] : project.Env)
        {
            std::wstring expandedVal(32768, L'\0');
            DWORD n = ExpandEnvironmentStringsW(val.c_str(), expandedVal.data(), 32768);
            expandedVal.resize(n > 0 ? n - 1 : 0);
            resolved.emplace_back(key, expandedVal);
        }
        return resolved;
    }

    bool ProjectManager::DeserializeFromJson(const std::string& json)
    {
        Json::Value root;
        Json::CharReaderBuilder builder;
        std::string errs;
        std::istringstream ss(json);

        if (!Json::parseFromStream(builder, ss, &root, &errs))
        {
            return false;
        }

        _projects.clear();

        const auto& projects = root["projects"];
        for (const auto& pj : projects)
        {
            Project project;
            project.Id          = NarrowToWide(pj["id"].asString());
            project.Name        = NarrowToWide(pj["name"].asString());
            project.FolderPath  = NarrowToWide(pj["folderPath"].asString());
            project.ColorScheme = NarrowToWide(pj.get("colorScheme", "").asString());
            project.StartupCommand = NarrowToWide(pj.get("startupCommand", "").asString());
            project.Icon        = NarrowToWide(pj.get("icon", "").asString());
            project.CreatedAt   = NarrowToWide(pj.get("createdAt", "").asString());
            project.LastOpenedAt = NarrowToWide(pj.get("lastOpenedAt", "").asString());
            project.TerminalProfileGuid = NarrowToWide(pj.get("terminalProfile", "").asString());

            const auto& typeStr = pj.get("type", "web").asString();
            if (typeStr == "app") project.Type = ProjectType::App;
            else if (typeStr == "ai-workflow") project.Type = ProjectType::AIWorkflow;
            else project.Type = ProjectType::Web;

            for (const auto& tag : pj["tags"])
            {
                project.Tags.push_back(NarrowToWide(tag.asString()));
            }

            for (const auto& port : pj["ports"])
            {
                project.Ports.push_back(port.asInt());
            }

            project.DevUrl    = NarrowToWide(pj.get("devUrl",    "").asString());
            project.DeployUrl = NarrowToWide(pj.get("deployUrl", "").asString());
            project.GitUrl    = NarrowToWide(pj.get("gitUrl",    "").asString());

            for (const auto& url : pj["urls"])
            {
                ProjectUrl u;
                u.Label = NarrowToWide(url["label"].asString());
                u.Url   = NarrowToWide(url["url"].asString());
                u.OpenOnStart = url.get("openOnStart", false).asBool();
                project.Urls.push_back(u);
            }

            // AI config
            const auto& ai = pj["aiTool"];
            if (!ai.isNull())
            {
                project.AIConfig.DefaultTool = NarrowToWide(ai.get("defaultTool", "claude").asString());
                project.AIConfig.AutoStartAI = ai.get("autoStartAI", false).asBool();

                const auto& claude = ai["claude"];
                if (!claude.isNull())
                {
                    project.AIConfig.Claude.StartCommand = NarrowToWide(claude.get("startCommand", "").asString());
                    project.AIConfig.Claude.Enabled = claude.get("enabled", false).asBool();
                    project.AIConfig.Claude.DangerouslySkipPermissions = claude.get("dangerouslySkipPermissions", false).asBool();
                    project.AIConfig.Claude.PermissionMode = NarrowToWide(claude.get("permissionMode", "default").asString());
                    project.AIConfig.Claude.Model = NarrowToWide(claude.get("model", "claude-sonnet-4-6").asString());
                    project.AIConfig.Claude.McpConfigPath = NarrowToWide(claude.get("mcpConfigPath", "").asString());
                    project.AIConfig.Claude.AppendSystemPrompt = NarrowToWide(claude.get("appendSystemPrompt", "").asString());
                    for (const auto& dir : claude["addDirs"])
                        project.AIConfig.Claude.AddDirs.push_back(NarrowToWide(dir.asString()));
                }

                const auto& codex = ai["codex"];
                if (!codex.isNull())
                    project.AIConfig.Codex.StartCommand = NarrowToWide(codex.get("startCommand", "").asString());

                const auto& gemini = ai["gemini"];
                if (!gemini.isNull())
                    project.AIConfig.Gemini.StartCommand = NarrowToWide(gemini.get("startCommand", "").asString());
            }

            // Env vars
            const auto& env = pj["env"];
            if (env.isObject())
            {
                for (const auto& key : env.getMemberNames())
                {
                    project.Env.emplace_back(NarrowToWide(key), NarrowToWide(env[key].asString()));
                }
            }

            _projects.push_back(std::move(project));
        }

        return true;
    }

    std::string ProjectManager::SerializeToJson() const
    {
        Json::Value root;
        root["$schema"] = "https://clickterminal.dev/schemas/clickterminal-schema.json";
        root["$version"] = 1;

        Json::Value projects(Json::arrayValue);
        for (const auto& p : _projects)
        {
            Json::Value pj;
            pj["id"]        = WideToNarrow(p.Id);
            pj["name"]      = WideToNarrow(p.Name);
            pj["folderPath"] = WideToNarrow(p.FolderPath);

            const char* typeStr = "web";
            if (p.Type == ProjectType::App) typeStr = "app";
            else if (p.Type == ProjectType::AIWorkflow) typeStr = "ai-workflow";
            pj["type"] = typeStr;

            if (!p.DevUrl.empty())    pj["devUrl"]    = WideToNarrow(p.DevUrl);
            if (!p.DeployUrl.empty()) pj["deployUrl"] = WideToNarrow(p.DeployUrl);
            if (!p.GitUrl.empty())    pj["gitUrl"]    = WideToNarrow(p.GitUrl);

            if (!p.ColorScheme.empty()) pj["colorScheme"] = WideToNarrow(p.ColorScheme);
            if (!p.StartupCommand.empty()) pj["startupCommand"] = WideToNarrow(p.StartupCommand);
            if (!p.Icon.empty()) pj["icon"] = WideToNarrow(p.Icon);
            if (!p.CreatedAt.empty()) pj["createdAt"] = WideToNarrow(p.CreatedAt);
            if (!p.LastOpenedAt.empty()) pj["lastOpenedAt"] = WideToNarrow(p.LastOpenedAt);

            Json::Value tags(Json::arrayValue);
            for (const auto& tag : p.Tags) tags.append(WideToNarrow(tag));
            pj["tags"] = tags;

            Json::Value ports(Json::arrayValue);
            for (auto port : p.Ports) ports.append(port);
            pj["ports"] = ports;

            Json::Value urls(Json::arrayValue);
            for (const auto& url : p.Urls)
            {
                Json::Value u;
                u["label"] = WideToNarrow(url.Label);
                u["url"]   = WideToNarrow(url.Url);
                u["openOnStart"] = url.OpenOnStart;
                urls.append(u);
            }
            pj["urls"] = urls;

            // AI config
            if (!p.AIConfig.DefaultTool.empty())
            {
                Json::Value ai;
                ai["defaultTool"] = WideToNarrow(p.AIConfig.DefaultTool);
                if (p.AIConfig.AutoStartAI) ai["autoStartAI"] = true;

                Json::Value claude;
                if (!p.AIConfig.Claude.StartCommand.empty()) claude["startCommand"] = WideToNarrow(p.AIConfig.Claude.StartCommand);
                claude["enabled"]    = p.AIConfig.Claude.Enabled;
                claude["dangerouslySkipPermissions"] = p.AIConfig.Claude.DangerouslySkipPermissions;
                claude["permissionMode"] = WideToNarrow(p.AIConfig.Claude.PermissionMode);
                if (!p.AIConfig.Claude.Model.empty()) claude["model"] = WideToNarrow(p.AIConfig.Claude.Model);
                if (!p.AIConfig.Claude.McpConfigPath.empty()) claude["mcpConfigPath"] = WideToNarrow(p.AIConfig.Claude.McpConfigPath);
                if (!p.AIConfig.Claude.AppendSystemPrompt.empty()) claude["appendSystemPrompt"] = WideToNarrow(p.AIConfig.Claude.AppendSystemPrompt);
                Json::Value addDirs(Json::arrayValue);
                for (const auto& dir : p.AIConfig.Claude.AddDirs) addDirs.append(WideToNarrow(dir));
                claude["addDirs"] = addDirs;
                ai["claude"] = claude;

                if (!p.AIConfig.Codex.StartCommand.empty())
                {
                    Json::Value codex;
                    codex["startCommand"] = WideToNarrow(p.AIConfig.Codex.StartCommand);
                    ai["codex"] = codex;
                }
                if (!p.AIConfig.Gemini.StartCommand.empty())
                {
                    Json::Value gemini;
                    gemini["startCommand"] = WideToNarrow(p.AIConfig.Gemini.StartCommand);
                    ai["gemini"] = gemini;
                }

                pj["aiTool"] = ai;
            }

            // Env
            if (!p.Env.empty())
            {
                Json::Value env;
                for (const auto& [k, v] : p.Env)
                {
                    env[WideToNarrow(k)] = WideToNarrow(v);
                }
                pj["env"] = env;
            }

            projects.append(pj);
        }

        root["projects"] = projects;

        Json::StreamWriterBuilder writerBuilder;
        writerBuilder["indentation"] = "    ";
        return Json::writeString(writerBuilder, root);
    }

    bool ProjectManager::WriteFileAtomic(const std::wstring& path, const std::string& content)
    {
        const std::wstring tempPath = path + L".tmp";

        std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) return false;

        file.write(content.c_str(), static_cast<std::streamsize>(content.size()));
        file.close();

        return MoveFileExW(tempPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
    }

    std::wstring ProjectManager::GenerateProjectId()
    {
        GUID guid{};
        CoCreateGuid(&guid);

        wchar_t buf[48];
        swprintf_s(buf, L"proj-%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                   guid.Data1, guid.Data2, guid.Data3,
                   guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
                   guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
        return buf;
    }

    std::wstring ProjectManager::GetDefaultConfigPath()
    {
        // Use USERPROFILE to get real (non-virtualized) AppData path.
        // FOLDERID_LocalAppData under MSIX is package-virtualized and wiped on reinstall.
        wchar_t profile[MAX_PATH] = {};
        DWORD len = GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
        if (len > 0 && len < MAX_PATH)
        {
            std::wstring dir = std::wstring(profile, len) + L"\\AppData\\Local\\ClickTerminal";
            std::filesystem::create_directories(dir);
            return dir + L"\\clickterminal.json";
        }
        // Fallback: virtualized path
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
        return path + L"\\clickterminal.json";
    }

} // namespace ClickTerminal
