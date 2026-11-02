@echo off
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\build.ps1" -Target CDAW
if errorlevel 1 ( echo. & echo BUILD FAILED & pause & exit /b 1 )
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\build.ps1" -Target CVerb_VST3
if errorlevel 1 ( echo. & echo C-VERB VST3 BUILD FAILED & pause & exit /b 1 )
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\build.ps1" -Target CMulticomp_VST3
if errorlevel 1 ( echo. & echo C-MULTICOMP VST3 BUILD FAILED & pause & exit /b 1 )
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\build.ps1" -Target CComp_VST3
if errorlevel 1 ( echo. & echo C-COMP VST3 BUILD FAILED & pause & exit /b 1 )
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\build.ps1" -Target CDelay_VST3
if errorlevel 1 ( echo. & echo C-DELAY VST3 BUILD FAILED & pause & exit /b 1 )
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\build.ps1" -Target CChorus_VST3
if errorlevel 1 ( echo. & echo C-CHORUS VST3 BUILD FAILED & pause & exit /b 1 )
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\build.ps1" -Target CPhaser_VST3
if errorlevel 1 ( echo. & echo C-PHASER VST3 BUILD FAILED & pause & exit /b 1 )
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\build.ps1" -Target CFilter_VST3
if errorlevel 1 ( echo. & echo C-FILTER VST3 BUILD FAILED & pause & exit /b 1 )
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\build.ps1" -Target CDist_VST3
if errorlevel 1 ( echo. & echo C-DIST VST3 BUILD FAILED & pause & exit /b 1 )
powershell -NoProfile -ExecutionPolicy Bypass -File "scriptsbuild.ps1" -Target CEsser_VST3
if errorlevel 1 ( echo. & echo C-ESSER VST3 BUILD FAILED & pause & exit /b 1 )
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\build.ps1" -Target CSynth_VST3
if errorlevel 1 ( echo. & echo C-SYNTH VST3 BUILD FAILED & pause & exit /b 1 )
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\build.ps1" -Target CLead_VST3
if errorlevel 1 ( echo. & echo C-LEAD VST3 BUILD FAILED & pause & exit /b 1 )
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\publish.ps1"
echo. & echo Done. Start CDAW from dist\CDAW\CDAW.exe or your desktop shortcut.
pause
