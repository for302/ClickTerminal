# 03 — UI 컴포넌트

모든 UI 컴포넌트는 C++/WinRT + XAML로 구현되며, WinUI 2.x (Microsoft.UI.Xaml / MUX) 기반입니다.

---

## ProjectSidebar — 프로젝트 사이드바

**파일**: `src/cascadia/TerminalApp/ProjectSidebar.h/.cpp/.xaml`

### 레이아웃 구조

```
ProjectSidebar (UserControl, 너비 250px)
└── Grid (3행)
    ├── Row 0 (높이 36px): 헤더 영역
    │   ├── TextBlock "PROJECTS"
    │   ├── Button AISetupButton (Glyph: EDE3 ⚙)
    │   └── Button AddProjectButton (Glyph: E710 +)
    ├── Row 1 (남은 공간): 스크롤 뷰
    │   └── ScrollViewer
    │       └── StackPanel ProjectListPanel (Spacing: 2px)
    │           └── [동적 생성: 프로젝트 카드들]
    └── Row 2 (높이 40px): 푸터
        └── Button SettingsButton
```

### WinRT 이벤트 (IDL 정의)

```idl
// ProjectSidebar.idl
event Windows.Foundation.TypedEventHandler<ProjectSidebar, OpenTerminalEventArgs>
    OpenTerminalRequested;   // 프로젝트 터미널 열기 요청

event Windows.Foundation.TypedEventHandler<ProjectSidebar, StartAIEventArgs>
    StartAIRequested;        // AI 도구 시작 요청

event Windows.Foundation.TypedEventHandler<ProjectSidebar, StopAIEventArgs>
    StopAIRequested;         // AI 도구 중지 요청

event Windows.Foundation.TypedEventHandler<ProjectSidebar, OpenUrlEventArgs>
    OpenUrlRequested;        // URL 열기 요청

event Windows.Foundation.TypedEventHandler<ProjectSidebar, CTuxThemeChangedArgs>
    CTuxThemeChanged;        // 테마 변경 알림
```

### 공개 메서드

```cpp
// ProjectSidebar.h
void Refresh();                                              // 프로젝트 목록 새로고침
void RefreshTheme();                                         // 테마 색상 새로고침
void SetSessionActive(const hstring& projectId, bool active); // AI 세션 상태 업데이트
void UpdateContextUsage(const hstring& projectId,
                        uint32_t used, uint32_t total);      // 컨텍스트 미터 업데이트
ProjectManager& ProjectManagerRef();                         // 내부 매니저 참조
```

### 비공개 멤버 변수

```cpp
// ProjectSidebar.h (private)
std::unique_ptr<ProjectManager>   _projectManager;
std::unordered_map<std::wstring, bool>
                                  _activeSessions;    // projectId → AI 세션 활성 여부
std::unordered_map<std::wstring, std::pair<uint32_t,uint32_t>>
                                  _contextUsage;      // projectId → {used, total}
std::unordered_map<std::wstring, ContextMeter>
                                  _meterControls;     // projectId → UI 컨트롤
CTuxTheme                         _activeTheme;       // 현재 적용된 테마

// 비동기 다이얼로그 작업 (중복 열기 방지)
Windows::Foundation::IAsyncOperation<ContentDialogResult>
    _pendingSettingsOp;
Windows::Foundation::IAsyncOperation<ContentDialogResult>
    _pendingAddProjectOp;
Windows::Foundation::IAsyncOperation<ContentDialogResult>
    _pendingAISetupOp;
Windows::Foundation::IAsyncOperation<ContentDialogResult>
    _pendingEditOp;
```

### 비공개 메서드

```cpp
void _BuildProjectList();                   // StackPanel에 카드 동적 추가
UIElement _BuildProjectCard(const Project& project); // 단일 카드 UI 구성
void _ApplyTheme(const CTuxTheme& theme);   // 모든 요소에 색상 적용

// 컨텍스트 메뉴 액션
void _ShowRenameDialog(const std::wstring& projectId);
void _ShowDeleteConfirm(const std::wstring& projectId);
void _ShowColorSchemeDialog(const std::wstring& projectId);
void _ShowEditDialog(const std::wstring& projectId);  // SetInitialValues 호출

// XAML 이벤트 핸들러
void _AddProjectClicked(IInspectable const&, RoutedEventArgs const&);
void _SettingsClicked(IInspectable const&, RoutedEventArgs const&);
void _AISetupClicked(IInspectable const&, RoutedEventArgs const&);
```

### 헬퍼 함수 (cpp 파일 상단, 네임스페이스 없음)

```cpp
// 디버그 로그: %LOCALAPPDATA%\ClickTerminal\ctux-debug.log
void DbgLog(const std::wstring& msg);

// 비주얼 트리에서 이름으로 요소 검색 (재귀)
DependencyObject FindDescendantByName(DependencyObject root, hstring name);

// ContentDialog 배경 어둡게 처리 (연기 레이어)
void ApplyDarkSmoke(ContentDialog& dialog);

// "#RRGGBB" 또는 "#AARRGGBB" → Windows::UI::Color
Windows::UI::Color ParseHexColor(const std::wstring& hex);

// 배경 카드 색상 계산 (밝기 조정)
// Dark 모드: +18 밝게, Light 모드: -25 어둡게
Windows::UI::Color CardColor(Windows::UI::Color base, bool isLight);
```

### _ApplyTheme() 동작

```
1. RequestedTheme = ElementTheme::Light (isLight) or Dark
2. SidebarBorder.Background = SidebarBg
3. HeaderGrid.Background = SidebarHeaderBg
4. ProjectsLabel.Foreground = SidebarText
5. AddProjectButton.Foreground = SidebarText
6. SettingsButton.Foreground = SidebarTextMuted
7. _BuildProjectList() 재호출 (카드 색상 재적용)
```

---

## TabRowControl — 탭 행 컨트롤

**파일**: `src/cascadia/TerminalApp/TabRowControl.h/.cpp/.xaml`

### 레이아웃 구조

```
ContentPresenter (루트)
└── mux:TabView (VerticalAlignment=Bottom)
    ├── TabStripHeader:
    │   └── StackPanel (Orientation=Horizontal)
    │       ├── Border CTuxHeaderBorder (Width=250)
    │       │   └── TextBlock "CTux" (FontSize=14, Bold)
    │       └── FontIcon ElevationShield (Glyph: EA18 🛡, Visibility=Collapsed)
    └── TabStripFooter:
        └── mux:SplitButton NewTabButton
            └── FontIcon (Glyph: E710 +)
```

### 공개 메서드

```cpp
// 탭바 전체 색상 적용
void ApplyTheme(Windows::UI::Color tabBarBg, Windows::UI::Color textColor);

// XAML 이벤트 핸들러
void OnNewTabButtonClick(IInspectable const&, SplitButtonClickEventArgs const&);
void OnNewTabButtonDrop(IInspectable const&, DragEventArgs const&);
void OnNewTabButtonDragOver(IInspectable const&, DragEventArgs const&);

// 관찰 가능 속성
OBSERVABLE_PROPERTY(bool, ShowElevationShield);  // 관리자 실행 표시
```

### ApplyTheme() 세부 동작

```cpp
void TabRowControl::ApplyTheme(Color tabBarBg, Color textColor) {
    // 1. 밝기 계산 (luma)
    float lum = 0.2126f * R + 0.7152f * G + 0.0722f * B;
    bool isLight = lum > 128.0f;

    // 2. 선택/호버 색상 계산
    // Dark 모드: +25 (선택), +12 (호버)
    // Light 모드: -25 (선택), -12 (호버)
    Color selected = ShiftBrightness(tabBarBg, isLight ? -25 : +25);
    Color hovered  = ShiftBrightness(tabBarBg, isLight ? -12 : +12);

    // 3. 기본 배경 설정
    TabView().Background(SolidColorBrush(tabBarBg));
    CTuxHeaderBorder().Background(SolidColorBrush(tabBarBg));
    CTuxHeaderText().Foreground(SolidColorBrush(textColor));

    // 4. WinUI TabView 내부 리소스 오버라이드 (핵심!)
    //    TabContainerGrid가 ThemeResource로 배경을 제어하므로
    //    Resources 딕셔너리에 직접 오버라이드해야 탭 우측 배경도 변경됨
    TabView().Resources().Insert(
        winrt::box_value(hstring(L"TabViewBackground")), bg);

    // 5. 탭 항목 선택/호버 색상 오버라이드
    TabView().Resources().Insert(
        hstring(L"TabViewItemHeaderBackgroundSelected"), selected);
    TabView().Resources().Insert(
        hstring(L"TabViewItemHeaderBackgroundPointerOver"), hovered);
}
```

---

## AddProjectDialog — 프로젝트 추가/편집 다이얼로그

**파일**: `src/cascadia/TerminalApp/AddProjectDialog.h/.cpp/.xaml`

### XAML 구조

```
ContentDialog (MinWidth=420)
├── 리소스: ContentDialogSmokeBrush (#99000000)
└── StackPanel (Spacing=12)
    ├── TextBox ProjectNameBox (플레이스홀더: "Project name")
    ├── Grid (2열)
    │   ├── TextBox FolderPathBox (플레이스홀더: "Folder path")
    │   └── Button BrowseButton "..."
    ├── ComboBox ProjectTypeBox
    │   ├── [0] Web (Browser)        → "web"
    │   ├── [1] App                  → "app"
    │   └── [2] Cowork (AI Workflow) → "ai-workflow"
    ├── ComboBox AIToolBox
    │   ├── [0] Claude Code (Anthropic) → "claude"
    │   ├── [1] Codex CLI (OpenAI)      → "codex"
    │   ├── [2] Gemini (Google)         → "gemini"
    │   └── [3] None                    → ""
    └── ComboBox ColorSchemeBox (10개 스킴)
        ├── Default, CTux Dark, CTux Light
        ├── Campbell, One Half Dark, One Half Light
        └── Solarized Dark, Solarized Light, Tango Dark, Tango Light
```

### 공개 속성 접근자

```cpp
hstring ProjectName()    const;  // ProjectNameBox.Text()
hstring FolderPath()     const;  // FolderPathBox.Text()
hstring ProjectType()    const;  // 인덱스 → "web"|"app"|"ai-workflow"
hstring DefaultAITool()  const;  // 인덱스 → "claude"|"codex"|"gemini"|""
hstring ColorScheme()    const;  // 인덱스 → 스킴 이름
```

### SetInitialValues() — 편집 모드

```cpp
void AddProjectDialog::SetInitialValues(
    hstring name, hstring path,
    hstring type, hstring aiTool, hstring scheme)
{
    // 필드 채우기
    ProjectNameBox().Text(name);
    FolderPathBox().Text(path);

    // 문자열 → ComboBox 인덱스 역매핑
    // type: "web"→0, "app"→1, "ai-workflow"→2
    // aiTool: "claude"→0, "codex"→1, "gemini"→2, ""→3

    // 다이얼로그 타이틀 및 버튼 텍스트 변경
    Title(box_value(hstring(L"프로젝트 편집")));
    PrimaryButtonText(hstring(L"수정"));
}
```

### _BrowseFolderClicked() — 폴더 선택

```cpp
// COM IFileOpenDialog 사용 (모던 파일 다이얼로그)
IFileOpenDialog* pfd;
CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
    IID_PPV_ARGS(&pfd));

DWORD dwOptions;
pfd->GetOptions(&dwOptions);
pfd->SetOptions(dwOptions | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);

pfd->Show(ownerHwnd);

IShellItem* psi;
pfd->GetResult(&psi);
// 경로를 FolderPathBox에 설정
```

### 포커스 설정 — 영문 입력 보장

```cpp
// 생성자에서 등록
Opened([this](IInspectable const&, ContentDialogOpenedEventArgs const&) {
    // Loaded 대신 Opened: 다이얼로그 애니메이션 완료 후 HWND 포커스 이전
    // Keyboard: Win32 입력 컨텍스트까지 초기화 (ASCII 입력 보장)
    ProjectNameBox().Focus(FocusState::Keyboard);
});
// ❌ 절대 Loaded + FocusState::Programmatic 조합 사용 금지
//    → HWND 포커스가 터미널에 남아 ASCII(영문) 입력 차단
```

---

## ContextMeter — AI 컨텍스트 미터

**파일**: `src/ClickTerminal/ContextMeter.xaml/.h`

### 구조

```
ContextMeter (UserControl)
└── StackPanel (Orientation=Horizontal, Spacing=2)
    └── [10개 Rectangle] (filled/empty 세그먼트)
        ├── 채워진 세그먼트: #89B4FA (CTux Blue)
        └── 빈 세그먼트: #363653 (어두운 배경)
```

### 렌더링 로직

```cpp
// ContextUsage::FilledSegments()
uint8_t FilledSegments() const {
    if (TotalTokens == 0) return 0;
    float ratio = (float)UsedTokens / TotalTokens;
    return (uint8_t)(ratio * 10.0f + 0.5f);  // 반올림
}

// 미터 업데이트: ProjectSidebar::UpdateContextUsage()
void UpdateContextUsage(hstring projectId, uint32_t used, uint32_t total) {
    _contextUsage[projectId] = {used, total};
    auto meter = _meterControls.find(projectId);
    if (meter != _meterControls.end()) {
        // 세그먼트 수 계산 후 UI 업데이트
        uint8_t filled = ContextUsage{used, total}.FilledSegments();
        meter->second.UpdateSegments(filled);
    }
}
```

---

## SettingsDialog — 설정 다이얼로그

**파일**: `src/cascadia/TerminalApp/SettingsDialog.h/.cpp/.xaml`

### 구조

```
ContentDialog (다이얼로그)
└── Grid (2열: 네비게이션 | 컨텐츠)
    ├── StackPanel 네비게이션 (Background=DialogNavBg, Width=160)
    │   ├── Button "테마"
    │   ├── Button "AI 설정"
    │   └── Button "플러그인"
    └── ScrollViewer 컨텐츠 영역
        ├── [테마 페이지]
        │   └── 테마 선택 RadioButton 목록
        ├── [AI 설정 페이지]
        │   └── → AISetupPage 컴포넌트
        └── [플러그인 페이지]
            ├── ToggleSwitch GitPlugin
            └── ToggleSwitch PortPlugin
```

### 디버그 로그 포인트

```
[Settings] clicked       → SettingsButton 클릭 이벤트 수신
[Settings] XamlRoot null → XamlRoot가 설정 안 됨 (다이얼로그 표시 불가)
[Settings] dialog ctor threw → AppxManifest InProcessServer 등록 누락
[Settings] showing dialog... → 정상 경로 진입
```

---

## AISetupPage — AI 도구 설정 페이지

**파일**: `src/cascadia/TerminalApp/AISetupPage.h/.cpp/.xaml`

### 구조

```
AISetupPage (UserControl)
└── StackPanel
    ├── [Claude Code 섹션]
    │   ├── 설치 상태 표시 (설치됨/미설치)
    │   ├── 기본 Permission Mode 선택
    │   └── 기본 Model 선택
    ├── [Codex CLI 섹션]
    │   ├── 설치 상태 표시
    │   └── 기본 ApprovalMode 선택
    └── [Gemini 섹션]
        ├── 설치 상태 표시
        └── 기본 Model 선택
```

---

## TerminalPage — 메인 페이지 통합

**파일**: `src/cascadia/TerminalApp/TerminalPage.cpp` (업스트림 파일, ClickTerminal 수정 부분)

### ClickTerminal 추가 코드

```cpp
// 크래시 로그 헬퍼 (파일 경로: %LOCALAPPDATA%\ClickTerminal\ctux-crash.log)
void TerminalPage::_CrashLog(const std::wstring& msg) {
    // 앱 시작 시퀀스 추적용
    // 로그 포인트:
    //   "[TermPage] ctor start"      → 생성자 진입
    //   "[TabRow] ctor start"        → TabRowControl 생성
    //   "[TabRow] InitializeComponent OK" → XAML 초기화 성공
    // 이 로그로 resources.pri XBF 불일치 진단 가능
}

// ProjectSidebar 통합
// TerminalPage.xaml에 SplitView 또는 Grid로 사이드바 패널 추가
// ProjectSidebar의 이벤트 구독:
//   OpenTerminalRequested → 새 탭/창으로 터미널 열기
//   StartAIRequested      → AI 도구 프로세스 시작
//   StopAIRequested       → AI 도구 프로세스 종료
//   CTuxThemeChanged      → TabRowControl.ApplyTheme() 호출
```

---

## XAML 테마 리소스 구조

### RequestedTheme 설정

각 컴포넌트는 자체 `RequestedTheme`를 설정하여 독립적으로 Light/Dark 모드를 적용합니다.

```cpp
// ProjectSidebar._ApplyTheme()
this->RequestedTheme(
    _activeTheme.IsLightMode
        ? ElementTheme::Light
        : ElementTheme::Dark);
```

### TabViewBackground 오버라이드

WinUI TabView의 탭 스트립 배경은 `ThemeResource TabViewBackground`로 제어됩니다. `Background` 속성을 설정해도 내부 `TabContainerGrid`가 ThemeResource 값으로 덮어쓰기 때문에 반드시 Resources 딕셔너리에 직접 삽입해야 합니다.

```cpp
// ❌ 이것만으로는 우측 영역이 바뀌지 않음
TabView().Background(SolidColorBrush(color));

// ✅ 반드시 이것도 해야 함
TabView().Resources().Insert(
    winrt::box_value(hstring(L"TabViewBackground")),
    SolidColorBrush(color));
```
