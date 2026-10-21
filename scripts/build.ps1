# Configures and builds CDAW inside a Visual Studio developer environment.
#   .\scripts\build.ps1                       # RelWithDebInfo, build only
#   .\scripts\build.ps1 -Preset ninja-msvc-debug -Test
#   .\scripts\build.ps1 -Target CDAW_Tests -Test
param(
    [string] $Preset = 'ninja-msvc-relwithdebinfo',
    [string] $Target = '',
    [switch] $Test,
    [switch] $Fresh
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

function Enter-MsvcEnvironment {
    if ($env:VSCMD_ARG_TGT_ARCH -eq 'x64' -and (Get-Command cl -ErrorAction SilentlyContinue)) { return }

    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { throw "vswhere not found. Run scripts\setup-toolchain.ps1 first." }

    $vs = & $vswhere -latest -products * -version "[17.0,19.0)" `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw "No Visual Studio with the C++ x64 tools found. Run scripts\setup-toolchain.ps1." }

    Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
}

Enter-MsvcEnvironment

# Make sure freshly installed tools are visible even in an old shell.
foreach ($p in @("$env:ProgramFiles\CMake\bin", "$env:LOCALAPPDATA\Microsoft\WinGet\Links")) {
    if ((Test-Path $p) -and ($env:Path -notlike "*$p*")) { $env:Path = "$p;$env:Path" }
}

Push-Location $root
# Native tools write warnings to stderr; judge success by exit codes only.
$ErrorActionPreference = 'Continue'
try {
    $configureArgs = @('--preset', $Preset)
    if ($Fresh) { $configureArgs += '--fresh' }
    cmake @configureArgs
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed ($LASTEXITCODE)" }

    $buildArgs = @('--build', '--preset', $Preset)
    if ($Target) { $buildArgs += @('--target', $Target) }
    $buildArgs += @('--', '-k', '0')   # keep going: report every error in one pass
    cmake @buildArgs
    if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)" }

    if ($Test) {
        ctest --preset $Preset
        if ($LASTEXITCODE -ne 0) { throw "Tests failed ($LASTEXITCODE)" }
    }
}
finally {
    Pop-Location
}
