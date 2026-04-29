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
        std::wstring TerminalScheme  { L"CTux Dark" };
    };

    struct CTuxTheme
    {
        std::wstring    Name;
        bool            IsBuiltIn  { false };
        bool            IsLightMode{ false }; // true → WinRT Light element theme
        CTuxThemeColors Colors;
    };

    std::vector<CTuxTheme> GetBuiltInThemes();
}
