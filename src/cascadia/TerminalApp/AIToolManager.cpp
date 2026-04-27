// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "AIToolManager.h"
#include <filesystem>
#include <json/json.h>

namespace ClickTerminal
{
    AIToolManager::AIToolManager()
    {
        DetectInstalled();
    }

    void AIToolManager::DetectInstalled()
    {
        RefreshTool(AITool::Claude);
        RefreshTool(AITool::Codex);
        RefreshTool(AITool::Gemini);
    }

    static std::wstring FindInPath(const std::wstring& exeName)
    {
        // Check PATH via SearchPathW
        wchar_t buf[MAX_PATH] = {};
        wchar_t* filePart = nullptr;
        if (SearchPathW(nullptr, exeName.c_str(), L".cmd", MAX_PATH, buf, &filePart) ||
            SearchPathW(nullptr, exeName.c_str(), L".exe", MAX_PATH, buf, &filePart))
        {
            return buf;
        }
        return {};
    }

    static std::wstring ProbeVersionImpl(const std::wstring& exePath, const std::wstring& versionFlag)
    {
        if (exePath.empty()) return {};

        SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
        HANDLE hRead = INVALID_HANDLE_VALUE, hWrite = INVALID_HANDLE_VALUE;
        if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return {};

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.hStdOutput = hWrite;
        si.hStdError  = hWrite;
        si.wShowWindow = SW_HIDE;

        std::wstring cmd = L"\"" + exePath + L"\" " + versionFlag;
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE,
                            CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        {
            CloseHandle(hRead);
            CloseHandle(hWrite);
            return {};
        }

        CloseHandle(hWrite);
        WaitForSingleObject(pi.hProcess, 5000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        char outBuf[256] = {};
        DWORD bytesRead = 0;
        ReadFile(hRead, outBuf, sizeof(outBuf) - 1, &bytesRead, nullptr);
        CloseHandle(hRead);

        std::string out(outBuf, bytesRead);
        // Trim whitespace
        while (!out.empty() && (out.back() == '\r' || out.back() == '\n' || out.back() == ' '))
            out.pop_back();

        int len = MultiByteToWideChar(CP_UTF8, 0, out.c_str(), -1, nullptr, 0);
        std::wstring wout(static_cast<size_t>(len) - 1, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, out.c_str(), -1, wout.data(), len);
        return wout;
    }

    bool AIToolManager::RefreshTool(AITool tool)
    {
        AIToolConfig cfg;
        cfg.Tool = tool;

        switch (tool)
        {
        case AITool::Claude:
        {
            std::wstring exe = FindInPath(L"claude");
            cfg.ExecutablePath = exe;
            cfg.Installed = !exe.empty();
            cfg.Auth = AuthType::OAuth;
            cfg.ContextWindowTokens = 200000;
            cfg.ContextMeterEnabled = true;
            if (cfg.Installed)
            {
                cfg.Version = ProbeVersionImpl(exe, L"--version");
            }
            break;
        }
        case AITool::Codex:
        {
            std::wstring exe = FindInPath(L"codex");
            cfg.ExecutablePath = exe;
            cfg.Installed = !exe.empty();
            cfg.Auth = AuthType::OAuth;
            cfg.ContextWindowTokens = 128000;
            if (cfg.Installed)
            {
                cfg.Version = ProbeVersionImpl(exe, L"--version");
            }
            break;
        }
        case AITool::Gemini:
        {
            std::wstring exe = FindInPath(L"gemini");
            cfg.ExecutablePath = exe;
            cfg.Installed = !exe.empty();
            cfg.Auth = AuthType::ApiKey;
            cfg.ContextWindowTokens = 1000000;
            if (cfg.Installed)
            {
                cfg.Version = ProbeVersionImpl(exe, L"--version");
            }
            break;
        }
        }

        bool changed = (_toolConfigs.count(static_cast<uint8_t>(tool)) == 0) ||
                       (_toolConfigs[static_cast<uint8_t>(tool)].Installed != cfg.Installed);

        _toolConfigs[static_cast<uint8_t>(tool)] = cfg;

        if (changed && OnToolDetectionChanged)
        {
            OnToolDetectionChanged(tool, cfg.Installed);
        }

        return cfg.Installed;
    }

    std::optional<AIToolConfig> AIToolManager::GetToolConfig(AITool tool) const
    {
        auto it = _toolConfigs.find(static_cast<uint8_t>(tool));
        if (it == _toolConfigs.end()) return std::nullopt;
        return it->second;
    }

    std::vector<AIToolConfig> AIToolManager::GetAllInstalledTools() const
    {
        std::vector<AIToolConfig> result;
        for (const auto& [key, cfg] : _toolConfigs)
        {
            if (cfg.Installed) result.push_back(cfg);
        }
        return result;
    }

    bool AIToolManager::IsInstalled(AITool tool) const
    {
        auto it = _toolConfigs.find(static_cast<uint8_t>(tool));
        return it != _toolConfigs.end() && it->second.Installed;
    }

    LaunchCommand AIToolManager::GetLaunchCommand(
        const Project& project,
        AITool tool,
        bool dangerousSkipEnabled) const
    {
        switch (tool)
        {
        case AITool::Claude:  return BuildClaudeCommand(project, dangerousSkipEnabled);
        case AITool::Codex:   return BuildCodexCommand(project, dangerousSkipEnabled);
        case AITool::Gemini:  return BuildGeminiCommand(project, dangerousSkipEnabled);
        default:              return {};
        }
    }

    LaunchCommand AIToolManager::BuildClaudeCommand(const Project& project, bool dangerousSkipEnabled) const
    {
        auto it = _toolConfigs.find(static_cast<uint8_t>(AITool::Claude));
        const std::wstring exe = (it != _toolConfigs.end()) ? it->second.ExecutablePath : L"claude";

        LaunchCommand cmd;
        cmd.Executable = exe;
        cmd.WorkingDirectory = project.FolderPath;

        const auto& cfg = project.AIConfig.Claude;

        if (!cfg.Model.empty())
        {
            cmd.Args.push_back(L"--model");
            cmd.Args.push_back(cfg.Model);
        }

        if (!cfg.PermissionMode.empty() && cfg.PermissionMode != L"default")
        {
            cmd.Args.push_back(L"--permission-mode");
            cmd.Args.push_back(cfg.PermissionMode);
        }

        // Double-gate: both call-site AND project config must allow this
        if (dangerousSkipEnabled && cfg.DangerouslySkipPermissions)
        {
            cmd.Args.push_back(L"--dangerously-skip-permissions");
        }

        if (!cfg.McpConfigPath.empty())
        {
            cmd.Args.push_back(L"--mcp-config");
            cmd.Args.push_back(cfg.McpConfigPath);
        }

        if (!cfg.AppendSystemPrompt.empty())
        {
            cmd.Args.push_back(L"--append-system-prompt");
            cmd.Args.push_back(cfg.AppendSystemPrompt);
        }

        for (const auto& dir : cfg.AddDirs)
        {
            cmd.Args.push_back(L"--add-dir");
            cmd.Args.push_back(dir);
        }

        return cmd;
    }

    LaunchCommand AIToolManager::BuildCodexCommand(const Project& project, bool dangerousSkipEnabled) const
    {
        auto it = _toolConfigs.find(static_cast<uint8_t>(AITool::Codex));
        const std::wstring exe = (it != _toolConfigs.end()) ? it->second.ExecutablePath : L"codex";

        LaunchCommand cmd;
        cmd.Executable = exe;
        cmd.WorkingDirectory = project.FolderPath;

        const auto& cfg = project.AIConfig.Codex;
        if (!cfg.Model.empty())
        {
            cmd.Args.push_back(L"--model");
            cmd.Args.push_back(cfg.Model);
        }
        if (!cfg.ApprovalMode.empty())
        {
            cmd.Args.push_back(L"--approval-mode");
            cmd.Args.push_back(cfg.ApprovalMode);
        }

        (void)dangerousSkipEnabled;
        return cmd;
    }

    LaunchCommand AIToolManager::BuildGeminiCommand(const Project& project, bool /*dangerousSkipEnabled*/) const
    {
        auto it = _toolConfigs.find(static_cast<uint8_t>(AITool::Gemini));
        const std::wstring exe = (it != _toolConfigs.end()) ? it->second.ExecutablePath : L"gemini";

        LaunchCommand cmd;
        cmd.Executable = exe;
        cmd.WorkingDirectory = project.FolderPath;

        const auto& cfg = project.AIConfig.Gemini;
        if (!cfg.GeminiModel.empty())
        {
            cmd.Args.push_back(L"--model");
            cmd.Args.push_back(cfg.GeminiModel);
        }

        return cmd;
    }

    std::wstring LaunchCommand::ToCommandLine() const
    {
        std::wstring result = L"\"" + Executable + L"\"";
        for (const auto& arg : Args)
        {
            result += L" ";
            // Quote args that contain spaces
            if (arg.find(L' ') != std::wstring::npos)
            {
                result += L"\"" + arg + L"\"";
            }
            else
            {
                result += arg;
            }
        }
        return result;
    }

    void AIToolManager::RegisterSession(const std::wstring& sessionId, AITool tool, HANDLE processHandle)
    {
        SessionInfo info;
        info.ProcessHandle = processHandle;
        info.Tool = tool;
        _sessions[sessionId] = info;
    }

    void AIToolManager::UnregisterSession(const std::wstring& sessionId)
    {
        _sessions.erase(sessionId);
    }

    bool AIToolManager::SendExitCommand(const std::wstring& sessionId)
    {
        auto it = _sessions.find(sessionId);
        if (it == _sessions.end()) return false;

        const auto& session = it->second;
        if (session.StdinWrite == INVALID_HANDLE_VALUE) return false;

        const char exitCmd[] = "/exit\n";
        DWORD written = 0;
        return WriteFile(session.StdinWrite, exitCmd, sizeof(exitCmd) - 1, &written, nullptr) != 0;
    }

    std::optional<ContextUsage> AIToolManager::ParseContextUsageLine(
        AITool tool,
        const std::string& outputLine) const
    {
        if (tool != AITool::Claude) return std::nullopt;

        // Claude Code emits stream JSON lines. Look for usage fields:
        // {"type":"result","usage":{"input_tokens":1234,"output_tokens":56,...}}
        // or {"type":"assistant","message":{"usage":{"input_tokens":...}}}
        if (outputLine.find("\"input_tokens\"") == std::string::npos) return std::nullopt;

        Json::Value root;
        Json::CharReaderBuilder builder;
        std::string errs;
        std::istringstream ss(outputLine);

        if (!Json::parseFromStream(builder, ss, &root, &errs)) return std::nullopt;

        auto tryGetUsage = [&](const Json::Value& usage) -> std::optional<ContextUsage> {
            if (usage.isNull()) return std::nullopt;
            uint32_t input  = usage.get("input_tokens", 0).asUInt();
            uint32_t cache  = usage.get("cache_read_input_tokens", 0).asUInt();
            uint32_t output = usage.get("output_tokens", 0).asUInt();
            if (input + output == 0) return std::nullopt;

            ContextUsage u;
            u.UsedTokens  = input + cache + output;
            u.TotalTokens = 200000; // Claude's context window
            return u;
        };

        // Try top-level usage
        if (auto u = tryGetUsage(root["usage"])) return u;
        // Try message.usage
        if (auto u = tryGetUsage(root["message"]["usage"])) return u;

        return std::nullopt;
    }

} // namespace ClickTerminal
