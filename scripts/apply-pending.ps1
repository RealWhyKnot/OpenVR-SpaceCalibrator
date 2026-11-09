#!/usr/bin/env pwsh
[CmdletBinding()]
param([string[]] $WaitFor = @('vrserver', 'vrmonitor', 'SpaceCalibrator'))

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$pendingDir = $PSScriptRoot
$buildDir = Split-Path -Parent $pendingDir
$tree = Join-Path $pendingDir 'tree'
$pendingJson = Join-Path $pendingDir 'pending.json'
$logPath = Join-Path $pendingDir 'apply-pending.log'
$driverDll = Join-Path $buildDir '01spacecalibrator\bin\win64\driver_01spacecalibrator.dll'
$retiredDir = Join-Path $pendingDir 'retired'

function Add-ApplierLog {
    [CmdletBinding()]
    [OutputType([void])]
    param([Parameter(Mandatory)][string] $Message)
    $stamp = (Get-Date).ToString('yyyy-MM-dd HH:mm:ss')
    [IO.File]::AppendAllText($logPath, "$stamp $Message`r`n")
}

function Get-Blocker {
    [CmdletBinding()]
    [OutputType([string[]])]
    param()
    $names = @(Get-Process -Name $WaitFor -ErrorAction SilentlyContinue | Select-Object -ExpandProperty ProcessName)
    return [string[]]@($names | Sort-Object -Unique)
}

function Read-PendingBuild {
    [CmdletBinding()]
    [OutputType([pscustomobject])]
    param()
    return [IO.File]::ReadAllText($pendingJson) | ConvertFrom-Json
}

function Read-DllBuildId {
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Mandatory)][string] $Path)
    if (-not (Test-Path -LiteralPath $Path)) { return '' }
    $text = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($Path))
    $match = [regex]::Match($text, 'SPACECAL_BUILD_ID=([0-9a-f]{16})\x00')
    if ($match.Success) { return $match.Groups[1].Value }
    return ''
}

function Copy-BuildFolder {
    [CmdletBinding()]
    [OutputType([void])]
    param([Parameter(Mandatory)][string] $From, [Parameter(Mandatory)][string] $To)
    $null = & robocopy $From $To /E /R:10 /W:3 /NJH /NJS /NP /NFL /NDL
    $code = $LASTEXITCODE
    $global:LASTEXITCODE = 0
    if ($code -ge 8) { throw [IO.IOException]::new("robocopy $From to $To failed with code $code") }
}

function Test-FileLocked {
    [CmdletBinding()]
    [OutputType([bool])]
    param([Parameter(Mandatory)][string] $Path)
    if (-not (Test-Path -LiteralPath $Path)) { return $false }
    try {
        $stream = [IO.File]::Open($Path, 'Open', 'ReadWrite', 'None')
        $stream.Dispose()
        return $false
    } catch [IO.IOException] {
        return $true
    }
}

function Move-LockedDriver {
    [CmdletBinding()]
    [OutputType([void])]
    param()
    if (-not (Test-FileLocked -Path $driverDll)) { return }
    $null = New-Item -ItemType Directory -Force -Path $retiredDir
    $leaf = Split-Path -Leaf $driverDll
    $target = Join-Path $retiredDir ((Get-Date).ToString('yyyyMMdd-HHmmss') + '-' + $leaf)
    [IO.File]::Move($driverDll, $target)
    Add-ApplierLog "the driver DLL is held open by another process; moved it to $target"
}

function Remove-RetiredDriver {
    [CmdletBinding()]
    [OutputType([void])]
    param()
    if (-not (Test-Path -LiteralPath $retiredDir)) { return }
    foreach ($file in @(Get-ChildItem -LiteralPath $retiredDir -File)) {
        try {
            Remove-Item -LiteralPath $file.FullName -Force
            Add-ApplierLog "removed retired $($file.Name)"
        } catch [IO.IOException] {
            Add-ApplierLog "retired $($file.Name) is still in use; leaving it"
        } catch [UnauthorizedAccessException] {
            Add-ApplierLog "retired $($file.Name) is still in use; leaving it"
        }
    }
}

$mutex = [Threading.Mutex]::new($false, 'Local\SpaceCalibratorApplyPending')
if (-not $mutex.WaitOne(0)) {
    $mutex.Dispose()
    exit 0
}

try {
    Add-ApplierLog "applier started (pid $PID)"
    $waitingOn = ''
    $failedAttempts = 0
    while (Test-Path -LiteralPath $pendingJson) {
        if ($failedAttempts -ge 5) {
            Add-ApplierLog 'giving up after 5 failed attempts; the next Space Calibrator start tries again'
            break
        }
        $running = @(Get-Blocker)
        if ($running.Count -gt 0) {
            $now = $running -join ', '
            if ($now -ne $waitingOn) {
                Add-ApplierLog "waiting for $now to exit"
                $waitingOn = $now
            }
            Start-Sleep -Seconds 3
            continue
        }
        $waitingOn = ''

        $pending = Read-PendingBuild
        Add-ApplierLog "applying build $($pending.version) (driver $($pending.buildId))"
        try {
            Move-LockedDriver
            Copy-BuildFolder -From (Join-Path $tree '01spacecalibrator') -To (Join-Path $buildDir '01spacecalibrator')
            Copy-BuildFolder -From (Join-Path $tree 'artifacts\Release') -To (Join-Path $buildDir 'artifacts\Release')
        } catch [IO.IOException] {
            Add-ApplierLog "copy failed: $($_.Exception.Message)"
            $failedAttempts++
            Start-Sleep -Seconds 10
            continue
        } catch [UnauthorizedAccessException] {
            Add-ApplierLog "copy failed: $($_.Exception.Message)"
            $failedAttempts++
            Start-Sleep -Seconds 10
            continue
        }

        $installed = Read-DllBuildId -Path $driverDll
        if ($installed -ne $pending.buildId) {
            Add-ApplierLog "installed driver reads '$installed', expected '$($pending.buildId)'; retrying"
            $failedAttempts++
            Start-Sleep -Seconds 10
            continue
        }

        $current = Read-PendingBuild
        if ($current.version -ne $pending.version) {
            Add-ApplierLog "build $($current.version) was staged during the copy; applying again"
            continue
        }
        Remove-Item -LiteralPath $pendingJson -Force
        Add-ApplierLog "applied build $($pending.version); driver $installed is in place"
        Remove-RetiredDriver
    }
    if (-not (Test-Path -LiteralPath $pendingJson)) { Add-ApplierLog 'nothing staged, exiting' }
} finally {
    $mutex.ReleaseMutex()
    $mutex.Dispose()
}

exit 0
