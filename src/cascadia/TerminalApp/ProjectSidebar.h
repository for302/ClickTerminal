// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "ProjectSidebar.g.h"
#include "ProjectManager.h"
#include "AIToolManager.h"
#include "CTuxTheme.h"

namespace winrt::TerminalApp::implementation
{
    struct ProjectSidebar : ProjectSidebarT<ProjectSidebar>
    {
        ProjectSidebar();

        void Refresh();
        void SetSessionActive(const winrt::hstring& projectId, bool active);
        void UpdateContextUsage(const winrt::hstring& projectId, uint32_t usedTokens, uint32_t totalTokens);

        til::typed_event<winrt::Windows::Foundation::IInspectable, winrt::hstring> OpenTerminalRequested;
        til::typed_event<winrt::Windows::Foundation::IInspectable, winrt::hstring> StartAIRequested;
        til::typed_event<winrt::Windows::Foundation::IInspectable, winrt::hstring> StopAIRequested;
        til::typed_event<winrt::Windows::Foundation::IInspectable, winrt::hstring> OpenUrlRequested;
        til::typed_event<winrt::Windows::Foundation::IInspectable, winrt::hstring> CTuxThemeChanged;

        ClickTerminal::ProjectManager& ProjectManagerRef() { return *_projectManager; }

        // XAML event handlers must be public
        safe_void_coroutine _AddProjectClicked(const winrt::Windows::Foundation::IInspectable& sender,
                                               const winrt::Windows::UI::Xaml::RoutedEventArgs& e);
        safe_void_coroutine _SettingsClicked(const winrt::Windows::Foundation::IInspectable& sender,
                                             const winrt::Windows::UI::Xaml::RoutedEventArgs& e);
        safe_void_coroutine _AISetupClicked(const winrt::Windows::Foundation::IInspectable& sender,
                                            const winrt::Windows::UI::Xaml::RoutedEventArgs& e);

    private:
        std::unique_ptr<ClickTerminal::ProjectManager> _projectManager;
        std::unordered_map<std::wstring, bool>                            _activeSessions;
        std::unordered_map<std::wstring, std::pair<uint32_t, uint32_t>>  _contextUsage;
        std::unordered_map<std::wstring, winrt::TerminalApp::ContextMeter> _meterControls;
        ClickTerminal::CTuxTheme _activeTheme;

        winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::UI::Xaml::Controls::ContentDialogResult> _pendingSettingsOp{ nullptr };
        winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::UI::Xaml::Controls::ContentDialogResult> _pendingAddProjectOp{ nullptr };
        winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::UI::Xaml::Controls::ContentDialogResult> _pendingAISetupOp{ nullptr };

        void _BuildProjectList();
        winrt::Windows::UI::Xaml::UIElement _BuildProjectCard(const ClickTerminal::Project& project);
        void _ApplyTheme(const ClickTerminal::CTuxTheme& theme);

        safe_void_coroutine _ShowRenameDialog(std::wstring projectId);
        safe_void_coroutine _ShowDeleteConfirm(std::wstring projectId);
        safe_void_coroutine _ShowColorSchemeDialog(std::wstring projectId);
    };
}

namespace winrt::TerminalApp::factory_implementation
{
    BASIC_FACTORY(ProjectSidebar);
}
