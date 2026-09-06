#pragma once

#include <string>

namespace ClickTerminal
{
    // ------------------------------------------------------------------
    // Claude Code status line (the bar Claude Code draws at the bottom of
    // the terminal: model, context usage, cost, rate limits).
    //
    // Claude Code has no built-in switch for this — it renders whatever the
    // `statusLine` command in ~/.claude/settings.json prints to stdout.
    // So "on" means: write our PowerShell script + point `statusLine` at it;
    // "off" means: remove the `statusLine` key again.
    //
    // Docs: https://code.claude.com/docs/en/statusline
    // ------------------------------------------------------------------
    namespace ClaudeStatusLine
    {
        // %USERPROFILE%\.claude — empty if USERPROFILE is unavailable.
        std::wstring ClaudeDir();

        // %USERPROFILE%\.claude\settings.json
        std::wstring SettingsPath();

        // %USERPROFILE%\.claude\ctux-statusline.ps1
        std::wstring ScriptPath();

        // True when settings.json has a `statusLine` whose command points at
        // our generated script.
        bool IsEnabled();

        // True when settings.json has a `statusLine` that is NOT ours —
        // enabling would replace it (we back it up and restore on disable).
        bool HasForeignStatusLine();

        // Writes the script, points `statusLine` at it, and preserves every
        // other key in settings.json. Any pre-existing foreign statusLine is
        // saved to ctux-statusline-backup.json first.
        // Returns false and fills `error` on failure.
        bool Enable(std::wstring& error);

        // Removes our `statusLine` key, restoring a previously backed-up
        // foreign one if there is one. The script file is left on disk.
        bool Disable(std::wstring& error);
    }
}
