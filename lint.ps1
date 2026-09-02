#Requires -Version 5.1
[CmdletBinding()]
param(
	[switch]$Check,
	[switch]$FormatOnly,
	[switch]$ChangedOnly,
	[string]$ChangedBase = "origin/main"
)

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

function Resolve-RequiredCommand {
	param([string]$Name, [string]$InstallHint)
	$command = Get-Command $Name -ErrorAction SilentlyContinue
	if (-not $command) { throw "$Name was not found. $InstallHint" }
	return $command.Source
}

function Resolve-RunClangTidy {
	param([string]$ClangTidy)
	$command = Get-Command "run-clang-tidy" -ErrorAction SilentlyContinue
	if ($command) { return $command.Source }
	$dir = Split-Path -Parent $ClangTidy
	foreach ($candidate in @((Join-Path $dir "run-clang-tidy"), (Join-Path $dir "run-clang-tidy.py"))) {
		if (Test-Path -LiteralPath $candidate) { return $candidate }
	}
	throw "run-clang-tidy was not found next to clang-tidy or on PATH."
}

function Invoke-NativeQuiet {
	param([scriptblock]$Command, [string]$FailureMessage)
	$prevEap = $ErrorActionPreference
	$ErrorActionPreference = "Continue"
	try {
		& $Command 2>&1 | ForEach-Object {
			if ($_ -is [System.Management.Automation.ErrorRecord]) { Write-Host $_.Exception.Message } else { Write-Host $_ }
		}
	} finally {
		$ErrorActionPreference = $prevEap
	}
	if ($LASTEXITCODE -ne 0) { throw "$FailureMessage (exit $LASTEXITCODE)" }
}

function Get-ProjectCppFiles {
	$files = @(& git ls-files -co --exclude-standard -- "src/*.cpp" "src/*.h" "tests/*.cpp" "tests/*.h")
	if ($LASTEXITCODE -ne 0) { throw "Unable to list repository files." }
	return @($files | ForEach-Object { $_ -replace "\\", "/" } | Where-Object { $_ -ne "src/overlay/Resource.h" -and (Test-Path -LiteralPath $_) })
}

function Get-ChangedRepoFiles {
	param([string]$BaseRef)
	$paths = New-Object System.Collections.Generic.List[string]
	$mergeBase = & git merge-base HEAD $BaseRef 2>$null
	$haveBase = $LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($mergeBase)
	$global:LASTEXITCODE = 0
	$sources = @()
	if ($haveBase) { $sources += @($mergeBase.Trim() + "...HEAD") } else { Write-Host "changed-only: no merge-base with $BaseRef; using index and working tree." }
	$sources += @("--cached", "")
	foreach ($source in $sources) {
		$gitArgs = @("diff", "--name-only", "--diff-filter=ACMRTUXB")
		if (-not [string]::IsNullOrWhiteSpace($source)) { $gitArgs += $source }
		foreach ($path in @(& git @gitArgs)) {
			if (-not [string]::IsNullOrWhiteSpace($path)) { $paths.Add(($path -replace "\\", "/")) }
		}
	}
	foreach ($path in @(& git ls-files --others --exclude-standard)) {
		if (-not [string]::IsNullOrWhiteSpace($path)) { $paths.Add(($path -replace "\\", "/")) }
	}
	return @($paths | Sort-Object -Unique)
}

$changedLookup = $null
if ($ChangedOnly) {
	$changedLookup = @{}
	foreach ($path in (Get-ChangedRepoFiles -BaseRef $ChangedBase)) { $changedLookup[$path] = $true }
	Write-Host "changed-only: considering $($changedLookup.Count) changed file(s)."
}

$files = @(Get-ProjectCppFiles | Where-Object { $null -eq $changedLookup -or $changedLookup.ContainsKey($_) })
$clangFormat = Resolve-RequiredCommand "clang-format" "Install LLVM and put clang-format on PATH."

if ($files.Count -eq 0) {
	Write-Host "clang-format: no project C++ files to check."
} else {
	$listDir = Join-Path $PSScriptRoot "build\lint"
	New-Item -ItemType Directory -Force -Path $listDir | Out-Null
	$listPath = Join-Path $listDir "clang-format-files.txt"
	[System.IO.File]::WriteAllLines($listPath, $files, (New-Object System.Text.UTF8Encoding($false)))
	if ($Check) {
		Write-Host "clang-format: checking $($files.Count) file(s)."
		Invoke-NativeQuiet -Command { & $clangFormat "--dry-run" "--Werror" "--files=$listPath" } -FailureMessage "clang-format found files that need cleanup"
	} else {
		Write-Host "clang-format: formatting $($files.Count) file(s)."
		Invoke-NativeQuiet -Command { & $clangFormat "-i" "--files=$listPath" } -FailureMessage "clang-format failed"
	}
}

if ($FormatOnly) {
	Write-Host "format-only: skipped clang-tidy."
} else {
	$sources = @($files | Where-Object { $_ -match "\.cpp$" })
	$headersChanged = @($files | Where-Object { $_ -match "\.h$" }).Count -gt 0
	if ($ChangedOnly -and $sources.Count -eq 0 -and -not $headersChanged) {
		Write-Host "clang-tidy: no changed C++ files."
	} else {
		$clangTidy = Resolve-RequiredCommand "clang-tidy" "Install LLVM and put clang-tidy on PATH."
		$cmake = Resolve-RequiredCommand "cmake" "Install CMake."
		$ninja = Resolve-RequiredCommand "ninja" "Install Ninja."
		$python = Resolve-RequiredCommand "python" "Install Python."
		$runClangTidy = Resolve-RunClangTidy -ClangTidy $clangTidy
		$buildDir = Join-Path $PSScriptRoot "build\lint-tidy"
		Write-Host "clang-tidy: configuring compile database in $buildDir"
		Invoke-NativeQuiet -Command { & $cmake "-G" "Ninja" "-B" $buildDir "-S" $PSScriptRoot "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON" "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_POLICY_VERSION_MINIMUM=3.5" "-DSPACECAL_BUILD_TESTS=ON" "-Wno-dev" } -FailureMessage "clang-tidy compile database configure failed"
		$tidyArgs = @($runClangTidy, "-p", $buildDir, "-config-file", (Join-Path $PSScriptRoot ".clang-tidy"),
			"-header-filter", ".*OpenVR-SpaceCalibrator[/\\](src|tests)[/\\].*",
			"-clang-tidy-binary", $clangTidy, "-quiet", "-hide-progress",
			"-j", [string][Math]::Max(1, [Math]::Min([Environment]::ProcessorCount, 8)))
		if ($ChangedOnly -and -not $headersChanged) {
			foreach ($source in $sources) { $tidyArgs += (".*" + ([regex]::Escape($source) -replace "/", "[/\\]") + "$") }
		} else {
			$tidyArgs += ".*OpenVR-SpaceCalibrator[/\\](src|tests)[/\\].*\.cpp$"
		}
		if (-not $Check) { $tidyArgs += @("-fix", "-format", "-style=file") }
		Write-Host "clang-tidy: checking project translation units."
		Invoke-NativeQuiet -Command { & $python @tidyArgs } -FailureMessage "clang-tidy found diagnostics"
	}
}

if ($Check) { Write-Host "Lint check passed." } else { Write-Host "Lint cleanup complete." }
