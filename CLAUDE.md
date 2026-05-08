# ClickTerminal 프로젝트 지침

## 버전 관리 규칙 — 제일 중요, 반드시 준수

### 기능 추가/수정 시 버전 반드시 올릴 것

**버전 위치**: `src/cascadia/TerminalApp/TabRowControl.xaml` 의 `CTuxHeaderText` TextBlock
```xaml
<TextBlock x:Name="CTuxHeaderText" Text="CTux v0.0XX" .../>
```

**규칙**:
- 기능 추가, UI 변경, 버그 수정 등 사용자에게 보이는 변경이 있으면 **반드시** 버전을 올린다
- 형식: `CTux v0.0XX` (세 자리, 001~999)
- 빌드/배포 **전에** 버전을 올린다 — 배포 후에 올리면 뭐가 언제 바뀐지 추적 불가
- 대화 세션 중 여러 수정이 있어도 배포 시점 기준으로 한 번 올리면 됨
- Claude는 작업 완료 후 배포 전에 버전을 올렸는지 **항상 확인하고 언급**한다

**현재 버전**: v0.032

---

## 빌드 / 설치 규칙

### 설치 스크립트는 항상 .bat 파일로 만든다
- PowerShell(.ps1) 스크립트는 실행 즉시 창이 닫힘 → 사용자가 결과를 못 봄
- 반드시 `.bat` 파일로 만들고, 안에서 `powershell.exe -NoExit -Command "..."` 형태로 호출하거나 `pause` 명령을 마지막에 추가한다
- 관리자 권한 필요 시: `install-xxx.bat` 파일 안에서 UAC 자동 승격 처리

### 배치파일 기본 템플릿
```bat
@echo off
powershell -ExecutionPolicy Bypass -NoProfile -File "%~dp0script.ps1"
```
**`-NoExit` 쓰지 말 것** — ps1이 `Stop-Process -Id $PID`로 직접 창을 닫는다. `-NoExit`를 쓰면 ReadKey 후 PS 프롬프트가 남아 사용자가 수동으로 창을 닫아야 함.

ps1 마지막은 항상 이 패턴:
```powershell
Write-Host "Press any key to close..."
$null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
Stop-Process -Id $PID  # 창을 직접 종료
```
- .bat 자체에는 한글 쓰지 말 것 (무조건 깨짐)
- PowerShell 스크립트(.ps1)도 콘솔 출력은 영어로만
- 관리자 권한은 .bat이 아닌 .ps1 파일 안에서 자체 승격 처리:
```powershell
if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Start-Process powershell -Verb RunAs -ArgumentList "-ExecutionPolicy Bypass -File `"$PSCommandPath`""
    exit
}
```

### MSBuild 경로
`C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe`

### MSBuild 호출 시 SolutionDir 반드시 명시 — MSB4019 에러 방지

**증상**: `error MSB4019: 가져온 프로젝트 "...\TerminalApp\build\rules\CollectWildcardResources.targets"을(를) 찾을 수 없습니다.`

**원인**: MSBuild를 `.sln` 없이 `.vcxproj`만 직접 빌드하면 `$(SolutionDir)`이 vcxproj 파일이 있는 폴더(`src\cascadia\TerminalApp\`)로 설정된다. 그래서 `TerminalApp\build\rules\CollectWildcardResources.targets`를 찾으려 하지만 실제 파일은 `D:\Dev\20_PC\ClickTerminal\build\rules\`에 있어 실패.

**해결**: MSBuild 호출 시 `/p:SolutionDir` 명시 필수:
```powershell
& $msbuild $vcxproj /p:Configuration=Release /p:Platform=x64 /p:SolutionDir="D:\Dev\20_PC\ClickTerminal\" /t:Build /m /nologo /verbosity:minimal
```
→ `_build_and_deploy.ps1`에 이미 적용됨. 새 빌드 스크립트를 만들 때도 반드시 포함할 것.

### 빌드 대상 — 반드시 이 vcxproj를 빌드해야 함

**올바른 빌드 대상**: `src\cascadia\TerminalApp\dll\TerminalApp.vcxproj` (DynamicLibrary → TerminalApp.dll 생성)

**잘못된 대상** (절대 단독으로 빌드하면 안 됨): `src\cascadia\TerminalApp\TerminalAppLib.vcxproj`
- 이 프로젝트는 StaticLibrary (`TerminalAppLib.lib`)만 생성
- `TerminalApp.dll`은 절대 갱신되지 않음 → 코드 수정이 영원히 반영 안 됨

#### 왜 TerminalApp.vcxproj를 빌드하면 되나
```
TerminalApp.vcxproj (DynamicLibrary)
    └── ProjectReference ──→ TerminalAppLib.vcxproj (StaticLibrary)
```
MSBuild가 의존성 순서대로 자동으로 TerminalAppLib → TerminalApp 순으로 빌드하므로
`TerminalApp.vcxproj` 하나만 지정하면 된다.

→ **상세 설명**: `docs/04-build-deploy.md` 참조

- 패키지: wapproj는 .NET SDK 없어서 빌드 불가 → makeappx 수동 재패키징 방식 사용
- makeappx 경로: `C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\makeappx.exe`
- signtool 경로: `C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\signtool.exe`

### 재패키징 절차 (DLL 교체 후) — 반드시 이 순서 준수
1. `_msix_extract\pkg\` 에 기존 MSIX 압축 해제 (최초 1회)
2. `bin\x64\Release\TerminalApp\TerminalApp.dll` + `TerminalApp.winmd` 복사
3. **resources.pri 재생성** (XBF 변경 시 필수 — 아래 참조)
4. AppxSignature.p7x / AppxBlockMap.xml 삭제
5. makeappx로 재패키징
6. signtool로 서명 (PFX: `ClickTerminalDev.pfx`, 비밀번호: `ctuxdev`)
7. `.bat` 설치 스크립트로 인증서 신뢰 등록 + Add-AppxPackage

### resources.pri 재생성 — XAML 변경 시 필수
**왜 필요한가**: `resources.pri`는 모든 XAML의 XBF 바이너리를 EmbeddedData(Base64)로 직접 내장한다.
DLL만 교체하면 DLL 안의 코드는 새로워지지만, resources.pri의 XBF는 옛날 버전 그대로 남는다.
→ `InitializeComponent()` 안에서 `Application::LoadComponent()` 실패 → FAST_FAIL 크래시

**재생성 명령** (CascadiaPackage obj 폴더의 priconfig.xml 활용):
```powershell
Set-Location "D:\Dev\20_PC\ClickTerminal\src\cascadia\CascadiaPackage"
& "C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\makepri.exe" `
    new /pr "D:\Dev\20_PC\ClickTerminal\src\cascadia\CascadiaPackage" `
    /cf "obj\x64\Release\priconfig.xml" /o `
    /of "D:\Dev\20_PC\ClickTerminal\_msix_extract\pkg\resources.pri"
```
- priconfig.xml은 `obj\x64\Release\pri.resfiles`를 읽어 모든 컴포넌트 PRI를 병합
- TerminalApp.pri는 `bin\x64\Release\TerminalApp\TerminalApp.pri` (새 빌드 결과물)
- 이 명령 실행 전에 TerminalAppLib.vcxproj 빌드가 먼저 완료되어야 함

**크래시 진단 방법**:
- 크래시 로그: `C:\Users\Public\ctux-crash.log` 작성 여부 확인
- `[TermPage] ctor start`, `[TabRow] ctor start`는 찍히는데 `InitializeComponent OK`가 없으면
  → resources.pri XBF 불일치가 원인
- 앱 시작 시 0xc0000409 (FAST_FAIL_FATAL_APP_EXIT) → ucrtbase.dll → noexcept 경계에서 예외 전파

### signtool 서명 명령 (MSIX 전용) — 반드시 이 형식 사용
```powershell
# _msix_extract\ 폴더에서 실행
& "C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\signtool.exe" `
    sign /fd SHA256 /f "..\ClickTerminalDev.pfx" /p ctuxdev CascadiaPackage_new.msix
```
**절대 /p7 /p7co /p7ce 플래그를 쓰지 말 것.**
- `/p7` 계열 플래그는 서명을 별도 파일로 분리 저장 → MSIX 내부에 AppxSignature.p7x가 없음
- `Add-AppxPackage` 가 0x800B0100 "서명 없음" 오류 반환하면서 설치 실패
- 오류가 조용히 발생해서 "설치 성공처럼 보이지만 실제로는 이전 버전이 유지됨"

makeappx 명령:
```powershell
& "C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\makeappx.exe" `
    pack /d pkg /p CascadiaPackage_new.msix /nv /o
```

### 매니페스트 관리 — 새 WinRT 타입 추가 시
`_msix_extract\pkg\AppxManifest.xml` 의 `<InProcessServer>` 블록에 `<ActivatableClass>` 추가 필수.
누락 시 앱 시작 시 winrt::terminate() → FAST_FAIL_FATAL_APP_EXIT (0xc0000409, ucrtbase.dll) 크래시.
현재 등록된 커스텀 타입:
- TerminalApp.ContextMeter (line ~271)
- TerminalApp.SettingsDialog, ProjectSidebar (line ~275-276)
- TerminalApp.AddProjectDialog, AISetupPage (line ~293-294)

### 설치된 패키지 정보
- PackageFamilyName: `WindowsTerminalDev_xpqk32cx38ema`
- Publisher: `CN=ClickTerminalDev`
- 인증서: `_msix_extract\ClickTerminalDev_new.cer`

## 코드 위치

- 우리 추가 코드: `src/cascadia/TerminalApp/` 및 `src/ClickTerminal/`
- 테마 데이터: `src/ClickTerminal/CTuxTheme.h`, `CTuxSettings.h`
- 테마 구현: `src/cascadia/TerminalApp/CTuxSettingsManager.cpp`
- 사이드바: `ProjectSidebar.xaml/.h/.cpp`
- 설정창: `SettingsDialog.xaml/.h/.cpp`
- 탭바: `TabRowControl.xaml/.h/.cpp`

## 알려진 버그 패턴 및 해결책

### 탭바 우측 배경색 미적용 — TabViewBackground 리소스 오버라이드 필수

**증상**: `ApplyTheme()`으로 탭바 배경색을 설정해도 탭 스트립의 우측 영역(+버튼 이후)만 기본 색상으로 남음.

**원인**: WinUI `TabView`의 탭 스트립 배경은 `TabView.Background` 프로퍼티가 아니라 내부 `TabContainerGrid`의 `{ThemeResource TabViewBackground}` 리소스가 제어한다. `Background="Transparent"`를 설정해도 `TabContainerGrid`가 ThemeResource 색상으로 덮어씀.

**해결책**: `TabRowControl::ApplyTheme()` 에서 TabView의 Resources 딕셔너리에 직접 오버라이드:
```cpp
// TabRowControl.cpp - ApplyTheme()
TabView().Resources().Insert(winrt::box_value(winrt::hstring(L"TabViewBackground")), bg);
```
→ 이 한 줄을 누락하면 탭 스트립 우측이 항상 시스템 기본색으로 표시됨.

---

### ContentDialog TextBox 영문 입력 차단 — 확정 해결책 (2026-05-04)

**증상**: ContentDialog 안의 TextBox에서 한국어(TSF 경로)는 입력되는데 영문/숫자(WM_CHAR 경로)는 입력 안 됨.

---

#### 근본 원인

Xaml Islands 구조에서 터미널 페인 HWND와 XAML island HWND는 별개로 존재한다.

- **WM_CHAR** (영문 입력) 은 Win32 HWND 포커스를 따라간다 → 터미널 페인 HWND로 가버림
- **한국어 TSF** 는 HWND 포커스와 무관하게 XAML 포커스 요소에 전달됨 → 한국어만 됨
- `SetFocus(islandHwnd)` 를 호출해도 ContentDialog 자체가 Opened 이벤트 처리 직후 Win32 포커스를 터미널로 돌려보냄 (로그 확인: `after=island` → LostFocus 발생 → KeyDown시 `GetFocus=terminal`)

---

#### 실패한 시도 목록 (절대 다시 하지 말 것)

| 시도 | 실패 원인 |
|------|----------|
| `ImmSetConversionStatus` | Korean IME가 외부 TSF compartment 쓰기를 무시 |
| `GetAsyncKeyState(VK_HANGUL)` | 0x8000 반환 (toggle 비트=0) → Korean 모드 오판 |
| `GotFocus` 안에서 `SetFocus(island)` | ContentDialog가 포커스를 즉시 터미널로 환원 → GotFocus→LostFocus 무한루프 |
| `GetFocus() != islandHwnd` 체크로 루프 방지 시도 | XAML TextBox 활성시 GetFocus()는 island 자식 HWND → 항상 불일치 → 루프 계속 |
| `IsChild(island, GetFocus())` 체크 | 터미널이 SetFocus 직후 다시 포커스 가져감 → GotFocus 발생 시점엔 이미 terminal → 루프 |
| `ContentDialog.PreviewKeyDown` | Popup 컨텍스트에서 `FocusManager.GetFocusedElement()`가 TextBox를 못 찾음 → 삽입 안 됨 |

---

#### 확정 해결책

**영문 입력**: `WM_CHAR`를 완전히 포기하고 각 TextBox의 `KeyDown`에서 `ToUnicode()`로 문자 변환 후 직접 삽입.

```cpp
// 생성자에서 InitializeComponent() 이후, 각 TextBox마다 호출
auto attachInsert = [](winrt::Windows::UI::Xaml::Controls::TextBox box) {
    box.KeyDown([box](winrt::Windows::Foundation::IInspectable const&,
                      winrt::Windows::UI::Xaml::Input::KeyRoutedEventArgs const& e) {
        auto vk = static_cast<UINT>(e.OriginalKey());
        if (vk == 0xE5) return;  // VK_PROCESSKEY: 한국어 TSF — 건드리지 말 것
        if ((::GetKeyState(VK_CONTROL) & 0x8000) != 0) return;  // Ctrl 조합키 통과
        if ((::GetKeyState(VK_MENU)    & 0x8000) != 0) return;  // Alt 조합키 통과

        BYTE ks[256]; ::GetKeyboardState(ks);
        WCHAR ch[4] = {};
        // ToUnicode: shift/capslock 반영한 실제 문자 반환
        if (::ToUnicode(vk, ::MapVirtualKey(vk, MAPVK_VK_TO_VSC), ks, ch, 4, 0) != 1
            || ch[0] < L' ')  // 비인쇄 문자(BackSpace, Enter 등) 통과
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
attachInsert(ProjectNameBox());
attachInsert(FolderPathBox());
attachInsert(AIStartCommandBox());
// ↑ TextBox가 추가될 때마다 여기에 추가
```

**핵심 이유**: XAML `KeyDown`은 Win32 HWND 포커스와 무관하게 XAML 포커스 요소에 발생한다 (로그에서 GetFocus=terminal 이어도 KeyDown 발생 확인). BackSpace/Delete/화살표 등 비인쇄 키(ch[0] < 0x20)는 통과시켜 XAML TextBox 자체 처리에 맡긴다.

---

**한국어 모드 감지 — `GetKeyState` 사용 (확정)**

```cpp
// GetKeyState(VK_HANGUL) & 0x0001 = toggle 비트 = 1이면 한국어 모드
// GetAsyncKeyState는 틀림(0x8000 반환). TSF compartment도 오염되면 틀림.
SHORT ks = ::GetKeyState(VK_HANGUL);
bool isKorean = (ks & 0x0001) != 0;
```

**한국어 모드 해제 — Opened가 아닌 GotFocus에서 VK_HANGUL 전송**

```cpp
// Opened에서 _pendingHangulToggle = _IsKoreanModeActive() 저장
// GotFocus에서 소비 (이 시점에 TextBox TSF document context가 확립됨)
if (_pendingHangulToggle)
{
    _pendingHangulToggle = false;
    INPUT inp[2] = {};
    inp[0].type = INPUT_KEYBOARD; inp[0].ki.wVk = VK_HANGUL;
    inp[1] = inp[0]; inp[1].ki.dwFlags = KEYEVENTF_KEYUP;
    ::SendInput(2, inp, sizeof(INPUT));
    // VK_HANGUL이 TSF context를 리셋해 LostFocus 발생 →
    // RunAsync(Normal)로 XAML 포커스 복원
    Dispatcher().RunAsync(CoreDispatcherPriority::Normal, [this]() {
        if (_islandHwnd) ::SetFocus(_islandHwnd);
        ProjectNameBox().Focus(FocusState::Programmatic);
    });
}
```

**ContentDialog Opened 후 포커스 복원 — RunAsync 필수**

ContentDialog는 Opened 이벤트 핸들러가 모두 실행된 뒤 내부적으로 Win32 포커스를 리셋한다.
`RunAsync(Normal)`은 이 리셋 이후에 실행되므로 SetFocus + XAML Focus 재적용이 가능하다.

```cpp
// Opened 핸들러 마지막에:
Dispatcher().RunAsync(CoreDispatcherPriority::Normal, [this]() {
    if (_islandHwnd) ::SetFocus(_islandHwnd);
    ProjectNameBox().Focus(FocusState::Programmatic);
});
```

---

#### 새 ContentDialog 추가 시 체크리스트

1. `_islandHwnd` 멤버 추가 (`HWND _islandHwnd{ nullptr };`)
2. 생성자에서 `attachInsert()` 패턴으로 모든 TextBox에 KeyDown 삽입 핸들러 등록
3. Opened 핸들러: `ICoreWindowInterop`으로 island HWND 획득 → `_islandHwnd` 저장 → `SetFocus` → `RunAsync`로 재적용
4. GotFocus 핸들러: **SetFocus 호출 금지** (어떤 체크를 해도 루프 발생). VK_HANGUL 처리만 할 것
5. 한국어 감지: `GetKeyState(VK_HANGUL) & 0x0001` — 다른 방법은 전부 틀림

참고 구현: `AddProjectDialog.cpp`, `AddProjectDialog.h`

---

### 수정 후 배포 시 반드시 확인할 사항

코드를 수정했는데 설치 후 변경이 반영 안 된다면 → **resources.pri를 재생성했는지 확인**.
XAML을 건드리지 않았더라도, DLL 교체 시 resources.pri의 XBF와 C++ 코드가 불일치할 수 있음.
항상 위의 "재패키징 절차" 3단계(resources.pri 재생성)를 포함할 것.

---

### 코드 수정이 반영 안 되는 반복 패턴 — 빌드 없이 소스만 수정한 경우

**증상**: 소스 파일을 수정했지만 앱 실행 시 변경사항이 없음. "편집 버튼이 안 된다" 등 기능이 없는 것처럼 보임.

**원인**: 소스 파일(.cpp/.h)의 타임스탬프 > 빌드된 DLL의 타임스탬프인 경우.
즉, **소스를 수정했지만 빌드(MSBuild)를 실행하지 않아** 이전 버전 DLL이 그대로 설치된 것.

**진단 명령**:
```powershell
(Get-Item "D:\Dev\20_PC\ClickTerminal\src\cascadia\TerminalApp\ProjectSidebar.cpp").LastWriteTime
(Get-Item "D:\Dev\20_PC\ClickTerminal\bin\x64\Release\TerminalApp\TerminalApp.dll").LastWriteTime
```
소스 타임스탬프 > DLL 타임스탬프 → 리빌드 필요.

**해결**: `_build_and_deploy.bat` 실행 (빌드 + DLL 복사 + resources.pri 재생성 + 설치 한 번에 처리).

**예방**: 코드 수정 후 반드시 `_build_and_deploy.bat` 실행 → 설치 확인 → 테스트 순서를 지킬 것.
소스만 수정하고 "안 된다"고 판단하기 전에 **반드시 빌드 타임스탬프를 먼저 확인**한다.

### _build_and_deploy.bat 사전 점검(Pre-flight) 기능

`_build_and_deploy.ps1`에 Pre-flight 체크가 내장되어 있다. 빌드 시작 시 자동으로 실행:
- **Yellow** `[PRE-FLIGHT] Sources newer than DLL` → 수정된 파일 목록 출력, 이번 빌드에 포함됨
- **Green** `[PRE-FLIGHT] All sources up to date` → 소스 변경 없는 재빌드

빌드 창에서 `[PRE-FLIGHT]`가 **green**인데도 기능이 안 된다면 → 로직 버그 (빌드는 최신이지만 코드에 버그 있음).
`[PRE-FLIGHT]`가 **yellow**인데도 기능이 안 된다면 → 빌드 완료 후 테스트했는지 재확인.

### 설치 실패 0x80073CFF "Unsigned" — 인증서 신뢰 등록 누락

**증상**: Step 5 Install에서 0x80073CFF "패키지 원본 Unsigned" 오류로 설치 실패.

**원인**: 인증서가 시스템 신뢰 저장소에 없으면 `Add-AppxPackage`가 거부.

**해결**: `Add-AppxPackage` 호출 전에 인증서 신뢰 등록 필수:
```powershell
Import-Certificate -FilePath $cerPath -CertStoreLocation "Cert:\LocalMachine\Root" | Out-Null
Import-Certificate -FilePath $cerPath -CertStoreLocation "Cert:\LocalMachine\TrustedPeople" | Out-Null
```
CER 파일 경로 우선순위:
1. `D:\Dev\20_PC\ClickTerminal\ClickTerminalDev.cer`
2. `D:\Dev\20_PC\ClickTerminal\_msix_extract\ClickTerminalDev_new.cer`

### Add-AppxPackage 설치 방식 선택 — Developer Mode 여부에 따라 분기

**`-Register` (폴더 직접 등록)**: Developer Mode ON일 때만 작동. MSIX 패킹 불필요해서 빠름.
**MSIX 설치**: Developer Mode 상태 무관. makeappx + signtool 필요.

Developer Mode를 확인하지 않고 `-Register`를 먼저 시도하면 항상 0x80073CFF 에러가 출력됨 — 혼란스러우므로 반드시 분기 처리:
```powershell
$devMode = (Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\AppModelUnlock" -ErrorAction SilentlyContinue).AllowDevelopmentWithoutDevLicense -eq 1
if ($devMode) {
    # Try Add-AppxPackage -Register $manifest
}
if (-not $ok) {
    # makeappx pack + signtool sign + Add-AppxPackage -Path $msix
}
```
→ 이 PC는 Developer Mode OFF → 항상 MSIX 경로 사용.

---

### .bat 창이 스크립트 완료 후 즉시 닫히는 문제 — `-NoExit` 누락

**증상**: `_install_run.bat` 또는 `_build_and_deploy.bat` 실행 시 스크립트 마지막의 "Press any key to close..." 에서 키를 누르면 창이 즉시 닫힘.

**원인**: bat 파일이 `powershell -File script.ps1`을 `-NoExit` 없이 호출하면, ps1의 `ReadKey`가 완료되는 순간 PowerShell 프로세스가 종료되고 CMD 창도 같이 닫힌다.

**해결**: bat 파일에서 PowerShell 호출 시 반드시 `-NoExit` 포함:
```bat
@echo off
powershell -NoExit -ExecutionPolicy Bypass -NoProfile -File "%~dp0script.ps1"
```
→ 스크립트 종료 후 `PS>` 프롬프트에서 대기 → 사용자가 직접 창을 닫을 때까지 출력이 유지됨.

**현재 적용된 파일**: `_install_run.bat`, `_build_and_deploy.bat` (2026-05-03 수정)
**새 bat 파일 작성 시**: 위 패턴 반드시 적용.

---

### Settings / ContentDialog 디버그 로그

`%LOCALAPPDATA%\ClickTerminal\ctux-debug.log` 에서 확인:
- `[Settings] clicked` → 버튼 이벤트 정상 수신
- `[Settings] XamlRoot null` → XamlRoot 미설정 문제 (다이얼로그 표시 불가)
- `[Settings] dialog ctor threw` → AppxManifest InProcessServer 등록 누락
- `[Settings] showing dialog...` → 정상 경로
