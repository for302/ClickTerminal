// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "AddProjectDialog.g.h"

namespace winrt::TerminalApp::implementation
{
    struct AddProjectDialog : AddProjectDialogT<AddProjectDialog>
    {
        AddProjectDialog();

        winrt::hstring ProjectName();
        winrt::hstring FolderPath();
        winrt::hstring ProjectType();
        winrt::hstring DefaultAITool();
        winrt::hstring ColorScheme();

        // XAML event handler (must be public)
        void _BrowseFolderClicked(const winrt::Windows::Foundation::IInspectable& sender,
                                  const winrt::Windows::UI::Xaml::RoutedEventArgs& e);
    };
}

namespace winrt::TerminalApp::factory_implementation
{
    BASIC_FACTORY(AddProjectDialog);
}
