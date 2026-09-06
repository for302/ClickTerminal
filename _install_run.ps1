# Self-elevate to admin if needed
if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Start-Process powershell -Verb RunAs -ArgumentList "-NoExit -ExecutionPolicy Bypass -File `"$PSCommandPath`""
    exit
}

# Repo root — derived so a renamed/moved checkout keeps working.
$root = $PSScriptRoot

Write-Host "================================================"
Write-Host "  ClickTerminal Install (Register mode)"
Write-Host "================================================"
Write-Host ""

$manifestPath = "$root\_msix_extract\pkg\AppxManifest.xml"
$cerPath      = "$root\_msix_extract\ClickTerminalDev_new.cer"
$userProfile  = (Get-ItemProperty "HKCU:\Volatile Environment" -ErrorAction SilentlyContinue).USERPROFILE
if (-not $userProfile) { $userProfile = $env:USERPROFILE }
$realData     = "$userProfile\AppData\Local\ClickTerminal"

# --- Step 1: Trust certificate ---
Write-Host "[1/4] Trusting certificate..."
try {
    Import-Certificate -FilePath $cerPath -CertStoreLocation "Cert:\LocalMachine\Root" | Out-Null
    Import-Certificate -FilePath $cerPath -CertStoreLocation "Cert:\LocalMachine\TrustedPeople" | Out-Null
    Write-Host "      OK" -ForegroundColor Green
} catch {
    Write-Host "      Warning: $_" -ForegroundColor Yellow
}

# --- Step 2: Kill any running WT processes & remove old package ---
Write-Host "[2/4] Removing old package..."
Get-Process | Where-Object { $_.Name -like "*WindowsTerminal*" -or $_.Name -like "*OpenConsole*" } | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1
try { Get-AppxPackage -AllUsers -Name "*WindowsTerminalDev*" | Remove-AppxPackage -AllUsers -ErrorAction SilentlyContinue } catch {}
try { Get-AppxPackage -Name "*WindowsTerminalDev*" | Remove-AppxPackage -ErrorAction SilentlyContinue } catch {}
Start-Sleep -Seconds 3
Write-Host "      OK" -ForegroundColor Green

# --- Step 3: Install package ---
Write-Host "[3/4] Installing package..."
$ok = $false
$devMode = (Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\AppModelUnlock" -ErrorAction SilentlyContinue).AllowDevelopmentWithoutDevLicense -eq 1
if ($devMode) {
    try {
        Add-AppxPackage -Register $manifestPath -ErrorAction Stop
        $ok = $true
        Write-Host "      Registered from folder OK" -ForegroundColor Green
    } catch {
        Write-Host "      Register failed: $_" -ForegroundColor Yellow
    }
}
if (-not $ok) {
    $msixPath = "$root\_msix_extract\CascadiaPackage_new.msix"
    try {
        Add-AppxPackage -Path $msixPath -ForceApplicationShutdown -ErrorAction Stop
        $ok = $true
        Write-Host "      MSIX install OK" -ForegroundColor Green
    } catch {
        Write-Host "      MSIX FAILED: $_" -ForegroundColor Red
        Write-Host "      Please reboot and run again." -ForegroundColor Red
    }
}

# --- Step 4: Verify ---
Write-Host "[4/4] Verifying..."
$pkg = Get-AppxPackage -Name "*WindowsTerminalDev*"
if ($pkg) {
    Write-Host "      Package: $($pkg.Version)" -ForegroundColor Green
} else {
    Write-Host "      Package NOT found!" -ForegroundColor Red
}
New-Item -ItemType Directory -Path $realData -Force | Out-Null
if (Test-Path "$realData\clickterminal.json") { Write-Host "      Projects: OK" -ForegroundColor Green }
if (Test-Path "$realData\ctux-settings.json") { Write-Host "      Settings: OK" -ForegroundColor Green }

Write-Host ""
Write-Host "================================================"
if ($ok) {
    Write-Host "  SUCCESS! Search 'Windows Terminal Dev' in Start"
} else {
    Write-Host "  FAILED - Reboot PC then run again"
}
Write-Host "  Data: $realData"
Write-Host "================================================"
Write-Host ""
Write-Host "Press any key to close..."
$null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
Stop-Process -Id $PID
