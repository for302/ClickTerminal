// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "ProjectOrganizerDialog.g.h"

namespace winrt::TerminalApp::implementation
{
    struct ProjectOrganizerDialog : ProjectOrganizerDialogT<ProjectOrganizerDialog>
    {
        ProjectOrganizerDialog();

        // Inject current state: {"folders":[{id,name,order,collapsed}], "projects":[{id,name,order,folderId}]}
        void SetData(winrt::hstring const& json);

        bool           ShouldSave() { return _shouldSave; }
        winrt::hstring ResultJson();

        // XAML event handlers (must be public)
        void _AddFolderClicked(const winrt::Windows::Foundation::IInspectable& sender,
                               const winrt::Windows::UI::Xaml::RoutedEventArgs& e);
        void _OnTextBoxGotFocus(const winrt::Windows::Foundation::IInspectable& sender,
                                const winrt::Windows::UI::Xaml::RoutedEventArgs& e);

    private:
        struct FolderEntry
        {
            std::wstring Id;   // existing "folder-{uuid}" or temporary "new-N"
            std::wstring Name;
        };
        struct ProjEntry
        {
            std::wstring Id;
            std::wstring Name;
            std::wstring FolderId; // empty = root
        };

        std::vector<FolderEntry> _folders;   // display order == vector order
        std::vector<ProjEntry>   _projects;  // global order == vector order
        int                      _newFolderCounter{ 0 };
        bool                     _shouldSave{ false };
        bool                     _building{ false };   // suppress SelectionChanged during rebuild
        std::wstring             _dragProjectId;

        bool _pendingHangulToggle{ false };
        HWND _islandHwnd{ nullptr };

        void _RefreshList();
        void _AttachInsertHandler(winrt::Windows::UI::Xaml::Controls::TextBox const& box);
        std::vector<size_t> _DisplayOrderIndices() const;  // indices into _projects, grouped display order

        // Drag & drop reordering of project rows
        void _OnProjectDragStarting(const winrt::Windows::Foundation::IInspectable& sender,
                                    const winrt::Windows::UI::Xaml::DragStartingEventArgs& e);
        void _OnRowDragOver(const winrt::Windows::Foundation::IInspectable& sender,
                            const winrt::Windows::UI::Xaml::DragEventArgs& e);
        void _OnProjectDrop(const winrt::Windows::Foundation::IInspectable& sender,
                            const winrt::Windows::UI::Xaml::DragEventArgs& e);
        void _OnFolderHeaderDrop(const winrt::Windows::Foundation::IInspectable& sender,
                                 const winrt::Windows::UI::Xaml::DragEventArgs& e);
    };
}

namespace winrt::TerminalApp::factory_implementation
{
    BASIC_FACTORY(ProjectOrganizerDialog);
}
