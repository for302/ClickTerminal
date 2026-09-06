# 05 — 알려진 버그 패턴 및 해결책

프로젝트 개발 중 발생한 주요 버그와 그 원인 및 해결책을 기록합니다.

---

## [BUG-01] 탭 스트립 우측 배경색 미적용

**증상**  
`ApplyTheme()`으로 탭바 배경색을 설정해도 탭 스트립의 우측 영역(New Tab 버튼 이후 빈 공간)이 기본 색상으로 남음.

**원인**  
WinUI `TabView`의 탭 스트립 배경은 `TabView.Background` 프로퍼티가 아니라 내부 `TabContainerGrid`의 `{ThemeResource TabViewBackground}`로 제어됩니다. `Background="Transparent"`를 설정해도 `TabContainerGrid`가 ThemeResource 색상으로 덮어씁니다.

**해결**  
`TabRowControl::ApplyTheme()` 에서 TabView의 Resources 딕셔너리에 직접 오버라이드:

```cpp
// TabRowControl.cpp - ApplyTheme()
TabView().Resources().Insert(
    winrt::box_value(winrt::hstring(L"TabViewBackground")), bg);
```

이 한 줄 없이 `TabView().Background(brush)` 만으로는 우측 영역이 항상 시스템 기본색으로 표시됩니다.

---

## [BUG-02] ContentDialog TextBox 영문 입력 차단

**증상**  
ContentDialog(예: AddProjectDialog) 안의 TextBox에서 한국어(IME)는 입력되는데, 영문(ASCII) 및 숫자는 전혀 입력되지 않음.

**원인**  
Xaml Islands(Win32 호스팅) 환경에서 `Loaded` 이벤트는 다이얼로그 애니메이션 완료 **전**에 발생합니다.  
이 시점에 `FocusState::Programmatic`으로 포커스를 설정하면:
- XAML 논리 포커스만 이동
- Win32 HWND 포커스는 여전히 터미널 컨트롤에 남음
- `WM_CHAR` (영문 직접 입력)는 HWND 포커스를 따라가므로 TextBox에 도달하지 못함
- 한국어 IME는 TSF/IME 경로로 전달되므로 HWND 포커스와 무관하게 입력됨

**해결**  

```cpp
// AddProjectDialog.cpp 생성자
Opened([this](IInspectable const&, ContentDialogOpenedEventArgs const&) {
    // Opened: 애니메이션 완료 후 HWND 포커스도 다이얼로그로 이전된 시점
    // FocusState::Keyboard: Win32 입력 컨텍스트까지 완전히 초기화
    ProjectNameBox().Focus(winrt::Windows::UI::Xaml::FocusState::Keyboard);
});
```

**금지 패턴**  
```cpp
// ❌ HWND 포커스가 터미널에 남아 ASCII 입력을 항상 차단
Loaded([this](...) {
    ProjectNameBox().Focus(FocusState::Programmatic);
});
```

**적용 원칙**: 새로운 ContentDialog를 만들 때마다 위 `Opened + FocusState::Keyboard` 패턴을 적용.

---

## [BUG-03] MSB4019 — CollectWildcardResources.targets 없음

**증상**  
```
error MSB4019: 가져온 프로젝트 
"...\TerminalApp\build\rules\CollectWildcardResources.targets"을(를) 찾을 수 없습니다.
```

**원인**  
MSBuild를 `.sln` 없이 `.vcxproj`만 직접 빌드하면 `$(SolutionDir)`이 vcxproj 파일이 있는 폴더로 설정됩니다.

```
SolutionDir = D:\Dev\02_PC\ClickTerminal\src\cascadia\TerminalApp\
→ 찾는 경로: TerminalApp\build\rules\CollectWildcardResources.targets  (없음)
실제 경로:   D:\Dev\02_PC\ClickTerminal\build\rules\CollectWildcardResources.targets
```

**해결**  
MSBuild 호출 시 반드시 `/p:SolutionDir` 명시:

```powershell
& $msbuild $vcxproj `
    /p:SolutionDir="D:\Dev\02_PC\ClickTerminal\" `
    ...
```

---

## [BUG-04] resources.pri XBF 불일치 → FAST_FAIL 크래시

**증상**  
앱 실행 직후 충돌. `ctux-crash.log`에 `[TabRow] ctor start`는 있지만 `InitializeComponent OK`는 없음.  
이벤트 뷰어: `0xc0000409 (FAST_FAIL_FATAL_APP_EXIT)`, `ucrtbase.dll`.

**원인**  
`resources.pri`는 모든 XAML의 XBF(컴파일된 바이너리)를 Base64로 내장합니다. DLL만 교체하고 `resources.pri`를 재생성하지 않으면:
- DLL 안의 C++ 코드: 새 버전
- `resources.pri`의 XBF: 이전 버전
- `InitializeComponent()` 내부 `Application::LoadComponent()` 호출 시 불일치 감지 → noexcept 경계에서 예외 전파 → FAST_FAIL

**해결**  
빌드 후 반드시 `makepri` 실행:

```powershell
Set-Location "D:\Dev\02_PC\ClickTerminal\src\cascadia\CascadiaPackage"
& $makepri new `
    /pr "D:\Dev\02_PC\ClickTerminal\src\cascadia\CascadiaPackage" `
    /cf "obj\x64\Release\priconfig.xml" `
    /o `
    /of "D:\Dev\02_PC\ClickTerminal\_msix_extract\pkg\resources.pri"
```

XAML을 수정하지 않았더라도 재패키징 시 항상 실행.

---

## [BUG-05] AppxManifest InProcessServer 누락 → FAST_FAIL 크래시

**증상**  
새 WinRT 컴포넌트(예: `TerminalApp.MyNewControl`) 추가 후 앱 시작 시 즉시 충돌.  
`ctux-crash.log`에 `[TermPage] ctor start`조차 찍히지 않음.

**원인**  
`_msix_extract/pkg/AppxManifest.xml`의 `<InProcessServer>` 블록에 새 타입이 등록되지 않으면 WinRT 런타임이 `winrt::terminate()`를 호출합니다.

**해결**  
`AppxManifest.xml`에 항목 추가:

```xml
<ActivatableClass ActivatableClassId="TerminalApp.MyNewControl"
                  ThreadingModel="both" />
```

---

## [BUG-06] signtool /p7 플래그 → 서명 없는 MSIX

**증상**  
`Add-AppxPackage`가 `0x800B0100 "패키지의 서명이 유효하지 않습니다"` 오류 반환.  
설치가 조용히 실패하여 이전 버전이 유지된 것처럼 보임.

**원인**  
`signtool sign /p7 /p7co /p7ce` 플래그는 서명을 별도 파일로 분리 저장합니다.  
MSIX 내부에 `AppxSignature.p7x`가 생성되지 않아 패키지 서명 검증 실패.

**해결**  
플래그 없이 기본 서명:

```powershell
& $signtool sign /fd SHA256 /f "..\ClickTerminalDev.pfx" /p ctuxdev CascadiaPackage_new.msix
```

---

## [BUG-07] 설치 후 변경사항 없음 — 빌드 없이 소스만 수정

**증상**  
소스 파일을 수정했지만 앱 실행 시 변경사항이 없음.

**원인**  
소스 파일 타임스탬프 > DLL 타임스탬프 → 빌드를 실행하지 않아 이전 버전 DLL이 그대로 설치됨.

**진단**  
```powershell
(Get-Item "src\cascadia\TerminalApp\ProjectSidebar.cpp").LastWriteTime
(Get-Item "bin\x64\Release\TerminalApp\TerminalApp.dll").LastWriteTime
# 소스 > DLL 이면 리빌드 필요
```

**해결**  
`_build_and_deploy.bat` 실행.

**예방**  
코드 수정 → `_build_and_deploy.bat` → 설치 확인 → 테스트 순서 준수.  
Pre-flight 로그에서 `[PRE-FLIGHT] Sources newer than DLL`이 Yellow로 표시되면 이번 빌드에 포함됨.

---

## [BUG-08] .bat 창이 즉시 닫힘

**증상**  
`_install_run.bat` 실행 시 "Press any key to close..." 에서 키를 누르면 창이 즉시 닫힘.

**원인**  
`powershell -File script.ps1`을 `-NoExit` 없이 호출하면 ps1의 `ReadKey` 완료 시 PowerShell 프로세스가 종료되고 CMD 창도 함께 닫힘.

**해결**  
bat 파일에서 `-NoExit` 포함:

```bat
powershell -NoExit -ExecutionPolicy Bypass -NoProfile -File "%~dp0script.ps1"
```

---

## [BUG-09] Add-AppxPackage Developer Mode 오류 혼란

**증상**  
`Add-AppxPackage -Register`를 시도할 때 `0x80073CFF` 에러가 표시됨.  
에러 메시지가 "인증서" 관련인 것처럼 보여 혼란스러움.

**원인**  
Developer Mode가 OFF인 상태에서 `-Register` (폴더 직접 등록)를 시도하면 항상 실패합니다. 이는 인증서 문제가 아닙니다.

**해결**  
Developer Mode 상태를 먼저 확인하고 분기:

```powershell
$devMode = (Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\AppModelUnlock"
    -ErrorAction SilentlyContinue).AllowDevelopmentWithoutDevLicense -eq 1

if ($devMode) {
    Add-AppxPackage -Register $manifest
} else {
    # makeappx → signtool → Add-AppxPackage (MSIX 경로)
}
```

이 PC는 Developer Mode OFF → 항상 MSIX 경로 사용.

---

## 빠른 진단 로그 위치

| 로그 | 경로 | 내용 |
|------|------|------|
| 디버그 로그 | `%LOCALAPPDATA%\ClickTerminal\ctux-debug.log` | 버튼 클릭, 다이얼로그 상태, XamlRoot 여부 |
| 크래시 로그 | `%LOCALAPPDATA%\ClickTerminal\ctux-crash.log` | 생성자 진입 포인트, InitializeComponent 성공 여부 |

### 크래시 로그 판독표

| 마지막 로그 | 의미 | 원인 |
|------------|------|------|
| 아무것도 없음 | 앱 프로세스조차 시작 안 됨 | AppxManifest 누락 (winrt::terminate) |
| `[TermPage] ctor start` | TerminalPage 생성자 진입 | TabRowControl 생성 중 문제 |
| `[TabRow] ctor start` | TabRowControl 생성자 진입 | InitializeComponent 실패 |
| `[TabRow] InitializeComponent OK` | 정상 시작 | 이후 런타임 에러 |
