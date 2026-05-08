# ClickTerminal 개발 문서

ClickTerminal은 Windows Terminal 오픈소스를 기반으로 멀티 AI 도구 통합, 프로젝트 관리, 커스텀 테마를 추가한 포크 프로젝트입니다.

## 문서 목차

| 파일 | 내용 |
|------|------|
| [01-architecture.md](01-architecture.md) | 전체 아키텍처 및 디렉토리 구조 |
| [02-data-models.md](02-data-models.md) | 데이터 모델 (CTuxTheme, CTuxSettings, Project, AI 설정) |
| [03-ui-components.md](03-ui-components.md) | UI 컴포넌트 (ProjectSidebar, TabRowControl, Dialogs) |
| [04-build-deploy.md](04-build-deploy.md) | 빌드 및 배포 프로세스 전체 |
| [05-known-issues.md](05-known-issues.md) | 알려진 버그 패턴 및 해결책 |
| [06-feature-flows.md](06-feature-flows.md) | 기능별 코드 흐름 상세 |

## 빠른 참조

- **빌드**: `_build_and_deploy.bat` 실행
- **설치만**: `_install_run.bat` 실행
- **디버그 로그**: `%LOCALAPPDATA%\ClickTerminal\ctux-debug.log`
- **크래시 로그**: `%LOCALAPPDATA%\ClickTerminal\ctux-crash.log`
- **설정 파일**: `%LOCALAPPDATA%\ClickTerminal\ctux-settings.json`
- **프로젝트 목록**: `%LOCALAPPDATA%\ClickTerminal\clickterminal.json`

## 기술 스택

- **언어**: C++/WinRT, XAML (WinUI 2.x / MUX)
- **패키징**: MSIX (makeappx + signtool)
- **빌드**: MSBuild (VS 2022 BuildTools)
- **대상**: Windows 10/11 x64
