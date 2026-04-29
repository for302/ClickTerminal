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
        int shift = isLight ? -12 : 18;
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
        _activeTheme = settings.GetActiveTheme();
        DbgLog(L"[Sidebar] GetActiveTheme OK");

        _projectManager = std::make_unique<ClickTerminal::ProjectManager>(settings.ProjectsConfigPath);
        _projectManager->LoadProjects();
        DbgLog(L"[Sidebar] projects loaded");

        _ApplyTheme(_activeTheme);
        DbgLog(L"[Sidebar] ApplyTheme OK");
        _BuildProjectList();
        DbgLog(L"[Sidebar] BuildProjectList OK");
    }

    void ProjectSidebar::_ApplyTheme(const ClickTerminal::CTuxTheme& theme)
    {
        auto& c = theme.Colors;
        try
        {
            SidebarHeaderPanel().Background(MakeBrush(c.SidebarHeaderBg));
            SidebarBodyPanel().Background(MakeBrush(c.SidebarBg));
            SidebarFooterPanel().Background(MakeBrush(c.SidebarHeaderBg));
            ProjectsLabel().Foreground(MakeBrush(c.SidebarTextMuted));
        }
        catch (...) {}

        auto elemTheme = theme.IsLightMode ? ElementTheme::Light : ElementTheme::Dark;
        RequestedTheme(elemTheme);
        _activeTheme = theme;
    }

    void ProjectSidebar::Refresh()
    {
        _projectManager->ReloadProjects();
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

    void ProjectSidebar::_BuildProjectList()
    {
        _meterControls.clear();
        ProjectListPanel().Children().Clear();

        auto projects = _projectManager->GetAllProjects();
        if (projects.empty())
        {
            TextBlock emptyText;
            emptyText.Text(L"프로젝트가 없습니다.\n+ 버튼으로 추가하세요.");
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
        auto projectId = project.Id;
        bool isActive  = _activeSessions.count(projectId) && _activeSessions.at(projectId);

        // Outer border/card — slightly lighter/darker than sidebar body based on theme
        Border outer;
        outer.Padding({ 10, 8, 10, 8 });
        outer.Margin({ 0, 0, 0, 2 });
        outer.CornerRadius({ 6, 6, 6, 6 });
        outer.Background(SolidColorBrush{ CardColor(_activeTheme.Colors.SidebarBg, _activeTheme.IsLightMode) });

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
        Grid::SetColumn(typeBadge, 1);
        nameRow.Children().Append(typeBadge);

        // "···" context menu button
        Button menuBtn;
        FontIcon moreIcon;
        moreIcon.FontFamily(winrt::Windows::UI::Xaml::Media::FontFamily{ L"Segoe MDL2 Assets" });
        moreIcon.Glyph(L"\xE712");
        moreIcon.FontSize(10.0);
        menuBtn.Content(moreIcon);
        menuBtn.Width(24);
        menuBtn.Height(22);
        menuBtn.Padding({ 0, 0, 0, 0 });
        menuBtn.VerticalAlignment(VerticalAlignment::Center);
        menuBtn.Margin({ 2, 0, 0, 0 });

        MenuFlyout flyout;
        MenuFlyoutItem renameItem;
        renameItem.Text(L"이름 변경");
        renameItem.Click([projectId, weakSelf = get_weak()](const IInspectable&, const RoutedEventArgs&) {
            if (auto self = weakSelf.get())
                self->_ShowRenameDialog(projectId);
        });
        MenuFlyoutItem colorItem;
        colorItem.Text(L"색상 변경");
        colorItem.Click([projectId, weakSelf = get_weak()](const IInspectable&, const RoutedEventArgs&) {
            if (auto self = weakSelf.get())
                self->_ShowColorSchemeDialog(projectId);
        });
        MenuFlyoutItem deleteItem;
        deleteItem.Text(L"삭제");
        deleteItem.Click([projectId, weakSelf = get_weak()](const IInspectable&, const RoutedEventArgs&) {
            if (auto self = weakSelf.get())
                self->_ShowDeleteConfirm(projectId);
        });
        flyout.Items().Append(renameItem);
        flyout.Items().Append(colorItem);
        flyout.Items().Append(deleteItem);
        menuBtn.Flyout(flyout);

        Grid::SetColumn(menuBtn, 2);
        nameRow.Children().Append(menuBtn);
        panel.Children().Append(nameRow);

        // --- Folder path ---
        TextBlock pathText;
        pathText.Text(project.FolderPath);
        pathText.FontSize(10.0);
        pathText.TextTrimming(TextTrimming::CharacterEllipsis);
        pathText.Opacity(0.5);
        panel.Children().Append(pathText);

        // --- Ports / URLs ---
        if (!project.Ports.empty() || !project.Urls.empty())
        {
            StackPanel portsRow;
            portsRow.Orientation(Orientation::Horizontal);
            portsRow.Margin({ 0, 2, 0, 0 });
            portsRow.Spacing(2.0);

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
        btnRow.Margin({ 0, 6, 0, 0 });

        // Open Terminal button
        Button openBtn;
        openBtn.Content(box_value(L"Terminal"));
        openBtn.FontSize(11.0);
        openBtn.Padding({ 8, 3, 8, 3 });
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
        auto weakSelf = get_weak();

        // If a previous dialog was orphaned (e.g. window went fullscreen and popup disappeared),
        // cancel it so the XAML runtime releases the "one ContentDialog at a time" lock.
        if (_pendingSettingsOp)
        {
            try { _pendingSettingsOp.Cancel(); } catch (...) {}
            _pendingSettingsOp = nullptr;
            co_await winrt::resume_after(std::chrono::milliseconds(100));
            if (!weakSelf.get()) co_return;
        }

        winrt::TerminalApp::SettingsDialog dialog{ nullptr };
        try { dialog = winrt::TerminalApp::SettingsDialog{}; }
        catch (...) { co_return; }

        auto xamlRoot = this->XamlRoot();
        if (!xamlRoot) co_return;
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

    safe_void_coroutine ProjectSidebar::_AISetupClicked(
        const winrt::Windows::Foundation::IInspectable& /*sender*/,
        const winrt::Windows::UI::Xaml::RoutedEventArgs& /*e*/)
    {
        auto weakSelf = get_weak();

        if (_pendingAISetupOp)
        {
            try { _pendingAISetupOp.Cancel(); } catch (...) {}
            _pendingAISetupOp = nullptr;
            co_await winrt::resume_after(std::chrono::milliseconds(100));
            if (!weakSelf.get()) co_return;
        }

        auto self = weakSelf.get();
        if (!self) co_return;

        auto xamlRoot = XamlRoot();
        if (!xamlRoot) co_return;

        winrt::TerminalApp::AISetupPage dialog;
        dialog.XamlRoot(xamlRoot);
        dialog.RequestedTheme(_activeTheme.IsLightMode ? ElementTheme::Light : ElementTheme::Dark);
        ApplyDarkSmoke(dialog.as<ContentDialog>());

        auto showOp = dialog.ShowAsync();
        if (auto s = weakSelf.get()) s->_pendingAISetupOp = showOp;

        try { co_await showOp; }
        catch (...) {}

        if (auto s = weakSelf.get()) s->_pendingAISetupOp = nullptr;
    }

    safe_void_coroutine ProjectSidebar::_ShowRenameDialog(std::wstring projectId)
    {
        auto weakSelf = get_weak();
        auto theme = _activeTheme.IsLightMode ? ElementTheme::Light : ElementTheme::Dark;

        auto optProject = _projectManager->GetProjectById(projectId);
        if (!optProject) co_return;

        ContentDialog dialog;
        dialog.Title(box_value(L"이름 변경"));
        dialog.PrimaryButtonText(L"확인");
        dialog.CloseButtonText(L"취소");
        dialog.DefaultButton(ContentDialogButton::Primary);
        dialog.XamlRoot(this->XamlRoot());
        dialog.RequestedTheme(theme);
        ApplyDarkSmoke(dialog);

        TextBox nameBox;
        nameBox.Text(hstring{ optProject->Name });
        nameBox.PlaceholderText(L"프로젝트 이름");
        nameBox.Width(260);
        nameBox.SelectionStart(0);
        nameBox.SelectionLength(static_cast<int32_t>(optProject->Name.size()));
        dialog.Content(nameBox);

        auto result = co_await dialog.ShowAsync();

        auto self = weakSelf.get();
        if (!self) co_return;
        if (result != ContentDialogResult::Primary) co_return;

        auto newName = std::wstring{ nameBox.Text() };
        if (newName.empty()) co_return;

        auto project = *optProject;
        project.Name = newName;
        self->_projectManager->UpdateProject(project);
        self->_projectManager->SaveProjects();
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

        auto name        = std::wstring{ dialog.ProjectName() };
        auto folder      = std::wstring{ dialog.FolderPath() };
        auto typeStr     = std::wstring{ dialog.ProjectType() };
        auto aiTool      = std::wstring{ dialog.DefaultAITool() };
        auto colorScheme = std::wstring{ dialog.ColorScheme() };

        if (name.empty() || folder.empty()) co_return;

        ClickTerminal::Project project;
        project.Name        = name;
        project.FolderPath  = folder;
        project.ColorScheme = colorScheme;

        if (typeStr == L"app")          project.Type = ClickTerminal::ProjectType::App;
        else if (typeStr == L"ai-workflow") project.Type = ClickTerminal::ProjectType::AIWorkflow;
        else                            project.Type = ClickTerminal::ProjectType::Web;

        project.AIConfig.DefaultTool = aiTool;

        self->_projectManager->AddProject(std::move(project));
        self->_projectManager->SaveProjects();
        self->_BuildProjectList();
    }

    safe_void_coroutine ProjectSidebar::_ShowColorSchemeDialog(std::wstring projectId)
    {
        auto weakSelf = get_weak();
        auto theme = _activeTheme.IsLightMode ? ElementTheme::Light : ElementTheme::Dark;

        auto optProject = _projectManager->GetProjectById(projectId);
        if (!optProject) co_return;

        // Build a ComboBox with scheme options
        static constexpr const wchar_t* kSchemes[] = {
            L"",               L"CTux Dark",      L"CTux Light",
            L"Campbell",       L"One Half Dark",  L"One Half Light",
            L"Solarized Dark", L"Solarized Light", L"Tango Dark", L"Tango Light",
        };
        static constexpr const wchar_t* kLabels[] = {
            L"Default",        L"CTux Dark",      L"CTux Light",
            L"Campbell",       L"One Half Dark",  L"One Half Light",
            L"Solarized Dark", L"Solarized Light", L"Tango Dark", L"Tango Light",
        };

        ComboBox combo;
        combo.HorizontalAlignment(HorizontalAlignment::Stretch);
        for (auto lbl : kLabels)
            combo.Items().Append(box_value(hstring{ lbl }));

        // Pre-select current scheme
        int sel = 0;
        for (int i = 0; i < static_cast<int>(std::size(kSchemes)); ++i)
            if (optProject->ColorScheme == kSchemes[i]) { sel = i; break; }
        combo.SelectedIndex(sel);
        combo.MinWidth(260);

        ContentDialog dialog;
        dialog.Title(box_value(L"Terminal Color Scheme"));
        dialog.PrimaryButtonText(L"적용");
        dialog.CloseButtonText(L"취소");
        dialog.DefaultButton(ContentDialogButton::Primary);
        dialog.Content(combo);
        dialog.XamlRoot(this->XamlRoot());
        dialog.RequestedTheme(theme);
        ApplyDarkSmoke(dialog);

        auto result = co_await dialog.ShowAsync();

        auto self = weakSelf.get();
        if (!self) co_return;
        if (result != ContentDialogResult::Primary) co_return;

        auto idx = combo.SelectedIndex();
        if (idx < 0 || idx >= static_cast<int>(std::size(kSchemes))) co_return;

        auto project = *optProject;
        project.ColorScheme = kSchemes[idx];
        self->_projectManager->UpdateProject(project);
        self->_projectManager->SaveProjects();
        self->_BuildProjectList();
    }
}
