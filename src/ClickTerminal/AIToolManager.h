#pragma once

#include "ProjectManager.h"
#include <string>
#include <vector>
#include <optional>
#include <functional>
#include <unordered_map>
#include <windows.h>

namespace ClickTerminal
{
    enum class AITool : uint8_t
    {
        Claude = 0,
        Codex  = 1,
        Gemini = 2,
    };

    enum class AuthType : uint8_t
    {
        OAuth  = 0,
        ApiKey = 1,
        None   = 2,
    };

    enum class ClaudePermissionMode : uint8_t
    {
        Default           = 0,
        AcceptEdits       = 1,
        BypassPermissions = 2,
        Plan              = 3,
        Auto              = 4,
    };

    enum class CodexApprovalMode : uint8_t
    {
        Suggest  = 0,
        AutoEdit = 1,
        FullAuto = 2,
    };

    struct AIToolConfig
    {
        AITool       Tool;
        bool         Installed{ false };
        std::wstring ExecutablePath;
        std::wstring Version;
        AuthType     Auth{ AuthType::None };
        std::wstring AuthConfigPath;
        uint32_t     ContextWindowTokens{ 0 };
        bool         ContextMeterEnabled{ false };

        struct
        {
            std::wstring         Model{ L"claude-sonnet-4-6" };
            ClaudePermissionMode PermissionMode{ ClaudePermissionMode::Default };
            bool                 DangerouslySkipPermissions{ false };
        } ClaudeDefaults;

        struct
        {
            std::wstring      Model{ L"gpt-5.4" };
            CodexApprovalMode ApprovalMode{ CodexApprovalMode::Suggest };
        } CodexDefaults;

        struct
        {
            std::wstring Model{ L"gemini-2.5-pro" };
        } GeminiDefaults;
    };

    struct LaunchCommand
    {
        std::wstring              Executable;
        std::vector<std::wstring> Args;
        std::wstring              WorkingDirectory;
        std::vector<std::pair<std::wstring, std::wstring>> Env;

        std::wstring ToCommandLine() const;
    };

    struct ContextUsage
    {
        uint32_t UsedTokens{ 0 };
        uint32_t TotalTokens{ 0 };

        float FillRatio() const noexcept
        {
            if (TotalTokens == 0) return 0.0f;
            return static_cast<float>(UsedTokens) / static_cast<float>(TotalTokens);
        }

        // 0-9: which segment is the boundary (10 segments = 10% each)
        uint8_t FilledSegments() const noexcept
        {
            return static_cast<uint8_t>(FillRatio() * 10.0f);
        }
    };

    class AIToolManager
    {
    public:
        AIToolManager();

        AIToolManager(const AIToolManager&)            = delete;
        AIToolManager& operator=(const AIToolManager&) = delete;

        // Detection
        void DetectInstalled();
        bool RefreshTool(AITool tool);

        std::optional<AIToolConfig> GetToolConfig(AITool tool) const;
        std::vector<AIToolConfig>   GetAllInstalledTools() const;
        bool                        IsInstalled(AITool tool) const;

        // Build the full CLI invocation for a project.
        // dangerousSkipEnabled AND project config must both be true for flag to apply.
        LaunchCommand GetLaunchCommand(
            const Project& project,
            AITool         tool,
            bool           dangerousSkipEnabled) const;

        // NOTE: session lifecycle now lives in TerminalPage (per-pane CTuxPaneSession
        // tracking, CTuxIntegration.cpp). /exit is sent via TermControl.SendInput.

        // Parse Claude's stream-json output for token usage
        std::optional<ContextUsage> ParseContextUsageLine(
            AITool             tool,
            const std::string& outputLine) const;

        std::function<void(AITool, bool /*installed*/)> OnToolDetectionChanged;

    private:
        std::unordered_map<uint8_t, AIToolConfig> _toolConfigs;

        LaunchCommand BuildClaudeCommand(const Project& project, bool dangerousSkipEnabled) const;
        LaunchCommand BuildCodexCommand(const Project& project, bool dangerousSkipEnabled) const;
        LaunchCommand BuildGeminiCommand(const Project& project, bool dangerousSkipEnabled) const;

        static std::vector<std::wstring> GetClaudeSearchPaths();
        static std::vector<std::wstring> GetCodexSearchPaths();
        static std::vector<std::wstring> GetGeminiSearchPaths();
        static std::wstring              ProbeVersion(const std::wstring& exePath);

        std::wstring GetClaudePermissionModeFlag(ClaudePermissionMode mode) const;
        std::wstring GetCodexApprovalModeFlag(CodexApprovalMode mode) const;
    };

} // namespace ClickTerminal
