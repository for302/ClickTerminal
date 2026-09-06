// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "LayoutPickerDialog.g.h"
#include "LayoutManager.h"
#include <json/json.h>

namespace winrt::TerminalApp::implementation
{
    struct LayoutPickerDialog : LayoutPickerDialogT<LayoutPickerDialog>
    {
        LayoutPickerDialog();

        void SetProjectList(winrt::hstring const& projectsJson);
        void SetSavedLayouts(winrt::hstring const& layoutsJson);

        // Mode API: "add" | "edit" | "reorder" | "extend". Unset = legacy combined view.
        void SetMode(winrt::hstring const& mode);
        winrt::hstring Mode() { return _mode; }
        void SetEditTarget(winrt::hstring const& layoutId);
        // extend mode: lock the current pane grid (base) and only allow growing it
        void SetExtendBase(winrt::hstring const& baseJson);

        winrt::hstring LayoutName()    { return _layoutName; }
        winrt::hstring LayoutJson()    { return _layoutJson; }
        bool           ShouldSave()    { return _shouldSave; }
        bool           ShouldApply()   { return _shouldApply; }
        winrt::hstring DeleteId()      { return _deleteId; }
        winrt::hstring ApplySavedId()  { return _applySavedId; }
        winrt::hstring EditingId()     { return _editingId; }
        winrt::hstring ReorderedIdsJson();

        // XAML event handlers (must be public)
        void OnShapeCellClicked(const winrt::Windows::Foundation::IInspectable& sender,
                                const winrt::Windows::UI::Xaml::RoutedEventArgs& e);
        void OnApplyClicked(const winrt::Windows::Foundation::IInspectable& sender,
                            const winrt::Windows::UI::Xaml::Controls::ContentDialogButtonClickEventArgs& args);
        void OnSaveClicked(const winrt::Windows::Foundation::IInspectable& sender,
                           const winrt::Windows::UI::Xaml::Controls::ContentDialogButtonClickEventArgs& args);
        void OnSavedApplyClicked(const winrt::Windows::Foundation::IInspectable& sender,
                                 const winrt::Windows::UI::Xaml::RoutedEventArgs& e);
        void OnManageClicked(const winrt::Windows::Foundation::IInspectable& sender,
                             const winrt::Windows::UI::Xaml::RoutedEventArgs& e);
        void OnDeleteCurrentClicked(const winrt::Windows::Foundation::IInspectable& sender,
                                    const winrt::Windows::UI::Xaml::RoutedEventArgs& e);

        // Drag-drop for slot rearrangement
        void OnSlotDragStarting(const winrt::Windows::Foundation::IInspectable& sender,
                                const winrt::Windows::UI::Xaml::DragStartingEventArgs& e);
        void OnSlotDragOver(const winrt::Windows::Foundation::IInspectable& sender,
                            const winrt::Windows::UI::Xaml::DragEventArgs& e);
        void OnSlotDrop(const winrt::Windows::Foundation::IInspectable& sender,
                        const winrt::Windows::UI::Xaml::DragEventArgs& e);

    private:
        struct ProjectEntry { std::wstring Id; std::wstring Name; };
        struct LayoutEntry  { std::wstring Id; std::wstring Name; uint32_t Rows; uint32_t Cols;
                              std::vector<ClickTerminal::LayoutSlot> Slots; };

        std::vector<ProjectEntry> _projects;
        std::vector<LayoutEntry>  _savedLayouts;

        uint32_t _chosenRows{ 0 };
        uint32_t _chosenCols{ 0 };
        int      _dragSourceSlot{ -1 };

        winrt::hstring _mode;               // "" (legacy) | "add" | "edit" | "reorder" | "extend"
        int            _reorderDragSource{ -1 };

        // extend mode: base (locked) grid — existing panes that must be preserved
        uint32_t _baseRows{ 0 };
        uint32_t _baseCols{ 0 };
        std::vector<ClickTerminal::LayoutSlot> _baseSlots;

        // Per-slot ComboBox items (index = row*cols + col)
        std::vector<winrt::Windows::UI::Xaml::Controls::ComboBox> _slotBoxes;

        winrt::hstring _layoutName;
        winrt::hstring _layoutJson;
        bool           _shouldSave{ false };
        bool           _shouldApply{ false };
        winrt::hstring _deleteId;
        winrt::hstring _applySavedId;
        winrt::hstring _editingId;

        void _UpdateShapeCells();
        void _RebuildSlotGrid();
        // extend mode: is the slot at flat index (row*_chosenCols+col) part of the locked base grid?
        bool _IsSlotLocked(int32_t slotIdx) const
        {
            if (_mode != L"extend" || slotIdx < 0 || _chosenCols == 0)
                return false;
            const uint32_t r = static_cast<uint32_t>(slotIdx) / _chosenCols;
            const uint32_t c = static_cast<uint32_t>(slotIdx) % _chosenCols;
            return (r < _baseRows) && (c < _baseCols);
        }
        void _PopulateSlotBox(winrt::Windows::UI::Xaml::Controls::ComboBox const& box,
                              const std::wstring& selectedId);
        winrt::hstring _BuildLayoutJson(const std::wstring& name);
        void _LoadLayoutIntoEditor(const LayoutEntry& layout);
        void _RefreshSavedList();
        void _RefreshReorderList();
        void _ResetEditor();

        // Drag-drop for reorder-mode list items
        void _OnReorderDragStarting(const winrt::Windows::Foundation::IInspectable& sender,
                                    const winrt::Windows::UI::Xaml::DragStartingEventArgs& e);
        void _OnReorderDragOver(const winrt::Windows::Foundation::IInspectable& sender,
                                const winrt::Windows::UI::Xaml::DragEventArgs& e);
        void _OnReorderDrop(const winrt::Windows::Foundation::IInspectable& sender,
                            const winrt::Windows::UI::Xaml::DragEventArgs& e);

        void _OnTextBoxGotFocus(const winrt::Windows::Foundation::IInspectable&,
                                const winrt::Windows::UI::Xaml::RoutedEventArgs&);

        HWND _islandHwnd{ nullptr };
        bool _pendingHangulToggle{ false };

        static bool _IsKoreanModeActive() noexcept
        {
            return (::GetKeyState(VK_HANGUL) & 0x0001) != 0;
        }
    };
}

namespace winrt::TerminalApp::factory_implementation
{
    BASIC_FACTORY(LayoutPickerDialog);
}
