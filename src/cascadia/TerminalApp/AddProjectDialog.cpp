// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "AddProjectDialog.h"
#include "AddProjectDialog.g.cpp"

namespace winrt::TerminalApp::implementation
{
    AddProjectDialog::AddProjectDialog()
    {
        InitializeComponent();
    }

    winrt::hstring AddProjectDialog::ProjectName()
    {
        return ProjectNameBox().Text();
    }

    winrt::hstring AddProjectDialog::FolderPath()
    {
        return FolderPathBox().Text();
    }

    winrt::hstring AddProjectDialog::ProjectType()
    {
        switch (ProjectTypeBox().SelectedIndex())
        {
        case 1:  return L"app";
        case 2:  return L"ai-workflow";
        default: return L"web";
        }
    }

    winrt::hstring AddProjectDialog::DefaultAITool()
    {
        switch (AIToolBox().SelectedIndex())
        {
        case 0:  return L"claude";
        case 1:  return L"codex";
        case 2:  return L"gemini";
        default: return L"";
        }
    }

    winrt::hstring AddProjectDialog::ColorScheme()
    {
        static constexpr const wchar_t* kSchemes[] = {
            L"",               // Default
            L"CTux Dark",
            L"CTux Light",
            L"Campbell",
            L"One Half Dark",
            L"One Half Light",
            L"Solarized Dark",
            L"Solarized Light",
            L"Tango Dark",
            L"Tango Light",
        };
        auto idx = ColorSchemeBox().SelectedIndex();
        if (idx >= 0 && idx < static_cast<int>(std::size(kSchemes)))
            return kSchemes[idx];
        return L"";
    }

    void AddProjectDialog::_BrowseFolderClicked(const winrt::Windows::Foundation::IInspectable& /*sender*/,
                                                  const winrt::Windows::UI::Xaml::RoutedEventArgs& /*e*/)
    {
        // Use COM IFileOpenDialog to pick a folder (works without WinRT infrastructure)
        IFileOpenDialog* pfd = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&pfd));
        if (SUCCEEDED(hr))
        {
            DWORD dwOptions = 0;
            pfd->GetOptions(&dwOptions);
            pfd->SetOptions(dwOptions | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);

            hr = pfd->Show(nullptr);
            if (SUCCEEDED(hr))
            {
                IShellItem* psi = nullptr;
                hr = pfd->GetResult(&psi);
                if (SUCCEEDED(hr))
                {
                    PWSTR pszPath = nullptr;
                    if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &pszPath)))
                    {
                        FolderPathBox().Text(winrt::hstring{ pszPath });
                        CoTaskMemFree(pszPath);
                    }
                    psi->Release();
                }
            }
            pfd->Release();
        }
    }
}
