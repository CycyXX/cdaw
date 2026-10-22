$root = Split-Path -Parent $PSScriptRoot
$built = "$root\build\ninja-msvc-relwithdebinfo\Source\CDAW_artefacts\RelWithDebInfo"
$dist = "$root\dist\CDAW"
New-Item -ItemType Directory -Force $dist | Out-Null
Copy-Item "$built\CDAW.exe" $dist -Force
# Symbols next to the exe, so crash reports can name the functions involved.
if (Test-Path "$built\CDAW.pdb") { Copy-Item "$built\CDAW.pdb" $dist -Force }
Copy-Item "$built\DrumMaps" $dist -Recurse -Force
Write-Host "Published to $dist"

# VST3 plugins (C-Verb and C-Multicomp): into dist\VST3, then installed in "Common Files\VST3\CDAW" where
# Cubase and other hosts scan. That folder needs admin rights, so install-vst3.ps1 runs elevated (one UAC
# prompt); if that is declined the plugins go to the per-user VST3 folder instead.
# Rebuild first (incremental, quick when nothing changed) so the installed plugins never lag the shared code.
# To add a plugin: add its CMake target, folder and bundle name here (and to Build-CDAW.bat).
$plugins = @(
    @{ Target = 'CVerb_VST3';      Folder = 'CVerb';      Artefacts = 'CVerb_artefacts';      Bundle = 'C-Verb.vst3' },
    @{ Target = 'CMulticomp_VST3'; Folder = 'CMulticomp'; Artefacts = 'CMulticomp_artefacts'; Bundle = 'C-Multicomp.vst3' }
)

$distVst3 = "$root\dist\VST3"
$systemDir = "$env:ProgramFiles\Common Files\VST3\CDAW"
$userVst3 = "$env:LOCALAPPDATA\Programs\Common\VST3"
$bundles = @()

foreach ($plugin in $plugins) {
    & "$PSScriptRoot\build.ps1" -Target $plugin.Target *> $null
    if ($LASTEXITCODE -ne 0) { Write-Host "$($plugin.Bundle) build FAILED - not installing it"; exit 1 }

    $vst3 = "$root\build\ninja-msvc-relwithdebinfo\VST3\$($plugin.Folder)\$($plugin.Artefacts)\RelWithDebInfo\VST3\$($plugin.Bundle)"
    if (-not (Test-Path $vst3)) { continue }

    New-Item -ItemType Directory -Force $distVst3 | Out-Null
    Remove-Item "$distVst3\$($plugin.Bundle)" -Recurse -Force -ErrorAction SilentlyContinue
    Copy-Item $vst3 $distVst3 -Recurse -Force
    Get-ChildItem "$distVst3\$($plugin.Bundle)" -Recurse -Include *.ilk | Remove-Item -Force
    $bundles += $plugin.Bundle
}

if ($bundles.Count -gt 0) {
    Write-Host "Published VST3 plugins to $distVst3"

    $installer = "$PSScriptRoot\install-vst3.ps1"
    $isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

    try {
        if ($isAdmin) { & $installer -Source $distVst3 }
        else {
            $installerArgs = "-NoProfile -ExecutionPolicy Bypass -File `"$installer`" -Source `"$distVst3`""
            Start-Process powershell -ArgumentList $installerArgs -Verb RunAs -Wait -ErrorAction Stop
        }
    } catch { Write-Host "Elevated install did not run: $($_.Exception.Message)" }

    foreach ($bundle in $bundles) {
        if (Test-Path "$systemDir\$bundle") {
            Write-Host "Installed $bundle in $systemDir"
            # The same plugin in two scanned folders would show up twice in Cubase.
            Remove-Item "$userVst3\$bundle" -Recurse -Force -ErrorAction SilentlyContinue
            Remove-Item "$userVst3\CDAW\$bundle" -Recurse -Force -ErrorAction SilentlyContinue
        } else {
            New-Item -ItemType Directory -Force "$userVst3\CDAW" | Out-Null
            Remove-Item "$userVst3\CDAW\$bundle" -Recurse -Force -ErrorAction SilentlyContinue
            Copy-Item "$distVst3\$bundle" "$userVst3\CDAW" -Recurse -Force
            Remove-Item "$userVst3\$bundle" -Recurse -Force -ErrorAction SilentlyContinue
            Write-Host "System folder not updated; installed $bundle for this user in $userVst3\CDAW"
        }
    }
}
