#pragma once

#include "Calibration.h"
#include "DeviceAutoDetect.h"

#include <string>

class AutoDetectController
{
public:
	static AutoDetectController& Get();

	void Tick(double time);
	void Cancel();
	void Undo();
	void DismissResult();

	bool Active() const { return running_; }
	bool Cancelled() const { return cancelled_; }
	double Progress() const { return progress_; }
	int Score() const { return score_; }
	bool HasResult() const { return hasResult_; }
	const std::string& ResultReferenceLabel() const { return resultReferenceLabel_; }
	const std::string& ResultTargetLabel() const { return resultTargetLabel_; }

private:
	bool ShouldRun() const;
	void ApplyResult(int refId, int targetId, const VRState& state);

	spacecal::autodetect::State detect_;
	bool running_ = false;
	bool cancelled_ = false;
	double progress_ = 0.0;
	int score_ = 0;
	double lastTickTime_ = 0.0;
	double lastStateRefresh_ = 0.0;
	VRState cachedState_;

	bool hasResult_ = false;
	std::string resultReferenceLabel_;
	std::string resultTargetLabel_;

	struct Snapshot
	{
		std::string referenceTrackingSystem;
		std::string targetTrackingSystem;
		int32_t referenceID = -1;
		int32_t targetID = -1;
		StandbyDevice referenceStandby;
		StandbyDevice targetStandby;
	};
	Snapshot undo_;
};
