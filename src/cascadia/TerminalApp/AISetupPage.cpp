// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "AISetupPage.h"
#include "AISetupPage.g.cpp"
#include <winrt/Windows.Security.Credentials.h>

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::UI;
using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Xaml::Controls;
using namespace winrt::Windows::UI::Xaml::Media;
using namespace winrt::Windows::Security::Credentials;

namespace winrt::TerminalApp::implementation
{
    AISetupPage::AISetupPage()
    {
        InitializeComponent();
        _UpdateStatus();
    }

    // ── Static helpers ──────────────────────────────────────────────────────

    std::wstring AISetupPage::_ExeName(ClickTerminal::AITool tool)
    {
        switch (tool)
        {
        case ClickTerminal::AITool::Claude: return L"claude";
        case ClickTerminal::AITool::Codex:  return L"codex";
        case ClickTerminal::AITool::Gemini: return L"gemini";
        default:                            return {};
        }
    }

    bool AISetupPage::_CheckInstalled(ClickTerminal::AITool tool)
    {
        std::wstring name = _ExeName(tool);
        if (name.empty()) return false;
        wchar_t buf[MAX_PATH] = {};
        wchar_t* filePart = nullptr;
        return SearchPathW(nullptr, name.c_str(), L".cmd", MAX_PATH, buf, &filePart) != 0 ||
               SearchPathW(nullptr, name.c_str(), L".exe", MAX_PATH, buf, &filePart) != 0;
    }

    std::wstring AISetupPage::_InstallCmd(ClickTerminal::AITool tool)
    {
        switch (tool)
        {
        case ClickTerminal::AITool::Claude: return L"npm install -g @anthropic-ai/claude-code";
        case ClickTerminal::AITool::Codex:  return L"npm install -g @openai/codex";
        case ClickTerminal::AITool::Gemini: return L"npm install -g @google/gemini-cli";
        default:                            return {};
        }
    }

    ClickTerminal::AuthType AISetupPage::_AuthType(ClickTerminal::AITool tool)
    {
        switch (tool)
        {
        case ClickTerminal::AITool::Claude: return ClickTerminal::AuthType::OAuth;
        case ClickTerminal::AITool::Codex:  return ClickTerminal::AuthType::OAuth;
        case ClickTerminal::AITool::Gemini: return ClickTerminal::AuthType::ApiKey;
        default:                            return ClickTerminal::AuthType::None;
        }
    }

    winrt::hstring AISetupPage::_OAuthInstructions(ClickTerminal::AITool tool)
    {
        switch (tool)
        {
        case ClickTerminal::AITool::Claude:
            return L"Open a terminal and run  claude auth  to sign in with your Anthropic account.";
        case ClickTerminal::AITool::Codex:
            return L"Open a terminal and run  codex auth  to sign in with your OpenAI account.";
        default:
            return {};
        }
    }

    winrt::hstring AISetupPage::_CredentialName(ClickTerminal::AITool tool)
    {
        switch (tool)
        {
        case ClickTerminal::AITool::Gemini: return L"ClickTerminal/Gemini";
        default:                             return {};
        }
    }

    // ── UI update ────────────────────────────────────────────────────────────

    void AISetupPage::_UpdateStatus()
    {
        bool installed = _CheckInstalled(_selectedTool);

        if (installed)
        {
            StatusText().Text(L"✓  Installed");
            Windows::UI::Color green{};
            green.A = 0xFF; green.R = 0x0F; green.G = 0x9B; green.B = 0x58;
            StatusText().Foreground(SolidColorBrush{ green });

            InstallButton().Visibility(Visibility::Collapsed);
            InstallCommandBorder().Visibility(Visibility::Collapsed);
            IsPrimaryButtonEnabled(true);

            AuthSection().Visibility(Visibility::Visible);
            if (_AuthType(_selectedTool) == ClickTerminal::AuthType::ApiKey)
            {
                OAuthText().Visibility(Visibility::Collapsed);
                ApiKeySection().Visibility(Visibility::Visible);
            }
            else
            {
                OAuthText().Text(_OAuthInstructions(_selectedTool));
                OAuthText().Visibility(Visibility::Visible);
                ApiKeySection().Visibility(Visibility::Collapsed);
            }
        }
        else
        {
            StatusText().Text(L"Not installed");
            Windows::UI::Color gray{};
            gray.A = 0xFF; gray.R = 0xA0; gray.G = 0xA0; gray.B = 0xA0;
            StatusText().Foreground(SolidColorBrush{ gray });

            InstallButton().Visibility(Visibility::Visible);
            InstallButton().IsEnabled(true);
            InstallCommandBorder().Visibility(Visibility::Visible);
            InstallCommandText().Text(winrt::hstring(_InstallCmd(_selectedTool)));
            IsPrimaryButtonEnabled(false);

            AuthSection().Visibility(Visibility::Collapsed);
        }
    }

    // ── Event handlers ───────────────────────────────────────────────────────

    void AISetupPage::_ToolSelectionChanged(
        const winrt::Windows::Foundation::IInspectable& /*sender*/,
        const winrt::Windows::UI::Xaml::Controls::SelectionChangedEventArgs& /*e*/)
    {
        switch (ToolComboBox().SelectedIndex())
        {
        case 0: _selectedTool = ClickTerminal::AITool::Claude; break;
        case 1: _selectedTool = ClickTerminal::AITool::Codex;  break;
        case 2: _selectedTool = ClickTerminal::AITool::Gemini; break;
        default: break;
        }

        LogTextBlock().Text(L"");
        LogScrollViewer().Visibility(Visibility::Collapsed);
        KeySavedText().Visibility(Visibility::Collapsed);
        _UpdateStatus();
    }

    void AISetupPage::_InstallClicked(
        const winrt::Windows::Foundation::IInspectable& /*sender*/,
        const winrt::Windows::UI::Xaml::RoutedEventArgs& /*e*/)
    {
        InstallButton().IsEnabled(false);
        LogTextBlock().Text(L"");
        LogScrollViewer().Visibility(Visibility::Visible);
        _RunInstall(_InstallCmd(_selectedTool));
    }

    void AISetupPage::_AppendLog(const winrt::hstring& text)
    {
        LogTextBlock().Text(LogTextBlock().Text() + text);
    }

    winrt::fire_and_forget AISetupPage::_RunInstall(std::wstring cmd)
    {
        auto lifetime = get_strong();

        HANDLE hRead = INVALID_HANDLE_VALUE, hWrite = INVALID_HANDLE_VALUE;
        SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
        if (!CreatePipe(&hRead, &hWrite, &sa, 0))
        {
            co_await winrt::resume_foreground(lifetime->Dispatcher());
            lifetime->_AppendLog(L"Failed to create pipe.\r\n");
            lifetime->_OnInstallComplete(false);
            co_return;
        }
        SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOW si{};
        si.cb         = sizeof(si);
        si.dwFlags    = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.hStdOutput = hWrite;
        si.hStdError  = hWrite;
        si.wShowWindow = SW_HIDE;

        std::wstring fullCmd = L"cmd.exe /c " + cmd;
        PROCESS_INFORMATION pi{};
        bool started = !!CreateProcessW(nullptr, fullCmd.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
        CloseHandle(hWrite);

        if (!started)
        {
            CloseHandle(hRead);
            co_await winrt::resume_foreground(lifetime->Dispatcher());
            lifetime->_AppendLog(L"Failed to start installer. Is Node.js installed?\r\n");
            lifetime->_OnInstallComplete(false);
            co_return;
        }

        co_await winrt::resume_background();

        char buf[1024];
        DWORD bytesRead = 0;
        while (ReadFile(hRead, buf, sizeof(buf) - 1, &bytesRead, nullptr) && bytesRead > 0)
        {
            buf[bytesRead] = '\0';
            // Try UTF-8 first, fall back to system ACP
            int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                           buf, static_cast<int>(bytesRead), nullptr, 0);
            if (wlen <= 0)
                wlen = MultiByteToWideChar(CP_ACP, 0, buf, static_cast<int>(bytesRead), nullptr, 0);

            std::wstring wline(static_cast<size_t>(wlen > 0 ? wlen : 0), L'\0');
            if (wlen > 0)
                MultiByteToWideChar(CP_UTF8, 0, buf, static_cast<int>(bytesRead), wline.data(), wlen);

            co_await winrt::resume_foreground(lifetime->Dispatcher());
            lifetime->_AppendLog(winrt::hstring(wline));
            co_await winrt::resume_background();
        }

        CloseHandle(hRead);

        DWORD exitCode = 1;
        WaitForSingleObject(pi.hProcess, INFINITE);
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        co_await winrt::resume_foreground(lifetime->Dispatcher());
        lifetime->_OnInstallComplete(exitCode == 0);
    }

    void AISetupPage::_OnInstallComplete(bool success)
    {
        if (success)
        {
            _AppendLog(L"\r\nInstallation complete.\r\n");
            _UpdateStatus();
        }
        else
        {
            _AppendLog(L"\r\nInstallation failed. Check the log above.\r\n");
            InstallButton().IsEnabled(true);
        }
    }

    void AISetupPage::_SaveKeyClicked(
        const winrt::Windows::Foundation::IInspectable& /*sender*/,
        const winrt::Windows::UI::Xaml::RoutedEventArgs& /*e*/)
    {
        auto key = ApiKeyBox().Password();
        if (key.empty()) return;

        try
        {
            PasswordVault vault;
            auto resource = _CredentialName(_selectedTool);
            auto cred = PasswordCredential(resource, L"apikey", key);
            vault.Add(cred);
            KeySavedText().Visibility(Visibility::Visible);
            IsPrimaryButtonEnabled(true);
        }
        catch (...) {}
    }

} // namespace winrt::TerminalApp::implementation
