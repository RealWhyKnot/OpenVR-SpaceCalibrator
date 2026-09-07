#pragma once

#include "ReleaseSelector.h"

#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

enum class UpdateState
{
	Idle,
	Checking,
	Available,
	Staged,
	UpToDate,
	Failed
};

struct UpdateSettings
{
	bool checkOnStartup = true;
	bool autoInstall = true;
	std::string skippedTag;
};

class Updater
{
public:
	~Updater();

	void StartCheck(bool manual);
	void Poll();
	void Update();
	void Skip();
	void Later();
	std::string StatusText() const;
	spacecal::UpdateChannel Channel() const;
	void LoadLastRunNote();

	UpdateState state = UpdateState::Idle;
	bool manual = false;
	spacecal::GithubRelease available;
	std::string error;
	std::string lastRunNote;
	UpdateSettings settings;

private:
	struct Result
	{
		std::string error;
		std::vector<spacecal::GithubRelease> releases;
	};

	std::thread worker;
	std::mutex mutex;
	std::optional<Result> pending;
};

extern Updater UpdaterCtx;
