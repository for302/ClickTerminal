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
        winrt::hstring PluginsFolder();
        bool           GitPluginEnabled();
        bool           PortPluginEnabled();

        // Apply theme colors to dialog surfaces (call before ShowAsync)
        void ApplyTheme(const ClickTerminal::CTuxTheme& theme);

        // XAML event handlers (must be public)
        void _NavSelectionChanged(const winrt::Windows::Foundation::IInspectable& sender,
                                  const winrt::Windows::UI::Xaml::Controls::SelectionChangedEventArgs& e);
        void _BrowseConfigClicked(const winrt::Windows::Foundation::IInspectable& sender,
                                  const winrt::Windows::UI::Xaml::RoutedEventArgs& e);
        void _CheckUpdateClicked(const winrt::Windows::Foundation::IInspectable& sender,
                                 const winrt::Windows::UI::Xaml::RoutedEventArgs& e);
        void _BrowsePluginsFolderClicked(const winrt::Windows::Foundation::IInspectable& sender,
                                         const winrt::Windows::UI::Xaml::RoutedEventArgs& e);

        // Style tab handlers
        void _ThemePresetChanged(const winrt::Windows::Foundation::IInspectable& sender,
                                 const winrt::Windows::UI::Xaml::Controls::SelectionChangedEventArgs& e);
        void _AddThemeClicked(const winrt::Windows::Foundation::IInspectable& sender,
                              const winrt::Windows::UI::Xaml::RoutedEventArgs& e);
        void _DeleteThemeClicked(const winrt::Windows::Foundation::IInspectable& sender,
                                 const winrt::Windows::UI::Xaml::RoutedEventArgs& e);
        void _ColorLostFocus(const winrt::Windows::Foundation::IInspectable& sender,
                             const winrt::Windows::UI::Xaml::RoutedEventArgs& e);

        // Returns updated custom themes (also syncs current editing state)
        std::vector<ClickTerminal::CTuxTheme> GetUpdatedCustomThemes();

    private:
        void _PopulateThemeCombo();
        void _SyncEditingTheme();
        void _LoadColorEditor(const ClickTerminal::CTuxTheme& theme);
        void _UpdateSwatch(const std::wstring& fieldName, const std::wstring& hexColor);
        void _BrowseFolder(winrt::Windows::UI::Xaml::Controls::TextBox target);
        void _RefreshPluginsList();

        // All themes (built-in + custom) for the combo
        std::vector<ClickTerminal::CTuxTheme> _allThemes;
        ClickTerminal::CTuxTheme _editingTheme;
        bool _suppressThemeChange{ false };
    };
}

namespace winrt::TerminalApp::factory_implementation
{
    BASIC_FACTORY(SettingsDialog);
}
