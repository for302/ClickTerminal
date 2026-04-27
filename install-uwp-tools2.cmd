@echo off
echo Installing UWP workload via bootstrapper...
"C:\Users\for30\AppData\Local\Temp\vs_buildtools.exe" modify --installPath "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools" --add Microsoft.VisualStudio.Workload.UniversalBuildTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --includeRecommended --quiet --norestart --wait
echo Exit: %ERRORLEVEL%
pause
