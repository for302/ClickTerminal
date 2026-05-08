// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "ProjectSidebar.h"
#include "ProjectSidebar.g.cpp"
#include "ContextMeter.h"
#include "AddProjectDialog.h"
#include "AISetupPage.h"
#include "SettingsDialog.h"
#include "CTuxSettings.h"
#include <ShlObj.h>
#include <fstream>
#include <winrt/Windows.UI.Xaml.Shapes.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.System.Threading.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Xaml::Controls;
using namespace winrt::Windows::UI::Xaml::Media;

namespace
{
    static void DbgLog(const wchar_t* msg)
    {
        wchar_t raw[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, raw)))
        {
            std::wstring p = std::wstring(raw) + L"\\ClickTerminal\\ctux-debug.log";
            std::wofstream f(p, std::ios::app);
            f << msg << L"\n";
        }
    }

    // Recursively search the visual tree for a named element.
    static DependencyObject FindDescendantByName(DependencyObject const& root, hstring const& name)
    {
        int count = VisualTreeHelper::GetChildrenCount(root);
        for (int i = 0; i < count; ++i)
        {
            auto child = VisualTreeHelper::GetChild(root, i);
            if (auto fe = child.try_as<FrameworkElement>())
                if (fe.Name() == name) return child;
            if (auto found = FindDescendantByName(child, name))
                return found;
        }
        return nullptr;
    }

    static void ApplyDarkSmoke(ContentDialog const& dlg)
    {
        auto xamlRoot = dlg.XamlRoot();
        dlg.Opened([xamlRoot](ContentDialog const& sender, ContentDialogOpenedEventArgs const&)
        {
            DbgLog(L"[Smoke] Opened fired");

            DependencyObject node{ nullptr };

            // Approach 1: walk visual tree from the ContentDialog itself
            int topChildren = VisualTreeHelper::GetChildrenCount(sender);
            std::wstring childMsg = L"[Smoke] sender child count: " + std::to_wstring(topChildren);
            DbgLog(childMsg.c_str());

            node = FindDescendantByName(sender, L"SmokeLayerBackground");
            DbgLog(node ? L"[Smoke] Found via sender" : L"[Smoke] Not found via sender");

            // Approach 2: walk all open popups in the XamlRoot
            if (!node && xamlRoot)
            {
                try
                {
                    auto popups = VisualTreeHelper::GetOpenPopupsForXamlRoot(xamlRoot);
                    std::wstring popMsg = L"[Smoke] open popup count: " + std::to_wstring(popups.Size());
                    DbgLog(popMsg.c_str());
                    for (auto const& popup : popups)
                    {
                        node = FindDescendantByName(popup.as<DependencyObject>(), L"SmokeLayerBackground");
                        if (node) { DbgLog(L"[Smoke] Found in popup"); break; }
                    }
                    if (!node) DbgLog(L"[Smoke] Not found in any popup");
                }
                catch (...) { DbgLog(L"[Smoke] GetOpenPopupsForXamlRoot threw"); }
            }

            if (auto shape = node.try_as<winrt::Windows::UI::Xaml::Shapes::Shape>())
            {
                DbgLog(L"[Smoke] Applying dark fill");
                winrt::Windows::UI::Color c{ 0x99, 0, 0, 0 };
                shape.Fill(winrt::Windows::UI::Xaml::Media::SolidColorBrush{ c });
            }
            else
            {
                DbgLog(L"[Smoke] node null or not a Shape");
            }
        });
    }

    static winrt::Windows::UI::Color ParseHexColor(const std::wstring& hex)
    {
        std::wstring h = hex;
        if (!h.empty() && h[0] == L'#') h = h.substr(1);
        uint32_t v = 0;
        for (auto c : h) {
            v <<= 4;
            if (c >= L'0' && c <= L'9') v |= c - L'0';
            else if (c >= L'a' && c <= L'f') v |= c - L'a' + 10;
            else if (c >= L'A' && c <= L'F') v |= c - L'A' + 10;
        }
        if (h.size() == 6) return { 0xFF, uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v) };
        if (h.size() == 8) return { uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v) };
        return { 0xFF, 0x40, 0x40, 0x40 };
    }

    static SolidColorBrush MakeBrush(const std::wstring& hex)
    {
        return SolidColorBrush{ ParseHexColor(hex) };
    }

    // Lighten (dark theme) or darken (light theme) a hex color for card backgrounds
    static winrt::Windows::UI::Color CardColor(const std::wstring& hex, bool isLight)
    {
        auto c = ParseHexColor(hex);
        int shift = isLight ? -25 : 18;
        c.R = static_cast<uint8_t>(std::clamp((int)c.R + shift, 0, 255));
        c.G = static_cast<uint8_t>(std::clamp((int)c.G + shift, 0, 255));
        c.B = static_cast<uint8_t>(std::clamp((int)c.B + shift, 0, 255));
        return c;
    }
}

namespace winrt::TerminalApp::implementation
{
    ProjectSidebar::ProjectSidebar()
    {
        DbgLog(L"[Sidebar] ctor start");
        InitializeComponent();
        DbgLog(L"[Sidebar] InitializeComponent OK");

        auto settings = ClickTerminal::CTuxSettings::Load();
        DbgLog(L"[Sidebar] CTuxSettings loaded");
        _activeTheme        = settings.GetActiveTheme();
        _gitPluginEnabled   = settings.GitPluginEnabled;
        _portPluginEnabled  = settings.PortPluginEnabled;
        DbgLog(L"[Sidebar] GetActiveTheme OK");

        _projectManager = std::make_unique<ClickTerminal::ProjectManager>(settings.ProjectsConfigPath);
        _projectManager->LoadProjects();
        DbgLog(L"[Sidebar] projects loaded");

        _layoutManager = std::make_unique<ClickTerminal::LayoutManager>();
        _layoutManager->LoadLayouts();

        _ApplyTheme(_activeTheme);
        DbgLog(L"[Sidebar] ApplyTheme OK");
        _BuildProjectList();
        DbgLog(L"[Sidebar] BuildProjectList OK");
    }

    void ProjectSidebar::_ApplyTheme(const ClickTerminal::CTuxTheme& theme)
    {
        // Set WinUI theme FIRST so ThemeResource re-evaluation happens
        // before we set local brush values (prevents ThemeResource override)
        auto elemTheme = theme.IsLightMode ? ElementTheme::Light : ElementTheme::Dark;
        RequestedTheme(elemTheme);
        _activeTheme = theme;

        auto& c = theme.Colors;
        try
        {
            SidebarHeaderPanel().Background(MakeBrush(c.SidebarHeaderBg));
            SidebarBodyPanel().Background(MakeBrush(c.SidebarBg));
            SidebarFooterPanel().Background(MakeBrush(c.SidebarHeaderBg));
            ProjectsLabel().Foreground(MakeBrush(c.SidebarTextMuted));
        }
        catch (...) {}

        _BuildProjectList();
    }

    void ProjectSidebar::Refresh()
    {
        _projectManager->ReloadProjects();
        _BuildProjectList();
    }

    void ProjectSidebar::RefreshTheme()
    {
        auto settings       = ClickTerminal::CTuxSettings::Load();
        _activeTheme        = settings.GetActiveTheme();
        _gitPluginEnabled   = settings.GitPluginEnabled;
        _portPluginEnabled  = settings.PortPluginEnabled;
        _ApplyTheme(_activeTheme);
    }

    void ProjectSidebar::ShowEditProject(const winrt::hstring& projectId)
    {
        _ShowEditDialog(std::wstring{ projectId });
    }

    void ProjectSidebar::SetActiveTerminalByTitle(const winrt::hstring& tabTitle)
    {
        std::wstring title{ tabTitle };
        std::wstring newId;
        for (auto& project : _projectManager->GetAllProjects())
        {
            // Exact match = regular terminal tab; prefix "[" match = AI session tab
            if (title == project.Name ||
                (title.size() > project.Name.size() + 2 &&
                 title.substr(0, project.Name.size()) == project.Name &&
                 title[project.Name.size()] == L' ' && title[project.Name.size() + 1] == L'['))
            {
                newId = project.Id;
                break;
            }
        }
        if (newId == _activeTerminalProjectId) return;
        _activeTerminalProjectId = newId;
        _BuildProjectList();
    }

    void ProjectSidebar::SetSessionActive(const winrt::hstring& projectId, bool active)
    {
        auto id = std::wstring{ projectId };
        _activeSessions[id] = active;
        if (!active)
        {
            _contextUsage.erase(id);
            _meterControls.erase(id);
        }
        _BuildProjectList();
    }

    void ProjectSidebar::UpdateContextUsage(const winrt::hstring& projectId, uint32_t usedTokens, uint32_t totalTokens)
    {
        auto id = std::wstring{ projectId };
        _contextUsage[id] = { usedTokens, totalTokens };

        // Update existing meter control without rebuilding the whole list
        auto it = _meterControls.find(id);
        if (it != _meterControls.end())
        {
            auto meterImpl = winrt::get_self<implementation::ContextMeter>(it->second);
            if (meterImpl)
            {
                meterImpl->SetUsage(usedTokens, totalTokens);
            }
        }
    }

    void ProjectSidebar::RefreshLayouts()
    {
        _layoutManager->LoadLayouts();
        _BuildProjectList();
    }

    void ProjectSidebar::_BuildProjectList()
    {
        _meterControls.clear();
        ProjectListPanel().Children().Clear();

        _BuildLayoutSection();

        auto projects = _projectManager->GetAllProjects();
        if (projects.empty())
        {
            TextBlock emptyText;
            emptyText.Text(L"프로젝트가 없습니다.\n+ 버튼으로 추가하세요.");
            emptyText.FontSize(12.0);
            emptyText.Opacity(0.5);
            emptyText.TextWrapping(TextWrapping::Wrap);
            emptyText.Margin({ 8, 16, 8, 0 });
            emptyText.Foreground(MakeBrush(_activeTheme.Colors.SidebarTextMuted));
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

    void ProjectSidebar::_BuildLayoutSection()
    {
        auto layouts = _layoutManager->GetAllLayouts();
        if (layouts.empty()) return;

        TextBlock sectionLabel;
        sectionLabel.Text(L"LAYOUTS");
        sectionLabel.FontSize(9.0);
        sectionLabel.Opacity(0.45);
        sectionLabel.Margin({ 2, 2, 2, 4 });
        sectionLabel.Foreground(MakeBrush(_activeTheme.Colors.SidebarTextMuted));
        ProjectListPanel().Children().Append(sectionLabel);

        for (const auto& layout : layouts)
            ProjectListPanel().Children().Append(_BuildLayoutCard(layout));

        Border sep;
        sep.Height(1.0);
        sep.Margin({ 0, 8, 0, 6 });
        SolidColorBrush sepBrush;
        sepBrush.Color({ 45, 200, 200, 200 });
        sep.Background(sepBrush);
        ProjectListPanel().Children().Append(sep);
    }

    winrt::Windows::UI::Xaml::UIElement ProjectSidebar::_BuildLayoutCard(const ClickTerminal::Layout& layout)
    {
        auto layoutId = layout.Id;

        Border outer;
        outer.Padding({ 10, 6, 8, 6 });
        outer.Margin({ 0, 0, 0, 2 });
        outer.CornerRadius({ 6, 6, 6, 6 });
        outer.Background(SolidColorBrush{ CardColor(_activeTheme.Colors.SidebarBg, _activeTheme.IsLightMode) });

        Grid row;
        ColumnDefinition c1; c1.Width(MakeStar());
        ColumnDefinition c2; c2.Width(MakeAuto());
        ColumnDefinition c3; c3.Width(MakeAuto());
        ColumnDefinition c4; c4.Width(MakeAuto());
        row.ColumnDefinitions().Append(c1);
        row.ColumnDefinitions().Append(c2);
        row.ColumnDefinitions().Append(c3);
        row.ColumnDefinitions().Append(c4);

        TextBlock nameText;
        nameText.Text(hstring{ layout.Name });
        nameText.FontSize(12.0);
        nameText.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
        nameText.TextTrimming(TextTrimming::CharacterEllipsis);
        nameText.VerticalAlignment(VerticalAlignment::Center);
        nameText.Foreground(MakeBrush(_activeTheme.Colors.SidebarText));
        Grid::SetColumn(nameText, 0);
        row.Children().Append(nameText);

        TextBlock sizeBadge;
        sizeBadge.Text(hstring{ std::to_wstring(layout.Rows) + L"\xD7" + std::to_wstring(layout.Cols) });
        sizeBadge.FontSize(9.0);
        sizeBadge.Opacity(0.5);
        sizeBadge.VerticalAlignment(VerticalAlignment::Center);
        sizeBadge.Margin({ 4, 0, 4, 0 });
        sizeBadge.Foreground(MakeBrush(_activeTheme.Colors.SidebarText));
        Grid::SetColumn(sizeBadge, 1);
        row.Children().Append(sizeBadge);

        // ▶ Start button
        Button startBtn;
        FontIcon playIcon;
        playIcon.FontFamily(winrt::Windows::UI::Xaml::Media::FontFamily(L"Segoe MDL2 Assets"));
        playIcon.Glyph(L"\xE768");
        playIcon.FontSize(10.0);
        playIcon.Foreground(MakeBrush(_activeTheme.Colors.SidebarText));
        startBtn.Content(playIcon);
        startBtn.Width(26);
        startBtn.Height(22);
        startBtn.Padding({ 0, 0, 0, 0 });
        startBtn.VerticalAlignment(VerticalAlignment::Center);
        startBtn.Click([layoutId, weakSelf = get_weak()](const IInspectable&, const RoutedEventArgs&) {
            if (auto self = weakSelf.get())
                self->ApplyLayoutRequested.raise(*self, hstring{ layoutId });
        });
        Grid::SetColumn(startBtn, 2);
        row.Children().Append(startBtn);

        // ⚙ Settings button
        Button gearBtn;
        FontIcon gearIcon;
        gearIcon.FontFamily(winrt::Windows::UI::Xaml::Media::FontFamily(L"Segoe MDL2 Assets"));
        gearIcon.Glyph(L"\xE713");
        gearIcon.FontSize(10.0);
        gearIcon.Foreground(MakeBrush(_activeTheme.Colors.SidebarText));
        gearBtn.Content(gearIcon);
        gearBtn.Width(26);
        gearBtn.Height(22);
        gearBtn.Padding({ 0, 0, 0, 0 });
        gearBtn.VerticalAlignment(VerticalAlignment::Center);
        gearBtn.Margin({ 2, 0, 0, 0 });
        gearBtn.Click([layoutId, weakSelf = get_weak()](const IInspectable&, const RoutedEventArgs&) {
            if (auto self = weakSelf.get())
                self->EditLayoutRequested.raise(*self, hstring{ layoutId });
        });
        Grid::SetColumn(gearBtn, 3);
        row.Children().Append(gearBtn);

        outer.Child(row);
        return outer;
    }

    winrt::Windows::UI::Xaml::UIElement ProjectSidebar::_BuildProjectCard(const ClickTerminal::Project& project)
    {
        auto projectId = project.Id;
        bool isActive         = _activeSessions.count(projectId) && _activeSessions.at(projectId);
        bool isActiveTerminal = (projectId == _activeTerminalProjectId);

        // Outer border/card — slightly lighter/darker than sidebar body based on theme
        Border outer;
        outer.Padding({ 10, 8, 10, 8 });
        outer.Margin({ 0, 0, 0, 2 });
        outer.CornerRadius({ 6, 6, 6, 6 });
        outer.Background(SolidColorBrush{ CardColor(_activeTheme.Colors.SidebarBg, _activeTheme.IsLightMode) });

        // Active terminal project: colored left-edge accent bar + slightly brighter background
        if (isActiveTerminal)
        {
            outer.BorderThickness({ 3, 0, 0, 0 });
            outer.BorderBrush(SolidColorBrush{ ParseHexColor(_activeTheme.Colors.SidebarText) });
            outer.Padding({ 8, 8, 10, 8 }); // compensate left padding for the 3px border
            auto bg = CardColor(_activeTheme.Colors.SidebarBg, _activeTheme.IsLightMode);
            int shift = _activeTheme.IsLightMode ? -15 : 12;
            bg.R = static_cast<uint8_t>(std::clamp((int)bg.R + shift, 0, 255));
            bg.G = static_cast<uint8_t>(std::clamp((int)bg.G + shift, 0, 255));
            bg.B = static_cast<uint8_t>(std::clamp((int)bg.B + shift, 0, 255));
            outer.Background(SolidColorBrush{ bg });
        }

        // Main vertical stack
        StackPanel panel;
        panel.Orientation(Orientation::Vertical);
        panel.Spacing(4.0);

        // --- Name + type + menu row ---
        Grid nameRow;
        ColumnDefinition c1; c1.Width(MakeStar());
        ColumnDefinition c2; c2.Width(MakeAuto());
        ColumnDefinition c3; c3.Width(MakeAuto());
        nameRow.ColumnDefinitions().Append(c1);
        nameRow.ColumnDefinitions().Append(c2);
        nameRow.ColumnDefinitions().Append(c3);

        TextBlock nameText;
        nameText.Text(project.Name);
        nameText.FontSize(13.0);
        nameText.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
        nameText.TextTrimming(TextTrimming::CharacterEllipsis);
        nameText.VerticalAlignment(VerticalAlignment::Center);
        nameText.Foreground(MakeBrush(_activeTheme.Colors.SidebarText));
        Grid::SetColumn(nameText, 0);
        nameRow.Children().Append(nameText);

        const wchar_t* typeLabel = L"web";
        if (project.Type == ClickTerminal::ProjectType::App)        typeLabel = L"app";
        else if (project.Type == ClickTerminal::ProjectType::AIWorkflow) typeLabel = L"cowork";

        TextBlock typeBadge;
        typeBadge.Text(typeLabel);
        typeBadge.FontSize(9.0);
        typeBadge.Opacity(0.55);
        typeBadge.VerticalAlignment(VerticalAlignment::Center);
        typeBadge.Margin({ 4, 0, 0, 0 });
        typeBadge.Foreground(MakeBrush(_activeTheme.Colors.SidebarText));
        Grid::SetColumn(typeBadge, 1);
        nameRow.Children().Append(typeBadge);

        // ⚙ Settings button — direct click opens edit dialog
        Button menuBtn;
        FontIcon gearMenuIcon;
        gearMenuIcon.FontFamily(winrt::Windows::UI::Xaml::Media::FontFamily(L"Segoe MDL2 Assets"));
        gearMenuIcon.Glyph(L"\xE713");
        gearMenuIcon.FontSize(10.0);
        gearMenuIcon.Foreground(MakeBrush(_activeTheme.Colors.SidebarText));
        menuBtn.Content(gearMenuIcon);
        menuBtn.Width(24);
        menuBtn.Height(22);
        menuBtn.Padding({ 0, 0, 0, 0 });
        menuBtn.VerticalAlignment(VerticalAlignment::Center);
        menuBtn.Margin({ 2, 0, 0, 0 });
        menuBtn.Click([projectId, weakSelf = get_weak()](const IInspectable&, const RoutedEventArgs&) {
            if (auto self = weakSelf.get())
                self->_ShowEditDialog(projectId);
        });

        Grid::SetColumn(menuBtn, 2);
        nameRow.Children().Append(menuBtn);

        // URL icon buttons — to the right of gear
        {
            auto addUrlIcon = [&](const wchar_t* glyph, const wchar_t* tip, const std::wstring& url) {
                ColumnDefinition urlCol; urlCol.Width(MakeAuto());
                nameRow.ColumnDefinitions().Append(urlCol);
                int col = static_cast<int>(nameRow.ColumnDefinitions().Size()) - 1;

                Button ubtn;
                FontIcon uicon;
                uicon.FontFamily(winrt::Windows::UI::Xaml::Media::FontFamily(L"Segoe MDL2 Assets"));
                uicon.Glyph(glyph);
                uicon.FontSize(11.0);
                uicon.Foreground(MakeBrush(_activeTheme.Colors.SidebarText));
                ubtn.Content(uicon);
                ubtn.Width(22);
                ubtn.Height(22);
                ubtn.Padding({ 0, 0, 0, 0 });
                ubtn.BorderThickness({ 0, 0, 0, 0 });
                ubtn.VerticalAlignment(VerticalAlignment::Center);
                ubtn.Margin({ 2, 0, 0, 0 });
                ToolTipService::SetToolTip(ubtn, winrt::box_value(winrt::hstring{ tip }));
                auto u = url;
                ubtn.Click([u, weakSelf = get_weak()](const IInspectable&, const RoutedEventArgs&) {
                    if (auto self = weakSelf.get())
                        self->OpenUrlRequested.raise(*self, hstring{ u });
                });
                Grid::SetColumn(ubtn, col);
                nameRow.Children().Append(ubtn);
            };
            if (!project.DevUrl.empty())    addUrlIcon(L"\xE71B", L"Dev URL",    project.DevUrl);
            if (!project.DeployUrl.empty()) addUrlIcon(L"\xE774", L"Deploy URL", project.DeployUrl);
            if (!project.GitUrl.empty())    addUrlIcon(L"\xE943", L"Git URL",    project.GitUrl);
        }

        panel.Children().Append(nameRow);

        // --- Folder path ---
        TextBlock pathText;
        pathText.Text(project.FolderPath);
        pathText.FontSize(10.0);
        pathText.TextTrimming(TextTrimming::CharacterEllipsis);
        pathText.Opacity(0.5);
        pathText.Foreground(MakeBrush(_activeTheme.Colors.SidebarTextMuted));
        panel.Children().Append(pathText);

        // --- Git Integration ---
        if (_gitPluginEnabled)
        {
            TextBlock gitBlock;
            gitBlock.FontSize(10.0);
            gitBlock.Margin({ 0, 2, 0, 0 });
            gitBlock.Opacity(0.75);
            gitBlock.Foreground(MakeBrush(_activeTheme.Colors.SidebarTextMuted));
            gitBlock.Text(L"⏎ ...");
            panel.Children().Append(gitBlock);

            if (_gitStatusCache.count(project.Id))
                gitBlock.Text(_gitStatusCache[project.Id]);
            else
                _FetchGitStatusAsync(project.Id, project.FolderPath, gitBlock);
        }

        // --- Ports / URLs ---
        if (!project.Ports.empty() || !project.Urls.empty())
        {
            StackPanel portsRow;
            portsRow.Orientation(Orientation::Horizontal);
            portsRow.Margin({ 0, 2, 0, 0 });
            portsRow.Spacing(2.0);

            if (_portPluginEnabled && !project.Ports.empty())
            {
                // Port Monitor ON: check active status
                auto activePorts = _GetActiveTcpPorts();
                for (auto port : project.Ports)
                {
                    bool isActive = activePorts.count(port) > 0;
                    HyperlinkButton portLink;
                    portLink.Padding({ 0, 0, 0, 0 });
                    if (isActive)
                        portLink.NavigateUri(Uri{ hstring{ L"http://localhost:" + std::to_wstring(port) } });
                    TextBlock portText;
                    portText.Text((isActive ? L"● :" : L"○ :") + std::to_wstring(port));
                    portText.FontSize(10.0);
                    portText.Foreground(MakeBrush(_activeTheme.Colors.SidebarText));
                    portText.Opacity(isActive ? 1.0 : 0.4);
                    portLink.Content(portText);
                    portsRow.Children().Append(portLink);
                }
            }
            else
            {
                // Port Monitor OFF: show configured ports as-is (original behavior)
                for (auto port : project.Ports)
                {
                    HyperlinkButton portLink;
                    portLink.NavigateUri(Uri{ hstring{ L"http://localhost:" + std::to_wstring(port) } });
                    portLink.Padding({ 0, 0, 0, 0 });
                    TextBlock portText;
                    portText.Text(L":" + std::to_wstring(port));
                    portText.FontSize(10.0);
                    portText.Foreground(MakeBrush(_activeTheme.Colors.SidebarText));
                    portLink.Content(portText);
                    portsRow.Children().Append(portLink);
                }
            }

            for (const auto& url : project.Urls)
            {
                HyperlinkButton urlLink;
                urlLink.NavigateUri(Uri{ hstring{ url.Url } });
                urlLink.Padding({ 0, 0, 0, 0 });
                TextBlock urlText;
                urlText.Text(url.Label);
                urlText.FontSize(10.0);
                urlText.Foreground(MakeBrush(_activeTheme.Colors.SidebarText));
                urlLink.Content(urlText);
                portsRow.Children().Append(urlLink);
            }

            panel.Children().Append(portsRow);
        }

        // --- Action buttons ---
        StackPanel btnRow;
        btnRow.Orientation(Orientation::Horizontal);
        btnRow.Spacing(4.0);
        btnRow.Margin({ 0, 6, 0, 0 });

        // Open Terminal button
        Button openBtn;
        openBtn.Content(box_value(L"Terminal"));
        openBtn.FontSize(11.0);
        openBtn.Padding({ 8, 3, 8, 3 });
        openBtn.Foreground(MakeBrush(_activeTheme.Colors.SidebarText));
        openBtn.Click([projectId, weakSelf = get_weak()](const IInspectable&, const RoutedEventArgs&) {
            if (auto self = weakSelf.get())
                self->OpenTerminalRequested.raise(*self, hstring{ projectId });
        });
        btnRow.Children().Append(openBtn);

        // AI tool button(s)
        if (!project.AIConfig.DefaultTool.empty())
        {
            auto toolName = project.AIConfig.DefaultTool;

            if (!isActive)
            {
                Button aiBtn;
                aiBtn.Content(box_value(hstring{ toolName }));
                aiBtn.FontSize(11.0);
                aiBtn.Padding({ 8, 3, 8, 3 });
                aiBtn.Foreground(MakeBrush(_activeTheme.Colors.SidebarText));
                aiBtn.Click([projectId, weakSelf = get_weak()](const IInspectable&, const RoutedEventArgs&) {
                    if (auto self = weakSelf.get())
                        self->StartAIRequested.raise(*self, hstring{ projectId });
                });
                btnRow.Children().Append(aiBtn);
            }
            else
            {
                // Stop button
                Button stopBtn;
                stopBtn.Content(box_value(L"Stop"));
                stopBtn.FontSize(11.0);
                stopBtn.Padding({ 8, 3, 8, 3 });
                stopBtn.Foreground(MakeBrush(_activeTheme.Colors.SidebarText));
                stopBtn.Click([projectId, weakSelf = get_weak()](const IInspectable&, const RoutedEventArgs&) {
                    if (auto self = weakSelf.get())
                        self->StopAIRequested.raise(*self, hstring{ projectId });
                });
                btnRow.Children().Append(stopBtn);

                // Context meter
                winrt::TerminalApp::ContextMeter meter;
                if (_contextUsage.count(projectId))
                {
                    auto [used, total] = _contextUsage.at(projectId);
                    winrt::get_self<implementation::ContextMeter>(meter)->SetUsage(used, total);
                }
                meter.Margin({ 4, 0, 0, 0 });
                meter.VerticalAlignment(VerticalAlignment::Center);
                _meterControls[projectId] = meter;
                btnRow.Children().Append(meter);
            }
        }

        panel.Children().Append(btnRow);
        outer.Child(panel);

        return outer;
    }

    safe_void_coroutine ProjectSidebar::_SettingsClicked(
        const winrt::Windows::Foundation::IInspectable& /*sender*/,
        const winrt::Windows::UI::Xaml::RoutedEventArgs& /*e*/)
    {
        DbgLog(L"[Settings] clicked");
        auto weakSelf = get_weak();

        // If a previous dialog was orphaned (e.g. window went fullscreen and popup disappeared),
        // cancel it so the XAML runtime releases the "one ContentDialog at a time" lock.
        if (_pendingSettingsOp)
        {
            DbgLog(L"[Settings] cancelling pending op");
            try { _pendingSettingsOp.Cancel(); } catch (...) {}
            _pendingSettingsOp = nullptr;
            co_await winrt::resume_after(std::chrono::milliseconds(100));
            if (!weakSelf.get()) co_return;
        }

        winrt::TerminalApp::SettingsDialog dialog{ nullptr };
        try { dialog = winrt::TerminalApp::SettingsDialog{}; }
        catch (...) { DbgLog(L"[Settings] dialog ctor threw"); co_return; }

        auto xamlRoot = this->XamlRoot();
        if (!xamlRoot) { DbgLog(L"[Settings] XamlRoot null - cannot show dialog"); co_return; }
        DbgLog(L"[Settings] showing dialog...");
        dialog.XamlRoot(xamlRoot);
        {
            auto dlgImpl = winrt::get_self<implementation::SettingsDialog>(dialog);
            dlgImpl->ApplyTheme(_activeTheme);
        }
        ApplyDarkSmoke(dialog.as<ContentDialog>());

        auto showOp = dialog.ShowAsync();
        if (auto s = weakSelf.get()) s->_pendingSettingsOp = showOp;

        ContentDialogResult result{ ContentDialogResult::None };
        try { result = co_await showOp; }
        catch (...) { if (auto s = weakSelf.get()) s->_pendingSettingsOp = nullptr; co_return; }

        auto self = weakSelf.get();
        if (!self) co_return;
        self->_pendingSettingsOp = nullptr;
        if (result != ContentDialogResult::Primary) co_return;

        ClickTerminal::CTuxSettings newSettings;
        newSettings.ProjectsConfigPath = std::wstring{ dialog.ProjectsConfigPath() };
        newSettings.SelectedThemeName  = std::wstring{ dialog.SelectedThemeName() };
        newSettings.PluginsFolder      = std::wstring{ dialog.PluginsFolder() };
        newSettings.GitPluginEnabled   = dialog.GitPluginEnabled();
        newSettings.PortPluginEnabled  = dialog.PortPluginEnabled();
        self->_gitPluginEnabled  = newSettings.GitPluginEnabled;
        self->_portPluginEnabled = newSettings.PortPluginEnabled;
        // Preserve updated custom themes from dialog
        newSettings.CustomThemes = winrt::get_self<implementation::SettingsDialog>(dialog)->GetUpdatedCustomThemes();
        newSettings.Save();
        newSettings.ApplyTerminalTheme();

        auto newTheme = newSettings.GetActiveTheme();
        self->_ApplyTheme(newTheme);
        self->CTuxThemeChanged.raise(*self, hstring{ newTheme.Name });

        self->_projectManager = std::make_unique<ClickTerminal::ProjectManager>(newSettings.ProjectsConfigPath);
        self->_projectManager->LoadProjects();
        self->_BuildProjectList();
    }

    safe_void_coroutine ProjectSidebar::_ShowDeleteConfirm(std::wstring projectId)
    {
        auto weakSelf = get_weak();
        auto theme = _activeTheme.IsLightMode ? ElementTheme::Light : ElementTheme::Dark;

        auto optProject = _projectManager->GetProjectById(projectId);
        if (!optProject) co_return;

        ContentDialog dialog;
        dialog.Title(box_value(L"프로젝트 삭제"));
        dialog.Content(box_value(hstring{ L"'" + optProject->Name + L"' 를 삭제할까요?" }));
        dialog.PrimaryButtonText(L"삭제");
        dialog.CloseButtonText(L"취소");
        dialog.DefaultButton(ContentDialogButton::Close);
        dialog.XamlRoot(this->XamlRoot());
        dialog.RequestedTheme(theme);
        ApplyDarkSmoke(dialog);

        auto result = co_await dialog.ShowAsync();

        auto self = weakSelf.get();
        if (!self) co_return;
        if (result != ContentDialogResult::Primary) co_return;

        self->_projectManager->RemoveProject(projectId);
        self->_projectManager->SaveProjects();
        self->_BuildProjectList();
    }

    safe_void_coroutine ProjectSidebar::_AddProjectClicked(
        const winrt::Windows::Foundation::IInspectable& /*sender*/,
        const winrt::Windows::UI::Xaml::RoutedEventArgs& /*e*/)
    {
        auto weakSelf = get_weak();

        // Cancel any orphaned dialog from a previous call
        if (_pendingAddProjectOp)
        {
            try { _pendingAddProjectOp.Cancel(); } catch (...) {}
            _pendingAddProjectOp = nullptr;
            co_await winrt::resume_after(std::chrono::milliseconds(100));
            if (!weakSelf.get()) co_return;
        }

        winrt::TerminalApp::AddProjectDialog dialog;
        dialog.XamlRoot(this->XamlRoot());
        dialog.RequestedTheme(_activeTheme.IsLightMode ? ElementTheme::Light : ElementTheme::Dark);
        ApplyDarkSmoke(dialog.as<ContentDialog>());

        auto showOp = dialog.ShowAsync();
        if (auto s = weakSelf.get()) s->_pendingAddProjectOp = showOp;

        ContentDialogResult result{ ContentDialogResult::None };
        try { result = co_await showOp; }
        catch (...) { if (auto s = weakSelf.get()) s->_pendingAddProjectOp = nullptr; co_return; }

        if (auto s = weakSelf.get()) s->_pendingAddProjectOp = nullptr;

        auto self = weakSelf.get();
        if (!self) co_return;

        if (result != Controls::ContentDialogResult::Primary) co_return;

        auto name    = std::wstring{ dialog.ProjectName() };
        auto folder  = std::wstring{ dialog.FolderPath() };
        auto typeStr = std::wstring{ dialog.ProjectType() };
        auto aiTool  = std::wstring{ dialog.DefaultAITool() };

        if (name.empty() || folder.empty()) co_return;

        ClickTerminal::Project project;
        project.Name       = name;
        project.FolderPath = folder;

        if (typeStr == L"app")          project.Type = ClickTerminal::ProjectType::App;
        else if (typeStr == L"ai-workflow") project.Type = ClickTerminal::ProjectType::AIWorkflow;
        else                            project.Type = ClickTerminal::ProjectType::Web;

        project.AIConfig.DefaultTool             = aiTool;
        project.AIConfig.Claude.StartCommand     = std::wstring{ dialog.ClaudeStartCommand() };
        project.AIConfig.Codex.StartCommand      = std::wstring{ dialog.CodexStartCommand() };
        project.AIConfig.Gemini.StartCommand     = std::wstring{ dialog.GeminiStartCommand() };
        project.AIConfig.AutoStartAI             = dialog.AutoStartAI();

        self->_projectManager->AddProject(std::move(project));
        self->_projectManager->SaveProjects();
        self->_BuildProjectList();
    }

    safe_void_coroutine ProjectSidebar::_ShowEditDialog(std::wstring projectId)
    {
        auto weakSelf = get_weak();

        if (_pendingEditOp)
        {
            try { _pendingEditOp.Cancel(); } catch (...) {}
            _pendingEditOp = nullptr;
            co_await winrt::resume_after(std::chrono::milliseconds(100));
            if (!weakSelf.get()) co_return;
        }

        auto optProject = _projectManager->GetProjectById(projectId);
        if (!optProject) co_return;

        winrt::TerminalApp::AddProjectDialog dialog;
        dialog.XamlRoot(this->XamlRoot());
        dialog.RequestedTheme(_activeTheme.IsLightMode ? ElementTheme::Light : ElementTheme::Dark);
        ApplyDarkSmoke(dialog.as<ContentDialog>());

        winrt::hstring typeStr = L"web";
        if (optProject->Type == ClickTerminal::ProjectType::App)             typeStr = L"app";
        else if (optProject->Type == ClickTerminal::ProjectType::AIWorkflow) typeStr = L"ai-workflow";

        winrt::get_self<implementation::AddProjectDialog>(dialog)->SetInitialValues(
            hstring{ optProject->Name },
            hstring{ optProject->FolderPath },
            typeStr,
            hstring{ optProject->AIConfig.DefaultTool },
            hstring{ optProject->AIConfig.Claude.StartCommand },
            hstring{ optProject->AIConfig.Codex.StartCommand },
            hstring{ optProject->AIConfig.Gemini.StartCommand },
            optProject->AIConfig.AutoStartAI,
            hstring{ optProject->DevUrl },
            hstring{ optProject->DeployUrl },
            hstring{ optProject->GitUrl }
        );

        auto showOp = dialog.ShowAsync();
        if (auto s = weakSelf.get()) s->_pendingEditOp = showOp;

        ContentDialogResult result{ ContentDialogResult::None };
        try { result = co_await showOp; }
        catch (...) { if (auto s = weakSelf.get()) s->_pendingEditOp = nullptr; co_return; }

        if (auto s = weakSelf.get()) s->_pendingEditOp = nullptr;

        auto self = weakSelf.get();
        if (!self) co_return;

        // Delete button (secondary) — confirm then remove
        if (result == ContentDialogResult::Secondary)
        {
            ContentDialog confirmDlg;
            confirmDlg.Title(box_value(L"프로젝트 삭제"));
            confirmDlg.Content(box_value(hstring{ L"'" + optProject->Name + L"' 를 삭제할까요?" }));
            confirmDlg.PrimaryButtonText(L"삭제");
            confirmDlg.CloseButtonText(L"취소");
            confirmDlg.DefaultButton(ContentDialogButton::Close);
            confirmDlg.XamlRoot(self->XamlRoot());
            confirmDlg.RequestedTheme(_activeTheme.IsLightMode ? ElementTheme::Light : ElementTheme::Dark);
            ApplyDarkSmoke(confirmDlg);
            auto confirmResult = co_await confirmDlg.ShowAsync();
            if (auto s = weakSelf.get(); s && confirmResult == ContentDialogResult::Primary)
            {
                s->_projectManager->RemoveProject(projectId);
                s->_projectManager->SaveProjects();
                s->_BuildProjectList();
            }
            co_return;
        }

        if (result != ContentDialogResult::Primary) co_return;

        auto project = *optProject;
        project.Name       = std::wstring{ dialog.ProjectName() };
        project.FolderPath = std::wstring{ dialog.FolderPath() };

        auto typeStr2 = std::wstring{ dialog.ProjectType() };
        if      (typeStr2 == L"app")          project.Type = ClickTerminal::ProjectType::App;
        else if (typeStr2 == L"ai-workflow")  project.Type = ClickTerminal::ProjectType::AIWorkflow;
        else                                  project.Type = ClickTerminal::ProjectType::Web;

        project.AIConfig.DefaultTool         = std::wstring{ dialog.DefaultAITool() };
        project.AIConfig.Claude.StartCommand = std::wstring{ dialog.ClaudeStartCommand() };
        project.AIConfig.Codex.StartCommand  = std::wstring{ dialog.CodexStartCommand() };
        project.AIConfig.Gemini.StartCommand = std::wstring{ dialog.GeminiStartCommand() };
        project.AIConfig.AutoStartAI         = dialog.AutoStartAI();
        project.DevUrl    = std::wstring{ dialog.DevUrl() };
        project.DeployUrl = std::wstring{ dialog.DeployUrl() };
        project.GitUrl    = std::wstring{ dialog.GitUrl() };

        self->_projectManager->UpdateProject(project);
        self->_projectManager->SaveProjects();
        self->_BuildProjectList();
    }

    // -----------------------------------------------------------------------
    // Git Integration helpers
    // -----------------------------------------------------------------------

    std::wstring ProjectSidebar::_RunGitCommand(const std::wstring& folder, const std::wstring& args)
    {
        std::wstring cmd = L"git -C \"" + folder + L"\" " + args;

        SECURITY_ATTRIBUTES sa{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
        HANDLE hRead = nullptr, hWrite = nullptr;
        if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return {};

        STARTUPINFOW si{};
        si.cb          = sizeof(si);
        si.hStdOutput  = hWrite;
        si.hStdError   = INVALID_HANDLE_VALUE;
        si.dwFlags     = STARTF_USESTDHANDLES;

        PROCESS_INFORMATION pi{};
        std::wstring cmdBuf = cmd; // CreateProcessW needs mutable buffer
        BOOL ok = CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr,
                                 TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
        CloseHandle(hWrite);
        if (!ok) { CloseHandle(hRead); return {}; }

        std::string out;
        char buf[256];
        DWORD read = 0;
        while (ReadFile(hRead, buf, sizeof(buf) - 1, &read, nullptr) && read > 0)
        {
            buf[read] = '\0';
            out += buf;
        }
        CloseHandle(hRead);
        WaitForSingleObject(pi.hProcess, 3000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        // trim trailing newline
        while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();

        // narrow → wide
        if (out.empty()) return {};
        int len = MultiByteToWideChar(CP_UTF8, 0, out.c_str(), -1, nullptr, 0);
        std::wstring result(len, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, out.c_str(), -1, result.data(), len);
        while (!result.empty() && result.back() == L'\0') result.pop_back();
        return result;
    }

    winrt::fire_and_forget ProjectSidebar::_FetchGitStatusAsync(
        std::wstring projectId, std::wstring folderPath,
        winrt::Windows::UI::Xaml::Controls::TextBlock block)
    {
        auto weakSelf = get_weak();
        co_await winrt::resume_background();

        std::wstring branch = _RunGitCommand(folderPath, L"branch --show-current 2>NUL");
        if (branch.empty()) co_return;

        std::wstring status = _RunGitCommand(folderPath, L"status --porcelain 2>NUL");
        int dirty = 0;
        for (auto ch : status) if (ch == L'\n') dirty++;
        if (!status.empty() && status.back() != L'\n') dirty++; // count last line without trailing newline

        std::wstring label = L"⏎ " + branch; // ⎇
        if (dirty > 0) label += L" ✱" + std::to_wstring(dirty); // ✱

        auto self = weakSelf.get();
        if (!self) co_return;
        self->_gitStatusCache[projectId] = label;

        co_await winrt::resume_foreground(Dispatcher());
        if (block) block.Text(label);
    }

    // -----------------------------------------------------------------------
    // Port Monitor helpers
    // -----------------------------------------------------------------------

    std::unordered_set<int> ProjectSidebar::_GetActiveTcpPorts()
    {
        std::unordered_set<int> result;
        ULONG size = 0;
        GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0);
        if (size == 0) return result;
        std::vector<uint8_t> buf(size);
        auto* table = reinterpret_cast<MIB_TCPTABLE_OWNER_PID*>(buf.data());
        if (GetExtendedTcpTable(table, &size, FALSE, AF_INET,
                                TCP_TABLE_OWNER_PID_LISTENER, 0) == NO_ERROR)
        {
            for (DWORD i = 0; i < table->dwNumEntries; i++)
                result.insert(static_cast<int>(ntohs(static_cast<u_short>(table->table[i].dwLocalPort))));
        }
        return result;
    }

}
