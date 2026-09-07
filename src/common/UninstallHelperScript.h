#pragma once

#include "UpdateHelperScript.h"

#include <string>
#include <vector>

namespace spacecal {

	struct UninstallHelperParams
	{
		unsigned long overlayPid = 0;
		std::string logPath;
		std::vector<std::string> runtimeDriverDirs;
		std::string uninstallExe;
		std::string programFilesDir;
		std::vector<std::string> registryKeys;
		std::string startMenuShortcut;
	};

	inline std::string BuildUninstallHelperScript(const UninstallHelperParams& p)
	{
		std::string script;
		auto line = [&script](const std::string& text) {
			script += text + "\r\n";
		};
		line("$ErrorActionPreference = 'Stop'");
		line("function Log($m) { Add-Content -LiteralPath " + QuotePowerShell(p.logPath) +
		     " -Value (\"{0} {1}\" -f (Get-Date -Format s), $m) }");
		line("try {");
		line("    Log 'waiting for overlay " + std::to_string(p.overlayPid) + "'");
		line("    Wait-Process -Id " + std::to_string(p.overlayPid) + " -ErrorAction SilentlyContinue");
		line("    Log 'waiting for vrserver'");
		line("    while (Get-Process vrserver -ErrorAction SilentlyContinue) { Start-Sleep -Seconds 2 }");
		line("    while (Get-Process vrmonitor -ErrorAction SilentlyContinue) { Start-Sleep -Seconds 2 }");
		for (const std::string& dir : p.runtimeDriverDirs) {
			line("    if (Test-Path -LiteralPath " + QuotePowerShell(dir) + ") {");
			line("        Log ('removing runtime driver ' + " + QuotePowerShell(dir) + ")");
			line("        Remove-Item -LiteralPath " + QuotePowerShell(dir) + " -Recurse -Force");
			line("    }");
		}
		if (!p.uninstallExe.empty()) {
			line("    if (Test-Path -LiteralPath " + QuotePowerShell(p.uninstallExe) + ") {");
			line("        Log ('running uninstaller ' + " + QuotePowerShell(p.uninstallExe) + ")");
			line("        Start-Process -FilePath " + QuotePowerShell(p.uninstallExe) + " -ArgumentList '/S' -Wait");
			line("    }");
		}
		if (!p.programFilesDir.empty()) {
			line("    if (Test-Path -LiteralPath " + QuotePowerShell(p.programFilesDir) + ") {");
			line("        Log ('removing install folder ' + " + QuotePowerShell(p.programFilesDir) + ")");
			line("        Remove-Item -LiteralPath " + QuotePowerShell(p.programFilesDir) + " -Recurse -Force");
			line("    }");
		}
		for (const std::string& key : p.registryKeys) {
			line("    if (Test-Path -Path " + QuotePowerShell(key) + ") {");
			line("        Log ('removing registry key ' + " + QuotePowerShell(key) + ")");
			line("        Remove-Item -Path " + QuotePowerShell(key) + " -Recurse -Force");
			line("    }");
		}
		if (!p.startMenuShortcut.empty()) {
			line("    if (Test-Path -LiteralPath " + QuotePowerShell(p.startMenuShortcut) + ") {");
			line("        Remove-Item -LiteralPath " + QuotePowerShell(p.startMenuShortcut) + " -Force");
			line("    }");
		}
		line("    Log 'removed'");
		line("} catch {");
		line("    Log ($_ | Out-String)");
		line("    exit 1");
		line("}");
		return script;
	}

} // namespace spacecal
