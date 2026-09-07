#!/usr/bin/env pwsh
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string] $Path,
    [string] $Repository = 'RealWhyKnot/OpenVR-SpaceCalibrator'
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Path)) {
    throw "Release body not found at $Path."
}

$text = [System.IO.File]::ReadAllText($Path)

$own = 'https://github.com/' + $Repository + '/'
$scrubbed = $text.Replace($own, '')

$pattern = '(?i)#[0-9]+\b|hyblocker|pushrax|github\.com/[^/\s]+/OpenVR-SpaceCalibrator'
$hits = [regex]::Matches($scrubbed, $pattern) | ForEach-Object { $_.Value } | Select-Object -Unique

if ($hits) {
    throw "Release body references upstream: $($hits -join ', '). Check .github/release-base and the commit subjects."
}

Write-Host "No upstream references in $Path."
