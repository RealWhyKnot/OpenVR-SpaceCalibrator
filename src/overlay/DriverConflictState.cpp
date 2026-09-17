#include "stdafx.h"
#include "DriverConflictState.h"
#include "UnregisterDriverScript.h"

#include <shlobj.h>

#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>

DriverConflictState DriverConflictCtx;

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

	std::filesystem::path DriverDllIn(const std::filesystem::path& driverDir)
	{
		return driverDir / "bin" / "win64" / "driver_01spacecalibrator.dll";
	}

	std::filesystem::path Resolve(const std::filesystem::path& path)
	{
		std::error_code ec;
		std::filesystem::path resolved = std::filesystem::weakly_canonical(path, ec);
		return ec ? path : resolved;
	}

	bool SameDirectory(const std::filesystem::path& a, const std::filesystem::path& b)
	{
		std::error_code ec;
		if (std::filesystem::equivalent(a, b, ec)) return true;
		if (!ec) return false;
		return spacecal::NormalizePathKey(a.string()) == spacecal::NormalizePathKey(b.string());
	}

} // namespace

std::filesystem::path FindOwnDriverDir()
{
	wchar_t buffer[MAX_PATH];
	const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
	std::filesystem::path dir = std::filesystem::path(std::wstring(buffer, length)).parent_path();

	for (int depth = 0; depth < 3 && !dir.empty(); ++depth) {
		const std::filesystem::path candidate = dir / "01spacecalibrator";
		std::error_code ec;
		if (std::filesystem::is_regular_file(DriverDllIn(candidate), ec)) {
			return Resolve(candidate);
		}
		const std::filesystem::path parent = dir.parent_path();
		if (parent == dir) break;
		dir = parent;
	}
	return {};
}

void DriverConflictState::Refresh(spacecal::HandshakeOutcome handshake)
{
	const std::filesystem::path own = FindOwnDriverDir();
	ownDriverDir = own.empty() ? std::string() : own.string();

	spacecal::OpenVRPaths paths;
	const std::filesystem::path localAppData = KnownFolder(FOLDERID_LocalAppData);
	if (!localAppData.empty()) {
		spacecal::ParseOpenVRPaths(ReadFileText(localAppData / "openvr" / "openvrpaths.vrpath"), paths);
	}

	auto identify = [&](const std::string& entry) {
		if (!own.empty() && SameDirectory(Resolve(std::filesystem::path(entry)), own)) return ownDriverDir;
		return entry;
	};

	spacecal::ConflictInputs inputs;
	inputs.ownDriverDir = ownDriverDir;
	inputs.handshake = handshake;
	for (const std::string& entry : paths.externalDrivers) {
		inputs.externalDrivers.push_back(identify(entry));
	}

	vrpathregExe.clear();
	for (const std::string& runtime : paths.runtimes) {
		const std::filesystem::path runtimeDir(runtime);
		std::error_code ec;
		if (vrpathregExe.empty()) {
			const std::filesystem::path candidate = runtimeDir / "bin" / "win64" / "vrpathreg.exe";
			if (std::filesystem::is_regular_file(candidate, ec)) vrpathregExe = candidate.string();
		}
		for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(runtimeDir / "drivers", ec)) {
			if (!entry.is_directory(ec)) continue;
			if (!spacecal::IsSpaceCalibratorDriverLeaf(entry.path().filename().string())) continue;
			inputs.runtimeDriverFolders.push_back(identify(entry.path().string()));
		}
	}

	report = spacecal::ClassifyDriverConflict(inputs);
	if (report.tier == spacecal::ConflictTier::None) {
		dismissed = false;
		fixQueued = false;
		fixError.clear();
	}
}

std::string DriverConflictState::OwnDriverDllPath() const
{
	if (ownDriverDir.empty()) return {};
	return DriverDllIn(ownDriverDir).string();
}

std::string DriverConflictState::OwnDriverDllStamp() const
{
	const std::string dll = OwnDriverDllPath();
	if (dll.empty()) return {};
	std::error_code ec;
	const auto written = std::filesystem::last_write_time(std::filesystem::path(dll), ec);
	if (ec) return {};
	const auto system = std::chrono::clock_cast<std::chrono::system_clock>(written);
	return std::format("{:%Y-%m-%d %H:%M}", std::chrono::floor<std::chrono::minutes>(system));
}

bool DriverConflictState::QueueUnregister()
{
	fixError.clear();

	spacecal::UnregisterDriverParams params;
	params.vrpathregExe = vrpathregExe;
	for (const spacecal::RivalDriver& rival : report.rivals) {
		if (rival.kind == spacecal::RivalKind::ExternalDriver) params.driverDirs.push_back(rival.path);
	}
	if (params.driverDirs.empty()) {
		fixError = "there is no registered driver to unregister";
		return false;
	}
	if (params.vrpathregExe.empty()) {
		fixError = "could not find vrpathreg.exe in the SteamVR runtime";
		return false;
	}

	const std::filesystem::path localAppData = KnownFolder(FOLDERID_LocalAppData);
	if (localAppData.empty()) {
		fixError = "could not resolve local appdata";
		return false;
	}
	const std::filesystem::path base = localAppData / "SpaceCalibrator";
	std::error_code ec;
	std::filesystem::create_directories(base, ec);
	params.logPath = (base / "unregister-driver.log").string();

	const std::filesystem::path scriptPath = base / "unregister-driver.ps1";
	{
		std::ofstream script(scriptPath, std::ios::binary | std::ios::trunc);
		if (!script) {
			fixError = "could not write " + scriptPath.string();
			return false;
		}
		script << spacecal::BuildUnregisterDriverScript(params);
	}

	std::wstring commandLine =
	    L"powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File \"" + scriptPath.wstring() + L"\"";
	STARTUPINFOW startup = {};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION process = {};
	if (!CreateProcessW(nullptr, &commandLine[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
		fixError = "could not start the helper: " + std::to_string(GetLastError());
		return false;
	}
	CloseHandle(process.hThread);
	CloseHandle(process.hProcess);

	fixQueued = true;
	std::cout << "driver conflict: unregister helper started, log " << params.logPath << '\n';
	return true;
}
