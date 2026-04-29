@echo off
net session >nul 2>&1
if %errorLevel% neq 0 (
    powershell -Command "Start-Process '%~f0' -Verb RunAs"
    exit
)

echo === ClickTerminal Install ===

certutil -addstore Root "%~dp0ClickTerminalDev.cer"
if %errorLevel% neq 0 goto error

certutil -addstore TrustedPeople "%~dp0ClickTerminalDev.cer"

powershell -ExecutionPolicy Bypass -Command "Add-AppxPackage -Path '%~dp0src\cascadia\CascadiaPackage\AppPackages\CascadiaPackage_0.0.1.0_x64_Test\CascadiaPackage_0.0.1.0_x64.msix' -ForceApplicationShutdown; if($?) { Write-Host 'SUCCESS' } else { Write-Host 'FAILED' }"

echo.
echo Done. Search "Windows Terminal Dev" in Start Menu.
goto end

:error
echo FAILED - Run as Administrator required.

:end
pause
