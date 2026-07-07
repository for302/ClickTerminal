// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "ProjectSidebar.g.h"
#include "ProjectManager.h"
#include "AIToolManager.h"
#include "CTuxTheme.h"
#include "LayoutManager.h"

namespace winrt::TerminalApp::implementation
{
    struct ProjectSidebar : ProjectSidebarT<ProjectSidebar>
    {
        ProjectSidebar();

        void Refresh();
        void RefreshTheme();
        void SetSessionActive(const winrt::hstring& projectId, bool active);
        void UpdateContextUsage(const winrt::hstring& projectId, uint32_t usedTokens, uint32_t totalTokens);
        void SetActiveTerminalByTitle(const winrt::hstring& tabTitle);
        void ShowEditProject(const winrt::hstring& projectId);

        til::typed_event<winrt::Windows::Foundation::IInspectable, winrt::hstring> OpenTerminalRequested;
        til::typed_event<winrt::Windows::Foundation::IInspectable, winrt::hstring> StartAIRequested;
        til::typed_event<winrt::Windows::Foundation::IInspectable, winrt::hstring> StopAIRequested;
        til::typed_event<winrt::Windows::Foundation::IInspectable, winrt::hstring> OpenUrlRequested;
        til::typed_event<winrt::Windows::Foundation::IInspectable, winrt::hstring> CTuxThemeChanged;
        til::typed_event<winrt::Windows::Foundation::IInspectable, winrt::hstring> ApplyLayoutRequested;
        til::typed_event<winrt::Windows::Foundation::IInspectable, winrt::hstring> EditLayoutRequested;
        til::typed_event<winrt::Windows::Foundation::IInspectable, winrt::hstring> AddLayoutRequested;
        til::typed_event<winrt::Windows::Foundation::IInspectable, winrt::hstring> ReorderLayoutsRequested;

        void RefreshLayouts();

        ClickTerminal::ProjectManager& ProjectManagerRef() { return *_projectManager; }

        // XAML event handlers must be public
        safe_void_coroutine _AddProjectClicked(const winrt::Windows::Foundation::IInspectable& sender,
                                               const winrt::Windows::UI::Xaml::RoutedEventArgs& e);
        safe_void_coroutine _OrganizeClicked(const winrt::Windows::Foundation::IInspectable& sender,
                                             const winrt::Windows::UI::Xaml::RoutedEventArgs& e);
        safe_void_coroutine _SettingsClicked(const winrt::Windows::Foundation::IInspectable& sender,
                                             const winrt::Windows::UI::Xaml::RoutedEventArgs& e);

    private:
        std::unique_ptr<ClickTerminal::ProjectManager> _projectManager;
        std::unique_ptr<ClickTerminal::LayoutManager>  _layoutManager;
        std::unordered_map<std::wstring, bool>                            _activeSessions;
        std::unordered_map<std::wstring, std::pair<uint32_t, uint32_t>>  _contextUsage;
        std::unordered_map<std::wstring, winrt::TerminalApp::ContextMeter> _meterControls;
        ClickTerminal::CTuxTheme _activeTheme;
        std::wstring _activeTerminalProjectId; // projectId whose terminal tab is currently focused

        winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::UI::Xaml::Controls::ContentDialogResult> _pendingSettingsOp{ nullptr };
        winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::UI::Xaml::Controls::ContentDialogResult> _pendingAddProjectOp{ nullptr };
        winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::UI::Xaml::Controls::ContentDialogResult> _pendingEditOp{ nullptr };
        winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::UI::Xaml::Controls::ContentDialogResult> _pendingOrganizeOp{ nullptr };

        void _BuildProjectList();
        void _BuildLayoutSection();
        winrt::Windows::UI::Xaml::UIElement _BuildLayoutCard(const ClickTerminal::Layout& layout);
        winrt::Windows::UI::Xaml::UIElement _BuildProjectCard(const ClickTerminal::Project& project);
        void _ApplyTheme(const ClickTerminal::CTuxTheme& theme);

        safe_void_coroutine _ShowDeleteConfirm(std::wstring projectId);
        safe_void_coroutine _ShowEditDialog(std::wstring projectId);
    };
}

namespace winrt::TerminalApp::factory_implementation
{
    BASIC_FACTORY(ProjectSidebar);
}
