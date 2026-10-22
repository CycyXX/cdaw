# Copies every .vst3 bundle in -Source to "Common Files\VST3\CDAW" (needs admin rights).
param([Parameter(Mandatory)][string]$Source)
$dest = "$env:ProgramFiles\Common Files\VST3\CDAW"
$ok = $true
try {
    New-Item -ItemType Directory -Force $dest | Out-Null
    foreach ($bundle in Get-ChildItem $Source -Filter *.vst3) {
        if (Test-Path "$dest\$($bundle.Name)") { Remove-Item "$dest\$($bundle.Name)" -Recurse -Force -ErrorAction Stop }
        Copy-Item $bundle.FullName $dest -Recurse -Force -ErrorAction Stop
        Write-Host "Installed $($bundle.Name) in $dest"
    }
} catch {
    $ok = $false
    Write-Host "FAILED: $($_.Exception.Message) (close Cubase, which keeps loaded plugins locked, and try again)"
    Start-Sleep -Seconds 6
}
