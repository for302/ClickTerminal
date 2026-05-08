// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "TabRowControl.h"

#include "TabRowControl.g.cpp"
#include <winrt/Windows.UI.Xaml.Shapes.h>

using namespace winrt::Windows::ApplicationModel::DataTransfer;

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Windows::UI::Text;

namespace winrt
{
    namespace MUX = Microsoft::UI::Xaml;
    namespace WUX = Windows::UI::Xaml;
}

namespace winrt::TerminalApp::implementation
{
    TabRowControl::TabRowControl()
    {
        InitializeComponent();
    }

    // Method Description:
    // - Bound in the Xaml editor to the [+] button.
    // Arguments:
    // <unused>
    void TabRowControl::OnNewTabButtonClick(const IInspectable&, const Controls::SplitButtonClickEventArgs&)
    {
    }

    // Method Description:
    // - Bound in Drag&Drop of the Xaml editor to the [+] button.
    // Arguments:
    // <unused>
    void TabRowControl::OnNewTabButtonDrop(const IInspectable&, const winrt::Windows::UI::Xaml::DragEventArgs&)
    {
    }

    // Method Description:
    // - Bound in Drag-over of the Xaml editor to the [+] button.
    // Allows drop of 'StorageItems' which will be used as StartingDirectory
    // Arguments:
    //  - <unused>
    //  - e: DragEventArgs which hold the items
    void TabRowControl::ApplyTheme(winrt::Windows::UI::Color stripBg, winrt::Windows::UI::Color tabItemBg, winrt::Windows::UI::Color textColor)
    {
        try
        {
            // stripBg  → header (CTux label area), right empty footer, TabViewBackground
            // tabItemBg → tab pill backgrounds (inactive/selected/hover/pressed)
            // This separation creates visual distinction between the strip and the tab pills.
            auto strip = winrt::Windows::UI::Xaml::Media::SolidColorBrush{ stripBg };
            auto fg    = winrt::Windows::UI::Xaml::Media::SolidColorBrush{ textColor };
            auto item  = winrt::Windows::UI::Xaml::Media::SolidColorBrush{ tabItemBg };

            Background(strip);
            CTuxHeaderBorder().Background(strip);
            CTuxHeaderText().Foreground(fg);
            LayoutButtonIcon().Foreground(fg);
            SidebarToggleIcon().Foreground(fg);

            auto res = TabView().Resources();
            res.Insert(winrt::box_value(winrt::hstring(L"TabViewBackground")), strip);

            float lum = 0.2126f * tabItemBg.R + 0.7152f * tabItemBg.G + 0.0722f * tabItemBg.B;
            bool isLight = lum > 127.5f;

            auto makeShift = [&](int d) -> winrt::Windows::UI::Xaml::Media::SolidColorBrush {
                winrt::Windows::UI::Color c{
                    tabItemBg.A,
                    static_cast<uint8_t>(std::clamp(static_cast<int>(tabItemBg.R) + d, 0, 255)),
                    static_cast<uint8_t>(std::clamp(static_cast<int>(tabItemBg.G) + d, 0, 255)),
                    static_cast<uint8_t>(std::clamp(static_cast<int>(tabItemBg.B) + d, 0, 255))
                };
                return winrt::Windows::UI::Xaml::Media::SolidColorBrush{ c };
            };
            int selDelta = isLight ? -25 : 25;
            int hovDelta = isLight ? -12 : 12;

            res.Insert(winrt::box_value(winrt::hstring(L"TabViewItemHeaderBackground")), item);
            res.Insert(winrt::box_value(winrt::hstring(L"TabViewItemHeaderBackgroundSelected")), makeShift(selDelta));
            res.Insert(winrt::box_value(winrt::hstring(L"TabViewItemHeaderBackgroundPointerOver")), makeShift(hovDelta));
            res.Insert(winrt::box_value(winrt::hstring(L"TabViewItemHeaderBackgroundPressed")), makeShift(selDelta));
        }
        catch (...) {}
    }

    void TabRowControl::OnLayoutButtonClick(const IInspectable&, const winrt::Windows::UI::Xaml::RoutedEventArgs&)
    {
        LayoutButtonClicked.raise(*this, nullptr);
    }

    void TabRowControl::OnSidebarToggleClick(const IInspectable&, const winrt::Windows::UI::Xaml::RoutedEventArgs&)
    {
        SidebarToggleClicked.raise(*this, nullptr);
    }

    void TabRowControl::OnNewTabButtonDragOver(const IInspectable&, const winrt::Windows::UI::Xaml::DragEventArgs& e)
    {
        // We can only handle drag/dropping StorageItems (files).
        // If the format on the clipboard is anything else, returning
        // early here will prevent the drag/drop from doing anything.
        if (!e.DataView().Contains(StandardDataFormats::StorageItems()))
        {
            return;
        }

        // Make sure to set the AcceptedOperation, so that we can later receive the path in the Drop event
        e.AcceptedOperation(DataPackageOperation::Copy);

        const auto modifiers = static_cast<uint32_t>(e.Modifiers());
        if (WI_IsFlagSet(modifiers, static_cast<uint32_t>(DragDrop::DragDropModifiers::Alt)))
        {
            e.DragUIOverride().Caption(RS_(L"DropPathTabSplit/Text"));
        }
        else if (WI_IsFlagSet(modifiers, static_cast<uint32_t>(DragDrop::DragDropModifiers::Shift)))
        {
            e.DragUIOverride().Caption(RS_(L"DropPathTabNewWindow/Text"));
        }
        else
        {
            e.DragUIOverride().Caption(RS_(L"DropPathTabRun/Text"));
        }

        // Sets if the caption is visible
        e.DragUIOverride().IsCaptionVisible(true);
        // Sets if the dragged content is visible
        e.DragUIOverride().IsContentVisible(false);
        // Sets if the glyph is visible
        e.DragUIOverride().IsGlyphVisible(false);
    }
}
