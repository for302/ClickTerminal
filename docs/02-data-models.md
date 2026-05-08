# 02 — 데이터 모델

모든 데이터 모델은 `src/ClickTerminal/` 하위의 헤더 파일에 정의되며, JSON 직렬화/역직렬화를 통해 `%LOCALAPPDATA%\ClickTerminal\` 에 저장됩니다.

---

## CTuxTheme — 테마 색상 정의

**파일**: `src/ClickTerminal/CTuxTheme.h`

### CTuxThemeColors 구조체

UI 전체에 적용되는 색상 팔레트입니다. 모든 값은 `#RRGGBB` 또는 `#AARRGGBB` 형식의 `std::wstring`.

| 필드 | 기본값 (CTux Dark) | 설명 |
|------|-------------------|------|
| `SidebarBg` | `#252535` | 사이드바 배경 |
| `SidebarHeaderBg` | `#1A1A2A` | 사이드바 헤더 배경 |
| `SidebarText` | `#CDD6F4` | 사이드바 기본 텍스트 |
| `SidebarTextMuted` | `#7C7F93` | 사이드바 보조 텍스트 |
| `TabBarBg` | `#1A1A2A` | 탭바 배경 |
| `DialogBg` | `#2A2A3A` | 다이얼로그 배경 |
| `DialogNavBg` | `#1F1F2F` | 다이얼로그 네비게이션 배경 |
| `TerminalScheme` | `CTux Dark` | 터미널 색상 스킴 이름 |

### CTuxTheme 구조체

```cpp
struct CTuxTheme {
    std::wstring     Name;          // 테마 이름 ("CTux Dark" 등)
    bool             IsBuiltIn;     // true = 빌트인, false = 사용자 정의
    bool             IsLightMode;   // true = 라이트 모드 (ElementTheme::Light)
    CTuxThemeColors  Colors;        // 위 색상 팔레트
};
```

### 빌트인 테마 5종

`CTuxSettingsManager.cpp`의 `GetBuiltInThemes()` 함수에 하드코딩되어 있습니다.

| 이름 | 모드 | 주요 색상 특징 |
|------|------|---------------|
| `CTux Dark` | Dark | 보라-다크. SidebarBg: #252535 |
| `CTux Light` | Light | 밝은 회청색. SidebarBg: #F0F0FA |
| `Mocha` | Dark | 따뜻한 갈색. SidebarBg: #1E1E2E |
| `Ocean` | Dark | 딥블루. SidebarBg: #0D1117 |
| `Forest` | Dark | 다크그린. SidebarBg: #1A2015 |

### 관련 함수 (`CTuxSettingsManager.cpp`)

```cpp
// 5개 빌트인 테마 반환
std::vector<CTuxTheme> GetBuiltInThemes();

// JSON → CTuxThemeColors 변환
CTuxThemeColors ParseThemeColors(const nlohmann::json& j);

// CTuxThemeColors → JSON 변환
nlohmann::json SerializeThemeColors(const CTuxThemeColors& c);

// 누락된 색상 필드를 기본값으로 채움
void FillMissingColors(CTuxThemeColors& colors, bool isLight);
```

---

## CTuxSettings — 전역 설정

**파일**: `src/ClickTerminal/CTuxSettings.h`  
**저장 경로**: `%USERPROFILE%\AppData\Local\ClickTerminal\ctux-settings.json`

> **주의**: `%LOCALAPPDATA%`는 MSIX 가상화로 리다이렉트되므로 반드시 `%USERPROFILE%\AppData\Local`을 직접 구성해야 합니다.

### CTuxSettings 구조체

```cpp
struct CTuxSettings {
    // 핵심 설정
    std::wstring             ProjectsConfigPath;   // clickterminal.json 경로
    std::wstring             SelectedThemeName;    // 현재 테마명 (기본: "CTux Dark")
    std::wstring             PluginsFolder;        // 플러그인 폴더 (옵션)

    // 플러그인 토글
    bool                     GitPluginEnabled;     // Git 정보 표시
    bool                     PortPluginEnabled;    // 포트 모니터링

    // 사용자 정의 테마
    std::vector<CTuxTheme>   CustomThemes;         // 빌트인이 아닌 사용자 테마

    // 메서드
    CTuxTheme        GetActiveTheme() const;       // 현재 활성 테마 반환
    std::wstring     ThemeString() const;          // "dark" 또는 "light"
    static CTuxSettings Load();                    // JSON에서 로드
    void             Save() const;                 // JSON으로 저장
    static std::wstring GetDefaultDir();           // 기본 저장 디렉토리
};
```

### ctux-settings.json 스키마 (v3)

```json
{
  "schemaVersion": 3,
  "selectedThemeName": "CTux Dark",
  "projectsConfigPath": "C:\\Users\\user\\AppData\\Local\\ClickTerminal\\clickterminal.json",
  "pluginsFolder": "",
  "gitPluginEnabled": false,
  "portPluginEnabled": false,
  "customThemes": [
    {
      "name": "My Theme",
      "isLightMode": false,
      "colors": {
        "sidebarBg": "#202030",
        "sidebarHeaderBg": "#151525",
        "sidebarText": "#CCDDFF",
        "sidebarTextMuted": "#667799",
        "tabBarBg": "#151525",
        "dialogBg": "#252535",
        "dialogNavBg": "#1A1A2A",
        "terminalScheme": "One Half Dark"
      }
    }
  ]
}
```

### 스키마 버전 마이그레이션

| 버전 | 추가 필드 |
|------|---------|
| v1 | `theme`, `projectsConfigPath` |
| v2 | `selectedThemeName`, `customThemes`, `pluginsFolder` |
| v3 | `schemaVersion`, 색상 폴백 로직 추가 |

---

## Project — 프로젝트 구조체

**파일**: `src/ClickTerminal/ProjectManager.h`  
**저장 경로**: `%USERPROFILE%\AppData\Local\ClickTerminal\clickterminal.json`

### 열거형

```cpp
enum class ProjectType : uint8_t {
    Web        = 0,    // 웹 프로젝트
    App        = 1,    // 데스크탑/네이티브 앱
    AIWorkflow = 2     // AI 워크플로우
};

enum class ProjectManagerError : uint8_t {
    None        = 0,
    FileNotFound = 1,
    ParseError  = 2,
    WriteError  = 3,
    InvalidId   = 4,
    Duplicate   = 5
};
```

### ProjectUrl 구조체

```cpp
struct ProjectUrl {
    std::wstring Label;        // 표시 이름 ("App", "API" 등)
    std::wstring Url;          // URL
    bool         OpenOnStart;  // 프로젝트 열 때 브라우저에서 자동 열기
};
```

### ProjectAIConfig 구조체

```cpp
struct ClaudeConfig {
    bool                  Enabled;
    ClaudePermissionMode  PermissionMode;   // Default/AcceptEdits/BypassPermissions/Plan/Auto
    std::wstring          Model;            // "claude-sonnet-4-6" 등
    bool                  DangerouslySkipPermissions;
    std::wstring          McpConfigPath;    // MCP 서버 설정 경로
    std::wstring          AppendSystemPrompt;
    std::vector<wstring>  AddDirs;          // 추가 컨텍스트 폴더
};

struct CodexConfig {
    bool              Enabled;
    CodexApprovalMode ApprovalMode;  // Suggest/AutoEdit/FullAuto
    std::wstring      Model;         // "gpt-5.4" 등
};

struct GeminiConfig {
    bool         Enabled;
    std::wstring Model;  // "gemini-2.5-pro" 등
};

struct ProjectAIConfig {
    std::wstring  DefaultTool;   // "claude" | "codex" | "gemini" | ""
    ClaudeConfig  Claude;
    CodexConfig   Codex;
    GeminiConfig  Gemini;
};
```

### Project 구조체 (전체 필드)

```cpp
struct Project {
    std::wstring                           Id;               // "proj-{uuid4}"
    std::wstring                           Name;             // 표시 이름
    ProjectType                            Type;             // Web/App/AIWorkflow
    std::wstring                           FolderPath;       // 루트 폴더 경로
    std::wstring                           ColorScheme;      // 터미널 색상 스킴
    std::wstring                           StartupCommand;   // 시작 명령어
    std::wstring                           Icon;             // 아이콘 경로 (옵션)
    std::wstring                           TerminalProfileGuid; // 터미널 프로파일 GUID
    std::vector<std::wstring>              Tags;             // 태그 목록
    std::wstring                           CreatedAt;        // ISO8601 타임스탬프
    std::wstring                           LastOpenedAt;     // ISO8601 타임스탬프
    std::vector<int32_t>                   Ports;            // 사용 포트 목록
    std::vector<ProjectUrl>                Urls;             // 관련 URL 목록
    ProjectAIConfig                        AIConfig;         // AI 도구 설정
    std::vector<std::pair<wstring,wstring>> Env;             // 환경변수 {"KEY","VALUE"}
};
```

### clickterminal.json 스키마 (v1)

```json
{
  "$version": 1,
  "projects": [
    {
      "id": "proj-550e8400-e29b-41d4-a716-446655440000",
      "name": "MyWebApp",
      "type": "web",
      "folderPath": "C:\\Dev\\MyWebApp",
      "colorScheme": "CTux Dark",
      "startupCommand": "cmd.exe",
      "icon": "",
      "terminalProfileGuid": "{00000000-0000-0000-0000-000000000000}",
      "tags": ["react", "typescript"],
      "createdAt": "2026-01-01T00:00:00Z",
      "lastOpenedAt": "2026-05-01T12:00:00Z",
      "ports": [3000, 3001],
      "urls": [
        { "label": "App", "url": "http://localhost:3000", "openOnStart": true }
      ],
      "aiTool": {
        "defaultTool": "claude",
        "claude": {
          "enabled": true,
          "permissionMode": "default",
          "model": "claude-sonnet-4-6",
          "dangerouslySkipPermissions": false,
          "mcpConfigPath": "",
          "appendSystemPrompt": "",
          "addDirs": []
        },
        "codex": {
          "enabled": false,
          "approvalMode": "suggest",
          "model": "gpt-5.4"
        },
        "gemini": {
          "enabled": false,
          "model": "gemini-2.5-pro"
        }
      },
      "env": {
        "NODE_ENV": "development"
      }
    }
  ]
}
```

---

## ProjectManager — 프로젝트 CRUD

**파일**: `src/ClickTerminal/ProjectManager.h` / `src/cascadia/TerminalApp/ProjectManager.cpp`

### 템플릿 래퍼

```cpp
template<typename T>
struct ProjectResult {
    T                    Value;
    ProjectManagerError  Error;
    bool Ok() const { return Error == ProjectManagerError::None; }
};
```

### ProjectManager 클래스 전체 인터페이스

```cpp
class ProjectManager {
public:
    // 생성
    explicit ProjectManager(std::wstring configPath = L"");

    // 로드/저장
    ProjectResult<bool>     LoadProjects();
    ProjectResult<bool>     SaveProjects();

    // CRUD
    ProjectResult<Project>  AddProject(Project project);
    ProjectResult<Project>  UpdateProject(const Project& project);
    ProjectResult<bool>     RemoveProject(const std::wstring& id);

    // 조회
    std::optional<Project>  GetProjectById(const std::wstring& id) const;
    std::optional<Project>  GetProjectByPath(const std::wstring& path) const;
    std::vector<Project>    GetAllProjects() const;
    std::vector<Project>    GetProjectsByType(ProjectType type) const;

    // 메타데이터 갱신
    ProjectResult<bool>     TouchProject(const std::wstring& id); // lastOpenedAt 갱신

    // 자동 감지
    std::vector<Project>    AutoDetectProjects(
                                const std::vector<std::wstring>& searchPaths,
                                int maxDepth = 3);

    // 환경변수 전개 ($env:VAR → 실제 값)
    std::vector<std::pair<std::wstring,std::wstring>>
                            ResolveEnv(const Project& project) const;

    // 변경 콜백
    std::function<void()>   OnProjectsChanged;

private:
    std::wstring             _configPath;
    std::vector<Project>     _projects;
    std::wstring             _GenerateId() const;   // "proj-{uuid4}"
    bool                     _IsDuplicate(const std::wstring& path) const;
};
```

### 주요 구현 세부사항 (`ProjectManager.cpp`)

**LoadProjects()**
1. `_configPath` 없으면 `CTuxSettings::GetDefaultDir() + L"\\clickterminal.json"` 사용
2. 파일 없으면 `FileNotFound` 반환 (에러가 아닌 정상 케이스 — 첫 실행)
3. JSON 파싱 실패 시 `ParseError` 반환

**SaveProjects()**
1. 부모 디렉토리 `CreateDirectoryW` 호출 (존재해도 무시)
2. `.tmp` 파일에 먼저 쓰기
3. `MoveFileExW(tmp, dest, MOVEFILE_REPLACE_EXISTING)` — 원자적 교체
4. `OnProjectsChanged` 콜백 호출

**AutoDetectProjects()**  
검색 경로를 재귀 탐색하며 다음 파일 중 하나가 있으면 프로젝트로 인식:
- `.git/` 폴더 → Git 저장소
- `package.json` → Node.js 프로젝트
- `*.sln` → Visual Studio 솔루션

---

## AIToolManager — AI 도구 관리

**파일**: `src/ClickTerminal/AIToolManager.h` / `AIToolManager.cpp`

### 열거형

```cpp
enum class AITool : uint8_t {
    Claude = 0,
    Codex  = 1,
    Gemini = 2
};

enum class AuthType : uint8_t {
    OAuth  = 0,   // Claude Code: OAuth 브라우저 로그인
    ApiKey = 1,   // API 키 파일
    None   = 2    // 인증 불필요
};

enum class ClaudePermissionMode : uint8_t {
    Default             = 0,
    AcceptEdits         = 1,
    BypassPermissions   = 2,
    Plan                = 3,
    Auto                = 4
};

enum class CodexApprovalMode : uint8_t {
    Suggest  = 0,   // 수동 승인
    AutoEdit = 1,   // 파일 편집 자동
    FullAuto = 2    // 완전 자동
};
```

### AIToolConfig 구조체

```cpp
struct AIToolConfig {
    AITool        Tool;
    bool          Installed;
    std::wstring  ExecutablePath;     // which claude / where claude
    std::wstring  Version;            // claude --version 결과
    AuthType      Auth;
    std::wstring  AuthConfigPath;     // 인증 설정 파일 경로
    uint32_t      ContextWindowTokens; // 최대 컨텍스트 크기 (토큰)
    bool          ContextMeterEnabled;

    // 도구별 기본 설정
    ClaudeConfig  ClaudeDefaults;
    CodexConfig   CodexDefaults;
    GeminiConfig  GeminiDefaults;
};
```

### ContextUsage 구조체

```cpp
struct ContextUsage {
    uint32_t UsedTokens;
    uint32_t TotalTokens;

    float   FillRatio() const;        // 0.0~1.0
    uint8_t FilledSegments() const;   // 0~10 (ContextMeter 렌더링용)
};
```

### AIToolManager 클래스 인터페이스

```cpp
class AIToolManager {
public:
    void                          DetectInstalled();           // 모든 도구 감지
    bool                          RefreshTool(AITool tool);   // 특정 도구 재감지

    std::optional<AIToolConfig>   GetToolConfig(AITool tool) const;
    std::vector<AIToolConfig>     GetAllInstalledTools() const;
    bool                          IsInstalled(AITool tool) const;

    // 실행 명령 구성 (프로젝트 설정 + 도구 설정 조합)
    LaunchCommand                 GetLaunchCommand(
                                    const Project& project,
                                    AITool tool,
                                    bool skipEnabled = false);

    // PTY 세션 관리
    void  RegisterSession(const std::wstring& sessionId, AITool tool, HANDLE ptyHandle);
    void  UnregisterSession(const std::wstring& sessionId);
    bool  SendExitCommand(const std::wstring& sessionId); // stdin에 "/exit\n" 전송

    // 컨텍스트 파싱 (PTY stdout 라인 파싱)
    std::optional<ContextUsage>   ParseContextUsageLine(
                                    AITool tool,
                                    const std::wstring& line);

    // 변경 콜백
    std::function<void()>         OnToolDetectionChanged;
};
```

### LaunchCommand 구조

```cpp
struct LaunchCommand {
    std::wstring              Executable;   // "claude", "codex", "gemini"
    std::vector<std::wstring> Args;         // 커맨드라인 인자
    std::wstring              WorkingDir;   // 프로젝트 폴더
    std::vector<std::pair<std::wstring,std::wstring>> Env; // 추가 환경변수
};
```

Claude Code의 경우 생성되는 명령 예시:
```
claude --permission-mode default --model claude-sonnet-4-6 --add-dir src
```
