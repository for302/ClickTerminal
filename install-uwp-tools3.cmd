@echo off
echo Step 1: Clearing VS installer channel cache...
rd /s /q "%LOCALAPPDATA%\Microsoft\VisualStudio\Packages\_Channels" 2>nul
echo Step 2: Running VS BuildTools with UWP workload...
"C:\Users\for30\AppData\Local\Temp\vs_buildtools.exe" --wait --quiet --norestart --add Microsoft.VisualStudio.Workload.UniversalBuildTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --includeRecommended --installPath "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
echo Done. Exit code: %ERRORLEVEL%
pause
