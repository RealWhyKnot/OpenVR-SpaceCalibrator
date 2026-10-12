#pragma once

#include <string>

namespace spacecal {

	inline constexpr const char* UpdateHelperMutexName = "Local\\SpaceCalibratorUpdateHelper";

	struct UpdateHelperParams
	{
		unsigned long overlayPid = 0;
		std::string fromVersion;
		std::string tag;
		std::string appMutex;
		std::string setupUrl;
		std::string shaUrl;
		std::string setupName;
		std::string stagingDir;
		std::string installDir;
		std::string logPath;
	};

	inline bool IsTypographicSingleQuote(const std::string& value, size_t at)
	{
		if (at + 2 >= value.size()) return false;
		const auto byte = [&value](size_t i) {
			return static_cast<unsigned char>(value[i]);
		};
		return byte(at) == 0xE2 && byte(at + 1) == 0x80 && byte(at + 2) >= 0x98 && byte(at + 2) <= 0x9B;
	}

	inline std::string QuotePowerShell(const std::string& value)
	{
		std::string quoted = "'";
		for (size_t i = 0; i < value.size(); ++i) {
			if (value[i] == '\'') {
				quoted += "''";
			}
			else if (IsTypographicSingleQuote(value, i)) {
				const std::string quote = value.substr(i, 3);
				quoted += quote + quote;
				i += 2;
			}
			else {
				quoted += value[i];
			}
		}
		return quoted + "'";
	}

	inline std::string BuildUpdateHelperScript(const UpdateHelperParams& p)
	{
		std::string script = "\xEF\xBB\xBF";
		auto line = [&script](const std::string& text) {
			script += text + "\r\n";
		};
		line("$ErrorActionPreference = 'Stop'");
		line("$ProgressPreference = 'SilentlyContinue'");
		line("function Log($m) { Add-Content -LiteralPath " + QuotePowerShell(p.logPath) +
		     " -Value (\"{0} {1}\" -f (Get-Date -Format s), $m) -Encoding UTF8 }");
		line("function Busy {");
		line("    if (Get-Process vrserver -ErrorAction SilentlyContinue) { return $true }");
		line("    $app = $null");
		line("    try { if ([System.Threading.Mutex]::TryOpenExisting(" + QuotePowerShell(p.appMutex) +
		     ", [ref]$app)) { $app.Dispose(); return $true } } catch [System.UnauthorizedAccessException] { return $true }");
		line("    return $false");
		line("}");
		line("try { $lock = New-Object System.Threading.Mutex($false, " + QuotePowerShell(UpdateHelperMutexName) +
		     "); $owned = $lock.WaitOne(0) } catch [System.Threading.AbandonedMutexException] { $owned = $true } catch { exit 0 }");
		line("if (-not $owned) { exit 0 }");
		line("try {");
		line("    Log " + QuotePowerShell("update " + p.fromVersion + " to " + p.tag));
		line("    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12");
		line("    $staging = " + QuotePowerShell(p.stagingDir));
		line("    if (Test-Path -LiteralPath $staging) { Remove-Item -LiteralPath $staging -Recurse -Force }");
		line("    New-Item -ItemType Directory -Path $staging | Out-Null");
		line("    $setup = Join-Path $staging " + QuotePowerShell(p.setupName));
		line("    Log ('downloading ' + " + QuotePowerShell(p.setupUrl) + ")");
		line("    Invoke-WebRequest -Uri " + QuotePowerShell(p.setupUrl) + " -OutFile $setup -UseBasicParsing");
		line("    Invoke-WebRequest -Uri " + QuotePowerShell(p.shaUrl) + " -OutFile \"$setup.sha256\" -UseBasicParsing");
		line("    $expected = ((Get-Content -LiteralPath \"$setup.sha256\" -Raw).Trim() -split '\\s+')[0].ToLowerInvariant()");
		line("    $hasher = [System.Security.Cryptography.SHA256]::Create()");
		line("    $stream = [System.IO.File]::OpenRead($setup)");
		line("    try { $digest = $hasher.ComputeHash($stream) } finally { $stream.Dispose() }");
		line("    $actual = ([System.BitConverter]::ToString($digest) -replace '-', '').ToLowerInvariant()");
		line("    if ($expected -ne $actual) { throw \"sha256 mismatch: expected $expected got $actual\" }");
		line("    Log 'verified'");
		line("    Log 'waiting for overlay " + std::to_string(p.overlayPid) + "'");
		line("    Wait-Process -Id " + std::to_string(p.overlayPid) + " -ErrorAction SilentlyContinue");
		line("    $attempt = 0");
		line("    while ($true) {");
		line("        $attempt++");
		line("        Log 'waiting for SteamVR and Space Calibrator to close'");
		line("        while (Busy) { Start-Sleep -Seconds 2 }");
		line("        Log ('running ' + $setup)");
		line("        $psi = New-Object System.Diagnostics.ProcessStartInfo");
		line("        $psi.FileName = $setup");
		line("        $psi.Arguments = '/S /D=' + " + QuotePowerShell(p.installDir));
		line("        $psi.UseShellExecute = $false");
		line("        $run = [System.Diagnostics.Process]::Start($psi)");
		line("        $run.WaitForExit()");
		line("        $code = $run.ExitCode");
		line("        Log ('setup exited with code ' + $code)");
		line("        if ($code -eq 0) { break }");
		line("        if (-not ($code -eq 5 -or $code -eq 6) -or $attempt -ge 5) { throw ('setup exited with code ' + $code) }");
		line("        Start-Sleep -Seconds 5");
		line("    }");
		line("    Log 'installed'");
		line("    Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction SilentlyContinue");
		line("} catch {");
		line("    Log ($_ | Out-String)");
		line("    Log ('failed: ' + ($_.Exception.Message -replace '\\s+', ' ').Trim())");
		line("    exit 1");
		line("}");
		return script;
	}

} // namespace spacecal
