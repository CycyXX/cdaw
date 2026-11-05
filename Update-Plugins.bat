@echo off
rem Moves CDAW's plugins to the latest cdaw-plugins, then rebuilds and publishes CDAW: scripts\update-plugins.ps1.
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\update-plugins.ps1"
if errorlevel 1 ( echo. & echo UPDATE FAILED: see the lines above & pause & exit /b 1 )
echo. & echo Done. Start CDAW from dist\CDAW\CDAW.exe or your desktop shortcut.
pause
