#Requires -Version 5.1
[CmdletBinding()]
param(
	[ValidateSet("dev", "beta", "release")]
	[string]$Channel = "dev",
	[string]$Version = "",
	[switch]$SkipConfigure,
	[switch]$SkipTests
)

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

$hooksPath = & git config --get core.hooksPath 2>$null
$global:LASTEXITCODE = 0
if ([string]::IsNullOrWhiteSpace($hooksPath) -and (Test-Path -LiteralPath (Join-Path $PSScriptRoot ".githooks"))) {
	& git config core.hooksPath .githooks
	Write-Host "git hooks enabled (.githooks)"
}

if ([string]::IsNullOrWhiteSpace($Version)) {
	$stateDir = Join-Path $PSScriptRoot "build"
	New-Item -ItemType Directory -Force -Path $stateDir | Out-Null
	$statePath = Join-Path $stateDir "local_build_state.json"
	$today = (Get-Date).ToString("yyyy.M.d")
	$counter = 0
	if (Test-Path -LiteralPath $statePath) {
		$state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
		if ($state.date -eq $today) { $counter = [int]$state.counter + 1 }
	}
	$suffix = ([guid]::NewGuid().ToString("N").Substring(0, 4)).ToUpperInvariant()
	$Version = "$today.$counter-$suffix"
	$stateJson = @{ date = $today; counter = $counter } | ConvertTo-Json -Compress
	[System.IO.File]::WriteAllText($statePath, $stateJson, (New-Object System.Text.UTF8Encoding($false)))
}
[System.IO.File]::WriteAllText((Join-Path $PSScriptRoot "version.txt"), $Version, (New-Object System.Text.UTF8Encoding($false)))
Write-Host "build $Version ($Channel)"

$pendingDir = Join-Path $PSScriptRoot "build\pending"
$pendingJson = Join-Path $pendingDir "pending.json"
$applierMutex = "Local\SpaceCalibratorApplyPending"
if (Test-Path -LiteralPath $pendingJson) { Remove-Item -LiteralPath $pendingJson -Force }

$inUse = @(Get-Process -Name vrserver, vrmonitor, SpaceCalibrator -ErrorAction SilentlyContinue)
$driverDll = Join-Path $PSScriptRoot "build\01spacecalibrator\bin\win64\driver_01spacecalibrator.dll"
$driverLocked = $false
if (Test-Path -LiteralPath $driverDll) {
	try {
		$stream = [System.IO.File]::Open($driverDll, "Open", "ReadWrite", "None")
		$stream.Dispose()
	}
	catch [System.IO.IOException] {
		$driverLocked = $true
	}
}
$staging = $inUse.Count -gt 0 -or $driverLocked
$buildDir = "build"
if ($staging) {
	$buildDir = Join-Path $pendingDir "tree"
	if ($inUse.Count -gt 0) {
		Write-Host "SteamVR or Space Calibrator is running, so this build goes to $buildDir and is swapped in when SteamVR closes"
	}
	else {
		Write-Host "Another process (usually Steam) has the driver DLL open, so this build goes to $buildDir and the old DLL is moved aside"
	}
}

if (-not $SkipConfigure -or -not (Test-Path -LiteralPath (Join-Path $buildDir "CMakeCache.txt"))) {
	& cmake -A x64 -B $buildDir -S . -Wno-dev "-DCMAKE_POLICY_VERSION_MINIMUM=3.5" "-DSPACECAL_VERSION=$Version" "-DSPACECAL_CHANNEL=$Channel"
	if ($LASTEXITCODE -ne 0) { throw "cmake configure failed (exit $LASTEXITCODE)" }
}

& cmake --build $buildDir --config Release
if ($LASTEXITCODE -ne 0) { throw "build failed (exit $LASTEXITCODE)" }

if (-not $SkipTests) {
	& ctest --test-dir $buildDir -C Release --output-on-failure
	if ($LASTEXITCODE -ne 0) { throw "tests failed (exit $LASTEXITCODE)" }
}

if ($staging) {
	if ($SkipTests) {
		Write-Host "Not staged: a staged build has to pass the tests first"
		exit 0
	}
	$header = [System.IO.File]::ReadAllText((Join-Path $buildDir "generated\BuildId.h"))
	$match = [regex]::Match($header, 'SPACECAL_BUILD_ID "([0-9a-f]{16})"')
	if (-not $match.Success) { throw "no build ID in $buildDir\generated\BuildId.h" }
	Copy-Item -LiteralPath (Join-Path $PSScriptRoot "scripts\apply-pending.ps1") -Destination $pendingDir -Force
	$pendingState = @{ buildId = $match.Groups[1].Value; version = $Version } | ConvertTo-Json -Compress -Depth 2
	[System.IO.File]::WriteAllText($pendingJson, $pendingState, (New-Object System.Text.UTF8Encoding($false)))
	Write-Host "staged build $Version (driver $($match.Groups[1].Value))"

	$existing = $null
	if ([System.Threading.Mutex]::TryOpenExisting($applierMutex, [ref]$existing)) {
		$existing.Dispose()
		Write-Host "the applier is already waiting for SteamVR to close"
	}
	else {
		$applier = Join-Path $pendingDir "apply-pending.ps1"
		$arguments = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-WindowStyle", "Hidden", "-File", "`"$applier`"")
		Start-Process -FilePath "powershell.exe" -ArgumentList $arguments -WindowStyle Hidden
		Write-Host "the applier is waiting for SteamVR to close; it logs to $pendingDir\apply-pending.log"
	}
}
