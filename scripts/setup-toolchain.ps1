# Installs the C++ toolchain CDAW needs: MSVC Build Tools 2022, CMake and Ninja.
# Run once. The Visual Studio installer asks for administrator rights (UAC).

$ErrorActionPreference = 'Stop'

function Test-Tool($name) { $null -ne (Get-Command $name -ErrorAction SilentlyContinue) }

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"

function Get-VsPath {
    if (-not (Test-Path $vswhere)) { return $null }
    $p = & $vswhere -latest -products * -version "[17.0,19.0)" `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($p) { return $p } else { return $null }
}

if (-not (Get-VsPath)) {
    Write-Host "Installing Visual Studio 2022 Build Tools (C++ workload)..."
    winget install -e --id Microsoft.VisualStudio.2022.BuildTools --source winget `
        --accept-package-agreements --accept-source-agreements `
        --override "--wait --passive --norestart --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.Windows11SDK.26100 --includeRecommended"
} else {
    Write-Host "MSVC already installed at $(Get-VsPath)"
}

if (-not (Test-Tool cmake)) {
    Write-Host "Installing CMake..."
    winget install -e --id Kitware.CMake --source winget --accept-package-agreements --accept-source-agreements
}

if (-not (Test-Tool ninja)) {
    Write-Host "Installing Ninja..."
    winget install -e --id Ninja-build.Ninja --source winget --accept-package-agreements --accept-source-agreements
}

# Refresh PATH for this session so the checks below can see new tools.
$env:Path = [System.Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' +
            [System.Environment]::GetEnvironmentVariable('Path', 'User')

Write-Host ""
Write-Host "--- Verification ---"
$vs = Get-VsPath
if ($vs) { Write-Host "MSVC:  $vs" } else { Write-Warning "MSVC C++ tools not found" }
if (Test-Tool cmake) { cmake --version | Select-Object -First 1 } else { Write-Warning "cmake not on PATH (open a new terminal)" }
if (Test-Tool ninja) { "ninja " + (ninja --version) } else { Write-Warning "ninja not on PATH (open a new terminal)" }
