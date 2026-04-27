// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "ProjectSidebar.h"
#include "ProjectSidebar.g.cpp"

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Xaml::Controls;

namespace winrt::TerminalApp::implementation
{
    ProjectSidebar::ProjectSidebar()
    {
        InitializeComponent();

        _projectManager = std::make_unique<ClickTerminal::ProjectManager>();
        _projectManager->LoadProjects();

        _BuildProjectList();
    }

    void ProjectSidebar::Refresh()
    {
        _projectManager->ReloadProjects();
        _BuildProjectList();
    }

    void ProjectSidebar::SetSessionActive(const winrt::hstring& projectId, bool active)
    {
        _activeSessions[std::wstring{ projectId }] = active;
        _BuildProjectList();
    }

    void ProjectSidebar::UpdateContextUsage(const winrt::hstring& projectId, uint32_t usedTokens, uint32_t totalTokens)
    {
        _contextUsage[std::wstring{ projectId }] = { usedTokens, totalTokens };
        _BuildProjectList();
    }

    void ProjectSidebar::_BuildProjectList()
    {
        ProjectListPanel().Children().Clear();

        auto projects = _projectManager->GetAllProjects();
        if (projects.empty())
        {
            TextBlock emptyText;
            emptyText.Text(L"No projects yet.\nClick + to add one.");
            emptyText.FontSize(12.0);
            emptyText.Opacity(0.5);
            emptyText.TextWrapping(TextWrapping::Wrap);
            emptyText.Margin({ 8, 16, 8, 0 });
            ProjectListPanel().Children().Append(emptyText);
            return;
        }

        for (const auto& project : projects)
        {
            auto card = _BuildProjectCard(project);
            ProjectListPanel().Children().Append(card);
        }
    }

    static GridLength MakeStar()
    {
        GridLength gl;
        gl.Value = 1.0;
        gl.GridUnitType = GridUnitType::Star;
        return gl;
    }

    static GridLength MakeAuto()
    {
        GridLength gl;
        gl.Value = 1.0;
        gl.GridUnitType = GridUnitType::Auto;
        return gl;
    }

    winrt::Windows::UI::Xaml::UIElement ProjectSidebar::_BuildProjectCard(const ClickTerminal::Project& project)
    {
        // Outer border
        Border outer;
        outer.Padding({ 8, 8, 8, 8 });
        outer.Margin({ 0, 0, 0, 2 });

        // Try to use a subtle fill from theme resources
        auto resources = Application::Current().Resources();
        auto bgKey = box_value(L"CardBackgroundFillColorDefaultBrush");
        if (resources.HasKey(bgKey))
        {
            outer.Background(resources.Lookup(bgKey).try_as<Media::Brush>());
        }

        // Main vertical stack
        StackPanel panel;
        panel.Orientation(Orientation::Vertical);
        panel.Spacing(4.0);

        // --- Name row ---
        Grid nameRow;
        ColumnDefinition colStar;
        colStar.Width(MakeStar());
        ColumnDefinition colAuto;
        colAuto.Width(MakeAuto());
        nameRow.ColumnDefinitions().Append(colStar);
        nameRow.ColumnDefinitions().Append(colAuto);

        TextBlock nameText;
        nameText.Text(project.Name);
        nameText.FontSize(13.0);
        nameText.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
        nameText.TextTrimming(TextTrimming::CharacterEllipsis);
        nameText.VerticalAlignment(VerticalAlignment::Center);
        Grid::SetColumn(nameText, 0);
        nameRow.Children().Append(nameText);

        // Type badge text
        const wchar_t* typeLabel = L"web";
        if (project.Type == ClickTerminal::ProjectType::App) typeLabel = L"app";
        else if (project.Type == ClickTerminal::ProjectType::AIWorkflow) typeLabel = L"ai";

        Border typeBadge;
        typeBadge.Padding({ 4, 1, 4, 1 });
        typeBadge.VerticalAlignment(VerticalAlignment::Center);
        typeBadge.Opacity(0.7);

        TextBlock typeText;
        typeText.Text(typeLabel);
        typeText.FontSize(9.0);
        typeBadge.Child(typeText);
        Grid::SetColumn(typeBadge, 1);
        nameRow.Children().Append(typeBadge);

        panel.Children().Append(nameRow);

        // --- Folder path ---
        TextBlock pathText;
        pathText.Text(project.FolderPath);
        pathText.FontSize(10.0);
        pathText.TextTrimming(TextTrimming::CharacterEllipsis);
        pathText.Opacity(0.55);
        panel.Children().Append(pathText);

        // --- Ports / URLs ---
        if (!project.Ports.empty() || !project.Urls.empty())
        {
            StackPanel portsRow;
            portsRow.Orientation(Orientation::Horizontal);
            portsRow.Margin({ 0, 2, 0, 0 });
            portsRow.Spacing(4.0);

            for (auto port : project.Ports)
            {
                HyperlinkButton portLink;
                portLink.NavigateUri(Uri{ hstring{ L"http://localhost:" + std::to_wstring(port) } });
                portLink.Padding({ 0, 0, 0, 0 });

                TextBlock portText;
                portText.Text(L":" + std::to_wstring(port));
                portText.FontSize(10.0);
                portLink.Content(portText);
                portsRow.Children().Append(portLink);
            }

            for (const auto& url : project.Urls)
            {
                HyperlinkButton urlLink;
                urlLink.NavigateUri(Uri{ hstring{ url.Url } });
                urlLink.Padding({ 0, 0, 0, 0 });

                TextBlock urlText;
                urlText.Text(url.Label);
                urlText.FontSize(10.0);
                urlLink.Content(urlText);
                portsRow.Children().Append(urlLink);
            }

            panel.Children().Append(portsRow);
        }

        // --- Action buttons ---
        StackPanel btnRow;
        btnRow.Orientation(Orientation::Horizontal);
        btnRow.Spacing(4.0);
        btnRow.Margin({ 0, 4, 0, 0 });

        auto projectId = project.Id;

        // Open Terminal button
        Button openBtn;
        openBtn.Content(box_value(L"Terminal"));
        openBtn.FontSize(11.0);
        openBtn.Padding({ 8, 3, 8, 3 });
        openBtn.Click([projectId, weakSelf = get_weak()](const IInspectable&, const RoutedEventArgs&) {
            if (auto self = weakSelf.get())
            {
                self->OpenTerminalRequested.raise(*self, hstring{ projectId });
            }
        });
        btnRow.Children().Append(openBtn);

        // AI tool button
        if (!project.AIConfig.DefaultTool.empty())
        {
            auto toolName = project.AIConfig.DefaultTool;
            bool isActive = _activeSessions.count(projectId) && _activeSessions.at(projectId);

            if (!isActive)
            {
                Button aiBtn;
                aiBtn.Content(box_value(hstring{ toolName }));
                aiBtn.FontSize(11.0);
                aiBtn.Padding({ 8, 3, 8, 3 });
                aiBtn.Click([projectId, weakSelf = get_weak()](const IInspectable&, const RoutedEventArgs&) {
                    if (auto self = weakSelf.get())
                    {
                        self->StartAIRequested.raise(*self, hstring{ projectId });
                    }
                });
                btnRow.Children().Append(aiBtn);
            }
            else
            {
                Button stopBtn;
                stopBtn.Content(box_value(L"Stop"));
                stopBtn.FontSize(11.0);
                stopBtn.Padding({ 8, 3, 8, 3 });
                stopBtn.Click([projectId, weakSelf = get_weak()](const IInspectable&, const RoutedEventArgs&) {
                    if (auto self = weakSelf.get())
                    {
                        self->StopAIRequested.raise(*self, hstring{ projectId });
                    }
                });
                btnRow.Children().Append(stopBtn);

                // Context usage percentage
                if (_contextUsage.count(projectId))
                {
                    auto [used, total] = _contextUsage.at(projectId);
                    uint8_t pct = total > 0 ? static_cast<uint8_t>((used * 100ULL) / total) : 0;
                    TextBlock meterText;
                    meterText.Text(std::to_wstring(pct) + L"%");
                    meterText.FontSize(10.0);
                    meterText.Opacity(0.7);
                    meterText.VerticalAlignment(VerticalAlignment::Center);
                    meterText.Margin({ 4, 0, 0, 0 });
                    btnRow.Children().Append(meterText);
                }
            }
        }

        panel.Children().Append(btnRow);
        outer.Child(panel);

        return outer;
    }

    void ProjectSidebar::_AddProjectClicked(const winrt::Windows::Foundation::IInspectable& /*sender*/,
                                             const winrt::Windows::UI::Xaml::RoutedEventArgs& /*e*/)
    {
        // TODO: Open "Add Project" dialog — for now add a stub project for testing
        ClickTerminal::Project testProject;
        testProject.Name = L"New Project";
        testProject.Type = ClickTerminal::ProjectType::Web;
        testProject.FolderPath = L"C:\\";
        testProject.AIConfig.DefaultTool = L"claude";

        _projectManager->AddProject(std::move(testProject));
        _projectManager->SaveProjects();
        _BuildProjectList();
    }
}
