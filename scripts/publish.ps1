$root = Split-Path -Parent $PSScriptRoot
$built = "$root\build\ninja-msvc-relwithdebinfo\Source\CDAW_artefacts\RelWithDebInfo"
$dist = "$root\dist\CDAW"
New-Item -ItemType Directory -Force $dist | Out-Null
Copy-Item "$built\CDAW.exe" $dist -Force
# Symbols next to the exe, so crash reports can name the functions involved.
if (Test-Path "$built\CDAW.pdb") { Copy-Item "$built\CDAW.pdb" $dist -Force }
Copy-Item "$built\DrumMaps" $dist -Recurse -Force
Write-Host "Published to $dist"

# C-Verb VST3 (if built): into dist\VST3, and the per-user VST3 folder Cubase and other hosts scan.
$vst3 = "$root\build\ninja-msvc-relwithdebinfo\VST3\CVerb\CVerb_artefacts\RelWithDebInfo\VST3\C-Verb.vst3"
if (Test-Path $vst3) {
    $distVst3 = "$root\dist\VST3"
    New-Item -ItemType Directory -Force $distVst3 | Out-Null
    Remove-Item "$distVst3\C-Verb.vst3" -Recurse -Force -ErrorAction SilentlyContinue
    Copy-Item $vst3 $distVst3 -Recurse -Force
    Get-ChildItem "$distVst3\C-Verb.vst3" -Recurse -Include *.ilk | Remove-Item -Force

    $userVst3 = "$env:LOCALAPPDATA\Programs\Common\VST3"
    New-Item -ItemType Directory -Force $userVst3 | Out-Null
    Remove-Item "$userVst3\C-Verb.vst3" -Recurse -Force -ErrorAction SilentlyContinue
    Copy-Item "$distVst3\C-Verb.vst3" $userVst3 -Recurse -Force
    Write-Host "Published C-Verb.vst3 to $distVst3 and installed it in $userVst3"
}
