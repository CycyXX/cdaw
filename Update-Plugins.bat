@echo off
rem Moves CDAW's plugins to the latest cdaw-plugins, then rebuilds and publishes CDAW: scripts\update-plugins.ps1.
rem CDAW's Plugins > Check for Plugin Updates runs this with -Restart, to wait for CDAW to close and start it again.
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\update-plugins.ps1" %*
if errorlevel 1 ( echo. & echo UPDATE FAILED: see the lines above. Start CDAW from dist\CDAW\CDAW.exe or your desktop shortcut. & pause & exit /b 1 )
if /i "%~1"=="-Restart" ( echo. & echo Done: CDAW is starting again. & pause & exit /b 0 )
echo. & echo Done. Start CDAW from dist\CDAW\CDAW.exe or your desktop shortcut.
pause
