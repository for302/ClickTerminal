// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "SettingsDialog.h"
#include "SettingsDialog.g.cpp"
#include "CTuxSettings.h"
#include <filesystem>
#include <ShlObj.h>
#include <shobjidl.h>
#include <winrt/Windows.UI.Xaml.Shapes.h>

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Xaml::Controls;
using namespace winrt::Windows::UI::Xaml::Media;

namespace
{
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
}

namespace winrt::TerminalApp::implementation
{
    SettingsDialog::SettingsDialog()
    {
        InitializeComponent();

        auto s = ClickTerminal::CTuxSettings::Load();

        ConfigPathBox().Text(hstring{ s.ProjectsConfigPath });
        PluginsFolderBox().Text(hstring{ s.PluginsFolder });
        GitPluginToggle().IsOn(s.GitPluginEnabled);
        PortPluginToggle().IsOn(s.PortPluginEnabled);

        // Build _allThemes = built-ins + custom
        _allThemes = ClickTerminal::GetBuiltInThemes();
        for (auto& ct : s.CustomThemes)
            _allThemes.push_back(ct);

        _PopulateThemeCombo();
        _RefreshPluginsList();

        NavList().SelectedIndex(0);
    }

    // -----------------------------------------------------------------------
    // Public getters
    // -----------------------------------------------------------------------
    hstring SettingsDialog::ProjectsConfigPath() { return ConfigPathBox().Text(); }

    hstring SettingsDialog::Theme()
    {
        // Derive dark/light from selected theme
        auto idx = ThemePresetCombo().SelectedIndex();
        if (idx >= 0 && idx < (int)_allThemes.size())
            return _allThemes[idx].IsLightMode ? L"light" : L"dark";
        return L"dark";
    }

    hstring SettingsDialog::SelectedThemeName()
    {
        auto idx = ThemePresetCombo().SelectedIndex();
        if (idx >= 0 && idx < (int)_allThemes.size())
            return hstring{ _allThemes[idx].Name };
        return L"CTux Dark";
    }

    hstring SettingsDialog::PluginsFolder() { return PluginsFolderBox().Text(); }

    bool SettingsDialog::GitPluginEnabled()  { return GitPluginToggle().IsOn(); }
    bool SettingsDialog::PortPluginEnabled() { return PortPluginToggle().IsOn(); }

    std::vector<ClickTerminal::CTuxTheme> SettingsDialog::GetUpdatedCustomThemes()
    {
        // Sync any in-progress edit before returning
        _SyncEditingTheme();

        std::vector<ClickTerminal::CTuxTheme> result;
        for (auto& t : _allThemes)
            if (!t.IsBuiltIn) result.push_back(t);
        return result;
    }

    // -----------------------------------------------------------------------
    // Apply theme colors to dialog surfaces (before ShowAsync)
    // -----------------------------------------------------------------------
    void SettingsDialog::ApplyTheme(const ClickTerminal::CTuxTheme& theme)
    {
        try
        {
            ContentGrid().Background(SolidColorBrush{ ParseHexColor(theme.Colors.DialogBg) });
            NavPanel().Background(SolidColorBrush{ ParseHexColor(theme.Colors.DialogNavBg) });
            RequestedTheme(theme.IsLightMode ? ElementTheme::Light : ElementTheme::Dark);
        }
        catch (...) {}
    }

    // -----------------------------------------------------------------------
    // Nav selection
    // -----------------------------------------------------------------------
    void SettingsDialog::_NavSelectionChanged(const IInspectable& /*sender*/,
                                               const SelectionChangedEventArgs& e)
    {
        if (e.AddedItems().Size() == 0) return;
        auto item = e.AddedItems().GetAt(0).try_as<ListViewItem>();
        if (!item) return;
        auto tagStr = unbox_value_or<hstring>(item.Tag(), L"general");
        GeneralPanel().Visibility(tagStr == L"general" ? Visibility::Visible : Visibility::Collapsed);
        StylePanel().Visibility(tagStr == L"style"    ? Visibility::Visible : Visibility::Collapsed);
        PluginsPanel().Visibility(tagStr == L"plugins" ? Visibility::Visible : Visibility::Collapsed);
    }

    // -----------------------------------------------------------------------
    // Style tab — theme preset combo
    // -----------------------------------------------------------------------
    void SettingsDialog::_PopulateThemeCombo()
    {
        auto s = ClickTerminal::CTuxSettings::Load();
        _suppressThemeChange = true;
        ThemePresetCombo().Items().Clear();

        int selectIdx = 0;
        for (int i = 0; i < (int)_allThemes.size(); ++i)
        {
            ComboBoxItem item;
            item.Content(box_value(hstring{ _allThemes[i].Name }));
            ThemePresetCombo().Items().Append(item);
            if (_allThemes[i].Name == s.SelectedThemeName) selectIdx = i;
        }

        ThemePresetCombo().SelectedIndex(selectIdx);
        _suppressThemeChange = false;

        if (selectIdx < (int)_allThemes.size())
        {
            _editingTheme = _allThemes[selectIdx];
            bool isCustom = !_editingTheme.IsBuiltIn;
            DeleteThemeBtn().IsEnabled(isCustom);
            ColorEditorPanel().Visibility(isCustom ? Visibility::Visible : Visibility::Collapsed);
            if (isCustom) _LoadColorEditor(_editingTheme);
        }
    }

    void SettingsDialog::_ThemePresetChanged(const IInspectable& /*sender*/,
                                              const SelectionChangedEventArgs& /*e*/)
    {
        if (_suppressThemeChange) return;

        auto idx = ThemePresetCombo().SelectedIndex();
        if (idx < 0 || idx >= (int)_allThemes.size()) return;

        // Save edits to previous custom theme before switching
        _SyncEditingTheme();

        _editingTheme = _allThemes[idx];
        bool isCustom = !_editingTheme.IsBuiltIn;
        DeleteThemeBtn().IsEnabled(isCustom);
        ColorEditorPanel().Visibility(isCustom ? Visibility::Visible : Visibility::Collapsed);
        if (isCustom) _LoadColorEditor(_editingTheme);
    }

    void SettingsDialog::_AddThemeClicked(const IInspectable& /*sender*/, const RoutedEventArgs& /*e*/)
    {
        // Copy selected theme as new custom theme
        auto idx = ThemePresetCombo().SelectedIndex();
        ClickTerminal::CTuxTheme newTheme = (idx >= 0 && idx < (int)_allThemes.size())
            ? _allThemes[idx]
            : ClickTerminal::GetBuiltInThemes()[0];

        // Generate unique name
        newTheme.IsBuiltIn = false;
        newTheme.Name = L"Custom Theme";
        int suffix = 1;
        while (true)
        {
            bool exists = false;
            for (auto& t : _allThemes) if (t.Name == newTheme.Name) { exists = true; break; }
            if (!exists) break;
            newTheme.Name = L"Custom Theme " + std::to_wstring(++suffix);
        }

        _allThemes.push_back(newTheme);

        // Add to combo and select it
        _suppressThemeChange = true;
        ComboBoxItem item;
        item.Content(box_value(hstring{ newTheme.Name }));
        ThemePresetCombo().Items().Append(item);
        ThemePresetCombo().SelectedIndex((int)_allThemes.size() - 1);
        _suppressThemeChange = false;

        _editingTheme = newTheme;
        DeleteThemeBtn().IsEnabled(true);
        ColorEditorPanel().Visibility(Visibility::Visible);
        _LoadColorEditor(_editingTheme);
    }

    void SettingsDialog::_DeleteThemeClicked(const IInspectable& /*sender*/, const RoutedEventArgs& /*e*/)
    {
        auto idx = ThemePresetCombo().SelectedIndex();
        if (idx < 0 || idx >= (int)_allThemes.size() || _allThemes[idx].IsBuiltIn) return;

        _allThemes.erase(_allThemes.begin() + idx);

        _suppressThemeChange = true;
        ThemePresetCombo().Items().RemoveAt(idx);
        int newIdx = (idx > 0) ? idx - 1 : 0;
        ThemePresetCombo().SelectedIndex(newIdx);
        _suppressThemeChange = false;

        if (newIdx < (int)_allThemes.size())
        {
            _editingTheme = _allThemes[newIdx];
            bool isCustom = !_editingTheme.IsBuiltIn;
            DeleteThemeBtn().IsEnabled(isCustom);
            ColorEditorPanel().Visibility(isCustom ? Visibility::Visible : Visibility::Collapsed);
            if (isCustom) _LoadColorEditor(_editingTheme);
        }
    }

    // -----------------------------------------------------------------------
    // Color editor helpers
    // -----------------------------------------------------------------------
    void SettingsDialog::_LoadColorEditor(const ClickTerminal::CTuxTheme& theme)
    {
        auto& c = theme.Colors;
        ThemeNameBox().Text(hstring{ theme.Name });
        BaseModeCombo().SelectedIndex(theme.IsLightMode ? 1 : 0);

        ColorSidebarBg().Text(hstring{ c.SidebarBg });
        ColorSidebarHeaderBg().Text(hstring{ c.SidebarHeaderBg });
        ColorSidebarText().Text(hstring{ c.SidebarText });
        ColorSidebarTextMuted().Text(hstring{ c.SidebarTextMuted });
        ColorTabBarBg().Text(hstring{ c.TabBarBg });
        ColorDialogBg().Text(hstring{ c.DialogBg });
        ColorDialogNavBg().Text(hstring{ c.DialogNavBg });

        _UpdateSwatch(L"SidebarBg",        c.SidebarBg);
        _UpdateSwatch(L"SidebarHeaderBg",  c.SidebarHeaderBg);
        _UpdateSwatch(L"SidebarText",      c.SidebarText);
        _UpdateSwatch(L"SidebarTextMuted", c.SidebarTextMuted);
        _UpdateSwatch(L"TabBarBg",         c.TabBarBg);
        _UpdateSwatch(L"DialogBg",         c.DialogBg);
        _UpdateSwatch(L"DialogNavBg",      c.DialogNavBg);

        // Select terminal scheme
        auto scheme = c.TerminalScheme;
        for (uint32_t i = 0; i < SchemeCombo().Items().Size(); ++i)
        {
            auto ci = SchemeCombo().Items().GetAt(i).try_as<ComboBoxItem>();
            if (ci)
            {
                auto tag = std::wstring{ unbox_value_or<hstring>(ci.Tag(), L"") };
                if (tag == scheme) { SchemeCombo().SelectedIndex(i); break; }
            }
        }
    }

    void SettingsDialog::_UpdateSwatch(const std::wstring& fieldName, const std::wstring& hexColor)
    {
        try
        {
            auto swatch = FindName(hstring{ L"Swatch" + fieldName })
                          .try_as<winrt::Windows::UI::Xaml::Shapes::Rectangle>();
            if (swatch && !hexColor.empty())
                swatch.Fill(SolidColorBrush{ ParseHexColor(hexColor) });
        }
        catch (...) {}
    }

    void SettingsDialog::_ColorLostFocus(const IInspectable& sender, const RoutedEventArgs& /*e*/)
    {
        auto tb = sender.try_as<TextBox>();
        if (!tb) return;
        auto tag = std::wstring{ unbox_value_or<hstring>(tb.Tag(), L"") };
        auto hex = std::wstring{ tb.Text() };
        if (!hex.empty() && hex[0] != L'#') hex = L"#" + hex;

        _UpdateSwatch(tag, hex);

        // Update _editingTheme colors
        auto& c = _editingTheme.Colors;
        if      (tag == L"SidebarBg")        c.SidebarBg        = hex;
        else if (tag == L"SidebarHeaderBg")  c.SidebarHeaderBg  = hex;
        else if (tag == L"SidebarText")      c.SidebarText      = hex;
        else if (tag == L"SidebarTextMuted") c.SidebarTextMuted = hex;
        else if (tag == L"TabBarBg")         c.TabBarBg         = hex;
        else if (tag == L"DialogBg")         c.DialogBg         = hex;
        else if (tag == L"DialogNavBg")      c.DialogNavBg      = hex;
    }

    void SettingsDialog::_SyncEditingTheme()
    {
        auto idx = ThemePresetCombo().SelectedIndex();
        if (idx < 0 || idx >= (int)_allThemes.size() || _allThemes[idx].IsBuiltIn) return;

        // Apply name and base mode
        try
        {
            _editingTheme.Name        = std::wstring{ ThemeNameBox().Text() };
            _editingTheme.IsLightMode = (BaseModeCombo().SelectedIndex() == 1);

            // Apply terminal scheme
            auto si = SchemeCombo().SelectedIndex();
            if (si >= 0 && si < (int)SchemeCombo().Items().Size())
            {
                auto ci = SchemeCombo().Items().GetAt(si).try_as<ComboBoxItem>();
                if (ci) _editingTheme.Colors.TerminalScheme = std::wstring{ unbox_value_or<hstring>(ci.Tag(), L"") };
            }
        }
        catch (...) {}

        _allThemes[idx] = _editingTheme;

        // Sync combo label
        try
        {
            auto ci = ThemePresetCombo().Items().GetAt(idx).try_as<ComboBoxItem>();
            if (ci) ci.Content(box_value(hstring{ _editingTheme.Name }));
        }
        catch (...) {}
    }

    // -----------------------------------------------------------------------
    // Browse helpers
    // -----------------------------------------------------------------------
    void SettingsDialog::_BrowseFolder(TextBox target)
    {
        IFileOpenDialog* pfd = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&pfd));
        if (SUCCEEDED(hr))
        {
            DWORD dwOptions = 0;
            pfd->GetOptions(&dwOptions);
            pfd->SetOptions(dwOptions | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
            hr = pfd->Show(nullptr);
            if (SUCCEEDED(hr))
            {
                IShellItem* psi = nullptr;
                if (SUCCEEDED(pfd->GetResult(&psi)))
                {
                    PWSTR pszPath = nullptr;
                    if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &pszPath)))
                    {
                        target.Text(hstring{ pszPath });
                        CoTaskMemFree(pszPath);
                    }
                    psi->Release();
                }
            }
            pfd->Release();
        }
    }

    void SettingsDialog::_BrowseConfigClicked(const IInspectable& /*sender*/, const RoutedEventArgs& /*e*/)
    {
        IFileOpenDialog* pfd = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&pfd));
        if (SUCCEEDED(hr))
        {
            DWORD dwOptions = 0;
            pfd->GetOptions(&dwOptions);
            pfd->SetOptions(dwOptions | FOS_FORCEFILESYSTEM);
            COMDLG_FILTERSPEC filter[] = {
                { L"JSON Files", L"*.json" },
                { L"All Files",  L"*.*"    }
            };
            pfd->SetFileTypes(2, filter);
            pfd->SetDefaultExtension(L"json");
            pfd->SetFileName(L"clickterminal.json");
            hr = pfd->Show(nullptr);
            if (SUCCEEDED(hr))
            {
                IShellItem* psi = nullptr;
                if (SUCCEEDED(pfd->GetResult(&psi)))
                {
                    PWSTR pszPath = nullptr;
                    if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &pszPath)))
                    {
                        ConfigPathBox().Text(hstring{ pszPath });
                        CoTaskMemFree(pszPath);
                    }
                    psi->Release();
                }
            }
            pfd->Release();
        }
    }

    void SettingsDialog::_CheckUpdateClicked(const IInspectable& /*sender*/, const RoutedEventArgs& /*e*/)
    {
        UpdateStatusText().Text(L"Update check — coming soon.");
    }

    void SettingsDialog::_BrowsePluginsFolderClicked(const IInspectable& /*sender*/, const RoutedEventArgs& /*e*/)
    {
        _BrowseFolder(PluginsFolderBox());
        _RefreshPluginsList();
    }

    void SettingsDialog::_RefreshPluginsList()
    {
        PluginList().Items().Clear();
        auto folder = std::wstring{ PluginsFolderBox().Text() };
        if (folder.empty() || !std::filesystem::is_directory(folder)) return;

        for (const auto& entry : std::filesystem::directory_iterator(folder))
        {
            if (!entry.is_directory()) continue;
            if (!std::filesystem::exists(entry.path() / L"ctux-plugin.json")) continue;

            TextBlock tb;
            tb.Text(hstring{ L"• " + entry.path().filename().wstring() });
            tb.Padding({ 4, 2, 4, 2 });
            PluginList().Items().Append(tb);
        }
    }
}
