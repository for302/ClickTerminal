// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "SettingsDialog.g.h"
#include "CTuxTheme.h"

namespace winrt::TerminalApp::implementation
{
    struct SettingsDialog : SettingsDialogT<SettingsDialog>
    {
        SettingsDialog();

        winrt::hstring ProjectsConfigPath();
        winrt::hstring Theme();
        winrt::hstring SelectedThemeName();

        // Apply theme colors to dialog surfaces (call before ShowAsync)
        void ApplyTheme(const ClickTerminal::CTuxTheme& theme);

        // XAML event handlers (must be public)
        void _NavSelectionChanged(const winrt::Windows::Foundation::IInspectable& sender,
                                  const winrt::Windows::UI::Xaml::Controls::SelectionChangedEventArgs& e);
        void _BrowseConfigClicked(const winrt::Windows::Foundation::IInspectable& sender,
                                  const winrt::Windows::UI::Xaml::RoutedEventArgs& e);
        void _CheckUpdateClicked(const winrt::Windows::Foundation::IInspectable& sender,
                                 const winrt::Windows::UI::Xaml::RoutedEventArgs& e);

        // Dialog lifecycle — resize with parent window
        void _DialogOpened(const winrt::Windows::UI::Xaml::Controls::ContentDialog& sender,
                           const winrt::Windows::UI::Xaml::Controls::ContentDialogOpenedEventArgs& args);
        void _DialogClosed(const winrt::Windows::UI::Xaml::Controls::ContentDialog& sender,
                           const winrt::Windows::UI::Xaml::Controls::ContentDialogClosedEventArgs& args);

        // Style tab handlers
        void _ThemePresetChanged(const winrt::Windows::Foundation::IInspectable& sender,
                                 const winrt::Windows::UI::Xaml::Controls::SelectionChangedEventArgs& e);
        void _AddThemeClicked(const winrt::Windows::Foundation::IInspectable& sender,
                              const winrt::Windows::UI::Xaml::RoutedEventArgs& e);
        void _DeleteThemeClicked(const winrt::Windows::Foundation::IInspectable& sender,
                                 const winrt::Windows::UI::Xaml::RoutedEventArgs& e);
        void _ColorLostFocus(const winrt::Windows::Foundation::IInspectable& sender,
                             const winrt::Windows::UI::Xaml::RoutedEventArgs& e);
        void _PreviewRegionTapped(const winrt::Windows::Foundation::IInspectable& sender,
                                  const winrt::Windows::UI::Xaml::Input::TappedRoutedEventArgs& e);
        void _PickerColorChanged(const winrt::Windows::Foundation::IInspectable& sender,
                                 const winrt::Microsoft::UI::Xaml::Controls::ColorChangedEventArgs& args);

        // Returns updated custom themes (also syncs current editing state)
        std::vector<ClickTerminal::CTuxTheme> GetUpdatedCustomThemes();

    private:
        // ---- Updates (GitHub releases) ----
        // The single button walks Idle -> Checking -> Available -> Downloading;
        // _CheckUpdateClicked just dispatches on the current state.
        enum class UpdateState
        {
            Idle,
            Checking,
            Available,
            Downloading,
        };

        winrt::fire_and_forget _RunUpdateCheck();
        winrt::fire_and_forget _RunUpdateDownload();
        void _SetUpdateIdle(const std::wstring& status);

        UpdateState _updateState{ UpdateState::Idle };
        std::wstring _updateDownloadUrl;
        std::wstring _updateTag;

        void _PopulateThemeCombo();
        void _SyncEditingTheme();
        void _LoadColorEditor(const ClickTerminal::CTuxTheme& theme);
        void _UpdateSwatch(const std::wstring& fieldName, const std::wstring& hexColor);
        void _UpdatePreview();
        void _SelectColorField(const std::wstring& fieldName);
        void _SyncPickerToSelectedField();
        void _ApplyColorEdit(const std::wstring& fieldName, const std::wstring& hexColor,
                             bool updateTextBox, bool updatePicker);
        const ClickTerminal::CTuxThemeColors& _DisplayedColors() const;

        // All themes (built-in + custom) for the combo
        std::vector<ClickTerminal::CTuxTheme> _allThemes;
        ClickTerminal::CTuxTheme _editingTheme;
        bool _suppressThemeChange{ false };

        // Preview / color-picker state
        std::wstring _selectedColorField{ L"SidebarBg" };
        bool _suppressPickerChange{ false };

        winrt::event_token _rootSizeToken{};
        winrt::Windows::UI::Xaml::FrameworkElement _bgElement{ nullptr };
        winrt::Windows::UI::Xaml::Controls::Grid _layoutRoot{ nullptr };
        void _UpdateGridSize(winrt::Windows::Foundation::Size windowSize);
    };
}

namespace winrt::TerminalApp::factory_implementation
{
    BASIC_FACTORY(SettingsDialog);
}
