// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "SettingsDialog.h"
#include "SettingsDialog.g.cpp"
#include "CTuxSettings.h"
#include "CTuxVersion.h"
#include "CTuxClaudeStatusLine.h"
#include <filesystem>
#include <fstream>
#include <ShlObj.h>
#include <shobjidl.h>
#include <winrt/Windows.UI.Xaml.Shapes.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Headers.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Storage.Streams.h>

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

    // Strict parse: returns false (and leaves `out` untouched) unless hex is a
    // valid "#RRGGBB"/"RRGGBB" — used where invalid edits must be ignored.
    static bool TryParseHexColor(const std::wstring& hex, winrt::Windows::UI::Color& out)
    {
        const auto v = ClickTerminal::ParseThemeHex(hex, 0);
        if (v == 0) return false; // valid results always carry 0xFF alpha
        out = { uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v) };
        return true;
    }

    // Field-name → CTuxThemeColors member lookup (shared by editor/preview/picker)
    static std::wstring* ColorFieldPtr(ClickTerminal::CTuxThemeColors& c, const std::wstring& name)
    {
        if (name == L"SidebarBg")        return &c.SidebarBg;
        if (name == L"SidebarHeaderBg")  return &c.SidebarHeaderBg;
        if (name == L"SidebarText")      return &c.SidebarText;
        if (name == L"SidebarTextMuted") return &c.SidebarTextMuted;
        if (name == L"TabBarBg")         return &c.TabBarBg;
        if (name == L"DialogBg")         return &c.DialogBg;
        if (name == L"DialogNavBg")      return &c.DialogNavBg;
        if (name == L"PaneHeaderBg")     return &c.PaneHeaderBg;
        if (name == L"PaneHeaderText")   return &c.PaneHeaderText;
        if (name == L"PaneHeaderAccent") return &c.PaneHeaderAccent;
        if (name == L"ExitOverlayBg")    return &c.ExitOverlayBg;
        if (name == L"PaneBorder")       return &c.PaneBorder;
        return nullptr;
    }

    // Color field ↔ preview region element (x:Name) mapping
    struct PreviewRegion { const wchar_t* field; const wchar_t* element; };
    static constexpr PreviewRegion kPreviewRegions[] = {
        { L"SidebarBg",        L"PrevSidebar" },
        { L"SidebarHeaderBg",  L"PrevSidebarHeader" },
        { L"SidebarText",      L"PrevSidebarText" },
        { L"SidebarTextMuted", L"PrevSidebarTextMuted" },
        { L"TabBarBg",         L"PrevTabBar" },
        { L"PaneHeaderBg",     L"PrevPaneHeader" },
        { L"PaneHeaderText",   L"PrevPaneHeaderText" },
        { L"PaneHeaderAccent", L"PrevPaneHeaderAccent" },
        { L"ExitOverlayBg",    L"PrevExitOverlay" },
        { L"PaneBorder",       L"PrevPaneBorder" },
        { L"DialogBg",         L"PrevDialog" },
        { L"DialogNavBg",      L"PrevDialogNav" },
    };
}

namespace winrt::TerminalApp::implementation
{
    SettingsDialog::SettingsDialog()
    {
        InitializeComponent();

        auto s = ClickTerminal::CTuxSettings::Load();

        ConfigPathBox().Text(hstring{ s.ProjectsConfigPath });

        // Build _allThemes = built-ins + custom
        _allThemes = ClickTerminal::GetBuiltInThemes();
        for (auto& ct : s.CustomThemes)
            _allThemes.push_back(ct);

        _PopulateThemeCombo();
        _LoadStatusLineState();

        if (const auto build = ClickTerminal::CurrentVersionBuild(); build != 0)
        {
            UpdateStatusText().Text(hstring{ L"Installed " + ClickTerminal::FormatVersion(build) + L"." });
        }

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
    // Dialog lifecycle — resize ContentGrid with parent window
    // -----------------------------------------------------------------------
    void SettingsDialog::_UpdateGridSize(winrt::Windows::Foundation::Size windowSize)
    {
        double w = std::max(620.0, (double)windowSize.Width  * 0.78);
        double h = std::max(400.0, (double)windowSize.Height * 0.65);

        if (_bgElement) { _bgElement.Width(w); }

        // Do NOT set ContentGrid.Width — let it Stretch within BackgroundElement's padded
        // ContentPresenter (explicit Width causes left-edge clipping: "eneral" bug).
        ContentGrid().Height(h);
    }

    void SettingsDialog::_DialogOpened(const winrt::Windows::UI::Xaml::Controls::ContentDialog& /*sender*/,
                                        const winrt::Windows::UI::Xaml::Controls::ContentDialogOpenedEventArgs& /*args*/)
    {
        // Belt-and-suspenders: also set LayoutRoot to Transparent directly in addition to
        // the Resources override in ApplyTheme. Covers any post-template-apply reset.
        _layoutRoot = GetTemplateChild(hstring{L"LayoutRoot"}).try_as<Grid>();
        if (_layoutRoot)
            _layoutRoot.Background(SolidColorBrush{ winrt::Windows::UI::Colors::Transparent() });

        _bgElement = GetTemplateChild(hstring{L"BackgroundElement"}).try_as<FrameworkElement>();

        if (auto root = XamlRoot())
        {
            _UpdateGridSize(root.Size());
            _rootSizeToken = root.Changed([this](const winrt::Windows::UI::Xaml::XamlRoot& r,
                                                  const winrt::Windows::UI::Xaml::XamlRootChangedEventArgs&) {
                _UpdateGridSize(r.Size());
            });
        }
    }

    void SettingsDialog::_DialogClosed(const winrt::Windows::UI::Xaml::Controls::ContentDialog& /*sender*/,
                                        const winrt::Windows::UI::Xaml::Controls::ContentDialogClosedEventArgs& /*args*/)
    {
        if (auto root = XamlRoot())
        {
            root.Changed(_rootSizeToken);
            _rootSizeToken = {};
        }
        _bgElement  = nullptr;
        _layoutRoot = nullptr;
    }

    // -----------------------------------------------------------------------
    // Apply theme colors to dialog surfaces (before ShowAsync)
    // -----------------------------------------------------------------------
    void SettingsDialog::ApplyTheme(const ClickTerminal::CTuxTheme& theme)
    {
        try
        {
            auto bgBrush = SolidColorBrush{ ParseHexColor(theme.Colors.DialogBg) };

            // ContentDialogBackground → BackgroundElement.Background via ThemeResource
            Resources().Insert(winrt::box_value(hstring{L"ContentDialogBackground"}), bgBrush);

            // Make LayoutRoot transparent so the SmokeLayerBackground (#99000000) shows through
            // properly — revealing the dimmed app content behind the dialog (standard smoke effect).
            // Without this, RequestedTheme() re-evaluation resets LayoutRoot to white/gray.
            Resources().Insert(winrt::box_value(hstring{L"SystemControlPageBackgroundMediumAltMediumBrush"}),
                               SolidColorBrush{ winrt::Windows::UI::Colors::Transparent() });

            Background(bgBrush);
            ContentGrid().Background(bgBrush);
            NavPanel().Background(SolidColorBrush{ ParseHexColor(theme.Colors.DialogNavBg) });
            // RequestedTheme LAST — ThemeResource re-evaluation now finds both our overrides above
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
            ThemeColorPicker().Visibility(isCustom ? Visibility::Visible : Visibility::Collapsed);
            if (isCustom) _LoadColorEditor(_editingTheme);
            _UpdatePreview();
            _SelectColorField(_selectedColorField);
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
        ThemeColorPicker().Visibility(isCustom ? Visibility::Visible : Visibility::Collapsed);
        if (isCustom) _LoadColorEditor(_editingTheme);
        _UpdatePreview();
        _SelectColorField(_selectedColorField);
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
        ThemeColorPicker().Visibility(Visibility::Visible);
        _LoadColorEditor(_editingTheme);
        _UpdatePreview();
        _SelectColorField(_selectedColorField);
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
            ThemeColorPicker().Visibility(isCustom ? Visibility::Visible : Visibility::Collapsed);
            if (isCustom) _LoadColorEditor(_editingTheme);
            _UpdatePreview();
            _SelectColorField(_selectedColorField);
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
        ColorPaneHeaderBg().Text(hstring{ c.PaneHeaderBg });
        ColorPaneHeaderText().Text(hstring{ c.PaneHeaderText });
        ColorPaneHeaderAccent().Text(hstring{ c.PaneHeaderAccent });
        ColorExitOverlayBg().Text(hstring{ c.ExitOverlayBg });
        ColorPaneBorder().Text(hstring{ c.PaneBorder });

        _UpdateSwatch(L"SidebarBg",        c.SidebarBg);
        _UpdateSwatch(L"SidebarHeaderBg",  c.SidebarHeaderBg);
        _UpdateSwatch(L"SidebarText",      c.SidebarText);
        _UpdateSwatch(L"SidebarTextMuted", c.SidebarTextMuted);
        _UpdateSwatch(L"TabBarBg",         c.TabBarBg);
        _UpdateSwatch(L"DialogBg",         c.DialogBg);
        _UpdateSwatch(L"DialogNavBg",      c.DialogNavBg);
        _UpdateSwatch(L"PaneHeaderBg",     c.PaneHeaderBg);
        _UpdateSwatch(L"PaneHeaderText",   c.PaneHeaderText);
        _UpdateSwatch(L"PaneHeaderAccent", c.PaneHeaderAccent);
        _UpdateSwatch(L"ExitOverlayBg",    c.ExitOverlayBg);
        _UpdateSwatch(L"PaneBorder",       c.PaneBorder);

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

        _ApplyColorEdit(tag, hex, /*updateTextBox*/ false, /*updatePicker*/ true);
    }

    // -----------------------------------------------------------------------
    // Layout preview + color picker
    // -----------------------------------------------------------------------
    const ClickTerminal::CTuxThemeColors& SettingsDialog::_DisplayedColors() const
    {
        // _editingTheme always mirrors the selected theme (built-in or custom)
        return _editingTheme.Colors;
    }

    // Central edit path: writes the color into _editingTheme, then refreshes
    // swatch / (optionally) hex TextBox / preview / (optionally) picker.
    void SettingsDialog::_ApplyColorEdit(const std::wstring& fieldName, const std::wstring& hexColor,
                                         bool updateTextBox, bool updatePicker)
    {
        if (fieldName.empty()) return;

        if (auto* field = ColorFieldPtr(_editingTheme.Colors, fieldName))
            *field = hexColor;

        _UpdateSwatch(fieldName, hexColor);

        if (updateTextBox)
        {
            try
            {
                if (auto tb = FindName(hstring{ L"Color" + fieldName }).try_as<TextBox>())
                    tb.Text(hstring{ hexColor });
            }
            catch (...) {}
        }

        _UpdatePreview();

        if (updatePicker && fieldName == _selectedColorField)
            _SyncPickerToSelectedField();
    }

    void SettingsDialog::_UpdatePreview()
    {
        try
        {
            auto colors = _DisplayedColors(); // copy — ColorFieldPtr needs non-const
            for (const auto& region : kPreviewRegions)
            {
                auto border = FindName(hstring{ region.element }).try_as<Border>();
                if (!border) continue;
                const auto* value = ColorFieldPtr(colors, region.field);
                winrt::Windows::UI::Color c{};
                if (value && TryParseHexColor(*value, c))
                    border.Background(SolidColorBrush{ c }); // invalid hex → keep last valid
            }
        }
        catch (...) {}
    }

    void SettingsDialog::_SelectColorField(const std::wstring& fieldName)
    {
        if (!fieldName.empty())
            _selectedColorField = fieldName;

        try
        {
            // Selection marker: orange, deliberately outside the theme palette
            const SolidColorBrush accent{ winrt::Windows::UI::Color{ 0xFF, 0xFF, 0x8C, 0x00 } };
            const SolidColorBrush transparent{ winrt::Windows::UI::Colors::Transparent() };
            for (const auto& region : kPreviewRegions)
            {
                auto border = FindName(hstring{ region.element }).try_as<Border>();
                if (border)
                    border.BorderBrush(_selectedColorField == region.field ? accent : transparent);
            }
        }
        catch (...) {}

        _SyncPickerToSelectedField();
    }

    void SettingsDialog::_SyncPickerToSelectedField()
    {
        try
        {
            auto colors = _DisplayedColors();
            const auto* value = ColorFieldPtr(colors, _selectedColorField);
            winrt::Windows::UI::Color c{};
            if (!value || !TryParseHexColor(*value, c)) return;
            _suppressPickerChange = true;
            ThemeColorPicker().Color(c);
        }
        catch (...) {}
        _suppressPickerChange = false;
    }

    void SettingsDialog::_PreviewRegionTapped(const IInspectable& sender,
                                              const winrt::Windows::UI::Xaml::Input::TappedRoutedEventArgs& e)
    {
        // Stop bubbling so nested regions (e.g. header inside sidebar) don't
        // immediately get overridden by their parent region.
        e.Handled(true);

        auto fe = sender.try_as<FrameworkElement>();
        if (!fe) return;
        auto tag = std::wstring{ unbox_value_or<hstring>(fe.Tag(), L"") };
        if (tag.empty()) return;
        _SelectColorField(tag);
    }

    void SettingsDialog::_PickerColorChanged(const IInspectable& /*sender*/,
                                             const winrt::Microsoft::UI::Xaml::Controls::ColorChangedEventArgs& args)
    {
        if (_suppressPickerChange) return;
        if (_editingTheme.IsBuiltIn) return; // built-ins are read-only

        const auto c = args.NewColor();
        wchar_t buf[8];
        swprintf_s(buf, L"#%02X%02X%02X", c.R, c.G, c.B);
        _ApplyColorEdit(_selectedColorField, buf, /*updateTextBox*/ true, /*updatePicker*/ false);
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

    // ---- Updates ------------------------------------------------------------
    //
    // One button, three jobs. Idle asks GitHub for the newest release; if that
    // release is newer than the installed package the button turns into a
    // download button, and the download hands off to ClickTerminal-Setup.exe,
    // which shuts this app down and upgrades it in place.

    // -----------------------------------------------------------------------
    // Claude Code status line
    //
    // Claude Code renders the bottom bar only when `statusLine` in
    // ~/.claude/settings.json points at a command, so the toggle writes that
    // file directly. That is outside the dialog's Save/Cancel scope, so it
    // takes effect on flip and reports success or failure inline.
    // -----------------------------------------------------------------------
    void SettingsDialog::_LoadStatusLineState()
    {
        namespace SL = ClickTerminal::ClaudeStatusLine;

        const bool on = SL::IsEnabled();

        _suppressStatusLineToggle = true;
        StatusLineToggle().IsOn(on);
        _suppressStatusLineToggle = false;

        if (on)
            StatusLineHint().Text(L"On — restart Claude Code, or start a new session, to see it.");
        else if (SL::HasForeignStatusLine())
            StatusLineHint().Text(L"A different status line is configured. Turning this on replaces it; turning it off restores it.");
        else
            StatusLineHint().Text(L"");
    }

    void SettingsDialog::_StatusLineToggled(const IInspectable& /*sender*/, const RoutedEventArgs& /*e*/)
    {
        if (_suppressStatusLineToggle) return;

        namespace SL = ClickTerminal::ClaudeStatusLine;

        const bool wantOn = StatusLineToggle().IsOn();
        std::wstring error;
        const bool ok = wantOn ? SL::Enable(error) : SL::Disable(error);

        if (!ok)
        {
            // Snap the switch back so it never claims a state we failed to write.
            _suppressStatusLineToggle = true;
            StatusLineToggle().IsOn(!wantOn);
            _suppressStatusLineToggle = false;
            StatusLineHint().Text(winrt::hstring{ L"Failed: " + error });
            return;
        }

        StatusLineHint().Text(wantOn
            ? winrt::hstring{ L"On — restart Claude Code, or start a new session, to see it." }
            : winrt::hstring{ L"Off — the statusLine entry was removed from ~/.claude/settings.json." });
    }

    void SettingsDialog::_SetUpdateIdle(const std::wstring& status)
    {
        _updateState = UpdateState::Idle;
        CheckUpdateBtn().Content(winrt::box_value(winrt::hstring{ L"Check for Updates" }));
        CheckUpdateBtn().IsEnabled(true);
        UpdateStatusText().Text(winrt::hstring{ status });
    }

    void SettingsDialog::_CheckUpdateClicked(const IInspectable& /*sender*/, const RoutedEventArgs& /*e*/)
    {
        switch (_updateState)
        {
        case UpdateState::Idle:
            _RunUpdateCheck();
            break;
        case UpdateState::Available:
            _RunUpdateDownload();
            break;
        default:
            break; // a check or download is already in flight
        }
    }

    winrt::fire_and_forget SettingsDialog::_RunUpdateCheck()
    {
        auto strongThis{ get_strong() };

        _updateState = UpdateState::Checking;
        CheckUpdateBtn().IsEnabled(false);
        UpdateStatusText().Text(L"Checking for updates...");

        const auto current = ClickTerminal::CurrentVersionBuild();

        std::wstring tag;
        std::wstring url;
        std::wstring error;

        co_await winrt::resume_background();
        try
        {
            winrt::Windows::Web::Http::HttpClient client;
            // GitHub rejects API requests that arrive without a User-Agent.
            client.DefaultRequestHeaders().UserAgent().Append(
                winrt::Windows::Web::Http::Headers::HttpProductInfoHeaderValue{ L"ClickTerminal", L"1.0" });

            const auto body = co_await client.GetStringAsync(
                winrt::Windows::Foundation::Uri{ winrt::hstring{ ClickTerminal::LatestReleaseApiUrl } });

            const auto release = winrt::Windows::Data::Json::JsonObject::Parse(body);
            tag = release.GetNamedString(L"tag_name", L"");

            if (release.HasKey(L"assets"))
            {
                for (const auto& entry : release.GetNamedArray(L"assets"))
                {
                    const auto asset = entry.GetObject();
                    if (asset.GetNamedString(L"name", L"") == winrt::hstring{ ClickTerminal::SetupAssetName })
                    {
                        url = asset.GetNamedString(L"browser_download_url", L"");
                        break;
                    }
                }
            }
        }
        catch (...)
        {
            error = L"Update check failed - check your connection.";
        }

        co_await winrt::resume_foreground(Dispatcher());

        if (!error.empty())
        {
            _SetUpdateIdle(error);
            co_return;
        }

        const auto latest = ClickTerminal::ParseVersionTag(tag);
        if (latest == 0)
        {
            _SetUpdateIdle(L"Could not read the latest version from GitHub.");
            co_return;
        }

        if (current == 0)
        {
            // Unpackaged dev run - there is no installed version to compare against.
            _SetUpdateIdle(L"Latest release is " + ClickTerminal::FormatVersion(latest) +
                           L" (running unpackaged, no update available).");
            co_return;
        }

        if (latest <= current)
        {
            _SetUpdateIdle(L"Up to date (" + ClickTerminal::FormatVersion(current) + L").");
            co_return;
        }

        if (url.empty())
        {
            _SetUpdateIdle(ClickTerminal::FormatVersion(latest) +
                           L" is available, but it has no installer attached.");
            co_return;
        }

        _updateTag = ClickTerminal::FormatVersion(latest);
        _updateDownloadUrl = url;
        _updateState = UpdateState::Available;
        CheckUpdateBtn().Content(winrt::box_value(winrt::hstring{ L"Download && Install " + _updateTag }));
        CheckUpdateBtn().IsEnabled(true);
        UpdateStatusText().Text(winrt::hstring{ _updateTag + L" is available (installed " +
                                                ClickTerminal::FormatVersion(current) + L")." });
    }

    winrt::fire_and_forget SettingsDialog::_RunUpdateDownload()
    {
        auto strongThis{ get_strong() };

        _updateState = UpdateState::Downloading;
        CheckUpdateBtn().IsEnabled(false);
        UpdateStatusText().Text(L"Downloading installer...");

        const auto url = _updateDownloadUrl;
        const auto tag = _updateTag;

        std::wstring setupPath;
        std::wstring error;

        co_await winrt::resume_background();
        try
        {
            winrt::Windows::Web::Http::HttpClient client;
            client.DefaultRequestHeaders().UserAgent().Append(
                winrt::Windows::Web::Http::Headers::HttpProductInfoHeaderValue{ L"ClickTerminal", L"1.0" });

            const auto buffer = co_await client.GetBufferAsync(winrt::Windows::Foundation::Uri{ winrt::hstring{ url } });

            wchar_t tempDir[MAX_PATH]{};
            if (::GetTempPathW(MAX_PATH, tempDir) == 0)
            {
                throw std::runtime_error("no temp path");
            }
            setupPath = std::wstring{ tempDir } + L"ClickTerminal-Setup-" + tag + L".exe";

            std::vector<uint8_t> bytes(buffer.Length());
            winrt::Windows::Storage::Streams::DataReader::FromBuffer(buffer).ReadBytes(bytes);

            std::ofstream file{ setupPath, std::ios::binary | std::ios::trunc };
            file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            file.close();
            if (!file)
            {
                throw std::runtime_error("write failed");
            }
        }
        catch (...)
        {
            error = L"Download failed - try again, or grab the installer from GitHub.";
            setupPath.clear();
        }

        co_await winrt::resume_foreground(Dispatcher());

        if (!error.empty())
        {
            // Keep the download offer up so the user can retry.
            _updateState = UpdateState::Available;
            CheckUpdateBtn().IsEnabled(true);
            UpdateStatusText().Text(winrt::hstring{ error });
            co_return;
        }

        UpdateStatusText().Text(L"Starting installer - ClickTerminal will close and reopen.");
        ::ShellExecuteW(nullptr, L"open", setupPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        Hide();
    }
}
