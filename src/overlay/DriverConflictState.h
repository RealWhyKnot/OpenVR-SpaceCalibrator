#pragma once

#include "DriverBuild.h"
#include "DriverConflict.h"
#include "UnregisterDriverScript.h"

#include <filesystem>
#include <string>

struct DriverConflictState
{
	spacecal::ConflictReport report;
	spacecal::DriverBuilds builds;
	spacecal::StaleDriverFix staleFix = spacecal::StaleDriverFix::None;
	bool applierStarted = false;
	std::string ownDriverDir;
	std::string ownDllPath;
	std::string ownDllStamp;
	std::string onDiskScanKey;
	std::string onDiskBuildId;
	std::string vrpathregExe;
	std::string fixError;
	bool fixQueued = false;
	bool dismissed = false;

	void Refresh(spacecal::HandshakeOutcome handshake, const std::string& loadedBuildId);
	void RefreshOwnDriverDll();
	void RefreshDriverBuilds(spacecal::HandshakeOutcome handshake, const std::string& loadedBuildId);
	void StartPendingApplier(const std::filesystem::path& pendingDir);
	bool QueueUnregister();
	bool QueueRegisterOwn();
	bool StartDriverScript(spacecal::UnregisterDriverParams params, const char* baseName);

	std::string OwnDriverDllPath() const;
	std::string OwnDriverDllStamp() const;
};

extern DriverConflictState DriverConflictCtx;

std::filesystem::path FindOwnDriverDir();
