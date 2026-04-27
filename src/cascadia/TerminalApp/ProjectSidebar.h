// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "ProjectSidebar.g.h"
#include "ProjectManager.h"
#include "AIToolManager.h"

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

        ClickTerminal::ProjectManager& ProjectManagerRef() { return *_projectManager; }

        // XAML event handlers must be public so generated code can access them
        void _AddProjectClicked(const winrt::Windows::Foundation::IInspectable& sender,
                                const winrt::Windows::UI::Xaml::RoutedEventArgs& e);

    private:
        std::unique_ptr<ClickTerminal::ProjectManager> _projectManager;
        std::unordered_map<std::wstring, bool> _activeSessions;
        std::unordered_map<std::wstring, std::pair<uint32_t, uint32_t>> _contextUsage;

        void _BuildProjectList();
        winrt::Windows::UI::Xaml::UIElement _BuildProjectCard(const ClickTerminal::Project& project);
    };
}

namespace winrt::TerminalApp::factory_implementation
{
    BASIC_FACTORY(ProjectSidebar);
}
