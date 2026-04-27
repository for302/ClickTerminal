@echo off
echo Installing UWP Build Tools for VS2022 BuildTools...
"C:\Program Files (x86)\Microsoft Visual Studio\Installer\setup.exe" modify --productId Microsoft.VisualStudio.Product.BuildTools --channelId VisualStudio.17.Release --add Microsoft.VisualStudio.Workload.UniversalBuildTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --includeRecommended --quiet --norestart
echo Done. Exit code: %ERRORLEVEL%
pause
