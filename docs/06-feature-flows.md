# 06 — 기능별 코드 흐름 상세

각 주요 기능이 실행될 때 어떤 코드 경로를 거치는지 상세히 설명합니다.

---

## 1. 앱 시작 → 프로젝트 목록 표시

```
Windows Terminal 앱 실행
  │
  ▼
TerminalPage::TerminalPage()                    [TerminalPage.cpp]
  │  _CrashLog("[TermPage] ctor start")
  │  InitializeComponent() ← TerminalPage.xaml 로드
  │
  ▼
TabRowControl::TabRowControl()                  [TabRowControl.cpp]
  │  _CrashLog("[TabRow] ctor start")
  │  InitializeComponent() ← TabRowControl.xaml 로드
  │  _CrashLog("[TabRow] InitializeComponent OK")
  │
  ▼
ProjectSidebar::ProjectSidebar()                [ProjectSidebar.cpp]
  │  InitializeComponent() ← ProjectSidebar.xaml 로드
  │
  ├─ CTuxSettings::Load()                       [CTuxSettingsManager.cpp]
  │    ├─ USERPROFILE + \AppData\Local\ClickTerminal\ctux-settings.json 열기
  │    ├─ JSON 파싱 (schemaVersion 확인)
  │    └─ CTuxSettings 구조체 반환
  │
  ├─ ProjectManager(settings.ProjectsConfigPath)
  │    └─ ProjectManager::LoadProjects()        [ProjectManager.cpp]
  │         ├─ clickterminal.json 열기
  │         ├─ JSON 파싱
  │         └─ _projects 벡터 채우기
  │
  ├─ _ApplyTheme(settings.GetActiveTheme())     [ProjectSidebar.cpp]
  │    ├─ RequestedTheme = Dark/Light
  │    ├─ 각 UI 요소에 SolidColorBrush 적용
  │    └─ _BuildProjectList() 호출
  │
  └─ _BuildProjectList()                        [ProjectSidebar.cpp]
       ├─ ProjectListPanel.Children().Clear()
       ├─ for (auto& project : _projectManager->GetAllProjects())
       │    └─ _BuildProjectCard(project)
       │         ├─ Border (CardColor 배경)
       │         ├─ TextBlock (프로젝트 이름)
       │         ├─ TextBlock (폴더 경로, TextMuted)
       │         ├─ StackPanel (포트 번호 Chip들)
       │         ├─ [AI 활성 시] ContextMeter
       │         └─ StackPanel (액션 버튼: 터미널, AI, URL, ...)
       └─ ProjectListPanel.Children().Append(card)

결과: 사이드바에 프로젝트 카드 목록 표시
```

---

## 2. 프로젝트 추가 흐름

```
사용자가 AddProjectButton(+) 클릭
  │
  ▼
ProjectSidebar::_AddProjectClicked()            [ProjectSidebar.cpp]
  │  DbgLog("[Sidebar] AddProject clicked")
  │  _pendingAddProjectOp가 이미 진행 중이면 return (중복 방지)
  │
  ▼
AddProjectDialog 생성자                         [AddProjectDialog.cpp]
  │  InitializeComponent()
  │  Opened 이벤트 등록:
  │    ProjectNameBox().Focus(FocusState::Keyboard)
  │
  ▼
dialog.ShowAsync() 호출
  │  ContentDialog 표시 (애니메이션)
  │  Opened 이벤트 발생 → ProjectNameBox에 포커스
  │
사용자 입력:
  ├─ ProjectNameBox: 프로젝트 이름 입력
  ├─ FolderPathBox + BrowseButton:
  │    BrowseButton 클릭 → _BrowseFolderClicked()
  │      IFileOpenDialog (FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM)
  │      → 선택된 경로를 FolderPathBox에 설정
  ├─ ProjectTypeBox: Web/App/Cowork 선택
  ├─ AIToolBox: Claude/Codex/Gemini/None 선택
  └─ ColorSchemeBox: 색상 스킴 선택
  │
  ▼
Primary 버튼 ("추가") 클릭
  │  dialog.ProjectName(), FolderPath(), ProjectType()...
  │
  ▼
ProjectManager::AddProject(project)             [ProjectManager.cpp]
  │  project.Id = "proj-" + GenerateUUID()
  │  project.CreatedAt = ISO8601 현재 시각
  │  _IsDuplicate(path) 검사 (같은 경로 중복 방지)
  │  _projects.push_back(project)
  │
  ▼
ProjectManager::SaveProjects()                  [ProjectManager.cpp]
  │  .tmp 파일에 JSON 쓰기
  │  MoveFileExW(tmp → json) 원자적 교체
  │  OnProjectsChanged() 콜백 호출
  │
  ▼
ProjectSidebar::Refresh()                       [ProjectSidebar.cpp]
  └─ _BuildProjectList() 재호출 → 새 카드 표시
```

---

## 3. 프로젝트 편집 흐름

```
프로젝트 카드의 "편집" 버튼 클릭 (또는 컨텍스트 메뉴)
  │
  ▼
ProjectSidebar::_ShowEditDialog(projectId)      [ProjectSidebar.cpp]
  │
  ▼
AddProjectDialog 생성자
  │
  ▼
dialog.SetInitialValues(name, path, type, aiTool, scheme)
  │  필드에 기존 값 채우기
  │  Title = "프로젝트 편집"
  │  PrimaryButtonText = "수정"
  │
사용자가 값 수정 후 "수정" 클릭
  │
  ▼
Project updated = _projectManager->GetProjectById(projectId)
  │  기존 프로젝트를 불러와 수정된 필드만 갱신
  │
  ▼
ProjectManager::UpdateProject(updated)          [ProjectManager.cpp]
  │  find_if(id 일치 항목) → 교체
  │
  ▼
ProjectManager::SaveProjects() → 저장
  ▼
ProjectSidebar::Refresh() → UI 갱신
```

---

## 4. 터미널 열기 흐름

```
프로젝트 카드의 "터미널" 버튼 클릭
  │
  ▼
ProjectSidebar: OpenTerminalRequested 이벤트 발생
  │  EventArgs: { projectId, folderPath, colorScheme, terminalProfileGuid }
  │
  ▼
TerminalPage: OpenTerminalRequested 핸들러       [TerminalPage.cpp]
  │  project.FolderPath를 작업 디렉토리로 설정
  │  project.ColorScheme으로 터미널 색상 스킴 설정
  │  project.TerminalProfileGuid로 프로파일 선택 (비어있으면 기본)
  │
  ▼
새 터미널 탭 생성 (업스트림 Windows Terminal 코드)
  │  _CreateNewTabFromPane() 또는 유사 메서드
  │  StartupDirectory = project.FolderPath
  │
  ▼
ProjectManager::TouchProject(projectId)         [ProjectManager.cpp]
  └─ lastOpenedAt = 현재 시각 (최근 사용 정렬용)
```

---

## 5. AI 도구 실행 흐름

```
프로젝트 카드의 "AI 시작" 버튼 클릭
  │
  ▼
ProjectSidebar: StartAIRequested 이벤트 발생
  │  EventArgs: { projectId, aiTool }
  │
  ▼
TerminalPage: StartAIRequested 핸들러           [TerminalPage.cpp]
  │
  ▼
AIToolManager::GetLaunchCommand(project, tool)  [AIToolManager.cpp]
  │  도구별 커맨드라인 구성:
  │
  │  Claude Code:
  │    claude --permission-mode {mode}
  │           --model {model}
  │           [--add-dir dir ...]
  │           [--mcp-config path]
  │
  │  Codex CLI:
  │    codex --approval-mode {mode}
  │          --model {model}
  │
  │  Gemini:
  │    gemini --model {model}
  │
  ▼
새 터미널 탭 생성 (LaunchCommand.WorkingDir 에서)
  │  PTY 핸들 획득
  │
  ▼
AIToolManager::RegisterSession(sessionId, tool, ptyHandle)
  │  _sessions[sessionId] = {tool, ptyHandle}
  │
  ▼
ProjectSidebar::SetSessionActive(projectId, true)
  │  _activeSessions[projectId] = true
  │  카드 UI 업데이트: "AI 중지" 버튼 표시
  │
실행 중 (PTY stdout 모니터링):
  ▼
AIToolManager::ParseContextUsageLine(tool, line) [AIToolManager.cpp]
  │  각 도구별 출력 파싱 패턴:
  │  Claude: "context: {used}/{total}" 형식
  │  → ContextUsage 반환
  │
  ▼
ProjectSidebar::UpdateContextUsage(projectId, used, total)
  └─ ContextMeter UI 세그먼트 갱신 (0~10)
```

---

## 6. AI 도구 중지 흐름

```
프로젝트 카드의 "AI 중지" 버튼 클릭
  │
  ▼
ProjectSidebar: StopAIRequested 이벤트 발생
  │
  ▼
TerminalPage: StopAIRequested 핸들러
  │
  ▼
AIToolManager::SendExitCommand(sessionId)       [AIToolManager.cpp]
  │  PTY stdin에 "/exit\n" 전송 (정상 종료 유도)
  │  타임아웃 후 미종료 시 프로세스 강제 종료
  │
  ▼
AIToolManager::UnregisterSession(sessionId)
  │  _sessions.erase(sessionId)
  │
  ▼
ProjectSidebar::SetSessionActive(projectId, false)
  └─ 카드 UI: "AI 시작" 버튼으로 복귀, ContextMeter 숨김
```

---

## 7. 테마 변경 흐름

```
ProjectSidebar의 Settings 버튼 클릭
  │
  ▼
ProjectSidebar::_SettingsClicked()              [ProjectSidebar.cpp]
  │  DbgLog("[Settings] clicked")
  │  XamlRoot 확인 (null이면 DbgLog("[Settings] XamlRoot null") 후 return)
  │  _pendingSettingsOp 중복 방지
  │
  ▼
SettingsDialog 생성                             [SettingsDialog.cpp]
  │  ApplyDarkSmoke(dialog) → 배경 오버레이
  │  dialog.XamlRoot(this.XamlRoot())
  │
  ▼
dialog.ShowAsync()
  │
사용자가 테마 RadioButton 선택
  │
  ▼
SettingsDialog: ThemeSelected 이벤트 발생
  │  { selectedThemeName }
  │
  ▼
CTuxSettings settings = CTuxSettings::Load()
settings.SelectedThemeName = selectedThemeName
settings.Save()                                 [CTuxSettingsManager.cpp]
  │  ctux-settings.json 갱신
  │
  ▼
ProjectSidebar::RefreshTheme()                  [ProjectSidebar.cpp]
  │  settings = CTuxSettings::Load()
  │  _activeTheme = settings.GetActiveTheme()
  │  _ApplyTheme(_activeTheme)
  │
  ▼
_ApplyTheme(theme) → 모든 UI 요소 색상 재적용
  │
  ▼
CTuxThemeChanged 이벤트 발생
  │  { tabBarBg, textColor }
  │
  ▼
TerminalPage: CTuxThemeChanged 핸들러
  └─ TabRowControl::ApplyTheme(tabBarBg, textColor)
       → TabViewBackground 리소스 오버라이드
       → 탭바 전체 색상 갱신
```

---

## 8. resources.pri 빌드 흐름

```
MSBuild (TerminalAppLib.vcxproj 빌드)
  │  각 .xaml → .xbf 컴파일
  │  TerminalApp.pri 생성 (컴포넌트별 PRI)
  │  TerminalApp.dll, TerminalApp.winmd 생성
  │
  ▼
makepri new
  │  priconfig.xml 읽기
  │    → pri.resfiles 목록에서 모든 컴포넌트 PRI 수집
  │    → TerminalApp.pri (bin/x64/Release/TerminalApp/)
  │    → TerminalCore.pri, TerminalControl.pri, ... 등 업스트림 PRI들
  │  모든 PRI 병합
  │  각 XAML의 XBF를 Base64로 내장
  │
  ▼
resources.pri 생성
  └─ _msix_extract/pkg/resources.pri

앱 실행 시:
  Application::LoadComponent(uri, ...)
    → resources.pri에서 uri 해당 XBF 추출
    → XBF 역직렬화 → XAML 트리 구성
    → InitializeComponent() 완료
```

---

## 9. MSIX 재패키징 전체 흐름

```
[초기 1회] 기존 MSIX 압축 해제
makeappx unpack /p WindowsTerminalDev.msix /d _msix_extract/pkg/

[매 배포마다]
1. MSBuild → DLL/winmd/pri 생성
2. DLL/winmd 복사: bin/x64/Release/TerminalApp/ → _msix_extract/pkg/TerminalApp/
3. makepri → resources.pri 재생성
4. 구 서명 파일 삭제 (AppxSignature.p7x, AppxBlockMap.xml)
5. makeappx pack → CascadiaPackage_new.msix
6. signtool sign → MSIX 서명 (AppxSignature.p7x 내장)
7. Import-Certificate → 시스템 인증서 신뢰 등록
8. Add-AppxPackage → 설치
```

---

## 10. 디버그 로그 흐름

```cpp
// 디버그 로그 기록 경로
void DbgLog(const std::wstring& msg) {
    // 1. LOCALAPPDATA 환경변수 가져오기
    // 2. ClickTerminal\ 폴더 생성
    // 3. ctux-debug.log에 타임스탬프 + 메시지 추가
}

// 앱 시작 체크포인트 (ctux-crash.log)
"[TermPage] ctor start"          → TerminalPage 생성자 진입
"[TabRow] ctor start"            → TabRowControl 생성자 진입
"[TabRow] InitializeComponent OK" → XAML 정상 초기화

// 다이얼로그 체크포인트 (ctux-debug.log)
"[Settings] clicked"             → 설정 버튼 정상 수신
"[Settings] XamlRoot null"       → XamlRoot 미설정 (다이얼로그 불가)
"[Settings] dialog ctor threw"   → AppxManifest 등록 누락
"[Settings] showing dialog..."   → 정상 경로

// 사이드바 체크포인트 (ctux-debug.log)
"[Sidebar] AddProject clicked"   → 추가 버튼 정상 수신
"[Sidebar] AddProject saved"     → 저장 완료
```
