#include "stdafx.h"
#include "Updater.h"
#include "Configuration.h"
#include "UpdateHelperScript.h"
#include "Version.h"

#include <winhttp.h>
#include <shlobj.h>
#include <filesystem>
#include <fstream>
#include <iostream>

Updater UpdaterCtx;

namespace {

	const wchar_t* const ReleasesHost = L"api.github.com";
	const wchar_t* const ReleasesPath = L"/repos/RealWhyKnot/OpenVR-SpaceCalibrator/releases?per_page=20";

	std::string HttpGet(const wchar_t* host, const wchar_t* path, std::string& body)
	{
		std::wstring agent =
		    L"OpenVR-SpaceCalibrator/" + std::wstring(SPACECAL_VERSION_STRING, SPACECAL_VERSION_STRING + strlen(SPACECAL_VERSION_STRING));
		HINTERNET session =
		    WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
		if (!session) {
			return "WinHttpOpen failed: " + std::to_string(GetLastError());
		}
		std::string error;
		WinHttpSetTimeouts(session, 10000, 10000, 10000, 10000);
		HINTERNET connection = WinHttpConnect(session, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
		HINTERNET request = nullptr;
		if (!connection) {
			error = "WinHttpConnect failed: " + std::to_string(GetLastError());
		}
		else {
			request = WinHttpOpenRequest(connection, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
			                             WINHTTP_FLAG_SECURE);
			if (!request) {
				error = "WinHttpOpenRequest failed: " + std::to_string(GetLastError());
			}
		}
		if (request) {
			const wchar_t* headers = L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
			if (!WinHttpSendRequest(request, headers, static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
			    !WinHttpReceiveResponse(request, nullptr)) {
				error = "request failed: " + std::to_string(GetLastError());
			}
			else {
				DWORD status = 0;
				DWORD statusSize = sizeof(status);
				WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status,
				                    &statusSize, WINHTTP_NO_HEADER_INDEX);
				if (status != 200) {
					error = "http " + std::to_string(status);
				}
				else {
					DWORD available = 0;
					while (WinHttpQueryDataAvailable(request, &available) && available > 0) {
						std::string chunk(available, '\0');
						DWORD read = 0;
						if (!WinHttpReadData(request, &chunk[0], available, &read)) {
							break;
						}
						body.append(chunk, 0, read);
					}
				}
			}
		}
		if (request) WinHttpCloseHandle(request);
		if (connection) WinHttpCloseHandle(connection);
		WinHttpCloseHandle(session);
		return error;
	}

	std::string Narrow(const std::wstring& wide)
	{
		if (wide.empty()) {
			return std::string();
		}
		int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
		std::string out(size, '\0');
		WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), &out[0], size, nullptr, nullptr);
		return out;
	}

	std::wstring Widen(const std::string& narrow)
	{
		if (narrow.empty()) {
			return std::wstring();
		}
		int size = MultiByteToWideChar(CP_UTF8, 0, narrow.c_str(), static_cast<int>(narrow.size()), nullptr, 0);
		std::wstring out(size, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, narrow.c_str(), static_cast<int>(narrow.size()), &out[0], size);
		return out;
	}

	std::filesystem::path LocalAppDataDir()
	{
		PWSTR raw = nullptr;
		std::filesystem::path result;
		if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw))) {
			result = raw;
		}
		if (raw) {
			CoTaskMemFree(raw);
		}
		return result / L"SpaceCalibrator";
	}

	std::filesystem::path InstallDir()
	{
		wchar_t buffer[MAX_PATH];
		DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
		return std::filesystem::path(std::wstring(buffer, length)).parent_path();
	}

} // namespace

Updater::~Updater()
{
	if (worker.joinable()) {
		worker.join();
	}
}

spacecal::UpdateChannel Updater::Channel() const
{
	return spacecal::ParseUpdateChannel(SPACECAL_CHANNEL);
}

void Updater::StartCheck(bool manualCheck)
{
	if (Channel() == spacecal::UpdateChannel::Dev) {
		std::cout << "update: check skipped on dev channel\n";
		return;
	}
	if (state == UpdateState::Checking) {
		return;
	}
	if (worker.joinable()) {
		worker.join();
	}
	manual = manualCheck;
	state = UpdateState::Checking;
	error.clear();
	std::cout << "update: checking releases for the " << spacecal::UpdateChannelName(Channel()) << " channel, current "
	          << SPACECAL_VERSION_STRING << (manual ? " (manual)\n" : "\n");
	worker = std::thread([this]() {
		Result result;
		std::string body;
		result.error = HttpGet(ReleasesHost, ReleasesPath, body);
		if (result.error.empty()) {
			result.error = spacecal::ParseReleasesJson(body, result.releases);
		}
		std::lock_guard<std::mutex> lock(mutex);
		pending = std::move(result);
	});
}

void Updater::Poll()
{
	std::optional<Result> result;
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (!pending) {
			return;
		}
		result = std::move(pending);
		pending.reset();
	}
	if (!result->error.empty()) {
		error = result->error;
		state = UpdateState::Failed;
		std::cerr << "update: check failed: " << error << '\n';
		return;
	}
	spacecal::UpdateVersion current;
	if (!spacecal::ParseUpdateVersion(SPACECAL_VERSION_STRING, current)) {
		error = "version " SPACECAL_VERSION_STRING " is not in YYYY.M.D.N form";
		state = UpdateState::Failed;
		std::cerr << "update: " << error << '\n';
		return;
	}
	std::cout << "update: fetched " << result->releases.size() << " releases\n";
	const spacecal::GithubRelease* selected = spacecal::SelectRelease(result->releases, current, Channel());
	switch (spacecal::DecideUpdateAction(selected, manual, settings.autoInstall, settings.skippedTag)) {
		case spacecal::UpdateAction::None:
			state = UpdateState::UpToDate;
			std::cout << "update: no release newer than " << SPACECAL_VERSION_STRING << '\n';
			return;
		case spacecal::UpdateAction::Skip:
			state = UpdateState::Idle;
			std::cout << "update: " << selected->tag << " available but skipped by user\n";
			return;
		case spacecal::UpdateAction::AutoInstall:
			available = *selected;
			std::cout << "update: auto-installing " << available.tag << '\n';
			Update();
			return;
		case spacecal::UpdateAction::Prompt:
			available = *selected;
			state = UpdateState::Available;
			std::cout << "update: " << available.tag << " is newer than " << SPACECAL_VERSION_STRING << '\n';
			return;
	}
}

void Updater::LoadLastRunNote()
{
	lastRunNote.clear();
	std::ifstream log(LocalAppDataDir() / L"update.log");
	if (!log) {
		return;
	}
	std::string line;
	std::string lastLine;
	while (std::getline(log, line)) {
		if (!line.empty() && line.find_first_not_of(" \t\r") != std::string::npos) {
			lastLine = line;
		}
	}
	if (!lastLine.empty() && lastLine.find("installed") == std::string::npos) {
		lastRunNote = "the last update may not have finished; see update.log";
		std::cerr << "update: last helper run ended with: " << lastLine << '\n';
	}
}

void Updater::Update()
{
	std::filesystem::path base = LocalAppDataDir();
	std::error_code ec;
	std::filesystem::create_directories(base, ec);
	spacecal::UpdateHelperParams params;
	params.overlayPid = GetCurrentProcessId();
	params.zipUrl = available.zipUrl;
	params.shaUrl = available.shaUrl;
	params.zipName = spacecal::ReleaseZipName(available.tag);
	params.stagingDir = Narrow((base / L"update").wstring());
	params.installDir = Narrow(InstallDir().wstring());
	params.logPath = Narrow((base / L"update.log").wstring());

	std::filesystem::path scriptPath = base / L"update.ps1";
	{
		std::ofstream script(scriptPath, std::ios::binary | std::ios::trunc);
		if (!script) {
			error = "could not write " + Narrow(scriptPath.wstring());
			state = UpdateState::Failed;
			std::cerr << "update: " << error << '\n';
			return;
		}
		script << spacecal::BuildUpdateHelperScript(params);
	}

	std::wstring commandLine =
	    L"powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File \"" + scriptPath.wstring() + L"\"";
	STARTUPINFOW startup = {};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION process = {};
	std::wstring installDir = Widen(params.installDir);
	if (!CreateProcessW(nullptr, &commandLine[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, installDir.c_str(), &startup,
	                    &process)) {
		error = "could not start the update helper: " + std::to_string(GetLastError());
		state = UpdateState::Failed;
		std::cerr << "update: " << error << '\n';
		return;
	}
	CloseHandle(process.hThread);
	CloseHandle(process.hProcess);
	state = UpdateState::Staged;
	std::cout << "update: helper started for " << available.tag << ", installs after SteamVR closes; log " << params.logPath << '\n';
}

void Updater::Skip()
{
	settings.skippedTag = available.tag;
	SaveUpdateSettings(settings);
	state = UpdateState::Idle;
	std::cout << "update: user skipped " << available.tag << '\n';
}

void Updater::Later()
{
	state = UpdateState::Idle;
	std::cout << "update: user deferred " << available.tag << '\n';
}

std::string Updater::StatusText() const
{
	switch (state) {
		case UpdateState::Checking: return "checking for updates...";
		case UpdateState::Available: return "update available: " + available.tag;
		case UpdateState::Staged: return "update " + available.tag + " downloads now and installs after SteamVR closes";
		case UpdateState::UpToDate: return "up to date";
		case UpdateState::Failed: return "update check failed: " + error;
		default: return std::string();
	}
}
