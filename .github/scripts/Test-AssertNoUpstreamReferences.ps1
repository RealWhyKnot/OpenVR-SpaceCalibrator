#!/usr/bin/env pwsh
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$script:failures = 0
$assert = Join-Path $PSScriptRoot 'Assert-NoUpstreamReferences.ps1'

function Test-Body {
    param([string] $Name, [string] $Body, [bool] $ShouldThrow)
    $path = Join-Path ([System.IO.Path]::GetTempPath()) ("scrub-" + [System.Guid]::NewGuid().ToString('N') + ".md")
    [System.IO.File]::WriteAllText($path, $Body, (New-Object System.Text.UTF8Encoding($false)))
    $threw = $false
    try {
        & $assert -Path $path | Out-Null
    } catch {
        $threw = $true
    } finally {
        Remove-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue
    }
    if ($threw -eq $ShouldThrow) {
        Write-Host "ok: $Name"
    } else {
        $verb = if ($ShouldThrow) { 'should have thrown' } else { 'should not have thrown' }
        Write-Host "FAIL: $Name ($verb)" -ForegroundColor Red
        $script:failures++
    }
}

Test-Body 'a clean body passes' "## What's Changed`n- feat: a thing by @RealWhyKnot in abc1234`n" $false
Test-Body 'our own compare link is allowed' "**Full Changelog**: https://github.com/RealWhyKnot/OpenVR-SpaceCalibrator/compare/v1...v2`n" $false
Test-Body 'an upstream repo url is rejected' "See https://github.com/pushrax/OpenVR-SpaceCalibrator/pull/9`n" $true
Test-Body 'a fork owner url is rejected' "https://github.com/someoneelse/OpenVR-SpaceCalibrator`n" $true
Test-Body 'an upstream handle is rejected' "thanks to hyblocker for the fix`n" $true
Test-Body 'a bare issue number is rejected' "fixes #42 upstream`n" $true

Write-Host ''
if ($script:failures -gt 0) {
    Write-Host "$script:failures check(s) failed." -ForegroundColor Red
    exit 1
}
Write-Host 'All checks passed.' -ForegroundColor Green
exit 0
