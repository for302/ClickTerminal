// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "ContextMeter.g.h"

namespace winrt::TerminalApp::implementation
{
    struct ContextMeter : ContextMeterT<ContextMeter>
    {
        ContextMeter();

        void SetUsage(uint32_t usedTokens, uint32_t totalTokens);
        void Reset();

    private:
        uint32_t _usedTokens{ 0 };
        uint32_t _totalTokens{ 0 };

        void _UpdateSegments();
        winrt::Windows::UI::Xaml::Shapes::Rectangle _GetSegment(int index);
        winrt::Windows::UI::Xaml::Media::Brush _AccentBrush();
    };
}

namespace winrt::TerminalApp::factory_implementation
{
    BASIC_FACTORY(ContextMeter);
}
