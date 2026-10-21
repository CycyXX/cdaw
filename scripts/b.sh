#!/bin/sh
# Dev helper: build (optionally -Target X) and print unique errors.
powershell -NoProfile -ExecutionPolicy Bypass -Command "& 'C:\C_Data\Selfmade_Tools\CDAW\scripts\build.ps1' $* 2>&1 | Out-File -Encoding utf8 \"\$env:TEMP\cdaw_b.txt\"; exit \$LASTEXITCODE" >/dev/null 2>&1
rc=$?
grep -E "error C|error LNK|fatal error" "$TEMP/cdaw_b.txt" | sed 's#^.*Source.##' | sort -u | head -80
echo "build exit: $rc"
