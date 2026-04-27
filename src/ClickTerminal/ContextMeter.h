#pragma once

#include "AIToolManager.h"
#include <array>
#include <string>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Shapes.h>
#include <winrt/Windows.UI.Xaml.Media.h>

namespace ClickTerminal
{
    // 10-segment battery-style context window meter.
    // - Segments 0-6 (0-70%): accent color
    // - Segments 7-8 (70-90%): amber (#F0A500)
    // - Segment 9   (90-100%): red (#E81123)
    // Total width: ~78px segments + ~22px label = ~100px, height: 20px.
    //
    // Call SetUsage() from the UI thread whenever the PTY output parser
    // produces a new ContextUsage value.
    class ContextMeter
    {
    public:
        ContextMeter();

        void SetUsage(const ContextUsage& usage);
        void Reset();

        std::wstring ToolTipText() const;  // "45,231 / 200,000 tokens (22%)"

    private:
        static constexpr uint8_t kSegmentCount = 10;

        std::array<winrt::Windows::UI::Xaml::Shapes::Rectangle, kSegmentCount> _segs;

        ContextUsage _usage;
        uint8_t      _filledCount{ 0 };
        std::wstring _toolTipText;

        void RefreshSegments();

        winrt::Windows::UI::Xaml::Media::SolidColorBrush
            BrushForSegment(uint8_t segIndex, uint8_t totalFilled) const;
    };

} // namespace ClickTerminal
