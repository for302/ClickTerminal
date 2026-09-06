// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.
//
// Single source of truth for the "CTux v0.0NN" version at runtime.
//
// The authored version lives in TabRowControl.xaml; _build_and_deploy.ps1 copies
// it into AppxManifest.xml as 0.0.NN.0 before packing. Everything at runtime
// reads it back from the installed package, so a packaging step that forgot the
// bump shows up as a stale label instead of failing silently.

#pragma once

#include <winrt/Windows.ApplicationModel.h>
#include <string>
#include <string_view>

namespace ClickTerminal
{
    // Where the in-app updater looks for new builds.
    inline constexpr std::wstring_view LatestReleaseApiUrl{
        L"https://api.github.com/repos/for302/ClickTerminal/releases/latest"
    };
    inline constexpr std::wstring_view SetupAssetName{ L"ClickTerminal-Setup.exe" };

    // Build field of the running package's version — the NN in "CTux v0.0NN".
    // Returns 0 when running unpackaged (no Package::Current).
    inline uint32_t CurrentVersionBuild() noexcept
    {
        try
        {
            return winrt::Windows::ApplicationModel::Package::Current().Id().Version().Build;
        }
        catch (...)
        {
            return 0;
        }
    }

    // 40 -> "v0.040"
    inline std::wstring FormatVersion(uint32_t build)
    {
        wchar_t buf[32]{};
        ::swprintf_s(buf, L"v0.%03u", build);
        return buf;
    }

    // "v0.041" / "0.041" / "v0.41" -> 41. Returns 0 when nothing parses.
    inline uint32_t ParseVersionTag(std::wstring_view tag) noexcept
    {
        // Take the digits after the last '.', which is the build field in every
        // form the release tags have used.
        const auto dot = tag.rfind(L'.');
        if (dot == std::wstring_view::npos)
        {
            return 0;
        }

        uint32_t value = 0;
        bool any = false;
        for (size_t i = dot + 1; i < tag.size(); ++i)
        {
            const auto c = tag[i];
            if (c < L'0' || c > L'9')
            {
                break;
            }
            value = value * 10 + static_cast<uint32_t>(c - L'0');
            any = true;
        }
        return any ? value : 0;
    }
}
