# Self-elevate to admin if needed
if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Start-Process powershell -Verb RunAs -ArgumentList "-NoExit -ExecutionPolicy Bypass -File `"$PSCommandPath`""
    exit
}

Write-Host "================================================"
Write-Host "  ClickTerminal Install"
Write-Host "================================================"
Write-Host ""

$cerPath    = "D:\Dev\20_PC\ClickTerminal\_msix_extract\ClickTerminalDev_new.cer"
$msixPath   = "D:\Dev\20_PC\ClickTerminal\_msix_extract\CascadiaPackage_new.msix"
$localApp   = [Environment]::GetFolderPath("LocalApplicationData")
$virtData   = "$localApp\Packages\WindowsTerminalDev_xpqk32cx38ema\LocalCache\Local\ClickTerminal"
$realData   = "$localApp\ClickTerminal"

# --- Step 1: Migrate virtualized data to real path ---
Write-Host "[1/5] Migrating data from package sandbox to real path..."
if (Test-Path $virtData) {
    New-Item -ItemType Directory -Path $realData -Force | Out-Null
    Get-ChildItem $virtData | ForEach-Object {
        $dest = Join-Path $realData $_.Name
        if (-not (Test-Path $dest)) {
            Copy-Item $_.FullName $dest -Force
            Write-Host "      Migrated: $($_.Name)" -ForegroundColor Cyan
        } else {
            Write-Host "      Skipped (exists): $($_.Name)"
        }
    }
    Write-Host "      OK" -ForegroundColor Green
} else {
    Write-Host "      No existing data found (first install)" -ForegroundColor Yellow
}

# --- Step 2: Trust certificate ---
Write-Host "[2/5] Trusting certificate..."
try {
    Import-Certificate -FilePath $cerPath -CertStoreLocation "Cert:\LocalMachine\Root" | Out-Null
    Import-Certificate -FilePath $cerPath -CertStoreLocation "Cert:\LocalMachine\TrustedPeople" | Out-Null
    Write-Host "      OK" -ForegroundColor Green
} catch {
    Write-Host "      ERROR: $_" -ForegroundColor Red
}

# --- Step 3: Remove old package ---
Write-Host "[3/5] Removing old package..."
Get-AppxPackage "WindowsTerminalDev" | Remove-AppxPackage -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2
Write-Host "      OK" -ForegroundColor Green

# --- Step 4: Install new package ---
Write-Host "[4/5] Installing new package..."
try {
    Add-AppxPackage -Path $msixPath
    Write-Host "      Done!" -ForegroundColor Green
} catch {
    Write-Host "      ERROR: $_" -ForegroundColor Red
}

# --- Step 5: Verify data at real path ---
Write-Host "[5/5] Verifying data..."
if (Test-Path "$realData\clickterminal.json") {
    Write-Host "      Projects file: OK ($realData\clickterminal.json)" -ForegroundColor Green
} else {
    Write-Host "      No projects file yet (will be created on first run)" -ForegroundColor Yellow
}
if (Test-Path "$realData\ctux-settings.json") {
    Write-Host "      Settings file: OK" -ForegroundColor Green
}

Write-Host ""
Write-Host "================================================"
Write-Host "Search 'Windows Terminal Dev' in Start Menu"
Write-Host "NOTE: Data is now stored at:"
Write-Host "  $realData"
Write-Host "================================================"
Write-Host ""
Write-Host "Press any key to close..."
$null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
