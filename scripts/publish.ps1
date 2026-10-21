$root = Split-Path -Parent $PSScriptRoot
$built = "$root\build\ninja-msvc-relwithdebinfo\Source\CDAW_artefacts\RelWithDebInfo"
$dist = "$root\dist\CDAW"
New-Item -ItemType Directory -Force $dist | Out-Null
Copy-Item "$built\CDAW.exe" $dist -Force
# Symbols next to the exe, so crash reports can name the functions involved.
if (Test-Path "$built\CDAW.pdb") { Copy-Item "$built\CDAW.pdb" $dist -Force }
Copy-Item "$built\DrumMaps" $dist -Recurse -Force
Write-Host "Published to $dist"

