param([string]$InstallDir = '')

$ErrorActionPreference = 'Stop'

if (-not $InstallDir) {
    $packaged = $PSScriptRoot
    $built = Join-Path (Split-Path -Parent $PSScriptRoot) 'build'
    $InstallDir = if (Test-Path (Join-Path $packaged 'SpaceCalibrator.exe')) { $packaged } else { $built }
}

$driverDir = Join-Path $InstallDir '01spacecalibrator'
$overlayDir = if (Test-Path (Join-Path $InstallDir 'artifacts\Release\SpaceCalibrator.exe')) { Join-Path $InstallDir 'artifacts\Release' } else { $InstallDir }
$overlay = Join-Path $overlayDir 'SpaceCalibrator.exe'
$dll = Join-Path $driverDir 'bin\win64\driver_01spacecalibrator.dll'
foreach ($required in @($dll, (Join-Path $driverDir 'driver.vrdrivermanifest'), $overlay, (Join-Path $overlayDir 'manifest.vrmanifest'))) {
    if (-not (Test-Path $required)) { throw "not built: $required" }
}

if (Get-Process vrserver -ErrorAction SilentlyContinue) { throw 'SteamVR is running. Close it and run this script again.' }

$steam = (Get-ItemProperty 'HKCU:\Software\Valve\Steam').SteamPath
$vrpathreg = Join-Path $steam 'steamapps\common\SteamVR\bin\win64\vrpathreg.exe'
if (-not (Test-Path $vrpathreg)) { throw "vrpathreg not found: $vrpathreg" }

$paths = Join-Path $env:LOCALAPPDATA 'openvr\openvrpaths.vrpath'
$registered = @()
if (Test-Path $paths) {
    $json = Get-Content $paths -Raw | ConvertFrom-Json
    if ($json.PSObject.Properties['external_drivers']) { $registered = @($json.external_drivers) }
}
$resolvedDriver = (Resolve-Path $driverDir).Path
foreach ($entry in $registered) {
    $leaf = Split-Path -Leaf $entry
    if ($leaf -ieq '01spacecalibrator' -or $leaf -ieq 'driver_01spacecalibrator') {
        if ($entry -ieq $resolvedDriver) { continue }
        & $vrpathreg removedriver $entry
        $global:LASTEXITCODE = 0
        Write-Host "unregistered: $entry"
    }
}

$existing = & $vrpathreg finddriver 01spacecalibrator 2>$null
$found = $LASTEXITCODE -eq 0 -and $existing -and ((@($existing)[0]).Trim() -ieq $resolvedDriver)
$global:LASTEXITCODE = 0
if ($found) {
    Write-Host "already registered: $resolvedDriver"
} else {
    & $vrpathreg adddriver $resolvedDriver
    if ($LASTEXITCODE -ne 0) { throw "adddriver failed with exit code $LASTEXITCODE" }
    Write-Host "registered: $resolvedDriver"
}

$activate = Start-Process -FilePath $overlay -ArgumentList '-activatemultipledrivers' -WorkingDirectory $overlayDir -Wait -PassThru
if ($activate.ExitCode -ne 0) { throw "activatemultipledrivers failed with exit code $($activate.ExitCode)" }
Write-Host 'activateMultipleDrivers enabled'
$manifest = Start-Process -FilePath $overlay -ArgumentList '-installmanifest' -WorkingDirectory $overlayDir -Wait -PassThru
Write-Host "overlay manifest registered (exit $($manifest.ExitCode))"

Write-Host 'Done. Start SteamVR; the overlay autolaunches from this folder. Do not launch the Steam copy of Space Calibrator while this one is registered.'
