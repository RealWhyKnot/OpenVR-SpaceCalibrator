#pragma once

#include "DriverConflict.h"

#include <filesystem>
#include <string>

struct DriverConflictState
{
	spacecal::ConflictReport report;
	std::string ownDriverDir;
	std::string vrpathregExe;
	std::string fixError;
	bool fixQueued = false;
	bool dismissed = false;

	void Refresh(spacecal::HandshakeOutcome handshake);
	bool QueueUnregister();

	std::string OwnDriverDllPath() const;
	std::string OwnDriverDllStamp() const;
};

extern DriverConflictState DriverConflictCtx;

std::filesystem::path FindOwnDriverDir();
