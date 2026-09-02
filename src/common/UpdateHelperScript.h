#pragma once

#include <string>

namespace spacecal {

	struct UpdateHelperParams
	{
		unsigned long overlayPid = 0;
		std::string zipUrl;
		std::string shaUrl;
		std::string zipName;
		std::string stagingDir;
		std::string installDir;
		std::string logPath;
	};

	inline std::string QuotePowerShell(const std::string& value)
	{
		std::string quoted = "'";
		for (char c : value) {
			if (c == '\'') {
				quoted += "''";
			}
			else {
				quoted += c;
			}
		}
		return quoted + "'";
	}

	inline std::string BuildUpdateHelperScript(const UpdateHelperParams& p)
	{
		std::string script;
		auto line = [&script](const std::string& text) {
			script += text + "\r\n";
		};
		line("$ErrorActionPreference = 'Stop'");
		line("$ProgressPreference = 'SilentlyContinue'");
		line("function Log($m) { Add-Content -LiteralPath " + QuotePowerShell(p.logPath) +
		     " -Value (\"{0} {1}\" -f (Get-Date -Format s), $m) }");
		line("try {");
		line("    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12");
		line("    $staging = " + QuotePowerShell(p.stagingDir));
		line("    if (Test-Path -LiteralPath $staging) { Remove-Item -LiteralPath $staging -Recurse -Force }");
		line("    New-Item -ItemType Directory -Path $staging | Out-Null");
		line("    $zip = Join-Path $staging " + QuotePowerShell(p.zipName));
		line("    Log ('downloading ' + " + QuotePowerShell(p.zipUrl) + ")");
		line("    Invoke-WebRequest -Uri " + QuotePowerShell(p.zipUrl) + " -OutFile $zip -UseBasicParsing");
		line("    Invoke-WebRequest -Uri " + QuotePowerShell(p.shaUrl) + " -OutFile \"$zip.sha256\" -UseBasicParsing");
		line("    $expected = ((Get-Content -LiteralPath \"$zip.sha256\" -Raw).Trim() -split '\\s+')[0].ToLowerInvariant()");
		line("    $actual = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()");
		line("    if ($expected -ne $actual) { throw \"sha256 mismatch: expected $expected got $actual\" }");
		line("    Log 'verified'");
		line("    $payload = Join-Path $staging 'payload'");
		line("    Expand-Archive -LiteralPath $zip -DestinationPath $payload -Force");
		line("    if (-not (Test-Path -LiteralPath (Join-Path $payload 'SpaceCalibrator.exe'))) { throw 'SpaceCalibrator.exe missing from "
		     "archive' }");
		line("    Log 'waiting for overlay " + std::to_string(p.overlayPid) + "'");
		line("    Wait-Process -Id " + std::to_string(p.overlayPid) + " -ErrorAction SilentlyContinue");
		line("    Log 'waiting for vrserver'");
		line("    while (Get-Process vrserver -ErrorAction SilentlyContinue) { Start-Sleep -Seconds 2 }");
		line("    $attempt = 0");
		line("    while ($true) {");
		line("        try {");
		line("            Copy-Item -Path (Join-Path $payload '*') -Destination " + QuotePowerShell(p.installDir) + " -Recurse -Force");
		line("            break");
		line("        } catch {");
		line("            $attempt++");
		line("            if ($attempt -ge 10) { throw }");
		line("            Start-Sleep -Seconds 2");
		line("        }");
		line("    }");
		line("    Log 'installed'");
		line("    Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction SilentlyContinue");
		line("} catch {");
		line("    Log ($_ | Out-String)");
		line("    exit 1");
		line("}");
		return script;
	}

} // namespace spacecal
