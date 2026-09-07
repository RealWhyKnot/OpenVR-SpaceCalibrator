#include "stdafx.h"
#include "LegacyInstall.h"

#include <openvr.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>

static bool s_isGitHubVersionInstalled = false;

bool IsGithubVersionInstalled()
{
	return s_isGitHubVersionInstalled;
}

std::string GetRegistryString(const HKEY hKeyGroup, const char* szRegistryKey, const char* szRegistryPropKey) noexcept
{
	DWORD dwType = REG_SZ;
	HKEY hKey = 0;
	char buffer[4 * 1024] = {};
	DWORD bufferSize = sizeof(buffer);
	LSTATUS lResult = RegOpenKeyExA(hKeyGroup, szRegistryKey, 0, KEY_QUERY_VALUE, &hKey);
	if (lResult == ERROR_SUCCESS) {
		lResult = RegQueryValueExA(hKey, szRegistryPropKey, NULL, &dwType, (LPBYTE)&buffer, &bufferSize);
		if (lResult == ERROR_SUCCESS) {
			return buffer;
		}
	}
	return "";
}

bool UninstallGithubSpaceCalibrator()
{
	std::string uninstallKeyValue = GetRegistryString(
	    HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\OpenVRSpaceCalibrator", "UninstallString");
	if (uninstallKeyValue.empty()) {
		uninstallKeyValue = GetRegistryString(HKEY_LOCAL_MACHINE,
		                                      "SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\OpenVRSpaceCalibrator",
		                                      "UninstallString");
		if (uninstallKeyValue.empty()) {
			uninstallKeyValue = GetRegistryString(
			    HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\OpenVRSpaceCalibrator", "UninstallString");
		}
	}

	printf("uninst: %s\n", uninstallKeyValue.c_str());

	if (!uninstallKeyValue.empty()) {
		SHELLEXECUTEINFO shExInfo = {0};
		shExInfo.cbSize = sizeof(shExInfo);
		shExInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
		shExInfo.hwnd = 0;
		shExInfo.lpVerb = L"open";
		shExInfo.lpFile = L"ms-settings:appsfeatures";
		shExInfo.lpParameters = L"";
		shExInfo.lpDirectory = 0;
		shExInfo.nShow = SW_SHOW;
		shExInfo.hInstApp = 0;

		if (ShellExecuteExW(&shExInfo)) {
			return true;
		}
	}

	return false;
}

void CheckGithubVersionInstalledOnSteam()
{
	std::string uninstallKeyValue = GetRegistryString(
	    HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\OpenVRSpaceCalibrator", "UninstallString");
	if (!uninstallKeyValue.empty()) {
		s_isGitHubVersionInstalled = true;
		return;
	}

	uninstallKeyValue =
	    GetRegistryString(HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\OpenVRSpaceCalibrator",
	                      "UninstallString");
	if (!uninstallKeyValue.empty()) {
		s_isGitHubVersionInstalled = true;
		return;
	}

	uninstallKeyValue = GetRegistryString(
	    HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\OpenVRSpaceCalibrator", "UninstallString");
	if (!uninstallKeyValue.empty()) {
		s_isGitHubVersionInstalled = true;
		return;
	}

	char cVrRuntimePath[MAX_PATH] = {0};
	unsigned int szPathLen = 0;
	vr::VR_GetRuntimePath(cVrRuntimePath, MAX_PATH, &szPathLen);
	if (szPathLen > 0 && std::filesystem::is_directory(cVrRuntimePath)) {
		std::filesystem::path spaceCalibratorGitHubDriverPath = cVrRuntimePath;
		spaceCalibratorGitHubDriverPath = spaceCalibratorGitHubDriverPath / "drivers" / "01spacecalibrator";
		if (std::filesystem::is_directory(spaceCalibratorGitHubDriverPath)) {
			s_isGitHubVersionInstalled = true;
		}
	}
}
