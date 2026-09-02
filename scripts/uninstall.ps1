$ErrorActionPreference = 'Stop'

if (Get-Process vrserver -ErrorAction SilentlyContinue) { throw 'SteamVR is running. Close it and run this script again.' }

$steam = (Get-ItemProperty 'HKCU:\Software\Valve\Steam').SteamPath
$vrpathreg = Join-Path $steam 'steamapps\common\SteamVR\bin\win64\vrpathreg.exe'
if (-not (Test-Path $vrpathreg)) { throw "vrpathreg not found: $vrpathreg" }

$overlayDir = if (Test-Path (Join-Path $PSScriptRoot 'SpaceCalibrator.exe')) { $PSScriptRoot } else { Join-Path (Split-Path -Parent $PSScriptRoot) 'build\artifacts\Release' }
$overlay = Join-Path $overlayDir 'SpaceCalibrator.exe'
if (Test-Path $overlay) {
    Push-Location $overlayDir
    try {
        & $overlay -removemanifest
        $global:LASTEXITCODE = 0
        Write-Host 'overlay manifest removed'
    } finally {
        Pop-Location
    }
}

$existing = & $vrpathreg finddriver 01spacecalibrator 2>$null
$found = $LASTEXITCODE -eq 0 -and $existing
$global:LASTEXITCODE = 0
if (-not $found) {
    Write-Host '01spacecalibrator is not registered'
    exit 0
}
$path = (@($existing)[0]).Trim()
& $vrpathreg removedriver $path
if ($LASTEXITCODE -ne 0) { throw "removedriver failed with exit code $LASTEXITCODE" }
Write-Host "removed: $path"
