// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "ContextMeter.h"
#include "ContextMeter.g.cpp"

using namespace winrt;
using namespace winrt::Windows::UI;
using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Xaml::Controls;
using namespace winrt::Windows::UI::Xaml::Shapes;
using namespace winrt::Windows::UI::Xaml::Media;

namespace winrt::TerminalApp::implementation
{
    ContextMeter::ContextMeter()
    {
        InitializeComponent();
    }

    void ContextMeter::SetUsage(uint32_t usedTokens, uint32_t totalTokens)
    {
        _usedTokens  = usedTokens;
        _totalTokens = totalTokens;

        if (totalTokens > 0)
        {
            uint32_t pct = static_cast<uint32_t>((static_cast<uint64_t>(usedTokens) * 100ULL) / totalTokens);
            hstring tip{ std::to_wstring(pct) + L"% (" +
                         std::to_wstring(usedTokens / 1000) + L"k / " +
                         std::to_wstring(totalTokens / 1000) + L"k tokens)" };
            ToolTipService::SetToolTip(SegmentPanel(), box_value(tip));
        }
        else
        {
            ToolTipService::SetToolTip(SegmentPanel(), nullptr);
        }

        _UpdateSegments();
    }

    void ContextMeter::Reset()
    {
        _usedTokens  = 0;
        _totalTokens = 0;
        ToolTipService::SetToolTip(SegmentPanel(), nullptr);
        _UpdateSegments();
    }

    void ContextMeter::_UpdateSegments()
    {
        float ratio = (_totalTokens > 0)
            ? static_cast<float>(_usedTokens) / static_cast<float>(_totalTokens)
            : 0.0f;

        // 0–9 segments (each = 10%)
        int filled = static_cast<int>(ratio * 10.0f);
        if (filled > 10) filled = 10;

        auto accentBrush = _AccentBrush();
        SolidColorBrush warnBrush{ Color{ 0xFF, 0xF0, 0xA5, 0x00 } };
        SolidColorBrush critBrush{ Color{ 0xFF, 0xE8, 0x11, 0x23 } };
        SolidColorBrush emptyBrush{ Color{ 0x40, 0x80, 0x80, 0x80 } };

        for (int i = 0; i < 10; ++i)
        {
            auto seg = _GetSegment(i);
            if (!seg) continue;

            if (i < filled)
            {
                if (i >= 9)       seg.Fill(critBrush);
                else if (i >= 7)  seg.Fill(warnBrush);
                else              seg.Fill(accentBrush);
            }
            else
            {
                seg.Fill(emptyBrush);
            }
        }
    }

    winrt::Windows::UI::Xaml::Shapes::Rectangle ContextMeter::_GetSegment(int index)
    {
        switch (index)
        {
        case 0: return Seg0();
        case 1: return Seg1();
        case 2: return Seg2();
        case 3: return Seg3();
        case 4: return Seg4();
        case 5: return Seg5();
        case 6: return Seg6();
        case 7: return Seg7();
        case 8: return Seg8();
        case 9: return Seg9();
        default: return nullptr;
        }
    }

    Brush ContextMeter::_AccentBrush()
    {
        auto resources = Application::Current().Resources();
        auto key = box_value(L"SystemAccentColor");
        if (resources.HasKey(key))
        {
            auto accentColor = unbox_value<Color>(resources.Lookup(key));
            return SolidColorBrush{ accentColor };
        }
        return SolidColorBrush{ Color{ 0xFF, 0x00, 0x78, 0xD4 } }; // fallback blue
    }
}
