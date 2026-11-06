# Moves third_party\cdaw-plugins to the latest main of cdaw-plugins, then rebuilds and publishes CDAW.
# The move is left staged: commit it to keep it, as CDAW's other changes are.
# -Restart is for CDAW's Plugins > Check for Plugin Updates, which runs this as it closes: wait for CDAW
# to close first (the running exe can't be replaced), and start it again once it's published.
param([switch] $Restart)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$plugins = "$root\third_party\cdaw-plugins"

if ($Restart) {
    Write-Host 'Waiting for CDAW to close...'
    $deadline = (Get-Date).AddMinutes(2)
    while (Get-Process CDAW -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path.StartsWith("$root\", 'OrdinalIgnoreCase') }) {
        if ((Get-Date) -gt $deadline) {
            Write-Host 'NOT updated: CDAW is still open. Close it, then run Update-Plugins.bat.'
            exit 1
        }
        Start-Sleep -Milliseconds 500
    }
}

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
$published = $LASTEXITCODE

if ($Restart -and $published -eq 0) {
    Write-Host 'Starting CDAW.'
    Start-Process "$root\dist\CDAW\CDAW.exe"
}
exit $published
