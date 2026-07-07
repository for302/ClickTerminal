// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "ProjectOrganizerDialog.h"
#include "ProjectOrganizerDialog.g.cpp"
#include <json/json.h>
#include <algorithm>
#include <sstream>
#include <CoreWindow.h> // ICoreWindowInterop

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Xaml::Controls;
using namespace winrt::Windows::UI::Xaml::Media;
using namespace winrt::Windows::ApplicationModel::DataTransfer;

namespace
{
    std::wstring NarrowToWide(const std::string& s)
    {
        if (s.empty()) return {};
        const int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
        std::wstring result(static_cast<size_t>(len) - 1, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, result.data(), len);
        return result;
    }

    std::string WideToNarrow(const std::wstring& ws)
    {
        if (ws.empty()) return {};
        const int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string result(static_cast<size_t>(len) - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, result.data(), len, nullptr, nullptr);
        return result;
    }

    // GetKeyState(VK_HANGUL) toggle bit — the only reliable Korean-mode check.
    bool IsKoreanModeActive() noexcept
    {
        return (::GetKeyState(VK_HANGUL) & 0x0001) != 0;
    }

    GridLength MakeStar()
    {
        GridLength gl;
        gl.Value = 1.0;
        gl.GridUnitType = GridUnitType::Star;
        return gl;
    }

    GridLength MakeAuto()
    {
        GridLength gl;
        gl.Value = 1.0;
        gl.GridUnitType = GridUnitType::Auto;
        return gl;
    }
}

namespace winrt::TerminalApp::implementation
{
    ProjectOrganizerDialog::ProjectOrganizerDialog()
    {
        InitializeComponent();

        _AttachInsertHandler(NewFolderNameBox());

        PrimaryButtonClick([this](winrt::Windows::UI::Xaml::Controls::ContentDialog const&,
                                  winrt::Windows::UI::Xaml::Controls::ContentDialogButtonClickEventArgs const&) {
            _shouldSave = true;
        });

        Opened([this](winrt::Windows::Foundation::IInspectable const&,
                      winrt::Windows::UI::Xaml::Controls::ContentDialogOpenedEventArgs const&) {
            // Win32 focus must land on the XAML island HWND, otherwise WM_CHAR
            // goes to the terminal pane HWND and English input never arrives.
            if (const auto cw = winrt::Windows::UI::Core::CoreWindow::GetForCurrentThread())
            {
                if (const auto interop = cw.try_as<ICoreWindowInterop>())
                {
                    HWND islandHwnd = nullptr;
                    if (SUCCEEDED(interop->get_WindowHandle(&islandHwnd)) && islandHwnd)
                    {
                        _islandHwnd = islandHwnd;
                        ::SetFocus(islandHwnd);
                    }
                }
            }

            // Detect Korean mode now; send VK_HANGUL later in GotFocus when the
            // TextBox TSF document context is established.
            _pendingHangulToggle = IsKoreanModeActive();

            // ContentDialog resets Win32 focus after Opened returns; RunAsync(Normal)
            // queues after that reset so our SetFocus + XAML focus win.
            Dispatcher().RunAsync(
                winrt::Windows::UI::Core::CoreDispatcherPriority::Normal,
                [this]() {
                    if (_islandHwnd) ::SetFocus(_islandHwnd);
                    NewFolderNameBox().Focus(winrt::Windows::UI::Xaml::FocusState::Programmatic);
                });
        });
    }

    // WM_CHAR never reaches XAML TextBoxes in this Xaml Islands setup; insert
    // printable characters directly from KeyDown via ToUnicode().
    void ProjectOrganizerDialog::_AttachInsertHandler(winrt::Windows::UI::Xaml::Controls::TextBox const& box)
    {
        box.KeyDown([box](winrt::Windows::Foundation::IInspectable const&,
                          winrt::Windows::UI::Xaml::Input::KeyRoutedEventArgs const& e) {
            auto vk = static_cast<UINT>(e.OriginalKey());
            if (vk == 0xE5) return; // VK_PROCESSKEY: Korean TSF composition — leave alone
            if ((::GetKeyState(VK_CONTROL) & 0x8000) != 0) return;
            if ((::GetKeyState(VK_MENU)    & 0x8000) != 0) return;

            BYTE ks[256]; ::GetKeyboardState(ks);
            WCHAR ch[4] = {};
            if (::ToUnicode(vk, ::MapVirtualKey(vk, MAPVK_VK_TO_VSC), ks, ch, 4, 0) != 1
                || ch[0] < L' ') // non-printables (BackSpace, Enter, ...) pass through
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
    }

    void ProjectOrganizerDialog::_OnTextBoxGotFocus(
        const winrt::Windows::Foundation::IInspectable& /*sender*/,
        const winrt::Windows::UI::Xaml::RoutedEventArgs& /*e*/)
    {
        // NO SetFocus here — any SetFocus in GotFocus causes a focus loop.
        // Only the pending VK_HANGUL toggle is handled.
        if (_pendingHangulToggle)
        {
            _pendingHangulToggle = false;
            INPUT inp[2] = {};
            inp[0].type       = INPUT_KEYBOARD;
            inp[0].ki.wVk     = VK_HANGUL;
            inp[1]            = inp[0];
            inp[1].ki.dwFlags = KEYEVENTF_KEYUP;
            ::SendInput(2, inp, sizeof(INPUT));

            // VK_HANGUL resets the TSF context → LostFocus; restore afterwards.
            Dispatcher().RunAsync(
                winrt::Windows::UI::Core::CoreDispatcherPriority::Normal,
                [this]() {
                    if (_islandHwnd) ::SetFocus(_islandHwnd);
                    NewFolderNameBox().Focus(winrt::Windows::UI::Xaml::FocusState::Programmatic);
                });
        }
    }

    void ProjectOrganizerDialog::SetData(winrt::hstring const& json)
    {
        _folders.clear();
        _projects.clear();

        Json::Value root;
        Json::CharReaderBuilder builder;
        std::string errs;
        std::istringstream ss(WideToNarrow(std::wstring{ json }));
        if (Json::parseFromStream(builder, ss, &root, &errs))
        {
            struct OrderedFolder { FolderEntry Entry; uint32_t Order; };
            struct OrderedProj   { ProjEntry Entry; uint32_t Order; };
            std::vector<OrderedFolder> ofs;
            std::vector<OrderedProj>   ops;

            for (const auto& fj : root["folders"])
            {
                OrderedFolder of;
                of.Entry.Id   = NarrowToWide(fj.get("id", "").asString());
                of.Entry.Name = NarrowToWide(fj.get("name", "").asString());
                of.Order      = fj.get("order", 0).asUInt();
                if (!of.Entry.Id.empty()) ofs.push_back(std::move(of));
            }
            for (const auto& pj : root["projects"])
            {
                OrderedProj op;
                op.Entry.Id       = NarrowToWide(pj.get("id", "").asString());
                op.Entry.Name     = NarrowToWide(pj.get("name", "").asString());
                op.Entry.FolderId = NarrowToWide(pj.get("folderId", "").asString());
                op.Order          = pj.get("order", 0).asUInt();
                if (!op.Entry.Id.empty()) ops.push_back(std::move(op));
            }

            std::stable_sort(ofs.begin(), ofs.end(),
                             [](const OrderedFolder& a, const OrderedFolder& b) { return a.Order < b.Order; });
            std::stable_sort(ops.begin(), ops.end(),
                             [](const OrderedProj& a, const OrderedProj& b) { return a.Order < b.Order; });

            for (auto& of : ofs) _folders.push_back(std::move(of.Entry));
            for (auto& op : ops) _projects.push_back(std::move(op.Entry));
        }

        _RefreshList();
    }

    // Display order: each folder's projects (in _projects order), then root projects.
    std::vector<size_t> ProjectOrganizerDialog::_DisplayOrderIndices() const
    {
        std::vector<size_t> indices;
        indices.reserve(_projects.size());

        for (const auto& folder : _folders)
        {
            for (size_t i = 0; i < _projects.size(); ++i)
            {
                if (_projects[i].FolderId == folder.Id) indices.push_back(i);
            }
        }
        for (size_t i = 0; i < _projects.size(); ++i)
        {
            const auto& fid = _projects[i].FolderId;
            const bool known = !fid.empty() &&
                               std::any_of(_folders.begin(), _folders.end(),
                                           [&fid](const FolderEntry& f) { return f.Id == fid; });
            if (fid.empty() || !known) indices.push_back(i);
        }
        return indices;
    }

    winrt::hstring ProjectOrganizerDialog::ResultJson()
    {
        Json::Value root;

        Json::Value folders(Json::arrayValue);
        for (size_t i = 0; i < _folders.size(); ++i)
        {
            Json::Value fj;
            fj["id"]    = WideToNarrow(_folders[i].Id);
            fj["name"]  = WideToNarrow(_folders[i].Name);
            fj["order"] = static_cast<Json::UInt>(i);
            folders.append(fj);
        }
        root["folders"] = folders;

        Json::Value projects(Json::arrayValue);
        uint32_t order = 0;
        for (size_t idx : _DisplayOrderIndices())
        {
            const auto& p = _projects[idx];
            const bool known = !p.FolderId.empty() &&
                               std::any_of(_folders.begin(), _folders.end(),
                                           [&p](const FolderEntry& f) { return f.Id == p.FolderId; });
            Json::Value pj;
            pj["id"]       = WideToNarrow(p.Id);
            pj["order"]    = order++;
            pj["folderId"] = known ? WideToNarrow(p.FolderId) : std::string{};
            projects.append(pj);
        }
        root["projects"] = projects;

        Json::StreamWriterBuilder wb;
        wb["indentation"] = "";
        return hstring{ winrt::to_hstring(Json::writeString(wb, root)) };
    }

    void ProjectOrganizerDialog::_AddFolderClicked(
        const winrt::Windows::Foundation::IInspectable& /*sender*/,
        const winrt::Windows::UI::Xaml::RoutedEventArgs& /*e*/)
    {
        auto name = std::wstring{ NewFolderNameBox().Text() };
        const auto first = name.find_first_not_of(L" \t");
        if (first == std::wstring::npos) return; // empty / whitespace only
        const auto last = name.find_last_not_of(L" \t");
        name = name.substr(first, last - first + 1);

        FolderEntry folder;
        folder.Id   = L"new-" + std::to_wstring(++_newFolderCounter);
        folder.Name = name;
        _folders.push_back(std::move(folder));

        NewFolderNameBox().Text(L"");
        _RefreshList();
    }

    void ProjectOrganizerDialog::_RefreshList()
    {
        _building = true;
        OrganizerListPanel().Children().Clear();

        auto weakSelf = get_weak();

        // Deferred rebuild — never rebuild the tree from inside an event raised
        // by a control that lives in the tree being rebuilt.
        auto requestRefresh = [weakSelf](winrt::Windows::UI::Core::CoreDispatcher const& dispatcher) {
            dispatcher.RunAsync(
                winrt::Windows::UI::Core::CoreDispatcherPriority::Normal,
                [weakSelf]() {
                    if (auto self = weakSelf.get()) self->_RefreshList();
                });
        };

        // ---- project row builder -------------------------------------------
        auto makeProjectRow = [&](const ProjEntry& proj, bool indented) -> UIElement {
            Border row;
            row.CornerRadius({ 4, 4, 4, 4 });
            row.Padding({ 8, 6, 8, 6 });
            row.Margin(indented ? Thickness{ 16, 0, 0, 0 } : Thickness{ 0, 0, 0, 0 });
            row.Background(SolidColorBrush{ winrt::Windows::UI::Color{ 0x18, 0x80, 0x80, 0x80 } });
            row.Tag(winrt::box_value(hstring{ proj.Id }));
            row.CanDrag(true);
            row.AllowDrop(true);
            row.DragStarting({ this, &ProjectOrganizerDialog::_OnProjectDragStarting });
            row.DragOver({ this, &ProjectOrganizerDialog::_OnRowDragOver });
            row.Drop({ this, &ProjectOrganizerDialog::_OnProjectDrop });

            Grid grid;
            ColumnDefinition c0; c0.Width(MakeAuto());
            ColumnDefinition c1; c1.Width(MakeStar());
            ColumnDefinition c2; c2.Width(MakeAuto());
            grid.ColumnDefinitions().Append(c0);
            grid.ColumnDefinitions().Append(c1);
            grid.ColumnDefinitions().Append(c2);

            TextBlock grip;
            grip.Text(L"\x2261"); // ≡
            grip.FontSize(14.0);
            grip.Opacity(0.5);
            grip.VerticalAlignment(VerticalAlignment::Center);
            grip.Margin({ 0, 0, 8, 0 });
            Grid::SetColumn(grip, 0);
            grid.Children().Append(grip);

            TextBlock nameText;
            nameText.Text(hstring{ proj.Name });
            nameText.FontSize(13.0);
            nameText.VerticalAlignment(VerticalAlignment::Center);
            nameText.TextTrimming(TextTrimming::CharacterEllipsis);
            Grid::SetColumn(nameText, 1);
            grid.Children().Append(nameText);

            // Folder picker: "— 없음 —" + folders
            ComboBox folderBox;
            folderBox.MinWidth(120.0);
            folderBox.FontSize(12.0);
            folderBox.Margin({ 8, 0, 0, 0 });
            folderBox.Items().Append(winrt::box_value(hstring{ L"\x2014 없음 \x2014" }));
            int selected = 0;
            for (size_t fi = 0; fi < _folders.size(); ++fi)
            {
                folderBox.Items().Append(winrt::box_value(hstring{ _folders[fi].Name }));
                if (_folders[fi].Id == proj.FolderId) selected = static_cast<int>(fi) + 1;
            }
            folderBox.SelectedIndex(selected);

            auto projId = proj.Id;
            folderBox.SelectionChanged([weakSelf, requestRefresh, projId](
                                           winrt::Windows::Foundation::IInspectable const& s,
                                           SelectionChangedEventArgs const&) {
                auto self = weakSelf.get();
                if (!self || self->_building) return;
                auto box = s.try_as<ComboBox>();
                if (!box) return;
                const int idx = box.SelectedIndex();
                if (idx < 0) return;

                for (auto& p : self->_projects)
                {
                    if (p.Id == projId)
                    {
                        p.FolderId = (idx == 0 || static_cast<size_t>(idx) > self->_folders.size())
                                         ? std::wstring{}
                                         : self->_folders[static_cast<size_t>(idx) - 1].Id;
                        break;
                    }
                }
                requestRefresh(self->Dispatcher());
            });
            Grid::SetColumn(folderBox, 2);
            grid.Children().Append(folderBox);

            row.Child(grid);
            return row;
        };

        // ---- folder header builder -----------------------------------------
        auto makeFolderRow = [&](const FolderEntry& folder, size_t folderIdx) -> UIElement {
            Border row;
            row.CornerRadius({ 4, 4, 4, 4 });
            row.Padding({ 8, 4, 8, 4 });
            row.Margin({ 0, 4, 0, 0 });
            row.Background(SolidColorBrush{ winrt::Windows::UI::Color{ 0x30, 0x80, 0x80, 0x80 } });
            row.Tag(winrt::box_value(hstring{ folder.Id }));
            row.AllowDrop(true);
            row.DragOver({ this, &ProjectOrganizerDialog::_OnRowDragOver });
            row.Drop({ this, &ProjectOrganizerDialog::_OnFolderHeaderDrop });

            Grid grid;
            ColumnDefinition c0; c0.Width(MakeAuto());
            ColumnDefinition c1; c1.Width(MakeStar());
            ColumnDefinition c2; c2.Width(MakeAuto());
            ColumnDefinition c3; c3.Width(MakeAuto());
            ColumnDefinition c4; c4.Width(MakeAuto());
            grid.ColumnDefinitions().Append(c0);
            grid.ColumnDefinitions().Append(c1);
            grid.ColumnDefinitions().Append(c2);
            grid.ColumnDefinitions().Append(c3);
            grid.ColumnDefinitions().Append(c4);

            FontIcon icon;
            icon.FontFamily(Media::FontFamily{ L"Segoe MDL2 Assets" });
            icon.Glyph(L"\xE8B7"); // folder
            icon.FontSize(12.0);
            icon.Opacity(0.7);
            icon.VerticalAlignment(VerticalAlignment::Center);
            icon.Margin({ 0, 0, 8, 0 });
            Grid::SetColumn(icon, 0);
            grid.Children().Append(icon);

            // Inline-editable folder name
            TextBox nameBox;
            nameBox.Text(hstring{ folder.Name });
            nameBox.FontSize(12.0);
            nameBox.VerticalAlignment(VerticalAlignment::Center);
            _AttachInsertHandler(nameBox);
            nameBox.GotFocus({ this, &ProjectOrganizerDialog::_OnTextBoxGotFocus });
            {
                auto folderId = folder.Id;
                nameBox.TextChanged([weakSelf, folderId](winrt::Windows::Foundation::IInspectable const& s,
                                                         TextChangedEventArgs const&) {
                    auto self = weakSelf.get();
                    if (!self) return;
                    auto box = s.try_as<TextBox>();
                    if (!box) return;
                    for (auto& f : self->_folders)
                    {
                        if (f.Id == folderId)
                        {
                            f.Name = std::wstring{ box.Text() };
                            break;
                        }
                    }
                });
            }
            Grid::SetColumn(nameBox, 1);
            grid.Children().Append(nameBox);

            auto makeSmallBtn = [](const wchar_t* glyph, const wchar_t* tooltip) {
                Button btn;
                btn.Width(28.0);
                btn.Height(26.0);
                btn.Padding({ 0, 0, 0, 0 });
                btn.Margin({ 4, 0, 0, 0 });
                FontIcon i;
                i.FontFamily(Media::FontFamily{ L"Segoe MDL2 Assets" });
                i.Glyph(glyph);
                i.FontSize(10.0);
                btn.Content(i);
                ToolTipService::SetToolTip(btn, winrt::box_value(hstring{ tooltip }));
                return btn;
            };

            // ▲ move folder up
            auto upBtn = makeSmallBtn(L"\xE70E", L"위로");
            upBtn.IsEnabled(folderIdx > 0);
            upBtn.Click([weakSelf, requestRefresh, folderIdx](winrt::Windows::Foundation::IInspectable const&,
                                                              RoutedEventArgs const&) {
                auto self = weakSelf.get();
                if (!self || folderIdx == 0 || folderIdx >= self->_folders.size()) return;
                std::swap(self->_folders[folderIdx], self->_folders[folderIdx - 1]);
                requestRefresh(self->Dispatcher());
            });
            Grid::SetColumn(upBtn, 2);
            grid.Children().Append(upBtn);

            // ▼ move folder down
            auto downBtn = makeSmallBtn(L"\xE70D", L"아래로");
            downBtn.IsEnabled(folderIdx + 1 < _folders.size());
            downBtn.Click([weakSelf, requestRefresh, folderIdx](winrt::Windows::Foundation::IInspectable const&,
                                                                RoutedEventArgs const&) {
                auto self = weakSelf.get();
                if (!self || folderIdx + 1 >= self->_folders.size()) return;
                std::swap(self->_folders[folderIdx], self->_folders[folderIdx + 1]);
                requestRefresh(self->Dispatcher());
            });
            Grid::SetColumn(downBtn, 3);
            grid.Children().Append(downBtn);

            // 삭제 — folder removed immediately; member projects go to root
            auto delBtn = makeSmallBtn(L"\xE74D", L"폴더 삭제 (프로젝트는 유지)");
            {
                auto folderId = folder.Id;
                delBtn.Click([weakSelf, requestRefresh, folderId](winrt::Windows::Foundation::IInspectable const&,
                                                                  RoutedEventArgs const&) {
                    auto self = weakSelf.get();
                    if (!self) return;
                    self->_folders.erase(
                        std::remove_if(self->_folders.begin(), self->_folders.end(),
                                       [&folderId](const FolderEntry& f) { return f.Id == folderId; }),
                        self->_folders.end());
                    for (auto& p : self->_projects)
                    {
                        if (p.FolderId == folderId) p.FolderId.clear();
                    }
                    requestRefresh(self->Dispatcher());
                });
            }
            Grid::SetColumn(delBtn, 4);
            grid.Children().Append(delBtn);

            row.Child(grid);
            return row;
        };

        // ---- compose list ----------------------------------------------------
        for (size_t fi = 0; fi < _folders.size(); ++fi)
        {
            OrganizerListPanel().Children().Append(makeFolderRow(_folders[fi], fi));
            for (const auto& p : _projects)
            {
                if (p.FolderId == _folders[fi].Id)
                    OrganizerListPanel().Children().Append(makeProjectRow(p, true));
            }
        }

        bool anyRoot = false;
        for (const auto& p : _projects)
        {
            const bool known = !p.FolderId.empty() &&
                               std::any_of(_folders.begin(), _folders.end(),
                                           [&p](const FolderEntry& f) { return f.Id == p.FolderId; });
            if (p.FolderId.empty() || !known)
            {
                if (!anyRoot && !_folders.empty())
                {
                    TextBlock rootLabel;
                    rootLabel.Text(L"폴더 없음");
                    rootLabel.FontSize(10.0);
                    rootLabel.Opacity(0.5);
                    rootLabel.Margin({ 2, 6, 0, 0 });
                    OrganizerListPanel().Children().Append(rootLabel);
                }
                anyRoot = true;
                OrganizerListPanel().Children().Append(makeProjectRow(p, false));
            }
        }

        if (_projects.empty() && _folders.empty())
        {
            TextBlock emptyText;
            emptyText.Text(L"프로젝트가 없습니다.");
            emptyText.FontSize(12.0);
            emptyText.Opacity(0.5);
            OrganizerListPanel().Children().Append(emptyText);
        }

        _building = false;
    }

    // -----------------------------------------------------------------------
    // Drag & drop
    // -----------------------------------------------------------------------

    void ProjectOrganizerDialog::_OnProjectDragStarting(const winrt::Windows::Foundation::IInspectable& sender,
                                                        const winrt::Windows::UI::Xaml::DragStartingEventArgs& e)
    {
        if (auto border = sender.try_as<Border>())
        {
            _dragProjectId = std::wstring{ winrt::unbox_value_or<hstring>(border.Tag(), hstring{}) };
            e.Data().SetText(L"projectreorder");
            e.DragUI().SetContentFromDataPackage();
        }
    }

    void ProjectOrganizerDialog::_OnRowDragOver(const winrt::Windows::Foundation::IInspectable& /*sender*/,
                                                const winrt::Windows::UI::Xaml::DragEventArgs& e)
    {
        if (e.DataView().Contains(StandardDataFormats::Text()))
            e.AcceptedOperation(DataPackageOperation::Move);
    }

    // Drop onto a project row: insert the dragged project at the target's
    // position and adopt the target's folder.
    void ProjectOrganizerDialog::_OnProjectDrop(const winrt::Windows::Foundation::IInspectable& sender,
                                                const winrt::Windows::UI::Xaml::DragEventArgs& /*e*/)
    {
        const auto srcId = _dragProjectId;
        _dragProjectId.clear();
        if (srcId.empty()) return;

        auto border = sender.try_as<Border>();
        if (!border) return;
        const auto dstId = std::wstring{ winrt::unbox_value_or<hstring>(border.Tag(), hstring{}) };
        if (dstId.empty() || dstId == srcId) return;

        auto srcIt = std::find_if(_projects.begin(), _projects.end(),
                                  [&srcId](const ProjEntry& p) { return p.Id == srcId; });
        if (srcIt == _projects.end()) return;

        auto item = std::move(*srcIt);
        _projects.erase(srcIt);

        auto dstIt = std::find_if(_projects.begin(), _projects.end(),
                                  [&dstId](const ProjEntry& p) { return p.Id == dstId; });
        if (dstIt == _projects.end())
        {
            _projects.push_back(std::move(item));
        }
        else
        {
            item.FolderId = dstIt->FolderId; // adopt target's group
            _projects.insert(dstIt, std::move(item));
        }

        _RefreshList();
    }

    // Drop onto a folder header: move the project into that folder.
    void ProjectOrganizerDialog::_OnFolderHeaderDrop(const winrt::Windows::Foundation::IInspectable& sender,
                                                     const winrt::Windows::UI::Xaml::DragEventArgs& /*e*/)
    {
        const auto srcId = _dragProjectId;
        _dragProjectId.clear();
        if (srcId.empty()) return;

        auto border = sender.try_as<Border>();
        if (!border) return;
        const auto folderId = std::wstring{ winrt::unbox_value_or<hstring>(border.Tag(), hstring{}) };
        if (folderId.empty()) return;

        for (auto& p : _projects)
        {
            if (p.Id == srcId)
            {
                p.FolderId = folderId;
                break;
            }
        }
        _RefreshList();
    }
}
