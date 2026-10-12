#pragma once

#include "ReleaseSelector.h"

#include <condition_variable>
#include <memory>
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

	struct Shared
	{
		std::mutex mutex;
		std::condition_variable finished;
		bool done = true;
		std::optional<Result> pending;
	};

	std::thread worker;
	std::shared_ptr<Shared> shared = std::make_shared<Shared>();
};

extern Updater UpdaterCtx;
