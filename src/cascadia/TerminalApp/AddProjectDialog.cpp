// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "AddProjectDialog.h"
#include "AddProjectDialog.g.cpp"
#include <imm.h>
#pragma comment(lib, "imm32.lib")
#include <CoreWindow.h>  // ICoreWindowInterop

static void _WriteDebugLog(const wchar_t* msg)
{
    wchar_t path[MAX_PATH];
    ::ExpandEnvironmentStringsW(
        L"%LOCALAPPDATA%\\ClickTerminal\\ctux-debug.log", path, MAX_PATH);
    FILE* f = nullptr;
    _wfopen_s(&f, path, L"a");
    if (f) { fwprintf(f, L"%s\n", msg); fclose(f); }
}

// GetKeyState(VK_HANGUL) low bit = 1 → Korean mode active.
// Confirmed reliable: log showed 0xFF81 (toggle=1) when user was in Korean mode.
// GetAsyncKeyState was wrong (returns 0x8000, toggle=0). TSF compartment and
// ImmGetConversionStatus both read stale data from a compartment we wrote 0 to.
static bool _IsKoreanModeActive()
{
    SHORT ks = ::GetKeyState(VK_HANGUL);
    bool isKorean = (ks & 0x0001) != 0;
    wchar_t buf[64];
    swprintf_s(buf, L"[Dialog] IsKoreanMode: ks=0x%04X → %d", (unsigned short)ks, (int)isKorean);
    _WriteDebugLog(buf);
    return isKorean;
}


namespace winrt::TerminalApp::implementation
{
    AddProjectDialog::AddProjectDialog()
    {
        // Initialize per-tool default commands BEFORE InitializeComponent so that
        // the SelectionChanged handler (attached after init) can safely read them.
        _toolCommands[0] = L"claude";
        _toolCommands[1] = L"codex";
        _toolCommands[2] = L"gemini";
        _toolCommands[3] = L"";
        _previousToolIdx = 0;

        InitializeComponent();

        // Attach SelectionChanged AFTER InitializeComponent so AIStartCommandBox exists.
        // Attaching via XAML attribute fires during InitializeComponent (when SelectedIndex="0"
        // is applied) before AIStartCommandBox is constructed → null-access crash.
        AIToolBox().SelectionChanged({ this, &AddProjectDialog::_AIToolSelectionChanged });

        // WM_CHAR is stolen by the terminal Win32 HWND and never reaches the XAML TextBox.
        // XAML KeyDown fires regardless of Win32 HWND focus, so we intercept here and insert directly.
        // Attach to each TextBox individually — ContentDialog PreviewKeyDown has routing issues in Xaml Islands.
        auto attachInsert = [](winrt::Windows::UI::Xaml::Controls::TextBox box) {
            box.KeyDown([box](winrt::Windows::Foundation::IInspectable const&,
                              winrt::Windows::UI::Xaml::Input::KeyRoutedEventArgs const& e) {
                auto vk = static_cast<UINT>(e.OriginalKey());
                if (vk == 0xE5) return;  // VK_PROCESSKEY: IME composition — let TSF handle
                if ((::GetKeyState(VK_CONTROL) & 0x8000) != 0) return;
                if ((::GetKeyState(VK_MENU)    & 0x8000) != 0) return;

                BYTE ks[256]; ::GetKeyboardState(ks);
                WCHAR ch[4] = {};
                int r = ::ToUnicode(vk, ::MapVirtualKey(vk, MAPVK_VK_TO_VSC), ks, ch, 4, 0);
                if (r != 1 || ch[0] < L' ') return;

                auto sel  = box.SelectionStart();
                auto len  = box.SelectionLength();
                auto text = std::wstring{ box.Text() };
                if (len > 0) text.erase(sel, len);
                text.insert(sel, 1, ch[0]);
                box.Text(winrt::hstring{ text });
                box.SelectionStart(sel + 1);
                box.SelectionLength(0);

                wchar_t dbg[64];
                swprintf_s(dbg, L"[Dialog] KeyInsert: U+%04X", static_cast<unsigned>(ch[0]));
                _WriteDebugLog(dbg);
                e.Handled(true);
            });
        };
        attachInsert(ProjectNameBox());
        attachInsert(FolderPathBox());
        attachInsert(AIStartCommandBox());
        attachInsert(DevUrlBox());
        attachInsert(DeployUrlBox());
        attachInsert(GitUrlBox());

        Opened([this](winrt::Windows::Foundation::IInspectable const&,
                      winrt::Windows::UI::Xaml::Controls::ContentDialogOpenedEventArgs const&) {
            // Diagnostic logs
            ProjectNameBox().LostFocus([](winrt::Windows::Foundation::IInspectable const&,
                                          winrt::Windows::UI::Xaml::RoutedEventArgs const&) {
                auto fe = winrt::Windows::UI::Xaml::Input::FocusManager::GetFocusedElement();
                if (auto elem = fe.try_as<winrt::Windows::UI::Xaml::FrameworkElement>())
                    _WriteDebugLog((L"[Dialog] NameBox.LostFocus -> " + std::wstring{ elem.Name() }).c_str());
                else
                    _WriteDebugLog(L"[Dialog] NameBox.LostFocus -> (non-FE)");
            });
            ProjectNameBox().KeyDown([](winrt::Windows::Foundation::IInspectable const&,
                                        winrt::Windows::UI::Xaml::Input::KeyRoutedEventArgs const& e) {
                HWND fw = ::GetFocus();
                wchar_t buf[128];
                swprintf_s(buf, L"[Dialog] KeyDown: vk=0x%02X  GetFocus=0x%p",
                           static_cast<unsigned>(e.OriginalKey()), static_cast<void*>(fw));
                _WriteDebugLog(buf);
            });
            ProjectNameBox().CharacterReceived([](winrt::Windows::UI::Xaml::UIElement const&,
                                                   winrt::Windows::UI::Xaml::Input::CharacterReceivedRoutedEventArgs const& e) {
                wchar_t buf[32];
                swprintf_s(buf, L"[Dialog] CharReceived: U+%04X", static_cast<unsigned>(e.Character()));
                _WriteDebugLog(buf);
            });

            // Set Win32 HWND focus to the InputSite (XAML island) HWND.
            // Without this, WM_CHAR (English ASCII input) follows Win32 focus to the
            // terminal pane HWND, never reaching the TextBox. Korean TSF input works
            // regardless of HWND focus (TSF routes to XAML focus element directly).
            if (const auto cw = winrt::Windows::UI::Core::CoreWindow::GetForCurrentThread())
            {
                if (const auto interop = cw.try_as<ICoreWindowInterop>())
                {
                    HWND islandHwnd = nullptr;
                    if (SUCCEEDED(interop->get_WindowHandle(&islandHwnd)) && islandHwnd)
                    {
                        _islandHwnd = islandHwnd;
                        HWND focusBefore = ::GetFocus();
                        ::SetFocus(islandHwnd);
                        HWND focusAfter = ::GetFocus();
                        wchar_t buf[256];
                        swprintf_s(buf,
                            L"[Dialog] Opened: island=0x%p  before=0x%p  after=0x%p",
                            static_cast<void*>(islandHwnd),
                            static_cast<void*>(focusBefore),
                            static_cast<void*>(focusAfter));
                        _WriteDebugLog(buf);
                    }
                }
            }

            // Detect Korean mode now; send VK_HANGUL in GotFocus (not here).
            // Reason: at Opened time the TextBox TSF document context may not be
            // established yet — Korean IME's ITfKeyEventSink has no target to process
            // VK_HANGUL. Sending it in GotFocus (after TSF context is active) works.
            _pendingHangulToggle = _IsKoreanModeActive();
            _WriteDebugLog(_pendingHangulToggle ? L"[Dialog] Opened: Korean → will toggle in GotFocus"
                                               : L"[Dialog] Opened: English → no toggle needed");

            // ContentDialog internally resets Win32 HWND focus after Opened fires (synchronous).
            // RunAsync(Normal) queues after that reset, so SetFocus+XAML Focus land on top.
            Dispatcher().RunAsync(
                winrt::Windows::UI::Core::CoreDispatcherPriority::Normal,
                [this]() {
                    if (_islandHwnd) ::SetFocus(_islandHwnd);
                    ProjectNameBox().Focus(winrt::Windows::UI::Xaml::FocusState::Programmatic);
                    HWND fw = ::GetFocus();
                    wchar_t buf[128];
                    swprintf_s(buf, L"[Dialog] RunAsync(Opened): GetFocus=0x%p", static_cast<void*>(fw));
                    _WriteDebugLog(buf);
                });
        });
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

    // Sync TextBox content to _toolCommands for the currently visible tool before returning.
    // The user may have typed directly without switching tools (which normally triggers the sync).
    static void _SyncCurrentTextBox(winrt::Windows::UI::Xaml::Controls::ComboBox const& toolBox,
                                    winrt::Windows::UI::Xaml::Controls::TextBox const& cmdBox,
                                    std::wstring (&cmds)[4])
    {
        auto idx = toolBox.SelectedIndex();
        if (idx >= 0 && idx < 4)
            cmds[idx] = std::wstring{ cmdBox.Text() };
    }

    winrt::hstring AddProjectDialog::ClaudeStartCommand()
    {
        _SyncCurrentTextBox(AIToolBox(), AIStartCommandBox(), _toolCommands);
        return hstring{ _toolCommands[0] };
    }
    winrt::hstring AddProjectDialog::CodexStartCommand()
    {
        _SyncCurrentTextBox(AIToolBox(), AIStartCommandBox(), _toolCommands);
        return hstring{ _toolCommands[1] };
    }
    winrt::hstring AddProjectDialog::GeminiStartCommand()
    {
        _SyncCurrentTextBox(AIToolBox(), AIStartCommandBox(), _toolCommands);
        return hstring{ _toolCommands[2] };
    }

    bool AddProjectDialog::AutoStartAI()
    {
        auto checked = AutoStartAIBox().IsChecked();
        return checked && checked.Value();
    }

    winrt::hstring AddProjectDialog::DevUrl()    { return DevUrlBox().Text(); }
    winrt::hstring AddProjectDialog::DeployUrl() { return DeployUrlBox().Text(); }
    winrt::hstring AddProjectDialog::GitUrl()    { return GitUrlBox().Text(); }

    static constexpr const wchar_t* DefaultCommandForIndex(int idx)
    {
        switch (idx)
        {
        case 0:  return L"claude";
        case 1:  return L"codex";
        case 2:  return L"gemini";
        default: return L"";
        }
    }

    void AddProjectDialog::_AIToolSelectionChanged(
        const winrt::Windows::Foundation::IInspectable& /*sender*/,
        const winrt::Windows::UI::Xaml::Controls::SelectionChangedEventArgs& /*e*/)
    {
        if (_previousToolIdx >= 0 && _previousToolIdx < 4)
            _toolCommands[_previousToolIdx] = std::wstring{ AIStartCommandBox().Text() };

        auto idx = AIToolBox().SelectedIndex();
        if (idx < 0 || idx > 3) idx = 3;

        const wchar_t* defaultCmd = DefaultCommandForIndex(idx);
        auto& stored = _toolCommands[idx];
        if (stored.empty()) stored = defaultCmd;

        AIStartCommandBox().PlaceholderText(hstring{ defaultCmd });
        AIStartCommandBox().Text(hstring{ stored });

        _previousToolIdx = idx;
    }

    void AddProjectDialog::SetInitialValues(
        winrt::hstring const& name, winrt::hstring const& path,
        winrt::hstring const& type, winrt::hstring const& aiTool,
        winrt::hstring const& claudeCmd,
        winrt::hstring const& codexCmd,
        winrt::hstring const& geminiCmd,
        bool autoStartAI,
        winrt::hstring const& devUrl,
        winrt::hstring const& deployUrl,
        winrt::hstring const& gitUrl)
    {
        ProjectNameBox().Text(name);
        FolderPathBox().Text(path);

        if      (type == L"app")         ProjectTypeBox().SelectedIndex(1);
        else if (type == L"ai-workflow") ProjectTypeBox().SelectedIndex(2);
        else                             ProjectTypeBox().SelectedIndex(0);

        int toolIdx = 3;
        if      (aiTool == L"claude") toolIdx = 0;
        else if (aiTool == L"codex")  toolIdx = 1;
        else if (aiTool == L"gemini") toolIdx = 2;
        _previousToolIdx = toolIdx;
        AIToolBox().SelectedIndex(toolIdx);  // fires _AIToolSelectionChanged, may clobber _toolCommands

        // Re-apply commands AFTER SelectedIndex so the handler's overwrite is undone
        _toolCommands[0] = claudeCmd.empty() ? L"claude" : std::wstring{ claudeCmd };
        _toolCommands[1] = codexCmd.empty()  ? L"codex"  : std::wstring{ codexCmd };
        _toolCommands[2] = geminiCmd.empty() ? L"gemini" : std::wstring{ geminiCmd };
        _toolCommands[3] = L"";

        const wchar_t* defaultCmd = DefaultCommandForIndex(toolIdx);
        AIStartCommandBox().PlaceholderText(hstring{ defaultCmd });
        AIStartCommandBox().Text(hstring{ _toolCommands[toolIdx] });

        AutoStartAIBox().IsChecked(winrt::box_value(autoStartAI).as<winrt::Windows::Foundation::IReference<bool>>());

        DevUrlBox().Text(devUrl);
        DeployUrlBox().Text(deployUrl);
        GitUrlBox().Text(gitUrl);

        Title(winrt::box_value(winrt::hstring(L"프로젝트 편집")));
        PrimaryButtonText(L"수정");
        SecondaryButtonText(L"삭제");
    }

    void AddProjectDialog::_OnTextBoxGotFocus(
        const winrt::Windows::Foundation::IInspectable& /*sender*/,
        const winrt::Windows::UI::Xaml::RoutedEventArgs& /*e*/)
    {
        // If dialog opened while Korean mode was active, toggle to English now.
        // Done here (not in Opened) so the TextBox TSF document context is established —
        // Korean IME's ITfKeyEventSink needs an active TSF context to process VK_HANGUL.
        if (_pendingHangulToggle)
        {
            _pendingHangulToggle = false;
            _WriteDebugLog(L"[Dialog] GotFocus: sending VK_HANGUL");
            INPUT inp[2] = {};
            inp[0].type       = INPUT_KEYBOARD;
            inp[0].ki.wVk     = VK_HANGUL;   // 0x15
            inp[1]            = inp[0];
            inp[1].ki.dwFlags = KEYEVENTF_KEYUP;
            ::SendInput(2, inp, sizeof(INPUT));

            // VK_HANGUL processing causes LostFocus on the TextBox (TSF resets context).
            // RunAsync(Normal) runs after the OS processes the queued VK_HANGUL message,
            // restoring Win32 + XAML focus so the user can type immediately.
            Dispatcher().RunAsync(
                winrt::Windows::UI::Core::CoreDispatcherPriority::Normal,
                [this]() {
                    _WriteDebugLog(L"[Dialog] RunAsync: restoring focus after VK_HANGUL");
                    if (const auto cw = winrt::Windows::UI::Core::CoreWindow::GetForCurrentThread())
                    {
                        if (const auto interop = cw.try_as<ICoreWindowInterop>())
                        {
                            HWND island = nullptr;
                            if (SUCCEEDED(interop->get_WindowHandle(&island)) && island)
                                ::SetFocus(island);
                        }
                    }
                    ProjectNameBox().Focus(winrt::Windows::UI::Xaml::FocusState::Programmatic);
                    _WriteDebugLog(L"[Dialog] RunAsync: focus restored");
                });
        }
    }

    void AddProjectDialog::_BrowseFolderClicked(const winrt::Windows::Foundation::IInspectable& /*sender*/,
                                                  const winrt::Windows::UI::Xaml::RoutedEventArgs& /*e*/)
    {
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
