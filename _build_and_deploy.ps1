# Self-elevate to admin if needed
if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Start-Process powershell -Verb RunAs -ArgumentList "-NoExit -ExecutionPolicy Bypass -File `"$PSCommandPath`""
    exit
}

$msbuild   = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
$vcxproj   = "D:\Dev\20_PC\ClickTerminal\src\cascadia\TerminalApp\dll\TerminalApp.vcxproj"
$makepri   = "C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\makepri.exe"
$makeappx  = "C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\makeappx.exe"
$signtool  = "C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\signtool.exe"

$binDir    = "D:\Dev\20_PC\ClickTerminal\bin\x64\Release\TerminalApp"
$pkgDir    = "D:\Dev\20_PC\ClickTerminal\_msix_extract\pkg"
$priRoot   = "D:\Dev\20_PC\ClickTerminal\src\cascadia\CascadiaPackage"
$priConfig = "$priRoot\obj\x64\Release\priconfig.xml"
$pfx       = "D:\Dev\20_PC\ClickTerminal\ClickTerminalDev.pfx"
$msix      = "D:\Dev\20_PC\ClickTerminal\_msix_extract\CascadiaPackage_new.msix"
$manifest  = "$pkgDir\AppxManifest.xml"

Write-Host "================================================"
Write-Host "  ClickTerminal Build + Deploy"
Write-Host "================================================"
Write-Host ""

# ---- Pre-flight: stale source check ----
$dllBin = "$binDir\TerminalApp.dll"
if (Test-Path $dllBin) {
    $dllTime = (Get-Item $dllBin).LastWriteTime
    $srcRoot = "D:\Dev\20_PC\ClickTerminal\src\cascadia\TerminalApp"
    $stale = Get-ChildItem "$srcRoot\*.cpp","$srcRoot\*.h","$srcRoot\*.xaml" -ErrorAction SilentlyContinue |
             Where-Object { $_.LastWriteTime -gt $dllTime }
    if ($stale) {
        Write-Host "[PRE-FLIGHT] Sources newer than DLL ($($dllTime.ToString('HH:mm:ss'))):" -ForegroundColor Yellow
        $stale | ForEach-Object {
            Write-Host "   $($_.Name)  ($($_.LastWriteTime.ToString('HH:mm:ss')))" -ForegroundColor Yellow
        }
        Write-Host "  -> These changes will be included in this build." -ForegroundColor Yellow
    } else {
        Write-Host "[PRE-FLIGHT] All sources up to date with DLL." -ForegroundColor Green
    }
} else {
    Write-Host "[PRE-FLIGHT] No existing DLL - first build." -ForegroundColor Cyan
}
Write-Host ""

# ---- Step 1: Build ----
Write-Host "[1/5] Building TerminalApp.dll..." -ForegroundColor Cyan
$buildStartTime = Get-Date
& $msbuild $vcxproj /p:Configuration=Release /p:Platform=x64 /p:SolutionDir="D:\Dev\20_PC\ClickTerminal\" /t:Build /m /nologo /verbosity:minimal
if ($LASTEXITCODE -ne 0) {
    Write-Host "      BUILD FAILED (exit $LASTEXITCODE)" -ForegroundColor Red
    Write-Host "Press any key to exit..."
    $null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
    Stop-Process -Id $PID
}
# Verify DLL was actually updated by this build
$dllAfterBuild = (Get-Item $dllBin -ErrorAction SilentlyContinue)
if ($dllAfterBuild -and $dllAfterBuild.LastWriteTime -lt $buildStartTime) {
    Write-Host "      WARNING: TerminalApp.dll was NOT updated! Build may have used cache or wrong project." -ForegroundColor Red
    Write-Host "      DLL time: $($dllAfterBuild.LastWriteTime), Build started: $($buildStartTime.ToString('HH:mm:ss'))" -ForegroundColor Red
} else {
    Write-Host "      Build OK (DLL updated at $($dllAfterBuild.LastWriteTime.ToString('HH:mm:ss')))" -ForegroundColor Green
}

# ---- Step 2: Copy DLL ----
Write-Host "[2/5] Copying DLL to pkg..." -ForegroundColor Cyan
Copy-Item "$binDir\TerminalApp.dll"   "$pkgDir\TerminalApp.dll"   -Force
Copy-Item "$binDir\TerminalApp.winmd" "$pkgDir\TerminalApp.winmd" -Force
Write-Host "      DLL copied" -ForegroundColor Green

# ---- Step 3: Regenerate resources.pri ----
Write-Host "[3/5] Regenerating resources.pri..." -ForegroundColor Cyan
if (-not (Test-Path $priConfig)) {
    Write-Host "      priconfig.xml not found at: $priConfig" -ForegroundColor Red
    Write-Host "      Skipping pri regeneration (may cause XBF mismatch)" -ForegroundColor Yellow
} else {
    Push-Location $priRoot
    & $makepri new /pr $priRoot /cf $priConfig /o /of "$pkgDir\resources.pri" 2>&1 | Out-Null
    Pop-Location
    if ($LASTEXITCODE -eq 0) {
        Write-Host "      resources.pri OK" -ForegroundColor Green
    } else {
        Write-Host "      makepri failed (exit $LASTEXITCODE) - continuing anyway" -ForegroundColor Yellow
    }
}

# ---- Step 4: Remove old signature files ----
Write-Host "[4/5] Cleaning signature files..." -ForegroundColor Cyan
Remove-Item "$pkgDir\AppxSignature.p7x" -ErrorAction SilentlyContinue
Remove-Item "$pkgDir\AppxBlockMap.xml"  -ErrorAction SilentlyContinue
Write-Host "      Cleaned" -ForegroundColor Green

# ---- Step 5: Trust certificate + Install ----
Write-Host "[5/5] Installing package..." -ForegroundColor Cyan

# Trust signing certificate before install (required for MSIX path)
$cerCandidates = @(
    "D:\Dev\20_PC\ClickTerminal\ClickTerminalDev.cer",
    "D:\Dev\20_PC\ClickTerminal\_msix_extract\ClickTerminalDev_new.cer"
)
foreach ($cerPath in $cerCandidates) {
    if (Test-Path $cerPath) {
        try {
            Import-Certificate -FilePath $cerPath -CertStoreLocation "Cert:\LocalMachine\Root" | Out-Null
            Import-Certificate -FilePath $cerPath -CertStoreLocation "Cert:\LocalMachine\TrustedPeople" | Out-Null
            Write-Host "      Certificate trusted: $(Split-Path $cerPath -Leaf)" -ForegroundColor Green
        } catch {
            Write-Host "      Certificate import warning: $_" -ForegroundColor Yellow
        }
        break
    }
}

Get-Process | Where-Object { $_.Name -like "*WindowsTerminal*" -or $_.Name -like "*OpenConsole*" } | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1
try { Get-AppxPackage -AllUsers -Name "*WindowsTerminalDev*" | Remove-AppxPackage -AllUsers -ErrorAction SilentlyContinue } catch {}
try { Get-AppxPackage -Name "*WindowsTerminalDev*" | Remove-AppxPackage -ErrorAction SilentlyContinue } catch {}
Start-Sleep -Seconds 3

$ok = $false
$devMode = (Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\AppModelUnlock" -ErrorAction SilentlyContinue).AllowDevelopmentWithoutDevLicense -eq 1
if ($devMode) {
    try {
        Add-AppxPackage -Register $manifest -ErrorAction Stop
        $ok = $true
        Write-Host "      Registered from folder OK" -ForegroundColor Green
    } catch {
        Write-Host "      Register failed: $_" -ForegroundColor Yellow
    }
}
if (-not $ok) {
    Write-Host "      Packing + signing MSIX..." -ForegroundColor Cyan
    Push-Location "D:\Dev\20_PC\ClickTerminal\_msix_extract"
    & $makeappx pack /d pkg /p CascadiaPackage_new.msix /nv /o
    if ($LASTEXITCODE -ne 0) {
        Write-Host "      makeappx FAILED (exit $LASTEXITCODE)" -ForegroundColor Red
        Pop-Location; $null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown"); Stop-Process -Id $PID
    }
    & $signtool sign /fd SHA256 /f $pfx /p ctuxdev CascadiaPackage_new.msix
    if ($LASTEXITCODE -ne 0) {
        Write-Host "      signtool FAILED (exit $LASTEXITCODE)" -ForegroundColor Red
        Pop-Location; $null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown"); Stop-Process -Id $PID
    }
    Pop-Location
    try {
        Add-AppxPackage -Path $msix -ForceApplicationShutdown -ErrorAction Stop
        $ok = $true
        Write-Host "      MSIX install OK" -ForegroundColor Green
    } catch {
        Write-Host "      MSIX FAILED: $_" -ForegroundColor Red
    }
}

Write-Host ""
Write-Host "================================================"
$pkg = Get-AppxPackage -Name "*WindowsTerminalDev*"
if ($pkg) {
    Write-Host "  Package: $($pkg.Version)" -ForegroundColor Green
    Write-Host "  SUCCESS - Launching app..." -ForegroundColor Green
    Write-Host "================================================"
    Write-Host ""
    Start-Sleep -Seconds 1
    $aumid = "$($pkg.PackageFamilyName)!App"
    Start-Process "explorer.exe" "shell:AppsFolder\$aumid"
} else {
    Write-Host "  Package NOT found - install failed" -ForegroundColor Red
    Write-Host "================================================"
}
Write-Host ""
Write-Host "Press any key to close..."
$null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
Stop-Process -Id $PID
