// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.
//
// CTuxIntegration.cpp -- ClickTerminal (CTux) integration for TerminalPage.
// This file contains all ClickTerminal-specific TerminalPage member
// definitions: sidebar event handlers (open terminal / start-stop AI /
// theme propagation), the layout system (pane tree building, layout
// picker dialog) and the per-pane project info overlay strip.
//
// Same pattern as TabManagement.cpp: TerminalPage members split into a
// separate translation unit.
//

#include "pch.h"
#include "TerminalPage.h"

#include "TabRowControl.h"
#include "CTuxSettings.h"

#include <ShlObj.h>
#include <algorithm>
#include <fstream>

using namespace winrt;
using namespace winrt::Microsoft::Terminal::Control;
using namespace winrt::Microsoft::Terminal::Settings::Model;
using namespace winrt::Microsoft::Terminal::TerminalConnection;
using namespace winrt::Microsoft::Terminal;
using namespace winrt::Windows::Foundation::Collections;
using namespace winrt::Windows::UI;
using namespace winrt::Windows::UI::Core;
using namespace winrt::Windows::UI::Text;
using namespace winrt::Windows::UI::Xaml::Controls;
using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Xaml::Media;
using namespace ::TerminalApp;

namespace winrt
{
    namespace MUX = Microsoft::UI::Xaml;
    namespace WUX = Windows::UI::Xaml;
    using IInspectable = Windows::Foundation::IInspectable;
}

namespace winrt::TerminalApp::implementation
{
    // ClickTerminal: Open URL in default browser
    void TerminalPage::_SidebarOpenUrlRequested(const winrt::Windows::Foundation::IInspectable& /*sender*/,
                                                 const winrt::hstring& url)
    {
        if (!url.empty())
            ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    // ------------------------------------------------------------------------
    // CTux debug log helper (same file as ProjectSidebar's DbgLog)
    // ------------------------------------------------------------------------
    static void CTuxLog(const std::wstring& msg)
    {
        wchar_t raw[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, raw)))
        {
            std::wstring p = std::wstring(raw) + L"\\ClickTerminal\\ctux-debug.log";
            std::wofstream f(p, std::ios::app);
            f << msg << L"\n";
        }
    }

    // ------------------------------------------------------------------------
    // Per-pane session tracking helpers
    // ------------------------------------------------------------------------

    // Drop records whose pane or tab has been destroyed.
    void TerminalPage::_CTuxPruneSessions()
    {
        _ctuxSessions.erase(
            std::remove_if(_ctuxSessions.begin(), _ctuxSessions.end(),
                           [](const CTuxPaneSession& s) { return s.pane.expired() || !s.tab.get(); }),
            _ctuxSessions.end());
    }

    void TerminalPage::_CTuxRegisterSession(const std::wstring& projectId, const std::wstring& toolId,
                                            bool aiRunning, bool openedFromSidebar,
                                            const winrt::TerminalApp::Tab& tab, const std::shared_ptr<Pane>& pane)
    {
        if (!pane) return;
        _CTuxPruneSessions();
        // Reuse an existing record for the same pane
        for (auto& s : _ctuxSessions)
        {
            if (auto p = s.pane.lock(); p && p == pane)
            {
                s.projectId = projectId;
                if (!toolId.empty()) s.toolId = toolId;
                s.aiRunning = s.aiRunning || aiRunning;
                s.openedFromSidebar = s.openedFromSidebar || openedFromSidebar;
                s.tab = winrt::make_weak(tab);
                return;
            }
        }
        CTuxPaneSession rec;
        rec.projectId = projectId;
        rec.toolId = toolId;
        rec.aiRunning = aiRunning;
        rec.openedFromSidebar = openedFromSidebar;
        rec.tab = winrt::make_weak(tab);
        rec.pane = pane;
        _ctuxSessions.push_back(std::move(rec));
    }

    void TerminalPage::_CTuxMarkAIRunning(const std::shared_ptr<Pane>& pane, const std::wstring& toolId, bool running)
    {
        for (auto& s : _ctuxSessions)
        {
            if (auto p = s.pane.lock(); p && p == pane)
            {
                s.aiRunning = running;
                if (!toolId.empty()) s.toolId = toolId;
                return;
            }
        }
    }

    bool TerminalPage::_CTuxProjectHasRunningAI(const std::wstring& projectId)
    {
        for (auto& s : _ctuxSessions)
            if (s.aiRunning && s.projectId == projectId && !s.pane.expired() && s.tab.get())
                return true;
        return false;
    }

    // Resolve the command used to launch the project's default AI tool.
    // The user-configured "AI Start Command" always wins; fall back to the
    // AIToolManager-assembled command line, then to the bare tool name.
    std::wstring TerminalPage::_CTuxResolveAICommand(const ClickTerminal::Project& project) const
    {
        const auto& tool = project.AIConfig.DefaultTool;
        if (tool.empty()) return {};

        std::wstring startCmd;
        if (tool == L"claude")      startCmd = project.AIConfig.Claude.StartCommand;
        else if (tool == L"codex")  startCmd = project.AIConfig.Codex.StartCommand;
        else if (tool == L"gemini") startCmd = project.AIConfig.Gemini.StartCommand;
        if (!startCmd.empty())
        {
            CTuxLog(L"[CTux] resolve cmd (StartCommand): " + startCmd);
            return startCmd;
        }

        ClickTerminal::AITool t = ClickTerminal::AITool::Claude;
        if (tool == L"codex")       t = ClickTerminal::AITool::Codex;
        else if (tool == L"gemini") t = ClickTerminal::AITool::Gemini;

        ClickTerminal::AIToolManager aimgr;
        auto launchCmd = aimgr.GetLaunchCommand(project, t, false);
        auto cmdLine = launchCmd.ToCommandLine();
        if (!cmdLine.empty())
        {
            CTuxLog(L"[CTux] resolve cmd (assembled): " + cmdLine);
            return cmdLine;
        }
        return std::wstring{ tool };
    }

    // Send input once the control's connection is ready. Fixes the race where
    // SendInput on a freshly created (unfocused) pane was silently dropped.
    void TerminalPage::_CTuxSendWhenReady(const winrt::Microsoft::Terminal::Control::TermControl& ctrl,
                                          const winrt::hstring& cmd)
    {
        if (!ctrl || cmd.empty()) return;

        using ConnState = winrt::Microsoft::Terminal::TerminalConnection::ConnectionState;

        if (ctrl.ConnectionState() == ConnState::Connected)
        {
            ctrl.SendInput(cmd);
            CTuxLog(L"[CTux] SendWhenReady: sent immediately");
            return;
        }

        CTuxLog(L"[CTux] SendWhenReady: queued (connection not ready)");

        auto sent = std::make_shared<bool>(false);
        auto connToken = std::make_shared<winrt::event_token>();

        *connToken = ctrl.ConnectionStateChanged(
            [sent, connToken, cmd](const winrt::Windows::Foundation::IInspectable& sender,
                                   const winrt::Windows::Foundation::IInspectable&) {
                auto c = sender.try_as<winrt::Microsoft::Terminal::Control::TermControl>();
                if (!c) return;
                if (*sent)
                {
                    c.ConnectionStateChanged(*connToken);
                    return;
                }
                if (c.ConnectionState() == ConnState::Connected)
                {
                    *sent = true;
                    c.SendInput(cmd);
                    c.ConnectionStateChanged(*connToken);
                    CTuxLog(L"[CTux] SendWhenReady: sent on ConnectionStateChanged");
                }
            });

        // Safety net: force-send once after 5 seconds if the event never fired.
        auto weakCtrl = winrt::make_weak(ctrl);
        WUX::DispatcherTimer timer;
        timer.Interval(std::chrono::seconds(5));
        auto tickToken = std::make_shared<winrt::event_token>();
        *tickToken = timer.Tick(
            [timer, tickToken, weakCtrl, sent, connToken, cmd](const winrt::Windows::Foundation::IInspectable&,
                                                               const winrt::Windows::Foundation::IInspectable&) {
                timer.Stop();
                timer.Tick(*tickToken); // self-revoke breaks the ref cycle
                if (*sent) return;
                *sent = true;
                if (auto c = weakCtrl.get())
                {
                    c.SendInput(cmd);
                    c.ConnectionStateChanged(*connToken);
                    CTuxLog(L"[CTux] SendWhenReady: timeout fallback fired");
                }
            });
        timer.Start();
    }

    // Subscribe to the connection output to track Claude context usage.
    void TerminalPage::_CTuxAttachContextMonitor(const std::wstring& projectId,
                                                 const winrt::Microsoft::Terminal::Control::TermControl& ctrl)
    {
        if (!ctrl) return;
        if (_aiOutputTokens.count(projectId)) return; // already attached

        auto conn = ctrl.Connection();
        if (!conn) return;

        auto pid = projectId;
        auto weakPage = get_weak();

        auto token = conn.TerminalOutput([weakPage, pid](const winrt::array_view<const char16_t> data) {
            if (data.empty()) return;

            std::wstring ws(data.begin(), data.end());
            std::wstring clean;
            clean.reserve(ws.size());
            bool inEsc = false;
            for (wchar_t c : ws)
            {
                if (c == L'\x1b') { inEsc = true; continue; }
                if (inEsc) { if ((c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z')) inEsc = false; continue; }
                clean += c;
            }

            if (clean.find(L"input_tokens") == std::wstring::npos) return;

            auto pos = clean.find(L"\"input_tokens\"");
            if (pos == std::wstring::npos) return;

            auto start = clean.rfind(L'{', pos);
            if (start == std::wstring::npos) return;
            auto end = clean.find(L'}', pos);
            if (end == std::wstring::npos) return;

            std::wstring json = clean.substr(start, end - start + 1);

            auto extractNum = [&](const std::wstring& key) -> uint32_t {
                auto k = json.find(key);
                if (k == std::wstring::npos) return 0;
                k += key.size();
                while (k < json.size() && !iswdigit(json[k])) ++k;
                uint32_t val = 0;
                while (k < json.size() && iswdigit(json[k])) { val = val * 10 + (json[k] - L'0'); ++k; }
                return val;
            };

            uint32_t inputTokens  = extractNum(L"\"input_tokens\":");
            uint32_t cacheRead    = extractNum(L"\"cache_read_input_tokens\":");
            uint32_t outputTokens = extractNum(L"\"output_tokens\":");
            uint32_t used = inputTokens + cacheRead + outputTokens;

            if (used == 0) return;

            if (auto page = weakPage.get())
                page->Sidebar().UpdateContextUsage(hstring{ pid }, used, 200000);
        });

        _aiOutputTokens[projectId] = token;
    }

    // ClickTerminal: Open a terminal tab in the project's folder
    void TerminalPage::_SidebarOpenTerminalRequested(const winrt::Windows::Foundation::IInspectable& /*sender*/,
                                                      const winrt::hstring& projectId)
    {
        auto sidebar = winrt::get_self<implementation::ProjectSidebar>(Sidebar());
        if (!sidebar) return;

        auto& pm = sidebar->ProjectManagerRef();
        auto project = pm.GetProjectById(std::wstring{ projectId });
        if (!project) return;

        auto id = std::wstring{ projectId };

        _CTuxPruneSessions();

        // If we already opened a terminal for this project (tab or layout pane), focus it.
        for (auto& s : _ctuxSessions)
        {
            if (s.projectId != id) continue;
            auto tab = s.tab.get();
            auto pane = s.pane.lock();
            if (!tab || !pane) continue;
            uint32_t idx{};
            if (!_tabs.IndexOf(tab, idx)) continue;
            if (auto tabImpl = _GetTabImpl(tab))
                if (auto pid = pane->Id())
                    tabImpl->FocusPane(pid.value());
            _SelectTab(idx);
            pm.TouchProject(id);
            return;
        }

        // Open new terminal tab
        Microsoft::Terminal::Settings::Model::NewTerminalArgs args;
        args.StartingDirectory(project->FolderPath);

        if (!project->Name.empty())
            args.TabTitle(project->Name);

        if (!project->ColorScheme.empty())
            args.ColorScheme(project->ColorScheme);

        const uint32_t tabsBefore = _tabs.Size();
        _OpenNewTerminalViaDropdown(args);

        if (_tabs.Size() > tabsBefore)
        {
            auto newTab = _tabs.GetAt(_tabs.Size() - 1);
            std::shared_ptr<Pane> pane;
            winrt::Microsoft::Terminal::Control::TermControl ctrl{ nullptr };
            if (auto tabImpl = _GetTabImpl(newTab))
            {
                pane = tabImpl->GetActivePane();
                ctrl = tabImpl->GetActiveTerminalControl();
            }
            if (pane)
                _CTuxRegisterSession(id, L"", false, true, newTab, pane);

            // AutoStartAI: launch the default AI tool in this fresh terminal
            if (project->AIConfig.AutoStartAI && !project->AIConfig.DefaultTool.empty() && ctrl)
            {
                auto launchCmd = _CTuxResolveAICommand(*project);
                if (!launchCmd.empty())
                {
                    _CTuxSendWhenReady(ctrl, hstring{ launchCmd + L"\r" });
                    _CTuxMarkAIRunning(pane, std::wstring{ project->AIConfig.DefaultTool }, true);
                    if (project->AIConfig.DefaultTool == L"claude")
                        _CTuxAttachContextMonitor(id, ctrl);
                    Sidebar().SetSessionActive(projectId, true);
                }
            }
        }

        pm.TouchProject(id);
    }

    // ClickTerminal: Launch AI tool — reuse an idle project terminal (after asking
    // the user), or open a new tab. The event handler keeps its void signature;
    // the actual work runs in the _CTuxStartAIFlow coroutine so we can await a
    // ContentDialog.
    void TerminalPage::_SidebarStartAIRequested(const winrt::Windows::Foundation::IInspectable& /*sender*/,
                                                 const winrt::hstring& projectId)
    {
        _CTuxStartAIFlow(projectId);
    }

    // Always launches with the resolved AI Start Command (user setting first).
    safe_void_coroutine TerminalPage::_CTuxStartAIFlow(winrt::hstring projectId)
    {
        auto sidebar = winrt::get_self<implementation::ProjectSidebar>(Sidebar());
        if (!sidebar) co_return;

        auto& pm = sidebar->ProjectManagerRef();
        auto project = pm.GetProjectById(std::wstring{ projectId });
        if (!project) co_return;

        auto id = std::wstring{ projectId };
        const auto toolName = std::wstring{ project->AIConfig.DefaultTool };
        if (toolName.empty()) co_return;

        _CTuxPruneSessions();

        // If AI is already running for this project, show notice and return
        if (_CTuxProjectHasRunningAI(id))
        {
            _ShowControlNoticeDialog(
                hstring{ L"AI 이미 실행 중" },
                hstring{ L"이미 해당 프로젝트에서 " + toolName + L"가 시작 되었습니다." });
            co_return;
        }

        auto launchCmd = _CTuxResolveAICommand(*project);
        if (launchCmd.empty()) co_return;

        // Copy everything the new-tab path needs BEFORE any co_await — `project`
        // points into the ProjectManager and must not be used across suspension.
        const std::wstring projFolderPath{ project->FolderPath };
        const std::wstring projName{ project->Name };
        const std::wstring projColorScheme{ project->ColorScheme };

        // Is there a sidebar-opened terminal for this project with no AI running?
        bool hasReusable = false;
        for (auto& s : _ctuxSessions)
        {
            if (s.projectId != id || s.aiRunning || !s.openedFromSidebar) continue;
            if (!s.tab.get() || s.pane.expired()) continue;
            hasReusable = true;
            break;
        }

        bool reuseExisting = false;
        if (hasReusable)
        {
            WUX::Controls::ContentDialog dialog;
            dialog.Title(winrt::box_value(hstring{ L"기존 터미널에서 실행" }));
            dialog.Content(winrt::box_value(hstring{ L"이 프로젝트의 터미널 창이 이미 열려 있습니다.\n기존 터미널 창에서 실행하시겠습니까?" }));
            dialog.PrimaryButtonText(L"예");
            dialog.CloseButtonText(L"아니오");
            dialog.DefaultButton(WUX::Controls::ContentDialogButton::Primary);
            dialog.XamlRoot(XamlRoot());

            CTuxLog(L"[CTux] StartAI: reuse prompt shown");

            auto weakThis = get_weak();
            const auto result = co_await dialog.ShowAsync();
            if (!weakThis.get()) co_return;

            reuseExisting = (result == WUX::Controls::ContentDialogResult::Primary);
            CTuxLog(reuseExisting ? L"[CTux] StartAI: user chose existing" : L"[CTux] StartAI: user chose new");
        }

        if (reuseExisting)
        {
            // The session vector may have changed while the dialog was open —
            // re-query instead of holding a pointer across the co_await.
            _CTuxPruneSessions();
            for (auto& s : _ctuxSessions)
            {
                if (s.projectId != id || s.aiRunning || !s.openedFromSidebar) continue;
                auto tab = s.tab.get();
                auto pane = s.pane.lock();
                if (!tab || !pane) continue;
                uint32_t idx{};
                if (!_tabs.IndexOf(tab, idx)) continue;
                auto ctrl = pane->GetTerminalControl();
                if (!ctrl) continue;

                if (auto tabImpl = _GetTabImpl(tab))
                    if (auto paneId = pane->Id())
                        tabImpl->FocusPane(paneId.value());
                _SelectTab(idx);

                _CTuxSendWhenReady(ctrl, hstring{ launchCmd + L"\r" });
                s.aiRunning = true;
                s.toolId = toolName;
                if (toolName == L"claude")
                    _CTuxAttachContextMonitor(id, ctrl);
                Sidebar().SetSessionActive(projectId, true);
                co_return;
            }
            // The reusable session vanished while the dialog was open —
            // fall through to the new-tab path.
            CTuxLog(L"[CTux] StartAI: reusable session gone, opening new tab");
        }

        // No existing terminal (or user chose new) — open a new shell tab and
        // type the command into it
        Microsoft::Terminal::Settings::Model::NewTerminalArgs args;
        args.StartingDirectory(hstring{ projFolderPath });
        args.TabTitle(hstring{ projName + L" [" + toolName + L"]" });
        if (!projColorScheme.empty())
            args.ColorScheme(hstring{ projColorScheme });

        const uint32_t tabsBefore = _tabs.Size();
        _OpenNewTerminalViaDropdown(args);

        if (_tabs.Size() > tabsBefore)
        {
            auto newTab = _tabs.GetAt(_tabs.Size() - 1);
            std::shared_ptr<Pane> pane;
            winrt::Microsoft::Terminal::Control::TermControl ctrl{ nullptr };
            if (auto tabImpl = _GetTabImpl(newTab))
            {
                pane = tabImpl->GetActivePane();
                ctrl = tabImpl->GetActiveTerminalControl();
            }
            if (pane)
                _CTuxRegisterSession(id, toolName, true, true, newTab, pane);
            if (ctrl)
            {
                _CTuxSendWhenReady(ctrl, hstring{ launchCmd + L"\r" });
                if (toolName == L"claude")
                    _CTuxAttachContextMonitor(id, ctrl);
            }
        }

        Sidebar().SetSessionActive(projectId, true);
    }

    // ClickTerminal: Stop AI — send /exit to every running AI pane of the project
    void TerminalPage::_SidebarStopAIRequested(const winrt::Windows::Foundation::IInspectable& /*sender*/,
                                                const winrt::hstring& projectId)
    {
        auto id = std::wstring{ projectId };

        for (auto& s : _ctuxSessions)
        {
            if (!s.aiRunning || s.projectId != id) continue;
            if (auto pane = s.pane.lock())
            {
                if (auto ctrl = pane->GetTerminalControl())
                {
                    ctrl.SendInput(hstring{ L"/exit\r" });
                    CTuxLog(L"[CTux] StopAI: /exit sent");
                }
            }
            s.aiRunning = false;
        }

        // Revoke output monitoring token (connection may already be gone, just erase)
        _aiOutputTokens.erase(id);

        Sidebar().SetSessionActive(projectId, false);
    }

    // ClickTerminal: Propagate CTux theme change to the tab row
    void TerminalPage::_SidebarCTuxThemeChanged(const winrt::Windows::Foundation::IInspectable& /*sender*/,
                                                 const winrt::hstring& /*themeName*/)
    {
        auto ctuxSettings = ClickTerminal::CTuxSettings::Load();
        auto newTheme = ctuxSettings.GetActiveTheme();
        auto parseColor = [](const std::wstring& hex) -> winrt::Windows::UI::Color {
            std::wstring h = hex;
            if (!h.empty() && h[0] == L'#') h = h.substr(1);
            uint32_t v = 0;
            for (auto c : h) { v <<= 4; if (c >= L'0' && c <= L'9') v |= c - L'0'; else if (c >= L'a' && c <= L'f') v |= c - L'a' + 10; else if (c >= L'A' && c <= L'F') v |= c - L'A' + 10; }
            if (h.size() == 6) return { 0xFF, uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v) };
            return { 0xFF, 0x40, 0x40, 0x40 };
        };
        auto tabRowImpl = winrt::get_self<implementation::TabRowControl>(_tabRow);
        auto tabStripBg = parseColor(newTheme.Colors.SidebarBg);
        auto tabItemBg  = parseColor(newTheme.Colors.TabBarBg);
        tabRowImpl->ApplyTheme(tabStripBg, tabItemBg, parseColor(newTheme.Colors.SidebarText));
        // Propagate to GDI NC painter (same fix as startup — see Create block above).
        TitlebarBrush(winrt::Windows::UI::Xaml::Media::SolidColorBrush{ tabStripBg });
        winrt::get_self<implementation::ProjectSidebar>(Sidebar())->RefreshTheme();
    }

    // ClickTerminal: open a new tab hosting the given layout (pane tree + overlay + AI autostart)
    void TerminalPage::_CTuxOpenLayoutTab(const std::wstring& name, uint32_t rows, uint32_t cols,
                                          const std::vector<ClickTerminal::LayoutSlot>& slots,
                                          const std::vector<ClickTerminal::Project>& projects)
    {
        std::vector<std::weak_ptr<Pane>> slotPanes;
        auto paneTree = _BuildPaneTreeFromLayout(rows, cols, slots, projects, &slotPanes);
        if (!paneTree) return;

        auto newTab = _CreateNewTabFromPane(paneTree);
        if (!newTab) return;

        if (auto tabImpl = _GetTabImpl(newTab))
        {
            if (!name.empty())
                tabImpl->SetTabText(hstring{ name });
            if (rows * cols > 1)
                tabImpl->SetLayoutIcon();
        }
        if (rows * cols > 1)
            _BuildPaneInfoOverlay(rows, cols, slots, projects, newTab, slotPanes);
    }

    // ClickTerminal: Sidebar layout apply (▶ button on a saved layout card)
    void TerminalPage::_SidebarApplyLayoutRequested(const winrt::Windows::Foundation::IInspectable& /*sender*/,
                                                     const winrt::hstring& layoutId)
    {
        auto l = _layoutManager->GetLayoutById(std::wstring{ layoutId });
        if (!l) return;

        auto sidebar = winrt::get_self<implementation::ProjectSidebar>(Sidebar());
        if (!sidebar) return;

        auto projects = sidebar->ProjectManagerRef().GetAllProjects();
        _CTuxOpenLayoutTab(l->Name, l->Rows, l->Cols, l->Slots, projects);
        _layoutManager->TouchLayout(l->Id);
        _layoutManager->SaveLayouts();
        Sidebar().RefreshLayouts();
    }

    // ClickTerminal: Sidebar layout settings (⚙ on a layout card — edit THAT layout only)
    void TerminalPage::_SidebarEditLayoutRequested(const winrt::Windows::Foundation::IInspectable& /*sender*/,
                                                    const winrt::hstring& layoutId)
    {
        _ShowLayoutDialog(hstring{ L"edit" }, layoutId);
    }

    // ClickTerminal: + button on the LAYOUTS section header — add a new layout
    void TerminalPage::_SidebarAddLayoutRequested(const winrt::Windows::Foundation::IInspectable& /*sender*/,
                                                   const winrt::hstring& /*unused*/)
    {
        _ShowLayoutDialog(hstring{ L"add" }, hstring{});
    }

    // ClickTerminal: ⚙ button on the LAYOUTS section header — reorder layouts
    void TerminalPage::_SidebarReorderLayoutsRequested(const winrt::Windows::Foundation::IInspectable& /*sender*/,
                                                        const winrt::hstring& /*unused*/)
    {
        _ShowLayoutDialog(hstring{ L"reorder" }, hstring{});
    }

    // ClickTerminal: Sidebar toggle
    void TerminalPage::_TabRowSidebarToggleClicked(const winrt::Windows::Foundation::IInspectable& /*sender*/,
                                                    const winrt::Windows::Foundation::IInspectable& /*args*/)
    {
        _sidebarVisible = !_sidebarVisible;
        WUX::GridLength gl;
        gl.Value = _sidebarVisible ? 250.0 : 0.0;
        gl.GridUnitType = WUX::GridUnitType::Pixel;
        SidebarColumn().Width(gl);
        Sidebar().Visibility(_sidebarVisible ? Visibility::Visible : Visibility::Collapsed);
    }

    safe_void_coroutine TerminalPage::_ShowLayoutDialog(winrt::hstring mode, winrt::hstring editId)
    {
        auto dialog = winrt::make<implementation::LayoutPickerDialog>();

        // Populate project list
        if (auto sidebar = winrt::get_self<implementation::ProjectSidebar>(Sidebar()))
        {
            auto projects = sidebar->ProjectManagerRef().GetAllProjects();
            Json::Value arr(Json::arrayValue);
            auto toNarrow = [](const std::wstring& ws) {
                if (ws.empty()) return std::string{};
                int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, nullptr, 0, nullptr, nullptr);
                std::string r(static_cast<size_t>(len) - 1, '\0');
                WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, r.data(), len, nullptr, nullptr);
                return r;
            };
            for (const auto& p : projects)
            {
                Json::Value o;
                o["id"]   = toNarrow(p.Id);
                o["name"] = toNarrow(p.Name);
                arr.append(o);
            }
            Json::StreamWriterBuilder wb; wb["indentation"] = "";
            dialog.SetProjectList(winrt::to_hstring(Json::writeString(wb, arr)));
        }

        // Populate saved layouts
        {
            auto layouts = _layoutManager->GetAllLayouts();
            Json::Value arr(Json::arrayValue);
            auto toNarrow = [](const std::wstring& ws) {
                if (ws.empty()) return std::string{};
                int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, nullptr, 0, nullptr, nullptr);
                std::string r(static_cast<size_t>(len) - 1, '\0');
                WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, r.data(), len, nullptr, nullptr);
                return r;
            };
            for (const auto& l : layouts)
            {
                Json::Value lj;
                lj["id"]         = toNarrow(l.Id);
                lj["name"]       = toNarrow(l.Name);
                lj["rows"]       = static_cast<int>(l.Rows);
                lj["cols"]       = static_cast<int>(l.Cols);
                Json::Value slots(Json::arrayValue);
                for (const auto& s : l.Slots)
                {
                    Json::Value sj;
                    sj["row"]       = static_cast<int>(s.Row);
                    sj["col"]       = static_cast<int>(s.Col);
                    sj["projectId"] = toNarrow(s.ProjectId);
                    slots.append(sj);
                }
                lj["slots"] = slots;
                arr.append(lj);
            }
            Json::StreamWriterBuilder wb; wb["indentation"] = "";
            dialog.SetSavedLayouts(winrt::to_hstring(Json::writeString(wb, arr)));
        }

        // Apply dialog mode (add / edit / reorder). Empty mode keeps legacy combined view.
        if (!mode.empty())
            dialog.SetMode(mode);
        if (mode == L"edit" && !editId.empty())
            dialog.SetEditTarget(editId);

        dialog.XamlRoot(XamlRoot());
        auto result = co_await dialog.ShowAsync();

        if (result == winrt::Windows::UI::Xaml::Controls::ContentDialogResult::None)
        {
            // Cancelled — but may have deletion or saved-apply actions
        }

        // Handle reorder result (reorder mode only): persist the new order
        if (mode == L"reorder")
        {
            auto idsJson = dialog.ReorderedIdsJson();
            if (!idsJson.empty() && result == winrt::Windows::UI::Xaml::Controls::ContentDialogResult::Primary)
            {
                std::vector<std::wstring> ids;
                try
                {
                    Json::Value root;
                    Json::CharReaderBuilder b;
                    std::string errs;
                    std::istringstream ss(winrt::to_string(idsJson));
                    if (Json::parseFromStream(b, ss, &root, &errs) && root.isArray())
                    {
                        for (const auto& v : root)
                        {
                            auto s = v.asString();
                            if (s.empty()) continue;
                            int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
                            std::wstring w(static_cast<size_t>(len) - 1, L'\0');
                            MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), len);
                            ids.push_back(std::move(w));
                        }
                    }
                }
                catch (...) {}
                if (!ids.empty())
                {
                    _layoutManager->ReorderLayouts(ids);
                    _layoutManager->SaveLayouts();
                    Sidebar().RefreshLayouts();
                }
            }
            co_return;
        }

        // Handle delete (can happen before closing)
        if (!dialog.DeleteId().empty())
        {
            _layoutManager->RemoveLayout(std::wstring{ dialog.DeleteId() });
            _layoutManager->SaveLayouts();
            Sidebar().RefreshLayouts();
        }

        // Handle apply-saved
        if (!dialog.ApplySavedId().empty())
        {
            if (auto l = _layoutManager->GetLayoutById(std::wstring{ dialog.ApplySavedId() }))
            {
                if (auto sidebar = winrt::get_self<implementation::ProjectSidebar>(Sidebar()))
                {
                    auto projects = sidebar->ProjectManagerRef().GetAllProjects();
                    _CTuxOpenLayoutTab(l->Name, l->Rows, l->Cols, l->Slots, projects);
                    _layoutManager->TouchLayout(l->Id);
                    _layoutManager->SaveLayouts();
                    Sidebar().RefreshLayouts();
                }
            }
            co_return;
        }

        // Handle save + apply from editor
        if (dialog.ShouldSave() && !dialog.LayoutJson().empty())
        {
            // Parse the layout JSON and save
            ClickTerminal::Layout layout;
            auto parseJson = [](const winrt::hstring& json) -> ClickTerminal::Layout {
                ClickTerminal::Layout l;
                try
                {
                    Json::Value root;
                    Json::CharReaderBuilder b;
                    std::string errs;
                    std::istringstream ss(winrt::to_string(json));
                    if (!Json::parseFromStream(b, ss, &root, &errs)) return l;
                    auto toWide = [](const std::string& s) {
                        if (s.empty()) return std::wstring{};
                        int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
                        std::wstring r(static_cast<size_t>(len) - 1, L'\0');
                        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, r.data(), len);
                        return r;
                    };
                    l.Name = toWide(root.get("name","").asString());
                    l.Rows = static_cast<uint32_t>(root.get("rows",1).asInt());
                    l.Cols = static_cast<uint32_t>(root.get("cols",1).asInt());
                    for (const auto& sj : root["slots"])
                    {
                        ClickTerminal::LayoutSlot slot;
                        slot.Row       = static_cast<uint32_t>(sj.get("row",0).asInt());
                        slot.Col       = static_cast<uint32_t>(sj.get("col",0).asInt());
                        slot.ProjectId = toWide(sj.get("projectId","").asString());
                        l.Slots.push_back(slot);
                    }
                }
                catch (...) {}
                return l;
            };
            layout = parseJson(dialog.LayoutJson());
            if (!dialog.EditingId().empty())
            {
                layout.Id = std::wstring{ dialog.EditingId() };
                _layoutManager->UpdateLayout(layout);
            }
            else
            {
                _layoutManager->AddLayout(std::move(layout));
            }
            _layoutManager->SaveLayouts();
            Sidebar().RefreshLayouts();
        }

        if (dialog.ShouldApply() && !dialog.LayoutJson().empty())
        {
            _ApplyLayoutJson(dialog.LayoutJson());
        }
    }

    void TerminalPage::_ApplyLayoutJson(const winrt::hstring& layoutJson)
    {
        try
        {
            Json::Value root;
            Json::CharReaderBuilder b;
            std::string errs;
            std::istringstream ss(winrt::to_string(layoutJson));
            if (!Json::parseFromStream(b, ss, &root, &errs)) return;

            auto toWide = [](const std::string& s) {
                if (s.empty()) return std::wstring{};
                int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
                std::wstring r(static_cast<size_t>(len) - 1, L'\0');
                MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, r.data(), len);
                return r;
            };

            auto layoutName = toWide(root.get("name","").asString());
            uint32_t rows = static_cast<uint32_t>(root.get("rows",1).asInt());
            uint32_t cols = static_cast<uint32_t>(root.get("cols",1).asInt());
            rows = std::clamp(rows, 1u, 3u);
            cols = std::clamp(cols, 1u, 3u);

            std::vector<ClickTerminal::LayoutSlot> slots;
            for (const auto& sj : root["slots"])
            {
                ClickTerminal::LayoutSlot slot;
                slot.Row       = static_cast<uint32_t>(sj.get("row",0).asInt());
                slot.Col       = static_cast<uint32_t>(sj.get("col",0).asInt());
                slot.ProjectId = toWide(sj.get("projectId","").asString());
                slots.push_back(slot);
            }

            std::vector<ClickTerminal::Project> projects;
            if (auto sidebar = winrt::get_self<implementation::ProjectSidebar>(Sidebar()))
                projects = sidebar->ProjectManagerRef().GetAllProjects();

            _CTuxOpenLayoutTab(layoutName, rows, cols, slots, projects);
        }
        catch (...) {}
    }

    std::shared_ptr<Pane> TerminalPage::_BuildPaneTreeFromLayout(
        uint32_t rows, uint32_t cols,
        const std::vector<ClickTerminal::LayoutSlot>& slots,
        const std::vector<ClickTerminal::Project>& projects,
        std::vector<std::weak_ptr<Pane>>* slotPanes)
    {
        if (rows == 0 || cols == 0) return nullptr;

        // Build a lookup from (row,col) → projectId
        std::unordered_map<uint32_t, std::wstring> slotMap;
        for (const auto& s : slots)
            slotMap[s.Row * cols + s.Col] = s.ProjectId;

        // Project lookup
        auto findProject = [&](const std::wstring& id) -> const ClickTerminal::Project* {
            for (const auto& p : projects)
                if (p.Id == id) return &p;
            return nullptr;
        };

        // Create all leaf panes
        // panes[r][c] = shared_ptr<Pane>
        std::vector<std::vector<std::shared_ptr<Pane>>> panes(rows,
            std::vector<std::shared_ptr<Pane>>(cols, nullptr));

        for (uint32_t r = 0; r < rows; r++)
        {
            for (uint32_t c = 0; c < cols; c++)
            {
                uint32_t idx = r * cols + c;
                const auto* project = slotMap.count(idx) ? findProject(slotMap.at(idx)) : nullptr;

                Microsoft::Terminal::Settings::Model::NewTerminalArgs args{ nullptr };
                if (project)
                {
                    args = Microsoft::Terminal::Settings::Model::NewTerminalArgs{};
                    if (!project->FolderPath.empty())
                        args.StartingDirectory(winrt::hstring{ project->FolderPath });
                    if (!project->StartupCommand.empty())
                        args.Commandline(winrt::hstring{ project->StartupCommand });
                    if (!project->ColorScheme.empty())
                        args.ColorScheme(winrt::hstring{ project->ColorScheme });
                    args.TabTitle(winrt::hstring{ project->Name });
                }

                panes[r][c] = _MakePane(args, nullptr);
                if (!panes[r][c])
                    panes[r][c] = _MakePane(nullptr, nullptr);
            }
        }

        // Report each slot's leaf pane back to the caller (parallel to `slots`).
        // This replaces the old projectId-keyed map that collapsed duplicate
        // projects onto a single pane (last-write-wins bug).
        if (slotPanes)
        {
            slotPanes->clear();
            slotPanes->reserve(slots.size());
            for (const auto& s : slots)
            {
                if (s.Row < rows && s.Col < cols)
                    slotPanes->push_back(panes[s.Row][s.Col]);
                else
                    slotPanes->push_back({});
            }
        }

        // Column-first tree assembly
        // Step 1: build each column as vertical chain
        // SplitState::Vertical = top/bottom (rows), SplitState::Horizontal = left/right (columns)
        // Correction from code analysis: SplitState::Horizontal = Down (top/bottom rows)
        //                                SplitState::Vertical   = Right (left/right cols)
        std::vector<std::shared_ptr<Pane>> colPanes(cols);
        for (uint32_t c = 0; c < cols; c++)
        {
            colPanes[c] = panes[0][c];
            for (uint32_t r = 1; r < rows; r++)
            {
                // splitPosition = fraction for first child (top part already assembled)
                // Equal rows: r/(r+1)
                float pos = static_cast<float>(r) / static_cast<float>(r + 1);
                colPanes[c] = std::make_shared<Pane>(
                    colPanes[c], panes[r][c],
                    SplitState::Horizontal, // horizontal divider = top/bottom
                    pos);
            }
        }

        // Step 2: combine columns horizontally
        auto root = colPanes[0];
        for (uint32_t c = 1; c < cols; c++)
        {
            // Equal columns: c/(c+1)
            float pos = static_cast<float>(c) / static_cast<float>(c + 1);
            root = std::make_shared<Pane>(
                root, colPanes[c],
                SplitState::Vertical, // vertical divider = left/right
                pos);
        }

        return root;
    }

    // -----------------------------------------------------------------------
    // Per-pane project info overlay
    // -----------------------------------------------------------------------

    // ClickTerminal: overlay bookkeeping — one CTuxTabOverlay per layout tab, so
    // several layout tabs can coexist without clobbering each other's title strip.
    TerminalPage::CTuxTabOverlay* TerminalPage::_CTuxFindOverlay(const winrt::TerminalApp::Tab& tab)
    {
        // prune dead entries first
        _ctuxTabOverlays.erase(
            std::remove_if(_ctuxTabOverlays.begin(), _ctuxTabOverlays.end(),
                           [](const CTuxTabOverlay& o) { return !o.tab.get(); }),
            _ctuxTabOverlays.end());
        for (auto& o : _ctuxTabOverlays)
            if (o.tab.get() == tab)
                return &o;
        return nullptr;
    }

    void TerminalPage::_CTuxRemoveOverlay(const winrt::TerminalApp::Tab& tab)
    {
        _ctuxTabOverlays.erase(
            std::remove_if(_ctuxTabOverlays.begin(), _ctuxTabOverlays.end(),
                           [&](const CTuxTabOverlay& o) {
                               auto t = o.tab.get();
                               return !t || t == tab;
                           }),
            _ctuxTabOverlays.end());
    }

    void TerminalPage::_CTuxHideOverlayStrip()
    {
        auto strip = PaneInfoStrip();
        strip.Children().Clear();
        strip.ColumnDefinitions().Clear();
        strip.Visibility(Visibility::Collapsed);
        PaneInfoStripRow().Height(WUX::GridLength{ 0.0, WUX::GridUnitType::Pixel });
    }

    void TerminalPage::_BuildPaneInfoOverlay(
        uint32_t rows, uint32_t cols,
        const std::vector<ClickTerminal::LayoutSlot>& slots,
        const std::vector<ClickTerminal::Project>& projects,
        winrt::TerminalApp::Tab overlayTab,
        const std::vector<std::weak_ptr<Pane>>& slotPanes)
    {
        // Record per-tab overlay state (replaces any stale entry for this tab only)
        _CTuxRemoveOverlay(overlayTab);
        CTuxTabOverlay ov;
        ov.tab = winrt::make_weak(overlayTab);
        ov.rows = rows;
        ov.cols = cols;
        ov.slots = slots;
        ov.projects = projects;
        ov.slotPanes = slotPanes;
        _ctuxTabOverlays.push_back(std::move(ov));

        auto findProject = [&](const std::wstring& pid) -> const ClickTerminal::Project* {
            for (const auto& p : projects)
                if (p.Id == pid) return &p;
            return nullptr;
        };

        // Register a session record for every slot pane (one per pane, so the
        // same project may legitimately own several panes at once).
        for (size_t i = 0; i < slots.size() && i < slotPanes.size(); ++i)
        {
            if (slots[i].ProjectId.empty()) continue;
            if (auto pane = slotPanes[i].lock())
                _CTuxRegisterSession(slots[i].ProjectId, L"", false, true, overlayTab, pane);
        }

        _CTuxRepositionOverlay(_ctuxTabOverlays.back());
        PaneInfoStrip().Visibility(Visibility::Visible);
        PaneInfoStripRow().Height(WUX::GridLength{ 1.0, WUX::GridUnitType::Auto });

        // Auto-start AI in EVERY slot pane whose project enables AutoStartAI.
        // _CTuxSendWhenReady gates each send on the pane's connection readiness,
        // so no fragile one-shot Low-priority dispatch is needed anymore.
        for (size_t i = 0; i < slots.size() && i < slotPanes.size(); ++i)
        {
            const auto& slot = slots[i];
            if (slot.ProjectId.empty()) continue;

            const auto* proj = findProject(slot.ProjectId);
            if (!proj || !proj->AIConfig.AutoStartAI || proj->AIConfig.DefaultTool.empty()) continue;

            auto pane = slotPanes[i].lock();
            if (!pane) continue;
            auto ctrl = pane->GetTerminalControl();
            if (!ctrl) continue;

            auto launchCmd = _CTuxResolveAICommand(*proj);
            if (launchCmd.empty()) continue;

            CTuxLog(L"[CTux] Layout AutoStartAI slot=" + std::to_wstring(i) + L" cmd=" + launchCmd);
            _CTuxSendWhenReady(ctrl, hstring{ launchCmd + L"\r" });
            _CTuxMarkAIRunning(pane, std::wstring{ proj->AIConfig.DefaultTool }, true);
            if (proj->AIConfig.DefaultTool == L"claude")
                _CTuxAttachContextMonitor(slot.ProjectId, ctrl);
            Sidebar().SetSessionActive(hstring{ slot.ProjectId }, true);
        }
    }

    void TerminalPage::_CTuxRepositionOverlay(const CTuxTabOverlay& ov)
    {
        if (ov.cols == 0) return;

        auto strip = PaneInfoStrip();
        strip.Children().Clear();
        strip.ColumnDefinitions().Clear();

        auto findProject = [&](const std::wstring& id) -> const ClickTerminal::Project* {
            for (const auto& p : ov.projects)
                if (p.Id == id) return &p;
            return nullptr;
        };

        WUX::Media::SolidColorBrush bgBrush;
        { winrt::Windows::UI::Color c{ 255, 28, 28, 38 }; bgBrush.Color(c); }
        WUX::Media::SolidColorBrush whiteBrush;
        { winrt::Windows::UI::Color c{ 255, 255, 255, 255 }; whiteBrush.Color(c); }
        WUX::Media::SolidColorBrush grayBrush;
        { winrt::Windows::UI::Color c{ 255, 180, 180, 195 }; grayBrush.Color(c); }
        WUX::Media::SolidColorBrush accentBrush;
        { winrt::Windows::UI::Color c{ 255, 80, 160, 220 }; accentBrush.Color(c); }

        // Equal-width column per terminal column
        for (uint32_t c = 0; c < ov.cols; c++)
        {
            WUX::Controls::ColumnDefinition cd;
            cd.Width({ 1.0, WUX::GridUnitType::Star });
            strip.ColumnDefinitions().Append(cd);
        }

        for (uint32_t c = 0; c < ov.cols; c++)
        {
            // Pick the first slot in this column
            std::wstring projectId;
            for (const auto& slot : ov.slots)
                if (slot.Col == c) { projectId = slot.ProjectId; break; }

            const ClickTerminal::Project* proj = findProject(projectId);

            WUX::Controls::Border card;
            card.Background(bgBrush);
            Thickness pad{ 10.0, 5.0, 10.0, 6.0 };
            card.Padding(pad);
            card.HorizontalAlignment(HorizontalAlignment::Stretch);
            card.VerticalAlignment(VerticalAlignment::Stretch);

            WUX::Controls::StackPanel sp;
            sp.Orientation(WUX::Controls::Orientation::Vertical);
            sp.Spacing(4.0);

            // Header: [Name + type badge] | [url icons...] | [gear button]
            WUX::Controls::Grid headerGrid;
            {
                WUX::Controls::ColumnDefinition hc0;
                hc0.Width({ 1.0, WUX::GridUnitType::Star });
                headerGrid.ColumnDefinitions().Append(hc0);
                // URL icon cols + gear col added dynamically below
            }

            WUX::Controls::StackPanel nameRow;
            nameRow.Orientation(WUX::Controls::Orientation::Horizontal);
            nameRow.Spacing(6.0);
            nameRow.VerticalAlignment(VerticalAlignment::Center);

            WUX::Controls::TextBlock nameBlock;
            nameBlock.Text(proj ? hstring{ proj->Name } : hstring{ L"Terminal" });
            nameBlock.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
            nameBlock.FontSize(14.0);
            nameBlock.Foreground(whiteBrush);
            nameBlock.TextTrimming(WUX::TextTrimming::CharacterEllipsis);
            nameRow.Children().Append(nameBlock);

            if (proj)
            {
                const wchar_t* typeLabel = L"web";
                if (proj->Type == ClickTerminal::ProjectType::App)            typeLabel = L"app";
                else if (proj->Type == ClickTerminal::ProjectType::AIWorkflow) typeLabel = L"cowork";

                WUX::Controls::TextBlock typeBadge;
                typeBadge.Text(typeLabel);
                typeBadge.FontSize(9.0);
                typeBadge.Opacity(0.6);
                typeBadge.VerticalAlignment(VerticalAlignment::Bottom);
                typeBadge.Foreground(grayBrush);
                nameRow.Children().Append(typeBadge);

                // Folder path — right of the name/badge. nameRow is a horizontal
                // StackPanel, so CharacterEllipsis alone can't clip; shorten long
                // paths head-first and expose the full path via tooltip.
                if (!proj->FolderPath.empty())
                {
                    const std::wstring fullPath{ proj->FolderPath };
                    std::wstring shownPath = fullPath;
                    if (shownPath.size() > 40)
                        shownPath = L"..." + shownPath.substr(shownPath.size() - 37);

                    WUX::Controls::TextBlock pathBlock;
                    pathBlock.Text(hstring{ shownPath });
                    pathBlock.FontSize(10.0);
                    pathBlock.Opacity(0.55);
                    pathBlock.Foreground(grayBrush);
                    pathBlock.VerticalAlignment(VerticalAlignment::Bottom);
                    pathBlock.TextTrimming(WUX::TextTrimming::CharacterEllipsis);
                    WUX::Controls::ToolTipService::SetToolTip(pathBlock, winrt::box_value(hstring{ fullPath }));
                    nameRow.Children().Append(pathBlock);
                }
            }
            WUX::Controls::Grid::SetColumn(nameRow, 0);
            headerGrid.Children().Append(nameRow);

            if (proj)
            {
                // URL icon buttons (Dev=\xE71B link, Deploy=\xE774 globe) — between name and gear
                auto addUrlIcon = [&](const wchar_t* glyph, const std::wstring& urlStr) {
                    WUX::Controls::ColumnDefinition urlCd;
                    urlCd.Width({ 0.0, WUX::GridUnitType::Auto });
                    headerGrid.ColumnDefinitions().Append(urlCd);
                    int urlCol = static_cast<int>(headerGrid.ColumnDefinitions().Size()) - 1;

                    WUX::Controls::Button ubtn;
                    WUX::Controls::FontIcon uicon;
                    uicon.FontFamily(WUX::Media::FontFamily{ L"Segoe MDL2 Assets" });
                    uicon.Glyph(glyph);
                    uicon.FontSize(12.0);
                    uicon.Foreground(accentBrush);
                    ubtn.Content(uicon);
                    ubtn.Padding({ 4.0, 2.0, 0.0, 2.0 });
                    ubtn.BorderThickness({ 0.0, 0.0, 0.0, 0.0 });
                    WUX::Media::SolidColorBrush ubg;
                    { winrt::Windows::UI::Color tc{ 0, 0, 0, 0 }; ubg.Color(tc); }
                    ubtn.Background(ubg);
                    ubtn.VerticalAlignment(VerticalAlignment::Center);
                    WUX::Controls::ToolTipService::SetToolTip(ubtn, winrt::box_value(hstring{ urlStr }));
                    auto u = urlStr;
                    ubtn.Click([u](const IInspectable&, const WUX::RoutedEventArgs&) {
                        ShellExecuteW(nullptr, L"open", u.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    });
                    WUX::Controls::Grid::SetColumn(ubtn, urlCol);
                    headerGrid.Children().Append(ubtn);
                };

                if (!proj->DevUrl.empty())    addUrlIcon(L"\xE71B", proj->DevUrl);
                if (!proj->DeployUrl.empty()) addUrlIcon(L"\xE774", proj->DeployUrl);

                // Gear button — always rightmost
                {
                    WUX::Controls::ColumnDefinition gearCd;
                    gearCd.Width({ 0.0, WUX::GridUnitType::Auto });
                    headerGrid.ColumnDefinitions().Append(gearCd);
                    int gearCol = static_cast<int>(headerGrid.ColumnDefinitions().Size()) - 1;

                    WUX::Controls::Button gearBtn;
                    WUX::Controls::FontIcon gearIcon;
                    gearIcon.FontFamily(WUX::Media::FontFamily{ L"Segoe MDL2 Assets" });
                    gearIcon.Glyph(L"\xE713");
                    gearIcon.FontSize(13.0);
                    gearIcon.Foreground(grayBrush);
                    gearBtn.Content(gearIcon);
                    gearBtn.Padding({ 4.0, 2.0, 0.0, 2.0 });
                    gearBtn.BorderThickness({ 0.0, 0.0, 0.0, 0.0 });
                    WUX::Media::SolidColorBrush transparentBrush;
                    { winrt::Windows::UI::Color tc{ 0, 0, 0, 0 }; transparentBrush.Color(tc); }
                    gearBtn.Background(transparentBrush);
                    gearBtn.VerticalAlignment(VerticalAlignment::Center);
                    {
                        auto weakThis2{ get_weak() };
                        auto pid = projectId;
                        gearBtn.Click([weakThis2, pid](const IInspectable&, const WUX::RoutedEventArgs&) {
                            if (auto page{ weakThis2.get() })
                                if (auto sb = winrt::get_self<implementation::ProjectSidebar>(page->Sidebar()))
                                    sb->ShowEditProject(winrt::hstring{ pid });
                        });
                    }
                    WUX::Controls::Grid::SetColumn(gearBtn, gearCol);
                    headerGrid.Children().Append(gearBtn);
                }
            }

            sp.Children().Append(headerGrid);

            if (proj && (!proj->Ports.empty() || !proj->Urls.empty()))
            {
                WUX::Controls::StackPanel portsRow;
                portsRow.Orientation(WUX::Controls::Orientation::Horizontal);
                portsRow.Spacing(6.0);

                for (auto port : proj->Ports)
                {
                    WUX::Controls::HyperlinkButton portLink;
                    portLink.NavigateUri(winrt::Windows::Foundation::Uri{ hstring{ L"http://localhost:" + std::to_wstring(port) } });
                    portLink.Padding({ 0, 0, 0, 0 });
                    WUX::Controls::TextBlock portText;
                    portText.Text(hstring{ L":" + std::to_wstring(port) });
                    portText.FontSize(10.0);
                    portText.Foreground(accentBrush);
                    portLink.Content(portText);
                    portsRow.Children().Append(portLink);
                }
                for (const auto& url : proj->Urls)
                {
                    WUX::Controls::HyperlinkButton urlLink;
                    urlLink.NavigateUri(winrt::Windows::Foundation::Uri{ hstring{ url.Url } });
                    urlLink.Padding({ 0, 0, 0, 0 });
                    WUX::Controls::TextBlock urlText;
                    urlText.Text(hstring{ url.Label });
                    urlText.FontSize(10.0);
                    urlText.Foreground(accentBrush);
                    urlLink.Content(urlText);
                    portsRow.Children().Append(urlLink);
                }

                sp.Children().Append(portsRow);
            }

            card.Child(sp);
            WUX::Controls::Grid::SetColumn(card, static_cast<int32_t>(c));
            strip.Children().Append(card);
        }
    }
}
