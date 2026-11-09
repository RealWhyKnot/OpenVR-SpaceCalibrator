#include "stdafx.h"
#include "DriverConflictState.h"
#include "BuildId.h"

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

	const wchar_t* const kApplyPendingMutex = L"Local\\SpaceCalibratorApplyPending";

	bool StartHiddenPowerShell(const std::filesystem::path& script, DWORD& error)
	{
		std::wstring commandLine =
		    L"powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File \"" + script.wstring() + L"\"";
		STARTUPINFOW startup = {};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION process = {};
		BOOL started = CreateProcessW(nullptr, &commandLine[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_BREAKAWAY_FROM_JOB,
		                              nullptr, nullptr, &startup, &process);
		if (!started) {
			started = CreateProcessW(nullptr, &commandLine[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup,
			                         &process);
		}
		if (!started) {
			error = GetLastError();
			return false;
		}
		CloseHandle(process.hThread);
		CloseHandle(process.hProcess);
		return true;
	}

	const std::string& OrUnknown(const std::string& id)
	{
		static const std::string unknown = "unknown";
		return id.empty() ? unknown : id;
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

void DriverConflictState::Refresh(spacecal::HandshakeOutcome handshake, const std::string& loadedBuildId)
{
	const std::filesystem::path own = FindOwnDriverDir();
	ownDriverDir = own.empty() ? std::string() : own.string();
	RefreshOwnDriverDll();
	RefreshDriverBuilds(handshake, loadedBuildId);

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

void DriverConflictState::RefreshOwnDriverDll()
{
	ownDllPath.clear();
	ownDllStamp.clear();
	if (ownDriverDir.empty()) return;
	ownDllPath = DriverDllIn(ownDriverDir).string();
	std::error_code ec;
	const auto written = std::filesystem::last_write_time(std::filesystem::path(ownDllPath), ec);
	if (ec) return;
	const auto system = std::chrono::clock_cast<std::chrono::system_clock>(written);
	ownDllStamp = std::format("{:%Y-%m-%d %H:%M}", std::chrono::floor<std::chrono::minutes>(system));
}

void DriverConflictState::RefreshDriverBuilds(spacecal::HandshakeOutcome handshake, const std::string& loadedBuildId)
{
	spacecal::DriverBuilds next;
	next.app = SPACECAL_BUILD_ID;
	next.driverAnswered = handshake == spacecal::HandshakeOutcome::Ok;
	if (next.driverAnswered) next.loaded = loadedBuildId;
	if (!ownDllPath.empty()) {
		std::error_code sizeEc;
		std::error_code timeEc;
		const std::filesystem::path dll(ownDllPath);
		const auto size = std::filesystem::file_size(dll, sizeEc);
		const auto written = std::filesystem::last_write_time(dll, timeEc);
		const std::string key =
		    sizeEc || timeEc ? std::string() : std::format("{}|{}|{}", ownDllPath, size, written.time_since_epoch().count());
		if (key.empty() || key != onDiskScanKey) {
			onDiskBuildId = spacecal::FindBuildIdMarker(ReadFileText(ownDllPath));
			onDiskScanKey = key;
		}
		next.onDisk = onDiskBuildId;
	}

	std::filesystem::path pendingDir;
	if (!ownDriverDir.empty()) {
		pendingDir = std::filesystem::path(ownDriverDir).parent_path() / "pending";
		std::error_code ec;
		next.pendingStaged = std::filesystem::is_regular_file(pendingDir / "pending.json", ec);
	}

	const spacecal::StaleDriverFix fix = spacecal::ClassifyStaleDriver(next);
	if (fix != staleFix || next.loaded != builds.loaded || next.onDisk != builds.onDisk) {
		std::cout << "driver build: app " << next.app << ", loaded " << OrUnknown(next.loaded) << ", on disk " << OrUnknown(next.onDisk)
		          << ", " << spacecal::StaleDriverFixName(fix) << '\n';
	}
	builds = std::move(next);
	staleFix = fix;
	if (fix == spacecal::StaleDriverFix::PendingOnClose) StartPendingApplier(pendingDir);
}

void DriverConflictState::StartPendingApplier(const std::filesystem::path& pendingDir)
{
	if (applierStarted) return;
	applierStarted = true;

	HANDLE running = OpenMutexW(SYNCHRONIZE, FALSE, kApplyPendingMutex);
	if (running) {
		CloseHandle(running);
		return;
	}

	const std::filesystem::path script = pendingDir / "apply-pending.ps1";
	std::error_code ec;
	if (!std::filesystem::is_regular_file(script, ec)) {
		std::cerr << "driver build: staged build has no " << script.string() << '\n';
		return;
	}
	DWORD error = 0;
	if (!StartHiddenPowerShell(script, error)) {
		std::cerr << "driver build: could not start " << script.string() << ": " << error << '\n';
		return;
	}
	std::cout << "driver build: started " << script.string() << '\n';
}

std::string DriverConflictState::OwnDriverDllPath() const
{
	return ownDllPath;
}

std::string DriverConflictState::OwnDriverDllStamp() const
{
	return ownDllStamp;
}

bool DriverConflictState::StartDriverScript(spacecal::UnregisterDriverParams params, const char* baseName)
{
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
	params.logPath = (base / (std::string(baseName) + ".log")).string();

	const std::filesystem::path scriptPath = base / (std::string(baseName) + ".ps1");
	{
		std::ofstream script(scriptPath, std::ios::binary | std::ios::trunc);
		if (!script) {
			fixError = "could not write " + scriptPath.string();
			return false;
		}
		script << spacecal::BuildUnregisterDriverScript(params);
	}

	DWORD error = 0;
	if (!StartHiddenPowerShell(scriptPath, error)) {
		fixError = "could not start the helper: " + std::to_string(error);
		return false;
	}

	fixQueued = true;
	std::cout << "driver setup: " << baseName << " helper started, log " << params.logPath << '\n';
	return true;
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
	return StartDriverScript(std::move(params), "unregister-driver");
}

bool DriverConflictState::QueueRegisterOwn()
{
	if (fixQueued || ownDriverDir.empty() || report.ownRegistered || !report.rivals.empty()) {
		return false;
	}
	fixError.clear();
	spacecal::UnregisterDriverParams params;
	params.vrpathregExe = vrpathregExe;
	params.addDriverDir = ownDriverDir;
	if (!StartDriverScript(std::move(params), "register-driver")) {
		std::cerr << "driver setup: could not queue registration: " << fixError << '\n';
		return false;
	}
	return true;
}
