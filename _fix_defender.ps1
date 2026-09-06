# Fix Windows Defender false positive on OpenConsole.exe (Trojan:Win32/Cloxer)
# Requires administrator - self-elevates below.

if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Start-Process powershell -Verb RunAs -ArgumentList "-ExecutionPolicy Bypass -NoProfile -File `"$PSCommandPath`""
    exit
}

$ErrorActionPreference = "Continue"

Write-Host "================================================" -ForegroundColor Cyan
Write-Host " ClickTerminal - Defender False Positive Fix" -ForegroundColor Cyan
Write-Host "================================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Threat  : Trojan:Win32/Cloxer (ID 2147726362)"
Write-Host "File    : OpenConsole.exe (Windows Terminal ConPTY host)"
Write-Host "Verdict : FALSE POSITIVE - unmodified upstream binary"
Write-Host ""

$paths = @(
    "C:\Program Files\WindowsApps\WindowsTerminalDev_0.0.1.0_x64__xpqk32cx38ema",
    "D:\Dev\20_PC\ClickTerminal\bin\x64\Release",
    "D:\Dev\20_PC\ClickTerminal\_msix_extract",
    "D:\Dev\20_PC\ClickTerminal\src\cascadia\CascadiaPackage\bin\x64\Release"
)

# ---- Step 1: add exclusions -------------------------------------------------
Write-Host "[1/4] Adding Defender exclusion paths..." -ForegroundColor Yellow
$existing = (Get-MpPreference).ExclusionPath
foreach ($p in $paths) {
    if ($existing -contains $p) {
        Write-Host "      already excluded : $p" -ForegroundColor DarkGray
    } else {
        try {
            Add-MpPreference -ExclusionPath $p -ErrorAction Stop
            Write-Host "      added            : $p" -ForegroundColor Green
        } catch {
            Write-Host "      FAILED           : $p" -ForegroundColor Red
            Write-Host "      $($_.Exception.Message)" -ForegroundColor Red
        }
    }
}
Write-Host ""

# ---- Step 2: clear quarantine / threat history ------------------------------
Write-Host "[2/4] Clearing threat history for this detection..." -ForegroundColor Yellow
try {
    & "$env:ProgramData\Microsoft\Windows Defender\Platform\*\MpCmdRun.exe" -Restore -Name "Trojan:Win32/Cloxer" -All 2>&1 | Out-String | Write-Host
} catch {
    Write-Host "      restore skipped (nothing quarantined)" -ForegroundColor DarkGray
}
try { Remove-MpThreat -ErrorAction Stop } catch { Write-Host "      Remove-MpThreat: nothing active" -ForegroundColor DarkGray }
Write-Host ""

# ---- Step 3: verify file access ---------------------------------------------
Write-Host "[3/4] Verifying file access..." -ForegroundColor Yellow
$target = "C:\Program Files\WindowsApps\WindowsTerminalDev_0.0.1.0_x64__xpqk32cx38ema\OpenConsole.exe"
$ok = $false
if (Test-Path $target) {
    try {
        $h = Get-FileHash $target -Algorithm SHA256 -ErrorAction Stop
        Write-Host "      OK  - readable" -ForegroundColor Green
        Write-Host "      SHA256 $($h.Hash)" -ForegroundColor DarkGray
        $ok = $true
    } catch {
        Write-Host "      BLOCKED - still quarantined by Defender" -ForegroundColor Red
        Write-Host "      $($_.Exception.Message)" -ForegroundColor Red
    }
} else {
    Write-Host "      MISSING - file was deleted by Defender, reinstall needed" -ForegroundColor Red
}
Write-Host ""

# ---- Step 4: result ----------------------------------------------------------
Write-Host "[4/4] Result" -ForegroundColor Yellow
if ($ok) {
    Write-Host "      Fixed. Launch ClickTerminal and open a PowerShell tab." -ForegroundColor Green
} else {
    Write-Host "      Not fixed automatically. Do this manually:" -ForegroundColor Red
    Write-Host "      Windows Security > Virus & threat protection >" -ForegroundColor Red
    Write-Host "      Protection history > find Trojan:Win32/Cloxer > Actions > Allow" -ForegroundColor Red
    Write-Host "      Then run _build_and_deploy.bat to reinstall the package." -ForegroundColor Red
}
Write-Host ""
Write-Host "Tip: report the false positive at" -ForegroundColor DarkGray
Write-Host "     https://www.microsoft.com/en-us/wdsi/filesubmission" -ForegroundColor DarkGray
Write-Host "================================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Press any key to close..."
$null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
Stop-Process -Id $PID
