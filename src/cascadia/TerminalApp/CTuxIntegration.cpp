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
#include "TerminalPaneContent.h"
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
    // CTux hex color helper — "#RRGGBB" → WinRT Color (fallback: dark gray)
    // ------------------------------------------------------------------------
    static winrt::Windows::UI::Color CTuxParseHex(const std::wstring& hex)
    {
        const auto v = ClickTerminal::ParseThemeHex(hex);
        return { uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v) };
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
            {
                _CTuxRegisterSession(id, L"", false, true, newTab, pane);

                // Title strip: single-project tabs get a 1x1 overlay card too
                std::vector<ClickTerminal::LayoutSlot> slots{ ClickTerminal::LayoutSlot{ 0, 0, id } };
                std::vector<ClickTerminal::Project> projList{ *project };
                std::vector<std::weak_ptr<Pane>> slotPanes{ std::weak_ptr<Pane>{ pane } };
                _CTuxSetTabOverlay(1, 1, slots, projList, newTab, slotPanes);
            }

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
        auto tabRowImpl = winrt::get_self<implementation::TabRowControl>(_tabRow);
        auto tabStripBg = CTuxParseHex(newTheme.Colors.SidebarBg);
        auto tabItemBg  = CTuxParseHex(newTheme.Colors.TabBarBg);
        tabRowImpl->ApplyTheme(tabStripBg, tabItemBg, CTuxParseHex(newTheme.Colors.SidebarText));
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

    // ClickTerminal: build NewTerminalArgs from a project's settings (nullptr → default terminal)
    Microsoft::Terminal::Settings::Model::NewTerminalArgs TerminalPage::_CTuxArgsForProject(const ClickTerminal::Project* project) const
    {
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
        return args;
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

                auto args = _CTuxArgsForProject(project);

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

    // ClickTerminal: record (or replace) a tab's overlay state and show the strip.
    // Pure bookkeeping + UI — no session registration, no AI autostart, so it is
    // safe to call from every tab-creation path. Nicknames survive a re-record by
    // matching slot panes against the tab's previous overlay entry.
    void TerminalPage::_CTuxSetTabOverlay(
        uint32_t rows, uint32_t cols,
        const std::vector<ClickTerminal::LayoutSlot>& slots,
        const std::vector<ClickTerminal::Project>& projects,
        const winrt::TerminalApp::Tab& tab,
        const std::vector<std::weak_ptr<Pane>>& slotPanes)
    {
        // Carry nicknames over from the previous overlay of this tab (pane match)
        std::vector<std::wstring> nicknames(slots.size());
        if (auto old = _CTuxFindOverlay(tab))
        {
            for (size_t i = 0; i < slotPanes.size() && i < nicknames.size(); ++i)
            {
                auto p = slotPanes[i].lock();
                if (!p) continue;
                for (size_t j = 0; j < old->slotPanes.size() && j < old->nicknames.size(); ++j)
                {
                    if (old->slotPanes[j].lock() == p)
                    {
                        nicknames[i] = old->nicknames[j];
                        break;
                    }
                }
            }
        }

        _CTuxRemoveOverlay(tab);
        CTuxTabOverlay ov;
        ov.tab = winrt::make_weak(tab);
        ov.rows = rows;
        ov.cols = cols;
        ov.slots = slots;
        ov.projects = projects;
        ov.slotPanes = slotPanes;
        ov.nicknames = std::move(nicknames);
        _ctuxTabOverlays.push_back(std::move(ov));

        // Inject the pane headers only after the terminals have laid out and
        // started their connections. Adding the header row while a pane is still
        // sizing can leave the TermControl with 0 usable rows, which makes
        // ConptyCreatePseudoConsole fail with E_INVALIDARG (0x80070057) and the
        // pane shows "[error ... when launching pwsh.exe]".
        auto weakThis{ get_weak() };
        auto weakTab = winrt::make_weak(tab);
        Dispatcher().RunAsync(CoreDispatcherPriority::Low, [weakThis, weakTab]() {
            if (auto page{ weakThis.get() })
                if (auto t = weakTab.get())
                    if (auto o = page->_CTuxFindOverlay(t))
                        page->_CTuxRepositionOverlay(*o);
        });
    }

    // ClickTerminal: auto-start AI in the given slot panes (indices into ov.slots)
    // whose project enables AutoStartAI. _CTuxSendWhenReady gates each send on the
    // pane's connection readiness.
    void TerminalPage::_CTuxStartSlotsAI(const CTuxTabOverlay& ov, const std::vector<size_t>& slotIndices)
    {
        auto findProject = [&](const std::wstring& pid) -> const ClickTerminal::Project* {
            for (const auto& p : ov.projects)
                if (p.Id == pid) return &p;
            return nullptr;
        };

        for (auto i : slotIndices)
        {
            if (i >= ov.slots.size() || i >= ov.slotPanes.size()) continue;
            const auto& slot = ov.slots[i];
            if (slot.ProjectId.empty()) continue;

            const auto* proj = findProject(slot.ProjectId);
            if (!proj || !proj->AIConfig.AutoStartAI || proj->AIConfig.DefaultTool.empty()) continue;

            auto pane = ov.slotPanes[i].lock();
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

    void TerminalPage::_BuildPaneInfoOverlay(
        uint32_t rows, uint32_t cols,
        const std::vector<ClickTerminal::LayoutSlot>& slots,
        const std::vector<ClickTerminal::Project>& projects,
        winrt::TerminalApp::Tab overlayTab,
        const std::vector<std::weak_ptr<Pane>>& slotPanes)
    {
        _CTuxSetTabOverlay(rows, cols, slots, projects, overlayTab, slotPanes);

        // Register a session record for every slot pane (one per pane, so the
        // same project may legitimately own several panes at once).
        for (size_t i = 0; i < slots.size() && i < slotPanes.size(); ++i)
        {
            if (slots[i].ProjectId.empty()) continue;
            if (auto pane = slotPanes[i].lock())
                _CTuxRegisterSession(slots[i].ProjectId, L"", false, true, overlayTab, pane);
        }

        if (auto ov = _CTuxFindOverlay(overlayTab))
        {
            std::vector<size_t> all(ov->slots.size());
            for (size_t i = 0; i < all.size(); ++i)
                all[i] = i;
            _CTuxStartSlotsAI(*ov, all);
        }
    }

    void TerminalPage::_CTuxRepositionOverlay(const CTuxTabOverlay& ov)
    {
        if (ov.cols == 0) return;

        // Titles render inside each pane now (one per pane, so lower rows get one
        // too). The legacy single-row strip above the content stays hidden.
        _CTuxHideOverlayStrip();

        auto findProject = [&](const std::wstring& id) -> const ClickTerminal::Project* {
            for (const auto& p : ov.projects)
                if (p.Id == id) return &p;
            return nullptr;
        };

        // CTux theme colors for the pane header strip (was hardcoded dark values)
        const auto stripTheme = ClickTerminal::CTuxSettings::Load().GetActiveTheme();
        WUX::Media::SolidColorBrush bgBrush;
        bgBrush.Color(CTuxParseHex(stripTheme.Colors.PaneHeaderBg));
        WUX::Media::SolidColorBrush whiteBrush;
        whiteBrush.Color(CTuxParseHex(stripTheme.Colors.PaneHeaderText));
        WUX::Media::SolidColorBrush grayBrush; // muted text: PaneHeaderText at 70%
        grayBrush.Color(CTuxParseHex(stripTheme.Colors.PaneHeaderText));
        grayBrush.Opacity(0.7);
        WUX::Media::SolidColorBrush accentBrush;
        accentBrush.Color(CTuxParseHex(stripTheme.Colors.PaneHeaderAccent));

        // URL text shortening for the right-hand link rows
        auto shortenUrl = [](const std::wstring& u) {
            std::wstring s = u;
            if (s.rfind(L"https://", 0) == 0) s = s.substr(8);
            else if (s.rfind(L"http://", 0) == 0) s = s.substr(7);
            if (s.size() > 30) s = s.substr(0, 27) + L"...";
            return s;
        };

        // One title card per pane
        for (size_t slotIdx = 0; slotIdx < ov.slots.size() && slotIdx < ov.slotPanes.size(); ++slotIdx)
        {
            auto slotPane = ov.slotPanes[slotIdx].lock();
            if (!slotPane)
                continue; // that pane was closed

            // Only terminal panes can host a header
            auto paneContent = slotPane->GetContent();
            if (!paneContent)
                continue;
            auto termContent = paneContent.try_as<winrt::TerminalApp::TerminalPaneContent>();
            if (!termContent)
                continue;
            auto contentImpl = winrt::get_self<implementation::TerminalPaneContent>(termContent);
            if (!contentImpl)
                continue;

            const std::wstring projectId = ov.slots[slotIdx].ProjectId;
            const ClickTerminal::Project* proj = findProject(projectId);

            WUX::Controls::Border card;
            card.Background(bgBrush);
            Thickness pad{ 8.0, 4.0, 8.0, 5.0 };
            card.Padding(pad);
            card.HorizontalAlignment(HorizontalAlignment::Stretch);

            // Card layout: [left: title/nickname/ports (*)] | [right: url links + gear/split (Auto)]
            WUX::Controls::Grid outerGrid;
            {
                WUX::Controls::ColumnDefinition oc0;
                oc0.Width({ 1.0, WUX::GridUnitType::Star });
                outerGrid.ColumnDefinitions().Append(oc0);
                WUX::Controls::ColumnDefinition oc1;
                oc1.Width({ 0.0, WUX::GridUnitType::Auto });
                outerGrid.ColumnDefinitions().Append(oc1);
            }

            WUX::Controls::StackPanel sp;
            sp.Orientation(WUX::Controls::Orientation::Vertical);
            sp.Spacing(2.0);

            // Row 1: title + type badge (folder path moved into the title tooltip)
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
            if (proj && !proj->FolderPath.empty())
                WUX::Controls::ToolTipService::SetToolTip(nameBlock, winrt::box_value(hstring{ proj->FolderPath }));
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
            }
            sp.Children().Append(nameRow);

            // Row 2: nickname (session-only alias) + inline editor
            {
                const std::wstring nickname =
                    (slotIdx < ov.nicknames.size()) ? ov.nicknames[slotIdx] : std::wstring{};

                WUX::Controls::StackPanel nickRow;
                nickRow.Orientation(WUX::Controls::Orientation::Horizontal);
                nickRow.Spacing(4.0);
                nickRow.VerticalAlignment(VerticalAlignment::Center);

                WUX::Controls::TextBlock nickBlock;
                nickBlock.Text(nickname.empty() ? hstring{ L"별명 추가" } : hstring{ nickname });
                nickBlock.FontSize(11.0);
                nickBlock.Foreground(grayBrush);
                nickBlock.Opacity(nickname.empty() ? 0.5 : 0.9);
                nickBlock.VerticalAlignment(VerticalAlignment::Center);
                nickBlock.TextTrimming(WUX::TextTrimming::CharacterEllipsis);

                WUX::Controls::Button editBtn;
                WUX::Controls::FontIcon editIcon;
                editIcon.FontFamily(WUX::Media::FontFamily{ L"Segoe MDL2 Assets" });
                editIcon.Glyph(L"\xE70F");
                editIcon.FontSize(10.0);
                editIcon.Foreground(grayBrush);
                editBtn.Content(editIcon);
                editBtn.Padding({ 2.0, 1.0, 2.0, 1.0 });
                editBtn.BorderThickness({ 0.0, 0.0, 0.0, 0.0 });
                WUX::Media::SolidColorBrush ebg;
                { winrt::Windows::UI::Color tc{ 0, 0, 0, 0 }; ebg.Color(tc); }
                editBtn.Background(ebg);
                editBtn.VerticalAlignment(VerticalAlignment::Center);
                WUX::Controls::ToolTipService::SetToolTip(editBtn, winrt::box_value(hstring{ L"별명 수정" }));

                WUX::Controls::TextBox nickBox;
                nickBox.Text(hstring{ nickname });
                nickBox.FontSize(11.0);
                nickBox.MinWidth(120.0);
                nickBox.Padding({ 4.0, 2.0, 4.0, 2.0 });
                nickBox.Visibility(Visibility::Collapsed);

                auto weakPage{ get_weak() };
                auto weakTab = ov.tab;
                const size_t si = slotIdx;

                // Commit: write into the overlay model + update UI in place (no full
                // strip rebuild — rebuilding from inside the TextBox's own event
                // handlers would destroy the sender mid-dispatch).
                auto commitFn = [weakPage, weakTab, si, nickBlock, nickBox, editBtn]() {
                    std::wstring text{ nickBox.Text() };
                    if (auto page = weakPage.get())
                        if (auto tab = weakTab.get())
                            if (auto o = page->_CTuxFindOverlay(tab))
                                if (si < o->nicknames.size())
                                    o->nicknames[si] = text;
                    nickBlock.Text(text.empty() ? hstring{ L"별명 추가" } : hstring{ text });
                    nickBlock.Opacity(text.empty() ? 0.5 : 0.9);
                    nickBox.Visibility(Visibility::Collapsed);
                    nickBlock.Visibility(Visibility::Visible);
                    editBtn.Visibility(Visibility::Visible);
                };

                editBtn.Click([nickBlock, nickBox, editBtn](const IInspectable&, const WUX::RoutedEventArgs&) {
                    nickBlock.Visibility(Visibility::Collapsed);
                    editBtn.Visibility(Visibility::Collapsed);
                    nickBox.Visibility(Visibility::Visible);
                    nickBox.Focus(FocusState::Programmatic);
                    nickBox.SelectAll();
                });

                // Xaml Islands: WM_CHAR follows the Win32 HWND focus (terminal), so
                // English/digit input never reaches a XAML TextBox natively. Insert
                // characters ourselves from KeyDown via ToUnicode (same pattern as
                // AddProjectDialog). Korean TSF input (VK_PROCESSKEY) passes through.
                nickBox.KeyDown([commitFn, nickBox, nickBlock, editBtn, weakPage, weakTab, si](
                                    const IInspectable&, const WUX::Input::KeyRoutedEventArgs& e) {
                    const auto key = e.OriginalKey();
                    if (key == winrt::Windows::System::VirtualKey::Enter)
                    {
                        e.Handled(true);
                        commitFn();
                        return;
                    }
                    if (key == winrt::Windows::System::VirtualKey::Escape)
                    {
                        e.Handled(true);
                        std::wstring cur;
                        if (auto page = weakPage.get())
                            if (auto tab = weakTab.get())
                                if (auto o = page->_CTuxFindOverlay(tab))
                                    if (si < o->nicknames.size())
                                        cur = o->nicknames[si];
                        nickBox.Text(hstring{ cur });
                        nickBox.Visibility(Visibility::Collapsed);
                        nickBlock.Visibility(Visibility::Visible);
                        editBtn.Visibility(Visibility::Visible);
                        return;
                    }

                    auto vk = static_cast<UINT>(key);
                    if (vk == 0xE5) return; // VK_PROCESSKEY: Korean TSF — leave alone
                    if ((::GetKeyState(VK_CONTROL) & 0x8000) != 0) return;
                    if ((::GetKeyState(VK_MENU) & 0x8000) != 0) return;

                    BYTE ks[256];
                    ::GetKeyboardState(ks);
                    WCHAR ch[4] = {};
                    if (::ToUnicode(vk, ::MapVirtualKey(vk, MAPVK_VK_TO_VSC), ks, ch, 4, 0) != 1 || ch[0] < L' ')
                        return; // non-printing keys (BackSpace etc.) → TextBox default handling

                    auto sel = nickBox.SelectionStart();
                    auto len = nickBox.SelectionLength();
                    std::wstring text{ nickBox.Text() };
                    if (len > 0) text.erase(sel, len);
                    text.insert(sel, 1, ch[0]);
                    nickBox.Text(hstring{ text });
                    nickBox.SelectionStart(sel + 1);
                    nickBox.SelectionLength(0);
                    e.Handled(true);
                });

                nickBox.LostFocus([commitFn, nickBox](const IInspectable&, const WUX::RoutedEventArgs&) {
                    if (nickBox.Visibility() == Visibility::Visible)
                        commitFn();
                });

                nickRow.Children().Append(nickBlock);
                nickRow.Children().Append(editBtn);
                nickRow.Children().Append(nickBox);
                sp.Children().Append(nickRow);
            }

            // Row 3: ports / named URLs (unchanged)
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

            WUX::Controls::Grid::SetColumn(sp, 0);
            outerGrid.Children().Append(sp);

            // Right side: [Dev URL | gear] / [Deploy URL | split] — 2x2 grid
            WUX::Controls::Grid rightGrid;
            {
                WUX::Controls::RowDefinition rr0;
                rr0.Height({ 0.0, WUX::GridUnitType::Auto });
                rightGrid.RowDefinitions().Append(rr0);
                WUX::Controls::RowDefinition rr1;
                rr1.Height({ 0.0, WUX::GridUnitType::Auto });
                rightGrid.RowDefinitions().Append(rr1);
                WUX::Controls::ColumnDefinition rc0;
                rc0.Width({ 0.0, WUX::GridUnitType::Auto });
                rightGrid.ColumnDefinitions().Append(rc0);
                WUX::Controls::ColumnDefinition rc1;
                rc1.Width({ 0.0, WUX::GridUnitType::Auto });
                rightGrid.ColumnDefinitions().Append(rc1);
            }
            rightGrid.VerticalAlignment(VerticalAlignment::Center);

            // URL rows: icon + shortened URL text, click opens the browser
            auto addUrlLink = [&](int row, const wchar_t* glyph, const std::wstring& urlStr) {
                WUX::Controls::Button ubtn;
                WUX::Controls::StackPanel content;
                content.Orientation(WUX::Controls::Orientation::Horizontal);
                content.Spacing(4.0);
                WUX::Controls::FontIcon uicon;
                uicon.FontFamily(WUX::Media::FontFamily{ L"Segoe MDL2 Assets" });
                uicon.Glyph(glyph);
                uicon.FontSize(11.0);
                uicon.Foreground(accentBrush);
                content.Children().Append(uicon);
                WUX::Controls::TextBlock utext;
                utext.Text(hstring{ shortenUrl(urlStr) });
                utext.FontSize(10.0);
                utext.Foreground(accentBrush);
                utext.VerticalAlignment(VerticalAlignment::Center);
                content.Children().Append(utext);
                ubtn.Content(content);
                ubtn.Padding({ 4.0, 1.0, 4.0, 1.0 });
                ubtn.BorderThickness({ 0.0, 0.0, 0.0, 0.0 });
                WUX::Media::SolidColorBrush ubg;
                { winrt::Windows::UI::Color tc{ 0, 0, 0, 0 }; ubg.Color(tc); }
                ubtn.Background(ubg);
                ubtn.HorizontalAlignment(HorizontalAlignment::Right);
                WUX::Controls::ToolTipService::SetToolTip(ubtn, winrt::box_value(hstring{ urlStr }));
                auto u = urlStr;
                ubtn.Click([u](const IInspectable&, const WUX::RoutedEventArgs&) {
                    ShellExecuteW(nullptr, L"open", u.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                });
                WUX::Controls::Grid::SetRow(ubtn, row);
                WUX::Controls::Grid::SetColumn(ubtn, 0);
                rightGrid.Children().Append(ubtn);
            };

            if (proj && !proj->DevUrl.empty())    addUrlLink(0, L"\xE71B", proj->DevUrl);
            if (proj && !proj->DeployUrl.empty()) addUrlLink(1, L"\xE774", proj->DeployUrl);

            // Small icon button factory for the rightmost column
            auto makeIconBtn = [&](const wchar_t* glyph, const wchar_t* tooltip) {
                WUX::Controls::Button btn;
                WUX::Controls::FontIcon icon;
                icon.FontFamily(WUX::Media::FontFamily{ L"Segoe MDL2 Assets" });
                icon.Glyph(glyph);
                icon.FontSize(13.0);
                icon.Foreground(grayBrush);
                btn.Content(icon);
                btn.Padding({ 4.0, 2.0, 0.0, 2.0 });
                btn.BorderThickness({ 0.0, 0.0, 0.0, 0.0 });
                WUX::Media::SolidColorBrush tbg;
                { winrt::Windows::UI::Color tc{ 0, 0, 0, 0 }; tbg.Color(tc); }
                btn.Background(tbg);
                btn.VerticalAlignment(VerticalAlignment::Center);
                WUX::Controls::ToolTipService::SetToolTip(btn, winrt::box_value(hstring{ tooltip }));
                return btn;
            };

            if (proj)
            {
                // Gear button — project settings
                auto gearBtn = makeIconBtn(L"\xE713", L"프로젝트 설정");
                {
                    auto weakThis2{ get_weak() };
                    auto pid = projectId;
                    gearBtn.Click([weakThis2, pid](const IInspectable&, const WUX::RoutedEventArgs&) {
                        if (auto page{ weakThis2.get() })
                            if (auto sb = winrt::get_self<implementation::ProjectSidebar>(page->Sidebar()))
                                sb->ShowEditProject(winrt::hstring{ pid });
                    });
                }
                WUX::Controls::Grid::SetRow(gearBtn, 0);
                WUX::Controls::Grid::SetColumn(gearBtn, 1);
                rightGrid.Children().Append(gearBtn);
            }

            // Split (layout-extend) button — every card, project or not
            {
                auto splitBtn = makeIconBtn(L"\xF0E2", L"창 분할");
                auto weakThis3{ get_weak() };
                auto weakTab2 = ov.tab;
                splitBtn.Click([weakThis3, weakTab2](const IInspectable&, const WUX::RoutedEventArgs&) {
                    if (auto page{ weakThis3.get() })
                        if (auto tab = weakTab2.get())
                            page->_CTuxShowExtendDialog(tab);
                });
                WUX::Controls::Grid::SetRow(splitBtn, 1);
                WUX::Controls::Grid::SetColumn(splitBtn, 1);
                rightGrid.Children().Append(splitBtn);
            }

            WUX::Controls::Grid::SetColumn(rightGrid, 1);
            outerGrid.Children().Append(rightGrid);

            card.Child(outerGrid);
            contentImpl->CTuxSetHeader(card);
        }
    }

    // ClickTerminal: lazily create a 1x1 overlay for tabs that never went through
    // a CTux open path (e.g. the plain + button). Called on every tab selection.
    void TerminalPage::_CTuxEnsureOverlayForTab(const winrt::TerminalApp::Tab& tab)
    {
        if (_CTuxFindOverlay(tab))
            return;
        auto tabImpl = _GetTabImpl(tab);
        if (!tabImpl)
            return; // settings tab etc. — caller hides the strip
        auto pane = tabImpl->GetActivePane();
        if (!pane)
            return;

        // Recover the project id from the session records, if this tab has one
        _CTuxPruneSessions();
        std::wstring projectId;
        for (const auto& s : _ctuxSessions)
        {
            if (s.tab.get() == tab)
            {
                projectId = s.projectId;
                break;
            }
        }

        std::vector<ClickTerminal::Project> projects;
        if (!projectId.empty())
            if (auto sidebar = winrt::get_self<implementation::ProjectSidebar>(Sidebar()))
                if (auto p = sidebar->ProjectManagerRef().GetProjectById(projectId))
                    projects.push_back(*p);

        std::vector<ClickTerminal::LayoutSlot> slots{ ClickTerminal::LayoutSlot{ 0, 0, projectId } };
        std::vector<std::weak_ptr<Pane>> slotPanes{ std::weak_ptr<Pane>{ pane } };
        _CTuxSetTabOverlay(1, 1, slots, projects, tab, slotPanes);
    }

    // ClickTerminal: "창 분할" button — show the layout editor in extend mode with
    // the tab's current grid locked, then split the live tab in place. One-off
    // apply only: layouts.json is never touched from this path.
    safe_void_coroutine TerminalPage::_CTuxShowExtendDialog(winrt::TerminalApp::Tab tab)
    {
        auto ov = _CTuxFindOverlay(tab);
        if (!ov)
            co_return;

        auto toNarrow = [](const std::wstring& ws) {
            if (ws.empty()) return std::string{};
            int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, nullptr, 0, nullptr, nullptr);
            std::string r(static_cast<size_t>(len) - 1, '\0');
            WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, r.data(), len, nullptr, nullptr);
            return r;
        };

        // Serialize the tab's current grid as the locked base
        hstring baseJson;
        {
            Json::Value root;
            root["rows"] = static_cast<int>(ov->rows);
            root["cols"] = static_cast<int>(ov->cols);
            Json::Value slots(Json::arrayValue);
            for (const auto& s : ov->slots)
            {
                Json::Value sj;
                sj["row"]       = static_cast<int>(s.Row);
                sj["col"]       = static_cast<int>(s.Col);
                sj["projectId"] = toNarrow(s.ProjectId);
                slots.append(sj);
            }
            root["slots"] = slots;
            Json::StreamWriterBuilder wb; wb["indentation"] = "";
            baseJson = winrt::to_hstring(Json::writeString(wb, root));
        }

        auto dialog = winrt::make<implementation::LayoutPickerDialog>();

        if (auto sidebar = winrt::get_self<implementation::ProjectSidebar>(Sidebar()))
        {
            auto projects = sidebar->ProjectManagerRef().GetAllProjects();
            Json::Value arr(Json::arrayValue);
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

        // SetMode must come before SetExtendBase (SetMode resets the base state)
        dialog.SetMode(L"extend");
        dialog.SetExtendBase(baseJson);
        dialog.XamlRoot(XamlRoot());
        co_await dialog.ShowAsync();

        if (dialog.ShouldApply() && !dialog.LayoutJson().empty())
        {
            try
            {
                Json::Value root;
                Json::CharReaderBuilder b;
                std::string errs;
                std::istringstream ss(winrt::to_string(dialog.LayoutJson()));
                if (!Json::parseFromStream(b, ss, &root, &errs))
                    co_return;

                auto toWide = [](const std::string& s) {
                    if (s.empty()) return std::wstring{};
                    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
                    std::wstring r(static_cast<size_t>(len) - 1, L'\0');
                    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, r.data(), len);
                    return r;
                };

                uint32_t rows = static_cast<uint32_t>(root.get("rows", 1).asInt());
                uint32_t cols = static_cast<uint32_t>(root.get("cols", 1).asInt());
                rows = std::clamp(rows, 1u, 3u);
                cols = std::clamp(cols, 1u, 3u);

                std::vector<ClickTerminal::LayoutSlot> slots;
                for (const auto& sj : root["slots"])
                {
                    ClickTerminal::LayoutSlot slot;
                    slot.Row       = static_cast<uint32_t>(sj.get("row", 0).asInt());
                    slot.Col       = static_cast<uint32_t>(sj.get("col", 0).asInt());
                    slot.ProjectId = toWide(sj.get("projectId", "").asString());
                    slots.push_back(slot);
                }

                _CTuxExtendTabLayout(tab, rows, cols, slots);
            }
            catch (...) {}
        }
    }

    // ClickTerminal: grow a live tab from its current R0xC0 grid to R1xC1 by
    // splitting panes in place — existing panes (and their running sessions)
    // are never touched, only new panes are added.
    void TerminalPage::_CTuxExtendTabLayout(const winrt::TerminalApp::Tab& tab, uint32_t newRows, uint32_t newCols,
                                            const std::vector<ClickTerminal::LayoutSlot>& allSlots)
    {
        auto ov = _CTuxFindOverlay(tab);
        auto tabImpl = _GetTabImpl(tab);
        if (!ov || !tabImpl)
            return;

        const uint32_t R0 = std::max(1u, ov->rows);
        const uint32_t C0 = std::max(1u, ov->cols);
        const uint32_t R1 = std::clamp(newRows, 1u, 3u);
        const uint32_t C1 = std::clamp(newCols, 1u, 3u);
        if (R1 < R0 || C1 < C0 || (R1 == R0 && C1 == C0))
            return;

        std::vector<ClickTerminal::Project> projects;
        if (auto sidebar = winrt::get_self<implementation::ProjectSidebar>(Sidebar()))
            projects = sidebar->ProjectManagerRef().GetAllProjects();

        auto findProject = [&](const std::wstring& id) -> const ClickTerminal::Project* {
            for (const auto& p : projects)
                if (p.Id == id) return &p;
            return nullptr;
        };
        auto projectIdAt = [&](uint32_t r, uint32_t c) -> std::wstring {
            for (const auto& s : allSlots)
                if (s.Row == r && s.Col == c) return s.ProjectId;
            return {};
        };

        CTuxLog(L"[CTux] Extend begin R0=" + std::to_wstring(R0) + L" C0=" + std::to_wstring(C0) +
                L" -> R1=" + std::to_wstring(R1) + L" C1=" + std::to_wstring(C1));

        // Existing panes laid out on the expanded grid
        std::vector<std::vector<std::shared_ptr<Pane>>> paneAt(R1, std::vector<std::shared_ptr<Pane>>(C1, nullptr));
        for (size_t i = 0; i < ov->slots.size() && i < ov->slotPanes.size(); ++i)
        {
            const auto& s = ov->slots[i];
            if (s.Row < R1 && s.Col < C1)
                paneAt[s.Row][s.Col] = ov->slotPanes[i].lock();
        }

        // Remember the existing panes so we can repoint their session records after
        // splitting (a split moves a leaf's content into a brand-new Pane object).
        std::vector<std::pair<std::shared_ptr<Pane>, std::pair<uint32_t, uint32_t>>> oldExisting;
        for (uint32_t r = 0; r < R0; ++r)
            for (uint32_t c = 0; c < C0; ++c)
                if (paneAt[r][c])
                    oldExisting.push_back({ paneAt[r][c], { r, c } });

        // Leaf-only splitting. We NEVER split a parent pane: Pane::_Split's parent
        // branch is effectively untested in stock WT (focus only ever lands on
        // leaves) and corrupts the live visual tree, hanging the render thread.
        // Splitting a leaf moves its content into a new `original` pane and returns
        // {original, newPane}; we keep splitting the growing remainder so every
        // final cell is an equal fraction.

        // 1) Extend each existing column downward (add rows R0..R1-1). Split the
        //    column's bottom existing leaf, then keep splitting the new remainder.
        if (R1 > R0)
        {
            for (uint32_t c = 0; c < C0; ++c)
            {
                auto cur = paneAt[R0 - 1][c];
                if (!cur) continue; // that cell's pane was closed — skip the column
                for (uint32_t finalRow = R0 - 1; finalRow + 1 < R1; ++finalRow)
                {
                    const uint32_t span = R1 - finalRow;            // rows still packed in `cur`
                    const float newFrac = static_cast<float>(span - 1) / static_cast<float>(span);
                    auto newPane = _MakePane(_CTuxArgsForProject(findProject(projectIdAt(finalRow + 1, c))), nullptr);
                    if (!newPane) newPane = _MakePane(nullptr, nullptr);
                    if (!newPane) break;
                    CTuxLog(L"[CTux] row-split c=" + std::to_wstring(c) + L" finalRow=" + std::to_wstring(finalRow));
                    auto [original, added] = tabImpl->SplitPaneAt(cur, SplitDirection::Down, newFrac, newPane);
                    if (!original || !added) break;
                    paneAt[finalRow][c] = original; // existing/prev content settled here
                    cur = added;                    // remainder keeps the rest of the column
                }
                paneAt[R1 - 1][c] = cur;
            }
        }

        // 2) Add new columns (C0..C1-1) by splitting each row's rightmost pane to
        //    the right. Per-row leaf splits keep the divider fractions aligned.
        if (C1 > C0)
        {
            for (uint32_t r = 0; r < R1; ++r)
            {
                auto cur = paneAt[r][C0 - 1];
                if (!cur) continue; // this row's rightmost pane is gone — skip
                for (uint32_t finalCol = C0 - 1; finalCol + 1 < C1; ++finalCol)
                {
                    const uint32_t span = C1 - finalCol;            // cols still packed in `cur`
                    const float newFrac = static_cast<float>(span - 1) / static_cast<float>(span);
                    auto newPane = _MakePane(_CTuxArgsForProject(findProject(projectIdAt(r, finalCol + 1))), nullptr);
                    if (!newPane) newPane = _MakePane(nullptr, nullptr);
                    if (!newPane) break;
                    CTuxLog(L"[CTux] col-split r=" + std::to_wstring(r) + L" finalCol=" + std::to_wstring(finalCol));
                    auto [original, added] = tabImpl->SplitPaneAt(cur, SplitDirection::Right, newFrac, newPane);
                    if (!original || !added) break;
                    paneAt[r][finalCol] = original;
                    cur = added;
                }
                paneAt[r][C1 - 1] = cur;
            }
        }

        CTuxLog(L"[CTux] Extend splits done");

        // Repoint existing sessions: their content moved into the `original` leaf
        // that now lives at the same (r,c) in paneAt.
        for (const auto& [oldPane, rc] : oldExisting)
        {
            auto newPane = paneAt[rc.first][rc.second];
            if (!newPane || newPane == oldPane) continue;
            for (auto& s : _ctuxSessions)
            {
                if (s.tab.get() != tab) continue;
                if (s.pane.lock() == oldPane)
                    s.pane = newPane;
            }
        }

        // 3) Re-record the overlay for the full expanded grid (row-major).
        //    _CTuxSetTabOverlay carries nicknames over by pane matching.
        std::vector<ClickTerminal::LayoutSlot> slots;
        std::vector<std::weak_ptr<Pane>> slotPanes;
        slots.reserve(static_cast<size_t>(R1) * C1);
        slotPanes.reserve(static_cast<size_t>(R1) * C1);
        for (uint32_t r = 0; r < R1; ++r)
        {
            for (uint32_t c = 0; c < C1; ++c)
            {
                ClickTerminal::LayoutSlot s;
                s.Row = r;
                s.Col = c;
                s.ProjectId = projectIdAt(r, c);
                slots.push_back(std::move(s));
                slotPanes.push_back(paneAt[r][c] ? std::weak_ptr<Pane>{ paneAt[r][c] } : std::weak_ptr<Pane>{});
            }
        }
        _CTuxSetTabOverlay(R1, C1, slots, projects, tab, slotPanes);

        // 4) Register sessions + autostart AI for the NEW slots only — existing
        //    panes already have session records and possibly running AI.
        if (auto nov = _CTuxFindOverlay(tab))
        {
            std::vector<size_t> newIdx;
            for (size_t i = 0; i < nov->slots.size(); ++i)
            {
                const auto& s = nov->slots[i];
                if (s.Row < R0 && s.Col < C0)
                    continue; // existing region
                if (s.ProjectId.empty())
                    continue;
                if (i < nov->slotPanes.size())
                    if (auto p = nov->slotPanes[i].lock())
                        _CTuxRegisterSession(s.ProjectId, L"", false, true, tab, p);
                newIdx.push_back(i);
            }
            _CTuxStartSlotsAI(*nov, newIdx);
        }

        if (R1 * C1 > 1)
            tabImpl->SetLayoutIcon();
    }
}
