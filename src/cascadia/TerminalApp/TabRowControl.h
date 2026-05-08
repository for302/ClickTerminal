// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "winrt/Microsoft.UI.Xaml.Controls.h"

#include "TabRowControl.g.h"

namespace winrt::TerminalApp::implementation
{
    struct TabRowControl : TabRowControlT<TabRowControl>
    {
        TabRowControl();

        void OnNewTabButtonClick(const Windows::Foundation::IInspectable& sender, const Microsoft::UI::Xaml::Controls::SplitButtonClickEventArgs& args);
        void OnNewTabButtonDrop(const winrt::Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::DragEventArgs& e);
        void OnNewTabButtonDragOver(const winrt::Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::DragEventArgs& e);
        void OnLayoutButtonClick(const winrt::Windows::Foundation::IInspectable& sender,
                                 const winrt::Windows::UI::Xaml::RoutedEventArgs& e);
        void OnSidebarToggleClick(const winrt::Windows::Foundation::IInspectable& sender,
                                  const winrt::Windows::UI::Xaml::RoutedEventArgs& e);

        // Apply CTux theme colors directly (called from TerminalPage)
        void ApplyTheme(winrt::Windows::UI::Color stripBg, winrt::Windows::UI::Color tabItemBg, winrt::Windows::UI::Color textColor);

        // Accessor for TerminalPage to ShowAt the layout button
        winrt::Windows::UI::Xaml::Controls::Button GetLayoutButton() { return LayoutButton(); }

        til::typed_event<winrt::Windows::Foundation::IInspectable,
                         winrt::Windows::Foundation::IInspectable> LayoutButtonClicked;
        til::typed_event<winrt::Windows::Foundation::IInspectable,
                         winrt::Windows::Foundation::IInspectable> SidebarToggleClicked;

        til::property_changed_event PropertyChanged;
        WINRT_OBSERVABLE_PROPERTY(bool, ShowElevationShield, PropertyChanged.raise, false);
    };
}

namespace winrt::TerminalApp::factory_implementation
{
    BASIC_FACTORY(TabRowControl);
}
