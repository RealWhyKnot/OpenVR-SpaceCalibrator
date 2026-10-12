#!/usr/bin/env pwsh
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)][string] $OutDir,
  [string] $RepoRoot = (Get-Location).Path,
  [string] $BuildDir = ""
)

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path -LiteralPath $RepoRoot).Path
if (-not $BuildDir) { $BuildDir = Join-Path $repo "build" }

$driverDir = Join-Path $BuildDir "01spacecalibrator"
$overlayDir = Join-Path $BuildDir "artifacts\Release"
$overlayFiles = @("SpaceCalibrator.exe", "openvr_api.dll", "manifest.vrmanifest", "icon.png", "taskbar_icon.png")
foreach ($required in @((Join-Path $driverDir "bin\win64\driver_01spacecalibrator.dll"), (Join-Path $driverDir "driver.vrdrivermanifest")) + ($overlayFiles | ForEach-Object { Join-Path $overlayDir $_ })) {
  if (-not (Test-Path -LiteralPath $required)) { throw "missing build output: $required" }
}

if (Test-Path -LiteralPath $OutDir) { Remove-Item -LiteralPath $OutDir -Recurse -Force }
New-Item -ItemType Directory -Path $OutDir | Out-Null
$OutDir = (Resolve-Path -LiteralPath $OutDir).Path
Copy-Item -LiteralPath $driverDir -Destination (Join-Path $OutDir "01spacecalibrator") -Recurse
Get-ChildItem -LiteralPath (Join-Path $OutDir "01spacecalibrator") -Recurse -File -Filter "*.log" | Remove-Item -Force
foreach ($name in $overlayFiles) { Copy-Item -LiteralPath (Join-Path $overlayDir $name) -Destination $OutDir }
Copy-Item -LiteralPath (Join-Path $repo "scripts\install.ps1"), (Join-Path $repo "scripts\uninstall.ps1") -Destination $OutDir
Copy-Item -LiteralPath (Join-Path $repo "README.md"), (Join-Path $repo "LICENSE"), (Join-Path $repo "NOTICE") -Destination $OutDir
Get-ChildItem -LiteralPath $OutDir -Recurse -File | ForEach-Object { Write-Host $_.FullName.Substring($OutDir.Length + 1) }

$installerExcludes = @("install.ps1", "uninstall.ps1")
$runtimeFiles = @("01spacecalibrator\bin\win64\space_calibrator_driver.log", "01spacecalibrator\bin\win64\space_calibrator_driver.old.log")
$files = @(Get-ChildItem -LiteralPath $OutDir -Recurse -File -Force | Where-Object { $installerExcludes -notcontains $_.Name } | ForEach-Object { $_.FullName.Substring($OutDir.Length + 1) }) + $runtimeFiles
$dirs = @(Get-ChildItem -LiteralPath $OutDir -Recurse -Directory -Force | ForEach-Object { $_.FullName.Substring($OutDir.Length + 1) } | Sort-Object -Property @{ Expression = { ($_ -split '\\').Count }; Descending = $true }, @{ Expression = { $_ }; Descending = $false })
$lines = @($files | Sort-Object -Unique | ForEach-Object { 'Delete "$INSTDIR\' + $_.Replace('$', '$$') + '"' }) + @($dirs | ForEach-Object { 'RMDir "$INSTDIR\' + $_.Replace('$', '$$') + '"' })
$uninstallList = Join-Path (Split-Path -Parent $OutDir) "uninstall_files.nsh"
[System.IO.File]::WriteAllLines($uninstallList, [string[]]$lines, (New-Object System.Text.UTF8Encoding($false)))
Write-Host "wrote $uninstallList ($($files.Count) files, $($dirs.Count) folders)"
