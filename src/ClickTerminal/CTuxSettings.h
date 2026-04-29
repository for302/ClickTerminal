#pragma once

#include <string>
#include <vector>
#include <windows.h>
#include "CTuxTheme.h"

namespace ClickTerminal
{
    struct CTuxSettings
    {
        std::wstring ProjectsConfigPath;
        std::wstring SelectedThemeName{ L"CTux Dark" };
        std::wstring PluginsFolder;
        bool GitPluginEnabled { false };
        bool PortPluginEnabled{ false };
        std::vector<CTuxTheme> CustomThemes;

        // Returns the active CTuxTheme (built-in or custom)
        CTuxTheme GetActiveTheme() const;

        // "dark" or "light" for WinRT ElementTheme
        std::wstring ThemeString() const;

        static CTuxSettings Load();
        void Save() const;
        void ApplyTerminalTheme() const;

        static std::wstring GetDefaultDir();
    };
}
