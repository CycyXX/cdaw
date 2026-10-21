# Fetches third-party sources: Tracktion Engine (pinned, shallow) and its nested JUCE submodule.
# Safe to re-run.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$teUrl    = 'https://github.com/Tracktion/tracktion_engine.git'
$teCommit = '38d5a2750367d8a95a58da6d3bbab49823285f24'
$tePath   = 'third_party/tracktion_engine'

function Invoke-Git {
    git @args
    if ($LASTEXITCODE -ne 0) { throw "git $($args -join ' ') failed with exit code $LASTEXITCODE" }
}

if (-not (Test-Path (Join-Path $root '.git'))) {
    Invoke-Git init -b main
}
Invoke-Git config core.longpaths true

$teFull = Join-Path $root $tePath

if (-not (Test-Path (Join-Path $teFull '.git'))) {
    # git refuses to add a submodule into an existing (even empty) folder.
    if ((Test-Path $teFull) -and -not (Get-ChildItem $teFull -Force)) { Remove-Item $teFull -Force }

    $registered = (Test-Path (Join-Path $root '.gitmodules')) -and
                  (Select-String -Path (Join-Path $root '.gitmodules') -Pattern 'url\s*=' -Quiet)

    if ($registered) {
        # Fresh clone of CDAW: the submodule is registered, just initialise it shallowly.
        Invoke-Git submodule update --init --depth 1 $tePath
    } else {
        # Shallow: the engine's full history is several GB and not needed.
        Invoke-Git submodule add --depth 1 $teUrl $tePath
        Invoke-Git config -f .gitmodules "submodule.$tePath.shallow" true
    }
}

Invoke-Git -C $tePath fetch --depth 1 origin $teCommit
Invoke-Git -C $tePath checkout --quiet $teCommit

# Tracktion's .gitmodules points JUCE at an SSH URL; use HTTPS instead.
Invoke-Git -C $tePath submodule init
Invoke-Git -C $tePath config submodule.modules/juce.url https://github.com/juce-framework/JUCE.git
Invoke-Git -C $tePath submodule update --depth 1

Invoke-Git add .gitmodules $tePath

Write-Host "Tracktion Engine: $(git -C $tePath rev-parse --short HEAD)"
Write-Host "JUCE:             $(git -C "$tePath/modules/juce" rev-parse --short HEAD)"
