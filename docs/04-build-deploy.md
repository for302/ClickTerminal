# 04 — 빌드 및 배포 프로세스

---

## 전체 흐름 개요

```
소스 수정
    ↓
_build_and_deploy.bat   (또는 bat 없이 ps1 직접)
    ↓
[1/5] MSBuild 컴파일
    → TerminalApp.dll, TerminalApp.winmd, TerminalApp.pri 생성
    ↓
[2/5] DLL 복사
    → _msix_extract/pkg/TerminalApp/ 에 복사
    ↓
[3/5] resources.pri 재생성
    → makepri new → _msix_extract/pkg/resources.pri
    ↓
[4/5] 서명 파일 정리
    → AppxSignature.p7x, AppxBlockMap.xml 삭제
    ↓
[5/5] 인증서 신뢰 등록 + 패키지 설치
    → makeappx pack → signtool sign → Add-AppxPackage
```

---

## 빌드 스크립트 파일

| 파일 | 역할 |
|------|------|
| `_build_and_deploy.bat` | CMD에서 실행하는 진입점 (`-NoExit`로 PS 창 유지) |
| `_build_and_deploy.ps1` | 실제 빌드 + 배포 로직 (5단계) |
| `_install_run.bat` | 설치 전용 진입점 |
| `_install_run.ps1` | 인증서 등록 + 설치 로직 |

### .bat 파일 기본 형식

```bat
@echo off
powershell -NoExit -ExecutionPolicy Bypass -NoProfile -File "%~dp0script.ps1"
```

> **주의**: `-NoExit` 없이는 ps1의 `ReadKey` 완료 후 창이 즉시 닫힙니다.

### .ps1 파일 종료 패턴

```powershell
Write-Host "Press any key to close..."
$null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
Stop-Process -Id $PID   # 창을 직접 종료
```

---

## 도구 경로

| 도구 | 경로 |
|------|------|
| MSBuild | `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe` |
| makepri | `C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\makepri.exe` |
| makeappx | `C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\makeappx.exe` |
| signtool | `C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\signtool.exe` |

---

## [1/5] MSBuild 컴파일

### ⚠️ 반드시 TerminalApp.vcxproj (dll 서브폴더)를 빌드할 것

```
src\cascadia\TerminalApp\
├── TerminalAppLib.vcxproj   ← StaticLibrary (.lib만 생성, DLL 갱신 안 됨)  ← 잘못된 대상
└── dll\
    └── TerminalApp.vcxproj  ← DynamicLibrary (TerminalApp.dll 생성)        ← 올바른 대상
```

`TerminalApp.vcxproj`가 `TerminalAppLib.vcxproj`를 ProjectReference로 참조하므로,
`TerminalApp.vcxproj` 하나만 빌드하면 MSBuild가 두 프로젝트를 올바른 순서로 자동 빌드한다.

**과거에 TerminalAppLib.vcxproj만 빌드했을 때의 증상:**
- 빌드 로그에 "Build OK" 표시됨
- DLL 복사도 정상 완료됨
- 설치도 성공함
- BUT: `TerminalApp.dll` 타임스탬프가 이전 빌드 날짜 그대로 (코드가 반영 안 됨)

### 명령

```powershell
$msbuild = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
$vcxproj = "D:\Dev\02_PC\ClickTerminal\src\cascadia\TerminalApp\dll\TerminalApp.vcxproj"

& $msbuild $vcxproj `
    /p:Configuration=Release `
    /p:Platform=x64 `
    /p:SolutionDir="D:\Dev\02_PC\ClickTerminal\" `
    /t:Build `
    /m `
    /nologo `
    /verbosity:minimal
```

### `/p:SolutionDir` 필수 이유 (MSB4019)

`.sln` 없이 `.vcxproj`만 빌드하면 `$(SolutionDir)`이 vcxproj 디렉토리로 설정됩니다.

```
❌ SolutionDir = D:\Dev\02_PC\ClickTerminal\src\cascadia\TerminalApp\
   → CollectWildcardResources.targets를
     TerminalApp\build\rules\ 에서 찾음 → 없음 → MSB4019 에러

✅ /p:SolutionDir="D:\Dev\02_PC\ClickTerminal\"
   → CollectWildcardResources.targets를
     D:\Dev\02_PC\ClickTerminal\build\rules\ 에서 찾음 → 정상
```

### 빌드 결과물

```
bin/x64/Release/TerminalApp/
├── TerminalApp.dll     ← 주요 바이너리
├── TerminalApp.winmd   ← WinRT 메타데이터
└── TerminalApp.pri     ← 컴포넌트별 리소스 인덱스
```

### Pre-flight 체크 (빌드 전 자동 실행)

```powershell
# 소스 타임스탬프 vs DLL 타임스탬프 비교
$dllTime = (Get-Item "bin\x64\Release\TerminalApp\TerminalApp.dll").LastWriteTime
$sources = Get-ChildItem "src\cascadia\TerminalApp\*.cpp","src\cascadia\TerminalApp\*.h",
                          "src\ClickTerminal\*.h","src\ClickTerminal\*.cpp"
$newerSources = $sources | Where-Object { $_.LastWriteTime -gt $dllTime }

if ($newerSources) {
    Write-Host "[PRE-FLIGHT] Sources newer than DLL:" -ForegroundColor Yellow
    $newerSources | ForEach-Object { Write-Host "  $($_.Name)" }
} else {
    Write-Host "[PRE-FLIGHT] All sources up to date" -ForegroundColor Green
}
```

---

## [2/5] DLL 복사

```powershell
$src = "D:\Dev\02_PC\ClickTerminal\bin\x64\Release\TerminalApp\"
$dst = "D:\Dev\02_PC\ClickTerminal\_msix_extract\pkg\TerminalApp\"

Copy-Item "$src\TerminalApp.dll"  $dst -Force
Copy-Item "$src\TerminalApp.winmd" $dst -Force
```

---

## [3/5] resources.pri 재생성

### 왜 필수인가

`resources.pri`는 모든 XAML을 컴파일한 XBF(XAML Binary Format)를 Base64로 직접 내장합니다.

```
DLL만 교체하면:
  ┌─────────────────────┐   ┌────────────────────────┐
  │ TerminalApp.dll     │   │ resources.pri           │
  │ (새 코드)           │   │ (구 XBF)               │
  └─────────────────────┘   └────────────────────────┘
          ↓                           ↓
  InitializeComponent()     Application::LoadComponent()
      (새 버전)               (옛날 XBF 로드 시도)
                               → FAST_FAIL 크래시!
```

XAML을 건드리지 않았더라도 DLL 교체 시 XBF 불일치가 발생할 수 있으므로 **항상 재생성**합니다.

### 명령

```powershell
Set-Location "D:\Dev\02_PC\ClickTerminal\src\cascadia\CascadiaPackage"

& "C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\makepri.exe" `
    new `
    /pr "D:\Dev\02_PC\ClickTerminal\src\cascadia\CascadiaPackage" `
    /cf "obj\x64\Release\priconfig.xml" `
    /o `
    /of "D:\Dev\02_PC\ClickTerminal\_msix_extract\pkg\resources.pri"
```

| 인자 | 설명 |
|------|------|
| `new` | 새 PRI 생성 (기존 덮어쓰기) |
| `/pr` | 프로젝트 루트 (priconfig.xml 기준 디렉토리) |
| `/cf` | PRI 설정 파일 (obj/x64/Release/priconfig.xml) |
| `/o` | 이미 존재해도 덮어쓰기 |
| `/of` | 출력 파일 경로 |

`priconfig.xml`은 `pri.resfiles`를 읽어 모든 컴포넌트의 PRI를 병합합니다.  
`TerminalApp.pri`는 `bin\x64\Release\TerminalApp\TerminalApp.pri`에서 참조됩니다.

---

## [4/5] 서명 파일 정리

재패키징 전 구 서명 파일을 삭제합니다.

```powershell
Remove-Item "_msix_extract\pkg\AppxSignature.p7x" -ErrorAction SilentlyContinue
Remove-Item "_msix_extract\pkg\AppxBlockMap.xml"  -ErrorAction SilentlyContinue
```

---

## [5/5] 인증서 신뢰 등록 + 패키지 설치

### 인증서 등록

```powershell
$cerPath = "D:\Dev\02_PC\ClickTerminal\ClickTerminalDev.cer"
# 대안 경로
# $cerPath = "D:\Dev\02_PC\ClickTerminal\_msix_extract\ClickTerminalDev_new.cer"

Import-Certificate -FilePath $cerPath -CertStoreLocation "Cert:\LocalMachine\Root" | Out-Null
Import-Certificate -FilePath $cerPath -CertStoreLocation "Cert:\LocalMachine\TrustedPeople" | Out-Null
```

### Developer Mode 분기

```powershell
$devModeKey = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\AppModelUnlock"
$devMode = (Get-ItemProperty $devModeKey -ErrorAction SilentlyContinue)
    .AllowDevelopmentWithoutDevLicense -eq 1

$ok = $false

if ($devMode) {
    # 폴더 직접 등록 (빠름, Developer Mode 전용)
    try {
        $manifest = "D:\Dev\02_PC\ClickTerminal\_msix_extract\pkg\AppxManifest.xml"
        Add-AppxPackage -Register $manifest -ForceApplicationShutdown
        $ok = $true
    } catch { }
}

if (-not $ok) {
    # MSIX 패킹 → 서명 → 설치 (Developer Mode 불필요)
    _PackAndInstall
}
```

> **이 PC**: Developer Mode OFF → 항상 MSIX 경로 사용.

### makeappx — MSIX 패킹

```powershell
Set-Location "D:\Dev\02_PC\ClickTerminal\_msix_extract"

& "C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\makeappx.exe" `
    pack `
    /d pkg `
    /p CascadiaPackage_new.msix `
    /nv `
    /o
```

| 인자 | 설명 |
|------|------|
| `pack` | 패킹 모드 |
| `/d pkg` | 소스 폴더 |
| `/p CascadiaPackage_new.msix` | 출력 MSIX |
| `/nv` | 유효성 검사 건너뜀 |
| `/o` | 덮어쓰기 |

### signtool — MSIX 서명

```powershell
& "C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\signtool.exe" `
    sign `
    /fd SHA256 `
    /f "..\ClickTerminalDev.pfx" `
    /p ctuxdev `
    CascadiaPackage_new.msix
```

**절대 `/p7`, `/p7co`, `/p7ce` 플래그를 추가하지 말 것.**  
이 플래그들은 서명을 별도 파일로 분리하여 MSIX 내부에 `AppxSignature.p7x`가 생성되지 않습니다.  
결과: `Add-AppxPackage`가 `0x800B0100 "서명 없음"` 오류로 설치 실패.

### Add-AppxPackage — 설치

```powershell
Add-AppxPackage -Path "D:\Dev\02_PC\ClickTerminal\_msix_extract\CascadiaPackage_new.msix" `
    -ForceApplicationShutdown
```

---

## _install_run.ps1 — 설치 전용 스크립트

빌드 없이 현재 `_msix_extract/pkg/` 상태로 재설치할 때 사용합니다.

```
[1/4] 인증서 신뢰 등록
[2/4] 기존 패키지 제거 + 실행 중 프로세스 종료
      Get-AppxPackage -AllUsers -Name "*WindowsTerminalDev*" | Remove-AppxPackage
      3초 대기
[3/4] 패키지 설치 (Developer Mode 분기 동일)
[4/4] 검증
      - 패키지 존재 확인
      - clickterminal.json, ctux-settings.json 존재 확인
```

---

## AppxManifest.xml 관리

**경로**: `_msix_extract/pkg/AppxManifest.xml`

새로운 WinRT 컴포넌트를 추가할 때마다 `<InProcessServer>` 블록에 항목을 추가해야 합니다.

```xml
<Extensions>
  <Extension Category="windows.activatableClass.inProcessServer">
    <InProcessServer>
      <Path>TerminalApp\TerminalApp.dll</Path>
      
      <!-- 기존 등록 타입 -->
      <ActivatableClass ActivatableClassId="TerminalApp.ContextMeter"
                        ThreadingModel="both" />
      <ActivatableClass ActivatableClassId="TerminalApp.SettingsDialog"
                        ThreadingModel="both" />
      <ActivatableClass ActivatableClassId="TerminalApp.ProjectSidebar"
                        ThreadingModel="both" />
      <ActivatableClass ActivatableClassId="TerminalApp.AddProjectDialog"
                        ThreadingModel="both" />
      <ActivatableClass ActivatableClassId="TerminalApp.AISetupPage"
                        ThreadingModel="both" />
      <ActivatableClass ActivatableClassId="TerminalApp.TabRowControl"
                        ThreadingModel="both" />

      <!-- 새 타입 추가 시 여기에 추가 -->
    </InProcessServer>
  </Extension>
</Extensions>
```

누락 시 앱 시작 때 `winrt::terminate()` → `0xc0000409 FAST_FAIL_FATAL_APP_EXIT` 크래시.

---

## 설치 실패 진단

### 0x80073CFF "패키지 원본 Unsigned"

```
원인: 인증서가 시스템 신뢰 저장소에 없음
해결: Import-Certificate를 Root 및 TrustedPeople 양쪽에 실행
```

### 0x800B0100 "서명 없음"

```
원인: signtool에 /p7 계열 플래그를 사용함
해결: 플래그 없이 signtool sign /fd SHA256 /f pfx /p pw 형식으로 재서명
```

### FAST_FAIL 크래시 (0xc0000409)

```
진단: ctux-crash.log 확인
  - "[TabRow] InitializeComponent OK"가 없음
    → resources.pri XBF 불일치 → resources.pri 재생성 필요
  - "[TermPage] ctor start"도 없음
    → AppxManifest InProcessServer 등록 누락 (winrt::terminate)
```

---

## 소스 수정 후 체크리스트

```
□ 소스 파일 수정 완료
□ _build_and_deploy.bat 실행
□ Pre-flight에서 수정된 파일 목록 확인
□ MSBuild 성공 (에러 없음)
□ 설치 성공 메시지 확인
□ 앱 실행 후 변경사항 테스트
□ ctux-crash.log에 새 에러 없음 확인
```
