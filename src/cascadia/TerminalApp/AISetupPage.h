// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "AISetupPage.g.h"
#include "AIToolManager.h"

namespace winrt::TerminalApp::implementation
{
    struct AISetupPage : AISetupPageT<AISetupPage>
    {
        AISetupPage();

        // XAML event handlers (must be public)
        void _ToolSelectionChanged(const winrt::Windows::Foundation::IInspectable& sender,
                                   const winrt::Windows::UI::Xaml::Controls::SelectionChangedEventArgs& e);
        void _InstallClicked(const winrt::Windows::Foundation::IInspectable& sender,
                             const winrt::Windows::UI::Xaml::RoutedEventArgs& e);
        void _SaveKeyClicked(const winrt::Windows::Foundation::IInspectable& sender,
                             const winrt::Windows::UI::Xaml::RoutedEventArgs& e);

    private:
        ClickTerminal::AITool _selectedTool{ ClickTerminal::AITool::Claude };

        void _UpdateStatus();
        void _AppendLog(const winrt::hstring& text);
        winrt::fire_and_forget _RunInstall(std::wstring cmd);
        void _OnInstallComplete(bool success);

        static bool             _CheckInstalled(ClickTerminal::AITool tool);
        static std::wstring     _ExeName(ClickTerminal::AITool tool);
        static std::wstring     _InstallCmd(ClickTerminal::AITool tool);
        static ClickTerminal::AuthType _AuthType(ClickTerminal::AITool tool);
        static winrt::hstring   _OAuthInstructions(ClickTerminal::AITool tool);
        static winrt::hstring   _CredentialName(ClickTerminal::AITool tool);
    };
}

namespace winrt::TerminalApp::factory_implementation
{
    BASIC_FACTORY(AISetupPage);
}
