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

	UpdateState state = UpdateState::Idle;
	bool manual = false;
	spacecal::GithubRelease available;
	std::string error;
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
