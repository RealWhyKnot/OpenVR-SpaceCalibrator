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
foreach ($name in $overlayFiles) { Copy-Item -LiteralPath (Join-Path $overlayDir $name) -Destination $OutDir }
Copy-Item -LiteralPath (Join-Path $repo "scripts\install.ps1"), (Join-Path $repo "scripts\uninstall.ps1") -Destination $OutDir
Copy-Item -LiteralPath (Join-Path $repo "README.md"), (Join-Path $repo "LICENSE"), (Join-Path $repo "NOTICE") -Destination $OutDir
Get-ChildItem -LiteralPath $OutDir -Recurse -File | ForEach-Object { Write-Host $_.FullName.Substring($OutDir.Length + 1) }
