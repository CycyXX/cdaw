# Copies the CDAW build to dist\CDAW. The VST3s for Cubase and other hosts are installed from cdaw-plugins,
# by its scripts\install.ps1.
$root = Split-Path -Parent $PSScriptRoot
$plugins = "$root\third_party\cdaw-plugins"

# CDAW's built-in plugins are compiled from the submodule, so say so if that isn't the latest version.
# Uncommitted changes there are refused: nothing could rebuild what was published.
if (git -C $plugins status --porcelain --ignore-submodules) {
    Write-Host "NOT published: third_party\cdaw-plugins has uncommitted changes. Commit them in cdaw-plugins, then run scripts\update-plugins.ps1."
    exit 1
}

$current = git -C $plugins rev-parse HEAD
$latest = (git -C $plugins ls-remote origin refs/heads/main 2>$null) -split '\s+' | Select-Object -First 1
if (-not $latest) {
    Write-Host "WARNING: couldn't reach GitHub to check for a newer version of the plugins."
} elseif ($current -ne $latest) {
    Write-Host "WARNING: CDAW's plugins aren't the latest (built from $($current.Substring(0, 7)), main is $($latest.Substring(0, 7))). Run scripts\update-plugins.ps1 to update them."
}

$built = "$root\build\ninja-msvc-relwithdebinfo\Source\CDAW_artefacts\RelWithDebInfo"
$dist = "$root\dist\CDAW"
New-Item -ItemType Directory -Force $dist | Out-Null
Copy-Item "$built\CDAW.exe" $dist -Force
# Symbols next to the exe, so crash reports can name the functions involved.
if (Test-Path "$built\CDAW.pdb") { Copy-Item "$built\CDAW.pdb" $dist -Force }
Copy-Item "$built\DrumMaps" $dist -Recurse -Force
Write-Host "Published to $dist"
