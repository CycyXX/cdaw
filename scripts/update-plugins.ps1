# Moves third_party\cdaw-plugins to the latest main of cdaw-plugins, then rebuilds and publishes CDAW.
# The move is left staged: commit it to keep it, as CDAW's other changes are.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$plugins = "$root\third_party\cdaw-plugins"

if (git -C $plugins status --porcelain --ignore-submodules) {
    Write-Host "NOT updated: third_party\cdaw-plugins has uncommitted changes, which this would mix up with the new version."
    Write-Host "Commit them in cdaw-plugins, or discard them there, and run this again."
    exit 1
}

$before = git -C $plugins rev-parse --short HEAD
git -C $plugins fetch --quiet --depth 1 origin main
if ($LASTEXITCODE -ne 0) { throw "Couldn't fetch the plugins from GitHub." }
git -C $plugins checkout --quiet --detach FETCH_HEAD
if ($LASTEXITCODE -ne 0) { throw "Couldn't move to the new version of the plugins." }
$after = git -C $plugins rev-parse --short HEAD

git -C $root add third_party/cdaw-plugins
if ($before -eq $after) { Write-Host "The plugins are already the latest ($after)." }
else { Write-Host "Moved the plugins from $before to $after. Commit this in CDAW to keep it." }

& "$PSScriptRoot\build.ps1" -Target CDAW
& "$PSScriptRoot\publish.ps1"
exit $LASTEXITCODE
