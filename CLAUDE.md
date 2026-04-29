# ClickTerminal 프로젝트 지침

## 빌드 / 설치 규칙

### 설치 스크립트는 항상 .bat 파일로 만든다
- PowerShell(.ps1) 스크립트는 실행 즉시 창이 닫힘 → 사용자가 결과를 못 봄
- 반드시 `.bat` 파일로 만들고, 안에서 `powershell.exe -NoExit -Command "..."` 형태로 호출하거나 `pause` 명령을 마지막에 추가한다
- 관리자 권한 필요 시: `install-xxx.bat` 파일 안에서 UAC 자동 승격 처리

### 배치파일 기본 템플릿
```bat
@echo off
powershell -ExecutionPolicy Bypass -NoProfile -File "script.ps1"
```
- .bat 자체에는 한글 쓰지 말 것 (무조건 깨짐)
- PowerShell 스크립트(.ps1)도 콘솔 출력은 영어로만
- 관리자 권한은 .bat이 아닌 .ps1 파일 안에서 자체 승격 처리:
```powershell
if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Start-Process powershell -Verb RunAs -ArgumentList "-NoExit -ExecutionPolicy Bypass -File `"$PSCommandPath`""
    exit
}
```

### MSBuild 경로
`C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe`

### 빌드 대상
- 주 빌드: `src\cascadia\TerminalApp\TerminalAppLib.vcxproj` (에러 없이 빌드됨)
- 패키지: wapproj는 .NET SDK 없어서 빌드 불가 → makeappx 수동 재패키징 방식 사용
- makeappx 경로: `C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\makeappx.exe`
- signtool 경로: `C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\signtool.exe`

### 재패키징 절차 (DLL 교체 후)
1. `_msix_extract\pkg\` 에 기존 MSIX 압축 해제
2. `bin\x64\Release\WindowsTerminal\TerminalApp.dll` + `TerminalApp.winmd` 복사
3. AppxSignature.p7x / AppxBlockMap.xml 삭제
4. makeappx로 재패키징
5. signtool로 서명 (PFX: `ClickTerminalDev.pfx`, 비밀번호: `ctuxdev`)
6. `.bat` 설치 스크립트로 인증서 신뢰 등록 + Add-AppxPackage

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
