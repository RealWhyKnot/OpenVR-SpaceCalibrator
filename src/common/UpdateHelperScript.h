#pragma once

#include <string>

namespace spacecal {

	struct UpdateHelperParams
	{
		unsigned long overlayPid = 0;
		std::string setupUrl;
		std::string shaUrl;
		std::string setupName;
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
		line("    $setup = Join-Path $staging " + QuotePowerShell(p.setupName));
		line("    Log ('downloading ' + " + QuotePowerShell(p.setupUrl) + ")");
		line("    Invoke-WebRequest -Uri " + QuotePowerShell(p.setupUrl) + " -OutFile $setup -UseBasicParsing");
		line("    Invoke-WebRequest -Uri " + QuotePowerShell(p.shaUrl) + " -OutFile \"$setup.sha256\" -UseBasicParsing");
		line("    $expected = ((Get-Content -LiteralPath \"$setup.sha256\" -Raw).Trim() -split '\\s+')[0].ToLowerInvariant()");
		line("    $actual = (Get-FileHash -LiteralPath $setup -Algorithm SHA256).Hash.ToLowerInvariant()");
		line("    if ($expected -ne $actual) { throw \"sha256 mismatch: expected $expected got $actual\" }");
		line("    Log 'verified'");
		line("    Log 'waiting for overlay " + std::to_string(p.overlayPid) + "'");
		line("    Wait-Process -Id " + std::to_string(p.overlayPid) + " -ErrorAction SilentlyContinue");
		line("    Log 'waiting for vrserver'");
		line("    while (Get-Process vrserver -ErrorAction SilentlyContinue) { Start-Sleep -Seconds 2 }");
		line("    Log ('running ' + $setup)");
		line("    $run = Start-Process -FilePath $setup -ArgumentList ('/S /D=' + " + QuotePowerShell(p.installDir) + ") -Wait -PassThru");
		line("    if ($run.ExitCode -ne 0) { throw ('setup exited with code ' + $run.ExitCode) }");
		line("    Log 'installed'");
		line("    Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction SilentlyContinue");
		line("} catch {");
		line("    Log ($_ | Out-String)");
		line("    exit 1");
		line("}");
		return script;
	}

} // namespace spacecal
