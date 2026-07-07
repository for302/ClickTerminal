#pragma once
#include <string>
#include <vector>

namespace ClickTerminal
{
    struct CTuxThemeColors
    {
        std::wstring SidebarBg       { L"#252535" };
        std::wstring SidebarHeaderBg { L"#1A1A2A" };
        std::wstring SidebarText     { L"#CDD6F4" };
        std::wstring SidebarTextMuted{ L"#7C7F93" };
        std::wstring TabBarBg        { L"#1A1A2A" };
        std::wstring DialogBg        { L"#2A2A3A" };
        std::wstring DialogNavBg     { L"#1F1F2F" };
        std::wstring PaneHeaderBg    { L"#1C1C26" }; // layout pane title strip card background
        std::wstring PaneHeaderText  { L"#FFFFFF" }; // strip text
        std::wstring PaneHeaderAccent{ L"#50A0DC" }; // strip links / port color
        std::wstring ExitOverlayBg   { L"#0C0C16" }; // "AI exiting..." overlay on tab close
        std::wstring PaneBorder      { L"#333344" }; // pane boundary (unfocused)
        std::wstring TerminalScheme  { L"CTux Dark" };
    };

    // Parses "#RRGGBB" (or "RRGGBB") into 0xAARRGGBB (alpha forced to 0xFF).
    // Returns `fallback` when the input is not a valid 6-digit hex color.
    inline unsigned long ParseThemeHex(const std::wstring& hex, unsigned long fallback = 0xFF404040UL)
    {
        std::wstring h = hex;
        if (!h.empty() && h[0] == L'#') h = h.substr(1);
        if (h.size() != 6) return fallback;
        unsigned long v = 0;
        for (auto c : h)
        {
            v <<= 4;
            if (c >= L'0' && c <= L'9')      v |= c - L'0';
            else if (c >= L'a' && c <= L'f') v |= c - L'a' + 10;
            else if (c >= L'A' && c <= L'F') v |= c - L'A' + 10;
            else return fallback;
        }
        return 0xFF000000UL | v;
    }

    struct CTuxTheme
    {
        std::wstring    Name;
        bool            IsBuiltIn  { false };
        bool            IsLightMode{ false }; // true → WinRT Light element theme
        CTuxThemeColors Colors;
    };

    std::vector<CTuxTheme> GetBuiltInThemes();
}
