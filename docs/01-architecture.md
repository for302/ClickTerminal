# 01 — 전체 아키텍처 및 디렉토리 구조

## 프로젝트 개요

ClickTerminal은 Microsoft Windows Terminal(`microsoft/terminal`) 오픈소스를 포크해 다음 기능을 추가한 개발자 도구입니다.

| 추가 기능 | 설명 |
|---------|------|
| **프로젝트 사이드바** | 개발 프로젝트를 폴더 단위로 관리하고 원클릭으로 터미널 세션을 여는 패널 |
| **멀티 AI 도구 통합** | Claude Code, Codex CLI, Gemini를 프로젝트별로 설정하고 실행 |
| **AI 컨텍스트 미터** | AI 도구의 토큰 사용량을 실시간으로 10-세그먼트 UI로 표시 |
| **커스텀 테마 시스템** | 탭바, 사이드바, 다이얼로그 전체에 적용되는 테마 엔진 |
| **설정 다이얼로그** | 테마 선택, 플러그인 토글 등을 위한 전용 설정 창 |

---

## 레이어 구조

```
┌─────────────────────────────────────────────────────┐
│                   MSIX 패키지                        │
│  CascadiaPackage (WindowsTerminalDev)                │
├──────────────────┬──────────────────────────────────┤
│   TerminalApp.dll│  TerminalApp.winmd               │
├──────────────────┴──────────────────────────────────┤
│  WinUI 레이어 (C++/WinRT + XAML)                    │
│  ┌────────────┐  ┌──────────────┐  ┌─────────────┐  │
│  │TerminalPage│  │ProjectSidebar│  │TabRowControl│  │
│  └────────────┘  └──────────────┘  └─────────────┘  │
│  ┌─────────────────┐  ┌──────────────────────────┐  │
│  │ AddProjectDialog│  │SettingsDialog/AISetupPage│  │
│  └─────────────────┘  └──────────────────────────┘  │
├─────────────────────────────────────────────────────┤
│  비즈니스 로직 레이어 (src/ClickTerminal/)           │
│  ┌──────────────┐  ┌───────────────┐               │
│  │ProjectManager│  │AIToolManager  │               │
│  └──────────────┘  └───────────────┘               │
│  ┌──────────────────────────────────┐               │
│  │CTuxSettings / CTuxTheme          │               │
│  └──────────────────────────────────┘               │
├─────────────────────────────────────────────────────┤
│  데이터 레이어 (JSON 파일, %LOCALAPPDATA%)           │
│  clickterminal.json   ctux-settings.json            │
└─────────────────────────────────────────────────────┘
```

---

## 디렉토리 구조

### 전체 루트

```
D:\Dev\02_PC\ClickTerminal\
├── src/
│   ├── cascadia/
│   │   ├── TerminalApp/          ← WinUI 컴포넌트 (메인 작업 폴더)
│   │   ├── CascadiaPackage/      ← MSIX 패키지 정의 (AppxManifest.xml)
│   │   └── TerminalControl/      ← 업스트림 터미널 컨트롤
│   └── ClickTerminal/            ← 비즈니스 로직 헤더/구현
├── bin/
│   └── x64/Release/TerminalApp/  ← 빌드 결과물 (DLL, winmd, pri)
├── _msix_extract/
│   └── pkg/                      ← MSIX 재패키징 작업 폴더
├── docs/                         ← 개발 문서 (이 폴더)
├── _build_and_deploy.bat         ← 빌드+설치 통합 스크립트
├── _build_and_deploy.ps1         ← 빌드+설치 PowerShell 구현
├── _install_run.bat              ← 설치 전용 스크립트
├── _install_run.ps1              ← 설치 PowerShell 구현
├── ClickTerminalDev.pfx          ← 서명용 인증서 (비밀번호: ctuxdev)
├── ClickTerminalDev.cer          ← 공개 인증서 (신뢰 등록용)
└── CLAUDE.md                     ← AI 어시스턴트용 프로젝트 가이드
```

---

### src/ClickTerminal/ — 비즈니스 로직

```
src/ClickTerminal/
├── CTuxTheme.h                 ← 테마 색상 구조체 및 빌트인 테마 5종
├── CTuxSettings.h              ← 전역 설정 구조체 (Load/Save/GetActiveTheme)
├── ProjectManager.h            ← 프로젝트 CRUD 및 JSON 직렬화
├── AIToolManager.h             ← AI 도구 감지, 실행 명령 구성, 세션 관리
├── ContextMeter.xaml           ← 컨텍스트 미터 UI (10 세그먼트)
├── ContextMeter.h              ← 컨텍스트 미터 WinRT 컴포넌트
├── AIToolManager.cpp           ← AIToolManager 구현
└── clickterminal.schema.json   ← clickterminal.json 스키마 정의
```

---

### src/cascadia/TerminalApp/ — WinUI 컴포넌트

#### ClickTerminal 추가 파일 (우리가 작성한 코드)

```
src/cascadia/TerminalApp/
├── ProjectSidebar.h            ← 사이드바 WinRT 컴포넌트 헤더
├── ProjectSidebar.cpp          ← 사이드바 구현 (카드 생성, 이벤트 처리)
├── ProjectSidebar.xaml         ← 사이드바 UI 레이아웃
├── ProjectSidebar.idl          ← WinRT 인터페이스 정의
│
├── AddProjectDialog.h          ← 프로젝트 추가/편집 다이얼로그 헤더
├── AddProjectDialog.cpp        ← 다이얼로그 구현 (포커스 처리, 폴더 선택)
├── AddProjectDialog.xaml       ← 다이얼로그 UI (ContentDialog)
├── AddProjectDialog.idl        ← WinRT 인터페이스 정의
│
├── SettingsDialog.h            ← 설정 다이얼로그 헤더
├── SettingsDialog.cpp          ← 설정 다이얼로그 구현
├── SettingsDialog.xaml         ← 설정 다이얼로그 UI
├── SettingsDialog.idl          ← WinRT 인터페이스 정의
│
├── AISetupPage.h               ← AI 설정 페이지 헤더
├── AISetupPage.cpp             ← AI 설정 페이지 구현
├── AISetupPage.xaml            ← AI 설정 UI
├── AISetupPage.idl             ← WinRT 인터페이스 정의
│
├── CTuxSettingsManager.cpp     ← 테마/설정 관리 (빌트인 테마, JSON 파싱)
│
└── TabRowControl.cpp           ← 탭 행 컨트롤 (ApplyTheme 포함)
    TabRowControl.h
    TabRowControl.xaml
    TabRowControl.idl
```

#### 업스트림 파일 (ClickTerminal 기능 연동 수정)

```
├── TerminalPage.cpp            ← 메인 페이지 (_CrashLog, ProjectSidebar 통합)
├── TerminalPage.h              ← 메인 페이지 헤더
└── TerminalPage.xaml           ← 메인 레이아웃 (사이드바 패널 포함)
```

---

### src/cascadia/CascadiaPackage/ — MSIX 패키지 정의

```
src/cascadia/CascadiaPackage/
├── Package.appxmanifest         ← 원본 매니페스트 (소스)
└── obj/x64/Release/
    ├── priconfig.xml            ← resources.pri 생성 설정 (빌드 생성물)
    └── pri.resfiles             ← PRI 병합 목록
```

---

### _msix_extract/ — 재패키징 작업 영역

```
_msix_extract/
├── pkg/                         ← MSIX 압축 해제 폴더
│   ├── AppxManifest.xml         ← 실제 배포 매니페스트 (수동 편집)
│   ├── resources.pri            ← 리소스 인덱스 (makepri로 재생성)
│   ├── TerminalApp/
│   │   ├── TerminalApp.dll      ← 빌드 후 복사된 DLL
│   │   └── TerminalApp.winmd    ← WinRT 메타데이터
│   └── ...
└── CascadiaPackage_new.msix     ← makeappx 결과물
```

---

## 패키지 정보

| 항목 | 값 |
|------|-----|
| PackageFamilyName | `WindowsTerminalDev_xpqk32cx38ema` |
| Package Name | `WindowsTerminalDev` |
| Publisher | `CN=ClickTerminalDev` |
| PFX 파일 | `ClickTerminalDev.pfx` |
| PFX 비밀번호 | `ctuxdev` |
| CER 파일 | `ClickTerminalDev.cer` |

---

## 등록된 WinRT 활성화 클래스 (AppxManifest.xml)

`_msix_extract/pkg/AppxManifest.xml`의 `<InProcessServer>` 블록에 등록된 타입들:

```xml
<ActivatableClass ActivatableClassId="TerminalApp.ContextMeter"     ThreadingModel="both" />
<ActivatableClass ActivatableClassId="TerminalApp.SettingsDialog"   ThreadingModel="both" />
<ActivatableClass ActivatableClassId="TerminalApp.ProjectSidebar"   ThreadingModel="both" />
<ActivatableClass ActivatableClassId="TerminalApp.AddProjectDialog" ThreadingModel="both" />
<ActivatableClass ActivatableClassId="TerminalApp.AISetupPage"      ThreadingModel="both" />
<ActivatableClass ActivatableClassId="TerminalApp.TabRowControl"    ThreadingModel="both" />
```

> **중요**: 새로운 WinRT 컴포넌트를 추가할 때는 반드시 이 목록에 추가해야 한다. 누락 시 `winrt::terminate()` → `0xc0000409 FAST_FAIL` 크래시 발생.
