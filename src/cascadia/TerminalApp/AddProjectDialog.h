// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "AddProjectDialog.g.h"

namespace winrt::TerminalApp::implementation
{
    struct AddProjectDialog : AddProjectDialogT<AddProjectDialog>
    {
        AddProjectDialog();

        winrt::hstring ProjectName();
        winrt::hstring FolderPath();
        winrt::hstring ProjectType();
        winrt::hstring DefaultAITool();
        winrt::hstring ClaudeStartCommand();
        winrt::hstring CodexStartCommand();
        winrt::hstring GeminiStartCommand();
        bool           AutoStartAI();
        winrt::hstring DevUrl();
        winrt::hstring DeployUrl();
        winrt::hstring GitUrl();

        void SetInitialValues(winrt::hstring const& name, winrt::hstring const& path,
                              winrt::hstring const& type, winrt::hstring const& aiTool,
                              winrt::hstring const& claudeCmd,
                              winrt::hstring const& codexCmd,
                              winrt::hstring const& geminiCmd,
                              bool autoStartAI,
                              winrt::hstring const& devUrl = {},
                              winrt::hstring const& deployUrl = {},
                              winrt::hstring const& gitUrl = {});

        // XAML event handlers (must be public)
        void _BrowseFolderClicked(const winrt::Windows::Foundation::IInspectable& sender,
                                  const winrt::Windows::UI::Xaml::RoutedEventArgs& e);
        void _AIToolSelectionChanged(const winrt::Windows::Foundation::IInspectable& sender,
                                     const winrt::Windows::UI::Xaml::Controls::SelectionChangedEventArgs& e);
        void _OnTextBoxGotFocus(const winrt::Windows::Foundation::IInspectable& sender,
                                const winrt::Windows::UI::Xaml::RoutedEventArgs& e);

    private:
        // Per-tool commands: index matches ComboBox (0=claude, 1=codex, 2=gemini, 3=none)
        std::wstring _toolCommands[4];
        int _previousToolIdx{ 0 };
        bool _pendingHangulToggle{ false };
        HWND _islandHwnd{ nullptr };
    };
}

namespace winrt::TerminalApp::factory_implementation
{
    BASIC_FACTORY(AddProjectDialog);
}
