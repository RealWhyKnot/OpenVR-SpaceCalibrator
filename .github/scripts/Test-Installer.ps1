#Requires -Version 5.1
[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)][string]$Setup,
	[Parameter(Mandatory = $true)][string]$Version
)

$ErrorActionPreference = "Stop"
$script:failures = @()

function Assert([bool]$Condition, [string]$Message) {
	if ($Condition) {
		Write-Host "ok   $Message"
	}
	else {
		$script:failures += $Message
		Write-Host "FAIL $Message"
	}
}

$dir = Join-Path $env:TEMP "Space Cal Smoke"
$arpPath = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\SpaceCalibratorSmoothing"
$settingsPath = "HKCU:\Software\Classes\Local Settings\Software\OpenVR-SpaceCalibrator"
$localData = Join-Path $env:LOCALAPPDATA "SpaceCalibrator"
$lowData = Join-Path $env:USERPROFILE "AppData\LocalLow\SpaceCalibrator"
$shortcut = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\Space Calibrator.lnk"

$existing = @()
if (Test-Path -Path $arpPath) { $existing += "uninstall entry" }
if (Test-Path -Path $settingsPath) { $existing += "settings key" }
if (Test-Path -LiteralPath $localData) { $existing += $localData }
if (Test-Path -LiteralPath $lowData) { $existing += $lowData }
if (Test-Path -LiteralPath (Join-Path $env:LOCALAPPDATA "openvr\openvrpaths.vrpath")) { $existing += "openvrpaths.vrpath" }
if ($existing.Count -gt 0) {
	throw "refusing to run: this machine has Space Calibrator or SteamVR state that the test would destroy ($($existing -join ', '))"
}

if (Test-Path -LiteralPath $dir) { Remove-Item -LiteralPath $dir -Recurse -Force }

New-Item -ItemType Directory -Force -Path $localData | Out-Null
Set-Content -LiteralPath (Join-Path $localData "update.log") -Value "seed"
New-Item -ItemType Directory -Force -Path (Join-Path $lowData "Logs") | Out-Null
Set-Content -LiteralPath (Join-Path $lowData "Logs\smoke.log") -Value "seed"
New-Item -Path $settingsPath -Force | Out-Null
New-ItemProperty -Path $settingsPath -Name "Config" -Value "{}" -PropertyType String -Force | Out-Null

$install = Start-Process -FilePath $Setup -ArgumentList ("/S /D=" + $dir) -Wait -PassThru
Assert ($install.ExitCode -eq 0) "installer exit code 0 (got $($install.ExitCode))"

$installedFiles = @(
	"SpaceCalibrator.exe",
	"openvr_api.dll",
	"manifest.vrmanifest",
	"icon.png",
	"taskbar_icon.png",
	"README.md",
	"LICENSE",
	"NOTICE",
	"Uninstall.exe",
	"01spacecalibrator\driver.vrdrivermanifest",
	"01spacecalibrator\bin\win64\driver_01spacecalibrator.dll"
)
foreach ($f in $installedFiles) {
	Assert (Test-Path -LiteralPath (Join-Path $dir $f)) "installed $f"
}
foreach ($f in @("install.ps1", "uninstall.ps1")) {
	Assert (-not (Test-Path -LiteralPath (Join-Path $dir $f))) "excluded $f"
}

$arp = Get-ItemProperty -Path $arpPath
Assert ($arp.DisplayName -eq "Space Calibrator") "DisplayName (got $($arp.DisplayName))"
Assert ($arp.DisplayVersion -eq $Version) "DisplayVersion $Version (got $($arp.DisplayVersion))"
Assert ($arp.Publisher -eq "RealWhyKnot") "Publisher (got $($arp.Publisher))"
Assert ($arp.InstallLocation -eq $dir) "InstallLocation (got $($arp.InstallLocation))"
Assert ($arp.DisplayIcon -eq (Join-Path $dir "SpaceCalibrator.exe")) "DisplayIcon (got $($arp.DisplayIcon))"
Assert ($arp.UninstallString -eq ('"' + (Join-Path $dir "Uninstall.exe") + '"')) "UninstallString (got $($arp.UninstallString))"
Assert ($arp.QuietUninstallString -eq ('"' + (Join-Path $dir "Uninstall.exe") + '" /S')) "QuietUninstallString (got $($arp.QuietUninstallString))"
Assert ($arp.NoModify -eq 1) "NoModify"
Assert ($arp.NoRepair -eq 1) "NoRepair"
Assert ($arp.EstimatedSize -gt 0) "EstimatedSize (got $($arp.EstimatedSize))"
Assert (Test-Path -LiteralPath $shortcut) "start menu shortcut"

$uninstall = Start-Process -FilePath (Join-Path $dir "Uninstall.exe") -ArgumentList ("/S _?=" + $dir) -Wait -PassThru
Assert ($uninstall.ExitCode -eq 0) "uninstaller exit code 0 (got $($uninstall.ExitCode))"

Assert (-not (Test-Path -Path $arpPath)) "uninstall entry removed"
Assert (-not (Test-Path -Path $settingsPath)) "settings key removed"
Assert (-not (Test-Path -LiteralPath $localData)) "local appdata removed"
Assert (-not (Test-Path -LiteralPath $lowData)) "locallow logs removed"
Assert (-not (Test-Path -LiteralPath $shortcut)) "start menu shortcut removed"

$leftovers = @()
if (Test-Path -LiteralPath $dir) {
	$leftovers = @(Get-ChildItem -LiteralPath $dir -Recurse -Force | Where-Object { $_.Name -ne "Uninstall.exe" })
}
Assert ($leftovers.Count -eq 0) "install dir emptied (left: $(($leftovers | ForEach-Object Name) -join ', '))"
if (Test-Path -LiteralPath $dir) { Remove-Item -LiteralPath $dir -Recurse -Force }

if ($script:failures.Count -gt 0) {
	throw "installer smoke test failed: $($script:failures -join '; ')"
}
Write-Host "installer smoke test passed"
