// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "LayoutPickerDialog.h"
#include "LayoutPickerDialog.g.cpp"
#include <json/json.h>
#include <CoreWindow.h>  // ICoreWindowInterop

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Xaml::Controls;
using namespace winrt::Windows::UI::Xaml::Controls::Primitives;
using namespace winrt::Windows::ApplicationModel::DataTransfer;

namespace winrt::TerminalApp::implementation
{
    LayoutPickerDialog::LayoutPickerDialog()
    {
        InitializeComponent();

        // Fix: WM_CHAR is stolen by the terminal HWND in Xaml Islands.
        // Intercept KeyDown on each TextBox and insert directly via ToUnicode().
        auto attachInsert = [](winrt::Windows::UI::Xaml::Controls::TextBox box) {
            box.KeyDown([box](winrt::Windows::Foundation::IInspectable const&,
                              winrt::Windows::UI::Xaml::Input::KeyRoutedEventArgs const& e) {
                auto vk = static_cast<UINT>(e.OriginalKey());
                if (vk == 0xE5) return; // VK_PROCESSKEY: TSF Korean — let it through
                if ((::GetKeyState(VK_CONTROL) & 0x8000) != 0) return;
                if ((::GetKeyState(VK_MENU)    & 0x8000) != 0) return;

                BYTE ks[256]; ::GetKeyboardState(ks);
                WCHAR ch[4] = {};
                if (::ToUnicode(vk, ::MapVirtualKey(vk, MAPVK_VK_TO_VSC), ks, ch, 4, 0) != 1
                    || ch[0] < L' ')
                    return;

                auto sel  = box.SelectionStart();
                auto len  = box.SelectionLength();
                auto text = std::wstring{ box.Text() };
                if (len > 0) text.erase(sel, len);
                text.insert(sel, 1, ch[0]);
                box.Text(winrt::hstring{ text });
                box.SelectionStart(sel + 1);
                box.SelectionLength(0);
                e.Handled(true);
            });
        };
        attachInsert(LayoutNameBox());
        LayoutNameBox().GotFocus({ this, &LayoutPickerDialog::_OnTextBoxGotFocus });

        Opened([this](winrt::Windows::Foundation::IInspectable const&,
                      winrt::Windows::UI::Xaml::Controls::ContentDialogOpenedEventArgs const&) {
            // Get the Xaml Island HWND so we can restore Win32 focus after dialog opens
            if (auto coreWindow = winrt::Windows::UI::Core::CoreWindow::GetForCurrentThread())
            {
                if (auto interop = coreWindow.try_as<ICoreWindowInterop>())
                {
                    HWND islandHwnd = nullptr;
                    if (SUCCEEDED(interop->get_WindowHandle(&islandHwnd)) && islandHwnd)
                        _islandHwnd = islandHwnd;
                }
            }
            _pendingHangulToggle = _IsKoreanModeActive();

            // ContentDialog resets Win32 focus after Opened fires — use RunAsync to land on top
            Dispatcher().RunAsync(
                winrt::Windows::UI::Core::CoreDispatcherPriority::Normal,
                [this]() {
                    if (_islandHwnd) ::SetFocus(_islandHwnd);
                    LayoutNameBox().Focus(winrt::Windows::UI::Xaml::FocusState::Programmatic);
                });
        });
    }

    void LayoutPickerDialog::_OnTextBoxGotFocus(
        const winrt::Windows::Foundation::IInspectable& /*sender*/,
        const winrt::Windows::UI::Xaml::RoutedEventArgs& /*e*/)
    {
        if (!_pendingHangulToggle) return;
        _pendingHangulToggle = false;
        INPUT inp[2] = {};
        inp[0].type = INPUT_KEYBOARD; inp[0].ki.wVk = VK_HANGUL;
        inp[1] = inp[0]; inp[1].ki.dwFlags = KEYEVENTF_KEYUP;
        ::SendInput(2, inp, sizeof(INPUT));
        Dispatcher().RunAsync(
            winrt::Windows::UI::Core::CoreDispatcherPriority::Normal,
            [this]() {
                if (_islandHwnd) ::SetFocus(_islandHwnd);
                LayoutNameBox().Focus(winrt::Windows::UI::Xaml::FocusState::Programmatic);
            });
    }

    // -----------------------------------------------------------------------
    // Data setters called from TerminalPage before ShowAsync()
    // -----------------------------------------------------------------------

    void LayoutPickerDialog::SetProjectList(hstring const& projectsJson)
    {
        _projects.clear();
        try
        {
            Json::Value root;
            Json::CharReaderBuilder b;
            std::string errs;
            std::istringstream ss(winrt::to_string(projectsJson));
            if (Json::parseFromStream(b, ss, &root, &errs) && root.isArray())
            {
                for (const auto& pj : root)
                {
                    ProjectEntry e;
                    auto toWide = [](const std::string& s) {
                        if (s.empty()) return std::wstring{};
                        int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
                        std::wstring r(static_cast<size_t>(len) - 1, L'\0');
                        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, r.data(), len);
                        return r;
                    };
                    e.Id   = toWide(pj["id"].asString());
                    e.Name = toWide(pj["name"].asString());
                    _projects.push_back(std::move(e));
                }
            }
        }
        catch (...) {}

        // Rebuild slot boxes if grid is already visible
        if (_chosenRows > 0) _RebuildSlotGrid();
    }

    void LayoutPickerDialog::SetSavedLayouts(hstring const& layoutsJson)
    {
        _savedLayouts.clear();
        try
        {
            Json::Value root;
            Json::CharReaderBuilder b;
            std::string errs;
            std::istringstream ss(winrt::to_string(layoutsJson));
            if (Json::parseFromStream(b, ss, &root, &errs) && root.isArray())
            {
                auto toWide = [](const std::string& s) {
                    if (s.empty()) return std::wstring{};
                    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
                    std::wstring r(static_cast<size_t>(len) - 1, L'\0');
                    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, r.data(), len);
                    return r;
                };
                for (const auto& lj : root)
                {
                    LayoutEntry e;
                    e.Id   = toWide(lj["id"].asString());
                    e.Name = toWide(lj.get("name","").asString());
                    e.Rows = static_cast<uint32_t>(lj.get("rows", 1).asInt());
                    e.Cols = static_cast<uint32_t>(lj.get("cols", 1).asInt());
                    for (const auto& sj : lj["slots"])
                    {
                        ClickTerminal::LayoutSlot slot;
                        slot.Row       = static_cast<uint32_t>(sj.get("row",0).asInt());
                        slot.Col       = static_cast<uint32_t>(sj.get("col",0).asInt());
                        slot.ProjectId = toWide(sj.get("projectId","").asString());
                        e.Slots.push_back(slot);
                    }
                    _savedLayouts.push_back(std::move(e));
                }
            }
        }
        catch (...) {}

        _RefreshSavedList();
    }

    // -----------------------------------------------------------------------
    // Shape cell selection
    // -----------------------------------------------------------------------

    void LayoutPickerDialog::OnShapeCellClicked(const IInspectable& sender, const RoutedEventArgs&)
    {
        auto btn = sender.try_as<ToggleButton>();
        if (!btn) return;

        auto tag = winrt::unbox_value_or<hstring>(btn.Tag(), hstring{});
        auto tagStr = std::wstring{ tag };
        auto comma = tagStr.find(L',');
        if (comma == std::wstring::npos) return;

        uint32_t r = static_cast<uint32_t>(std::stoul(tagStr.substr(0, comma)));
        uint32_t c = static_cast<uint32_t>(std::stoul(tagStr.substr(comma + 1)));
        _chosenRows = r + 1;
        _chosenCols = c + 1;

        _UpdateShapeCells();
        _RebuildSlotGrid();

        SlotSection().Visibility(Visibility::Visible);
        NameSection().Visibility(Visibility::Visible);

        std::wstring lbl = std::to_wstring(_chosenRows) + L" row" +
                           (_chosenRows > 1 ? L"s" : L"") + L"  ×  " +
                           std::to_wstring(_chosenCols) + L" column" +
                           (_chosenCols > 1 ? L"s" : L"");
        ShapeLabel().Text(lbl);
    }

    void LayoutPickerDialog::_UpdateShapeCells()
    {
        // Names of all 9 cells in row-major order
        static const wchar_t* cellNames[] = {
            L"Cell00", L"Cell01", L"Cell02",
            L"Cell10", L"Cell11", L"Cell12",
            L"Cell20", L"Cell21", L"Cell22"
        };
        for (uint32_t ri = 0; ri < 3; ri++)
        {
            for (uint32_t ci = 0; ci < 3; ci++)
            {
                auto elem = FindName(hstring{ cellNames[ri * 3 + ci] });
                if (auto cell = elem.try_as<ToggleButton>())
                {
                    bool selected = (ri < _chosenRows) && (ci < _chosenCols);
                    cell.IsChecked(selected);
                }
            }
        }
    }

    // -----------------------------------------------------------------------
    // Slot grid build
    // -----------------------------------------------------------------------

    void LayoutPickerDialog::_RebuildSlotGrid()
    {
        _slotBoxes.clear();
        auto grid = SlotGrid();

        grid.RowDefinitions().Clear();
        grid.ColumnDefinitions().Clear();
        grid.Children().Clear();

        for (uint32_t r = 0; r < _chosenRows; r++)
        {
            RowDefinition rd;
            GridLength rowHeight{ 80.0, GridUnitType::Pixel };
            rd.Height(rowHeight);
            grid.RowDefinitions().Append(rd);
        }
        for (uint32_t c = 0; c < _chosenCols; c++)
        {
            ColumnDefinition cd;
            GridLength colWidth{ 1.0, GridUnitType::Star };
            cd.Width(colWidth);
            grid.ColumnDefinitions().Append(cd);
        }

        for (uint32_t r = 0; r < _chosenRows; r++)
        {
            for (uint32_t c = 0; c < _chosenCols; c++)
            {
                Border outer;
                Thickness borderThick{ 1.0, 1.0, 1.0, 1.0 };
                outer.BorderThickness(borderThick);
                winrt::Windows::UI::Xaml::CornerRadius cr{ 4.0, 4.0, 4.0, 4.0 };
                outer.CornerRadius(cr);
                Thickness marginThick{ 3.0, 3.0, 3.0, 3.0 };
                outer.Margin(marginThick);
                Thickness padThick{ 6.0, 6.0, 6.0, 6.0 };
                outer.Padding(padThick);
                outer.AllowDrop(true);
                outer.CanDrag(true);

                uint32_t slotIdx = r * _chosenCols + c;
                outer.Tag(winrt::box_value(static_cast<int32_t>(slotIdx)));

                outer.DragStarting({ this, &LayoutPickerDialog::OnSlotDragStarting });
                outer.DragOver({ this, &LayoutPickerDialog::OnSlotDragOver });
                outer.Drop({ this, &LayoutPickerDialog::OnSlotDrop });

                StackPanel inner;
                inner.Orientation(Orientation::Vertical);
                inner.Spacing(4);

                TextBlock label;
                label.Text(std::to_wstring(r + 1) + L"×" + std::to_wstring(c + 1));
                label.FontSize(10);
                label.Opacity(0.5);

                ComboBox box;
                box.PlaceholderText(L"— default terminal —");
                box.HorizontalAlignment(HorizontalAlignment::Stretch);
                _PopulateSlotBox(box, L"");

                inner.Children().Append(label);
                inner.Children().Append(box);
                outer.Child(inner);

                Grid::SetRow(outer, r);
                Grid::SetColumn(outer, c);
                grid.Children().Append(outer);

                _slotBoxes.push_back(box);
            }
        }
    }

    void LayoutPickerDialog::_PopulateSlotBox(ComboBox const& box, const std::wstring& selectedId)
    {
        box.Items().Clear();

        // First item: default terminal (empty id)
        ComboBoxItem defaultItem;
        defaultItem.Content(winrt::box_value(hstring{ L"— default terminal —" }));
        defaultItem.Tag(winrt::box_value(hstring{}));
        box.Items().Append(defaultItem);

        int selectIdx = 0;
        int idx = 1;
        for (const auto& p : _projects)
        {
            ComboBoxItem item;
            item.Content(winrt::box_value(hstring{ p.Name }));
            item.Tag(winrt::box_value(hstring{ p.Id }));
            box.Items().Append(item);
            if (!selectedId.empty() && p.Id == selectedId)
                selectIdx = idx;
            idx++;
        }
        box.SelectedIndex(selectIdx);
    }

    // -----------------------------------------------------------------------
    // Drag-drop slot rearrangement
    // -----------------------------------------------------------------------

    void LayoutPickerDialog::OnSlotDragStarting(const IInspectable& sender, const DragStartingEventArgs& e)
    {
        if (auto border = sender.try_as<Border>())
        {
            _dragSourceSlot = winrt::unbox_value_or<int32_t>(border.Tag(), -1);
            e.Data().SetText(L"layoutslot");
            e.DragUI().SetContentFromDataPackage();
        }
    }

    void LayoutPickerDialog::OnSlotDragOver(const IInspectable&, const DragEventArgs& e)
    {
        if (e.DataView().Contains(StandardDataFormats::Text()))
            e.AcceptedOperation(DataPackageOperation::Move);
    }

    void LayoutPickerDialog::OnSlotDrop(const IInspectable& sender, const DragEventArgs&)
    {
        if (_dragSourceSlot < 0) return;
        if (auto border = sender.try_as<Border>())
        {
            int32_t destSlot = winrt::unbox_value_or<int32_t>(border.Tag(), -1);
            if (destSlot < 0 || destSlot == _dragSourceSlot) return;

            auto srcIdx  = static_cast<size_t>(_dragSourceSlot);
            auto dstIdx  = static_cast<size_t>(destSlot);
            if (srcIdx >= _slotBoxes.size() || dstIdx >= _slotBoxes.size()) return;

            // Swap selected items between the two ComboBoxes
            int srcSel = _slotBoxes[srcIdx].SelectedIndex();
            int dstSel = _slotBoxes[dstIdx].SelectedIndex();
            _slotBoxes[srcIdx].SelectedIndex(dstSel);
            _slotBoxes[dstIdx].SelectedIndex(srcSel);
        }
        _dragSourceSlot = -1;
    }

    // -----------------------------------------------------------------------
    // Build layout JSON from current editor state
    // -----------------------------------------------------------------------

    hstring LayoutPickerDialog::_BuildLayoutJson(const std::wstring& name)
    {
        if (_chosenRows == 0 || _chosenCols == 0)
            return hstring{};

        auto toNarrow = [](const std::wstring& ws) {
            if (ws.empty()) return std::string{};
            int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, nullptr, 0, nullptr, nullptr);
            std::string r(static_cast<size_t>(len) - 1, '\0');
            WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, r.data(), len, nullptr, nullptr);
            return r;
        };

        Json::Value lj;
        lj["name"] = toNarrow(name);
        lj["rows"] = static_cast<int>(_chosenRows);
        lj["cols"] = static_cast<int>(_chosenCols);

        Json::Value slots(Json::arrayValue);
        for (uint32_t r = 0; r < _chosenRows; r++)
        {
            for (uint32_t c = 0; c < _chosenCols; c++)
            {
                size_t idx = r * _chosenCols + c;
                if (idx >= _slotBoxes.size()) continue;

                auto box = _slotBoxes[idx];
                std::wstring projectId;
                if (box.SelectedIndex() > 0)
                {
                    if (auto item = box.SelectedItem().try_as<ComboBoxItem>())
                        projectId = std::wstring{ winrt::unbox_value_or<hstring>(item.Tag(), hstring{}) };
                }

                Json::Value sj;
                sj["row"] = static_cast<int>(r);
                sj["col"] = static_cast<int>(c);
                sj["projectId"] = toNarrow(projectId);
                slots.append(sj);
            }
        }
        lj["slots"] = slots;

        Json::StreamWriterBuilder wb;
        wb["indentation"] = "";
        return hstring{ winrt::to_hstring(Json::writeString(wb, lj)) };
    }

    // -----------------------------------------------------------------------
    // Apply / Save button handlers (ContentDialog primary/secondary)
    // -----------------------------------------------------------------------

    void LayoutPickerDialog::OnApplyClicked(const IInspectable&,
                                             const ContentDialogButtonClickEventArgs& args)
    {
        if (_chosenRows == 0)
        {
            args.Cancel(true); // keep dialog open
            return;
        }
        auto name  = std::wstring{ LayoutNameBox().Text() };
        _layoutName  = hstring{ name };
        _layoutJson  = _BuildLayoutJson(name);
        _shouldApply = true;
        _shouldSave  = false;
    }

    void LayoutPickerDialog::OnSaveClicked(const IInspectable&,
                                            const ContentDialogButtonClickEventArgs& args)
    {
        if (_chosenRows == 0)
        {
            args.Cancel(true);
            return;
        }
        auto name = std::wstring{ LayoutNameBox().Text() };
        if (name.empty())
        {
            args.Cancel(true);
            LayoutNameBox().Focus(FocusState::Programmatic);
            return;
        }
        _layoutName  = hstring{ name };
        _layoutJson  = _BuildLayoutJson(name);
        _shouldSave  = true;
        _shouldApply = false;
    }

    // -----------------------------------------------------------------------
    // Saved layout list handlers
    // -----------------------------------------------------------------------

    void LayoutPickerDialog::OnSavedApplyClicked(const IInspectable& sender, const RoutedEventArgs&)
    {
        if (auto btn = sender.try_as<Button>())
        {
            _applySavedId = winrt::unbox_value_or<hstring>(btn.Tag(), hstring{});
            _shouldApply  = true;
            _shouldSave   = false;
            // Close dialog with primary result
            Hide();
        }
    }

    void LayoutPickerDialog::OnManageClicked(const IInspectable& sender, const RoutedEventArgs&)
    {
        auto btn = sender.try_as<Button>();
        if (!btn) return;
        auto id = std::wstring{ winrt::unbox_value_or<hstring>(btn.Tag(), hstring{}) };
        for (const auto& l : _savedLayouts)
        {
            if (l.Id == id)
            {
                _editingId = hstring{ l.Id };
                _LoadLayoutIntoEditor(l);
                EditorTitle().Text(hstring{ L"Edit: " + l.Name });
                DeleteButton().Visibility(Visibility::Visible);
                return;
            }
        }
    }

    void LayoutPickerDialog::OnDeleteCurrentClicked(const IInspectable&, const RoutedEventArgs&)
    {
        if (_editingId.empty()) return;
        _deleteId = _editingId;
        auto id = std::wstring{ _editingId };
        _savedLayouts.erase(std::remove_if(_savedLayouts.begin(), _savedLayouts.end(),
            [&id](const LayoutEntry& e) { return e.Id == id; }), _savedLayouts.end());
        _RefreshSavedList();

        // Reset editor to "New Layout" mode
        _editingId  = hstring{};
        _chosenRows = 0;
        _chosenCols = 0;
        _UpdateShapeCells();
        ShapeLabel().Text(hstring{ L"Click a cell to choose shape" });
        EditorTitle().Text(hstring{ L"New Layout" });
        DeleteButton().Visibility(Visibility::Collapsed);
        SlotSection().Visibility(Visibility::Collapsed);
        NameSection().Visibility(Visibility::Collapsed);
        LayoutNameBox().Text(hstring{});
    }

    // -----------------------------------------------------------------------
    // Helpers
    // -----------------------------------------------------------------------

    void LayoutPickerDialog::_LoadLayoutIntoEditor(const LayoutEntry& layout)
    {
        _chosenRows = layout.Rows;
        _chosenCols = layout.Cols;
        _UpdateShapeCells();
        _RebuildSlotGrid();
        SlotSection().Visibility(Visibility::Visible);
        NameSection().Visibility(Visibility::Visible);

        // Apply slot projectIds
        for (const auto& slot : layout.Slots)
        {
            size_t idx = slot.Row * _chosenCols + slot.Col;
            if (idx < _slotBoxes.size())
                _PopulateSlotBox(_slotBoxes[idx], slot.ProjectId);
        }

        LayoutNameBox().Text(hstring{ layout.Name });

        std::wstring lbl = std::to_wstring(_chosenRows) + L" row" +
                           (_chosenRows > 1 ? L"s" : L"") + L"  ×  " +
                           std::to_wstring(_chosenCols) + L" column" +
                           (_chosenCols > 1 ? L"s" : L"");
        ShapeLabel().Text(lbl);
    }

    void LayoutPickerDialog::_RefreshSavedList()
    {
        if (_savedLayouts.empty())
        {
            NoSavedLabel().Visibility(Visibility::Visible);
            SavedList().Visibility(Visibility::Collapsed);
            return;
        }

        NoSavedLabel().Visibility(Visibility::Collapsed);
        SavedList().Visibility(Visibility::Visible);
        SavedList().Children().Clear();

        for (const auto& l : _savedLayouts)
        {
            // Card: vertical stack — name, size badge, button row
            StackPanel card;
            card.Orientation(Orientation::Vertical);
            card.Spacing(4);
            Thickness cardPad{ 8.0, 8.0, 8.0, 8.0 };
            card.Padding(cardPad);

            TextBlock nameText;
            nameText.Text(hstring{ l.Name });
            nameText.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
            nameText.FontSize(13);
            nameText.TextTrimming(winrt::Windows::UI::Xaml::TextTrimming::CharacterEllipsis);

            TextBlock badge;
            badge.Text(hstring{ std::to_wstring(l.Rows) + L" × " + std::to_wstring(l.Cols) });
            badge.FontSize(11);
            badge.Opacity(0.6);

            // Button row: "열기" (accent) + "관리"
            StackPanel btnRow;
            btnRow.Orientation(Orientation::Horizontal);
            btnRow.Spacing(6);

            Button openBtn;
            openBtn.Content(winrt::box_value(hstring{ L"열기" }));
            openBtn.Tag(winrt::box_value(hstring{ l.Id }));
            {
                auto styleObj = Resources().TryLookup(winrt::box_value(hstring{ L"AccentButtonStyle" }));
                if (auto style = styleObj.try_as<winrt::Windows::UI::Xaml::Style>())
                    openBtn.Style(style);
            }
            openBtn.Click([this](const IInspectable& s, const RoutedEventArgs& e) {
                OnSavedApplyClicked(s, e); });

            Button manageBtn;
            manageBtn.Content(winrt::box_value(hstring{ L"관리" }));
            manageBtn.Tag(winrt::box_value(hstring{ l.Id }));
            manageBtn.Click([this](const IInspectable& s, const RoutedEventArgs& e) {
                OnManageClicked(s, e); });

            btnRow.Children().Append(openBtn);
            btnRow.Children().Append(manageBtn);

            card.Children().Append(nameText);
            card.Children().Append(badge);
            card.Children().Append(btnRow);

            // Wrap in a bordered container
            Border cardBorder;
            winrt::Windows::UI::Xaml::CornerRadius cr{ 6.0, 6.0, 6.0, 6.0 };
            cardBorder.CornerRadius(cr);
            Thickness borderThick{ 1.0, 1.0, 1.0, 1.0 };
            cardBorder.BorderThickness(borderThick);
            cardBorder.Child(card);

            SavedList().Children().Append(cardBorder);
        }
    }

} // namespace winrt::TerminalApp::implementation
