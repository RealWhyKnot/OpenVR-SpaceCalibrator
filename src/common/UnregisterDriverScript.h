#pragma once

#include "UpdateHelperScript.h"

#include <string>
#include <vector>

namespace spacecal {

	struct UnregisterDriverParams
	{
		std::string vrpathregExe;
		std::vector<std::string> driverDirs;
		std::string logPath;
	};

	inline std::string BuildUnregisterDriverScript(const UnregisterDriverParams& p)
	{
		std::string script;
		auto line = [&script](const std::string& text) {
			script += text + "\r\n";
		};
		line("$ErrorActionPreference = 'Stop'");
		line("function Log($m) { Add-Content -LiteralPath " + QuotePowerShell(p.logPath) +
		     " -Value (\"{0} {1}\" -f (Get-Date -Format s), $m) }");
		line("try {");
		line("    Log 'waiting for vrserver'");
		line("    while (Get-Process vrserver -ErrorAction SilentlyContinue) { Start-Sleep -Seconds 2 }");
		line("    while (Get-Process vrmonitor -ErrorAction SilentlyContinue) { Start-Sleep -Seconds 2 }");
		for (const std::string& dir : p.driverDirs) {
			line("    Log ('unregistering ' + " + QuotePowerShell(dir) + ")");
			line("    & " + QuotePowerShell(p.vrpathregExe) + " removedriver " + QuotePowerShell(dir));
			line("    $global:LASTEXITCODE = 0");
		}
		line("    Log 'done'");
		line("} catch {");
		line("    Log ($_ | Out-String)");
		line("    exit 1");
		line("}");
		return script;
	}

} // namespace spacecal
