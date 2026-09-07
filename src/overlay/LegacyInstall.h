#pragma once

#include <Windows.h>
#include <string>
#include <vector>

struct LegacyInstallInfo
{
	std::string uninstallExe;
	std::string programFilesDir;
	std::vector<std::string> runtimeDriverDirs;
	std::vector<std::string> registryKeys;
	bool steamAppInstalled = false;

	bool AnyRemovable() const
	{
		return !uninstallExe.empty() || !programFilesDir.empty() || !runtimeDriverDirs.empty() || !registryKeys.empty();
	}
};

std::string GetRegistryString(const HKEY hKeyGroup, const char* szRegistryKey, const char* szRegistryPropKey) noexcept;
void CheckGithubVersionInstalledOnSteam();
bool IsGithubVersionInstalled();
bool UninstallGithubSpaceCalibrator();
LegacyInstallInfo DetectLegacyInstalls();
bool QueueLegacyUninstall(const LegacyInstallInfo& info, std::string& error);
