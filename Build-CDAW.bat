@echo off
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\build.ps1" -Target CDAW
if errorlevel 1 ( echo. & echo BUILD FAILED & pause & exit /b 1 )
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\build.ps1" -Target CVerb_VST3
if errorlevel 1 ( echo. & echo C-VERB VST3 BUILD FAILED & pause & exit /b 1 )
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\build.ps1" -Target CMulticomp_VST3
if errorlevel 1 ( echo. & echo C-MULTICOMP VST3 BUILD FAILED & pause & exit /b 1 )
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\publish.ps1"
echo. & echo Done. Start CDAW from dist\CDAW\CDAW.exe or your desktop shortcut.
pause
