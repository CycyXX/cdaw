@echo off
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\build.ps1" -Target CDAW
if errorlevel 1 ( echo. & echo BUILD FAILED & pause & exit /b 1 )
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\publish.ps1"
if errorlevel 1 ( echo. & echo PUBLISH FAILED: see the lines above & pause & exit /b 1 )
echo. & echo Done. Start CDAW from dist\CDAW\CDAW.exe or your desktop shortcut.
echo The VST3s for Cubase are installed by Install-Plugins.bat in cdaw-plugins.
pause
