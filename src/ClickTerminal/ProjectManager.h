#pragma once

#include <string>
#include <vector>
#include <optional>
#include <functional>
#include <unordered_map>
#include <windows.h>

namespace ClickTerminal
{
    enum class ProjectType : uint8_t
    {
        Web        = 0,
        App        = 1,
        AIWorkflow = 2,
    };

    struct ProjectUrl
    {
        std::wstring Label;
        std::wstring Url;
        bool         OpenOnStart{ false };
    };

    struct AIToolInvocationConfig
    {
        std::wstring StartCommand;   // custom launch command for this specific tool
        bool         Enabled{ false };

        // Claude
        bool         DangerouslySkipPermissions{ false };
        std::wstring PermissionMode{ L"default" }; // default|acceptEdits|bypassPermissions|plan
        std::wstring Model{ L"claude-sonnet-4-6" };
        std::wstring McpConfigPath;
        std::wstring AppendSystemPrompt;
        std::vector<std::wstring> AddDirs;

        // Codex
        std::wstring ApprovalMode{ L"suggest" }; // suggest|auto-edit|full-auto

        // Gemini
        std::wstring GeminiModel{ L"gemini-2.5-pro" };
    };

    struct ProjectAIConfig
    {
        std::wstring           DefaultTool{ L"claude" };
        bool                   AutoStartAI{ false };
        AIToolInvocationConfig Claude;
        AIToolInvocationConfig Codex;
        AIToolInvocationConfig Gemini;
    };

    struct Project
    {
        std::wstring              Id;          // "proj-{uuid}"
        std::wstring              Name;
        ProjectType               Type{ ProjectType::Web };
        std::wstring              FolderPath;

        // Terminal
        std::wstring              ColorScheme;
        std::wstring              StartupCommand;
        std::wstring              Icon;
        std::wstring              TerminalProfileGuid;

        // Metadata
        std::vector<std::wstring> Tags;
        std::wstring              CreatedAt;
        std::wstring              LastOpenedAt;

        // Network
        std::vector<int32_t>      Ports;
        std::vector<ProjectUrl>   Urls;

        // Named URLs (shown as quick-launch buttons)
        std::wstring              DevUrl;
        std::wstring              DeployUrl;
        std::wstring              GitUrl;

        // AI
        ProjectAIConfig           AIConfig;

        // Env vars ($env:VAR tokens expanded at launch)
        std::vector<std::pair<std::wstring, std::wstring>> Env;
    };

    enum class ProjectManagerError : uint8_t
    {
        None         = 0,
        FileNotFound = 1,
        ParseError   = 2,
        WriteError   = 3,
        InvalidId    = 4,
        Duplicate    = 5,
    };

    template<typename T>
    struct ProjectResult
    {
        T                   Value{};
        ProjectManagerError Error{ ProjectManagerError::None };
        std::wstring        ErrorMessage;

        bool Ok() const noexcept { return Error == ProjectManagerError::None; }
    };

    class ProjectManager
    {
    public:
        explicit ProjectManager(std::wstring configPath = L"");

        ProjectManager(const ProjectManager&)            = delete;
        ProjectManager& operator=(const ProjectManager&) = delete;
        ProjectManager(ProjectManager&&)                 = default;

        // Load / save
        ProjectResult<bool>    LoadProjects();
        ProjectResult<bool>    SaveProjects();
        ProjectResult<bool>    ReloadProjects();

        // CRUD
        ProjectResult<Project> AddProject(Project project);
        ProjectResult<Project> UpdateProject(const Project& project);
        ProjectResult<bool>    RemoveProject(const std::wstring& id);

        // Queries
        std::optional<Project>      GetProjectById(const std::wstring& id) const;
        std::optional<Project>      GetProjectByPath(const std::wstring& folderPath) const;
        std::vector<Project>        GetAllProjects() const;
        std::vector<Project>        GetProjectsByType(ProjectType type) const;
        ProjectResult<bool>         TouchProject(const std::wstring& id);

        // Auto-detect projects from search paths (looks for .git, package.json, *.sln)
        std::vector<Project>        AutoDetectProjects(
                                        const std::vector<std::wstring>& searchPaths,
                                        uint32_t maxDepth = 2) const;

        // Expand $env:VAR tokens in project env map
        std::vector<std::pair<std::wstring, std::wstring>>
                                    ResolveEnv(const Project& project) const;

        // Fired after successful SaveProjects()
        std::function<void()> OnProjectsChanged;

    private:
        std::wstring         _configPath;
        std::vector<Project> _projects;
        bool                 _loaded{ false };

        bool        DeserializeFromJson(const std::string& json);
        std::string SerializeToJson() const;

        // Writes to .tmp then MoveFileEx(REPLACE_EXISTING) to avoid partial writes
        bool WriteFileAtomic(const std::wstring& path, const std::string& content);

        static std::wstring GenerateProjectId();    // CoCreateGuid -> "proj-{uuid}"
        static std::wstring GetDefaultConfigPath(); // same folder as settings.json
    };

} // namespace ClickTerminal
