#include "stdafx.h"
#include "LegacyInstall.h"
#include "UninstallHelperScript.h"

#include <openvr.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>

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

LegacyInstallInfo DetectLegacyInstalls()
{
	LegacyInstallInfo info;

	struct UninstallKey
	{
		HKEY root;
		const char* path;
		const char* psPath;
	};
	const UninstallKey uninstallKeys[] = {
	    {HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\OpenVRSpaceCalibrator",
	     "HKLM:\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\OpenVRSpaceCalibrator"},
	    {HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\OpenVRSpaceCalibrator",
	     "HKLM:\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\OpenVRSpaceCalibrator"},
	    {HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\OpenVRSpaceCalibrator",
	     "HKCU:\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\OpenVRSpaceCalibrator"},
	};

	for (const auto& key : uninstallKeys) {
		std::string uninstallString = GetRegistryString(key.root, key.path, "UninstallString");
		if (uninstallString.empty()) continue;
		info.registryKeys.push_back(key.psPath);
		if (info.uninstallExe.empty()) {
			std::string exe = uninstallString;
			if (!exe.empty() && exe.front() == '"') {
				const auto closing = exe.find('"', 1);
				exe = closing != std::string::npos ? exe.substr(1, closing - 1) : exe.substr(1);
			}
			if (std::filesystem::exists(exe)) {
				info.uninstallExe = exe;
			}
		}
	}

	if (!GetRegistryString(HKEY_LOCAL_MACHINE, "Software\\SpaceCalibrator\\Main", "").empty() ||
	    !GetRegistryString(HKEY_LOCAL_MACHINE, "Software\\SpaceCalibrator\\Driver", "").empty()) {
		info.registryKeys.push_back("HKLM:\\Software\\SpaceCalibrator");
	}

	PWSTR programFilesRaw = nullptr;
	if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramFilesX64, 0, nullptr, &programFilesRaw))) {
		std::filesystem::path legacyDir = std::filesystem::path(programFilesRaw) / L"SpaceCalibrator";
		if (std::filesystem::is_directory(legacyDir)) {
			info.programFilesDir = legacyDir.string();
		}
	}
	if (programFilesRaw) CoTaskMemFree(programFilesRaw);

	char cVrRuntimePath[MAX_PATH] = {0};
	unsigned int szPathLen = 0;
	vr::VR_GetRuntimePath(cVrRuntimePath, MAX_PATH, &szPathLen);
	if (szPathLen > 0 && std::filesystem::is_directory(cVrRuntimePath)) {
		for (const char* driverName : {"01spacecalibrator", "000spacecalibrator"}) {
			std::filesystem::path driverDir = std::filesystem::path(cVrRuntimePath) / "drivers" / driverName;
			if (std::filesystem::is_directory(driverDir)) {
				info.runtimeDriverDirs.push_back(driverDir.string());
			}
		}
	}

	info.steamAppInstalled = GetRegistryString(HKEY_CURRENT_USER, "Software\\Valve\\Steam\\Apps\\3368750", "Name") != "" ||
	                         GetRegistryString(HKEY_CURRENT_USER, "Software\\Valve\\Steam\\Apps\\3368750", "Installed") != "";

	return info;
}

bool QueueLegacyUninstall(const LegacyInstallInfo& info, std::string& error)
{
	PWSTR localAppDataRaw = nullptr;
	if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppDataRaw))) {
		error = "could not resolve local appdata";
		return false;
	}
	std::filesystem::path base = std::filesystem::path(localAppDataRaw) / L"SpaceCalibrator";
	CoTaskMemFree(localAppDataRaw);

	std::error_code ec;
	std::filesystem::create_directories(base, ec);

	spacecal::UninstallHelperParams params;
	params.overlayPid = GetCurrentProcessId();
	params.logPath = (base / L"uninstall-old.log").string();
	params.runtimeDriverDirs = info.runtimeDriverDirs;
	params.uninstallExe = info.uninstallExe;
	params.programFilesDir = info.programFilesDir;
	params.registryKeys = info.registryKeys;
	PWSTR programsRaw = nullptr;
	if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_CommonPrograms, 0, nullptr, &programsRaw))) {
		params.startMenuShortcut = (std::filesystem::path(programsRaw) / L"Space Calibrator.lnk").string();
	}
	if (programsRaw) CoTaskMemFree(programsRaw);

	const std::filesystem::path scriptPath = base / L"uninstall-old.ps1";
	{
		std::ofstream script(scriptPath, std::ios::binary | std::ios::trunc);
		if (!script) {
			error = "could not write " + scriptPath.string();
			return false;
		}
		script << spacecal::BuildUninstallHelperScript(params);
	}

	std::wstring parameters = L"-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File \"" + scriptPath.wstring() + L"\"";
	SHELLEXECUTEINFOW sei = {};
	sei.cbSize = sizeof(sei);
	sei.fMask = SEE_MASK_NOCLOSEPROCESS;
	sei.lpVerb = L"runas";
	sei.lpFile = L"powershell.exe";
	sei.lpParameters = parameters.c_str();
	sei.nShow = SW_HIDE;
	if (!ShellExecuteExW(&sei)) {
		error = "elevation was declined or failed (error " + std::to_string(GetLastError()) + ")";
		return false;
	}
	if (sei.hProcess) CloseHandle(sei.hProcess);
	printf("queued legacy uninstall, log at %s\n", params.logPath.c_str());
	return true;
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
