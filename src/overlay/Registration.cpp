#include "stdafx.h"
#include "Registration.h"
#include "Constants.h"
#include "DriverConflictState.h"
#include "RegistrationPlan.h"
#include "VRSession.h"

#include <openvr.h>
#include <shlobj.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

	std::filesystem::path KnownFolder(REFKNOWNFOLDERID id)
	{
		PWSTR raw = nullptr;
		std::filesystem::path result;
		if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &raw)) && raw) {
			result = raw;
		}
		if (raw) CoTaskMemFree(raw);
		return result;
	}

	std::string ReadFileText(const std::filesystem::path& path)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file) return {};
		return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
	}

	std::wstring Widen(const std::string& text)
	{
		if (text.empty()) return {};
		const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
		std::wstring wide(size, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), wide.data(), size);
		return wide;
	}

	int RunProcess(std::wstring commandLine)
	{
		STARTUPINFOW startup = {};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION process = {};
		if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
			return -1;
		}
		CloseHandle(process.hThread);
		WaitForSingleObject(process.hProcess, INFINITE);
		DWORD code = 1;
		GetExitCodeProcess(process.hProcess, &code);
		CloseHandle(process.hProcess);
		return static_cast<int>(code);
	}

	int RunVrpathreg(const std::string& exe, const wchar_t* verb, const std::string& dir)
	{
		return RunProcess(L"\"" + Widen(exe) + L"\" " + verb + L" \"" + Widen(dir) + L"\"");
	}

	bool LoadOpenVRPaths(spacecal::OpenVRPaths& paths)
	{
		const std::filesystem::path localAppData = KnownFolder(FOLDERID_LocalAppData);
		if (localAppData.empty()) return false;
		const std::string text = ReadFileText(localAppData / "openvr" / "openvrpaths.vrpath");
		if (text.empty()) return false;
		return spacecal::ParseOpenVRPaths(text, paths);
	}

	std::string FindVrpathreg(const spacecal::RegistrationPlan& plan)
	{
		for (const std::string& candidate : plan.vrpathregCandidates) {
			std::error_code ec;
			if (std::filesystem::is_regular_file(std::filesystem::path(candidate), ec)) return candidate;
		}
		return {};
	}

	std::filesystem::path ExeDir()
	{
		wchar_t buffer[MAX_PATH];
		const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
		return std::filesystem::path(std::wstring(buffer, length)).parent_path();
	}

	int FinishVRSetup()
	{
		auto vrErr = vr::VRInitError_None;
		vr::VR_Init(&vrErr, vr::VRApplication_Utility);
		if (vrErr != vr::VRInitError_None) {
			std::cerr << "SteamVR is not available: " << vr::VR_GetVRInitErrorAsEnglishDescription(vrErr) << '\n';
			vr::VR_Shutdown();
			return 2;
		}
		try {
			ActivateMultipleDrivers();
		}
		catch (std::runtime_error& e) {
			std::cerr << e.what() << '\n';
		}
		if (vr::VRApplications()->IsApplicationInstalled(OPENVR_APPLICATION_KEY)) {
			char oldWd[MAX_PATH] = {0};
			auto vrAppErr = vr::VRApplicationError_None;
			vr::VRApplications()->GetApplicationPropertyString(OPENVR_APPLICATION_KEY, vr::VRApplicationProperty_WorkingDirectory_String,
			                                                   oldWd, MAX_PATH, &vrAppErr);
			if (vrAppErr == vr::VRApplicationError_None && oldWd[0]) {
				const std::string oldManifest = std::string(oldWd) + "\\manifest.vrmanifest";
				vr::VRApplications()->RemoveApplicationManifest(oldManifest.c_str());
			}
		}
		if (vr::VRApplications()->IsApplicationInstalled(STEAM_OPENVR_APPLICATION_KEY)) {
			vr::VRApplications()->SetApplicationAutoLaunch(STEAM_OPENVR_APPLICATION_KEY, false);
		}
		const std::string manifest = (ExeDir() / "manifest.vrmanifest").string();
		std::cout << "adding manifest " << manifest << '\n';
		const auto addErr = vr::VRApplications()->AddApplicationManifest(manifest.c_str());
		int result = 0;
		if (addErr != vr::VRApplicationError_None) {
			std::cerr << "failed to add manifest: " << vr::VRApplications()->GetApplicationsErrorNameFromEnum(addErr) << '\n';
			result = 4;
		}
		else {
			vr::VRApplications()->SetApplicationAutoLaunch(OPENVR_APPLICATION_KEY, true);
		}
		vr::VR_Shutdown();
		return result;
	}

} // namespace

int RunRegister()
{
	const std::filesystem::path own = FindOwnDriverDir();
	if (own.empty()) {
		std::cerr << "driver files not found next to SpaceCalibrator.exe\n";
		return 4;
	}
	const std::string ownDir = own.string();

	spacecal::OpenVRPaths paths;
	if (!LoadOpenVRPaths(paths)) {
		std::cerr << "SteamVR has not been set up on this computer yet\n";
		return 3;
	}
	const spacecal::RegistrationPlan plan = spacecal::BuildRegistrationPlan(paths, ownDir);
	const std::string vrpathreg = FindVrpathreg(plan);
	if (vrpathreg.empty()) {
		std::cerr << "could not find vrpathreg.exe in the SteamVR runtime\n";
		return 3;
	}
	for (const std::string& dir : plan.removeDirs) {
		std::cout << "unregistering " << dir << '\n';
		RunVrpathreg(vrpathreg, L"removedriver", dir);
	}
	if (!plan.alreadyRegistered) {
		std::cout << "registering " << ownDir << '\n';
		if (RunVrpathreg(vrpathreg, L"adddriver", ownDir) != 0) {
			std::cerr << "vrpathreg adddriver failed\n";
			return 4;
		}
	}
	spacecal::OpenVRPaths verify;
	if (!LoadOpenVRPaths(verify) || !spacecal::BuildRegistrationPlan(verify, ownDir).alreadyRegistered) {
		std::cerr << "driver registration did not persist\n";
		return 4;
	}
	return FinishVRSetup();
}

int RunUnregister()
{
	int result = 0;
	auto vrErr = vr::VRInitError_None;
	vr::VR_Init(&vrErr, vr::VRApplication_Utility);
	if (vrErr == vr::VRInitError_None) {
		if (vr::VRApplications()->IsApplicationInstalled(OPENVR_APPLICATION_KEY)) {
			char wd[MAX_PATH] = {0};
			auto vrAppErr = vr::VRApplicationError_None;
			vr::VRApplications()->GetApplicationPropertyString(OPENVR_APPLICATION_KEY, vr::VRApplicationProperty_WorkingDirectory_String,
			                                                   wd, MAX_PATH, &vrAppErr);
			const std::string manifest = (vrAppErr == vr::VRApplicationError_None && wd[0]) ? std::string(wd) + "\\manifest.vrmanifest"
			                                                                                : (ExeDir() / "manifest.vrmanifest").string();
			std::cout << "removing manifest " << manifest << '\n';
			vr::VRApplications()->RemoveApplicationManifest(manifest.c_str());
		}
		vr::VR_Shutdown();
	}
	else {
		std::cerr << "SteamVR is not available: " << vr::VR_GetVRInitErrorAsEnglishDescription(vrErr) << '\n';
		vr::VR_Shutdown();
		result = 2;
	}

	std::filesystem::path own = FindOwnDriverDir();
	if (own.empty()) own = ExeDir() / "01spacecalibrator";

	spacecal::OpenVRPaths paths;
	if (!LoadOpenVRPaths(paths)) return 3;
	const std::vector<std::string> dirs = spacecal::OwnRegisteredDirs(paths, own.string());
	if (dirs.empty()) return result;
	const std::string vrpathreg = FindVrpathreg(spacecal::BuildRegistrationPlan(paths, own.string()));
	if (vrpathreg.empty()) {
		std::cerr << "could not find vrpathreg.exe in the SteamVR runtime\n";
		return 3;
	}
	for (const std::string& dir : dirs) {
		std::cout << "unregistering " << dir << '\n';
		RunVrpathreg(vrpathreg, L"removedriver", dir);
	}
	spacecal::OpenVRPaths verify;
	if (LoadOpenVRPaths(verify) && !spacecal::OwnRegisteredDirs(verify, own.string()).empty()) {
		std::cerr << "the driver is still registered\n";
		return 4;
	}
	return result;
}
