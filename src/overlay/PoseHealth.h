#pragma once

#ifndef _OPENVR_API
#include <openvr_driver.h>
#endif

namespace spacecal {

	inline constexpr double kStalePoseSec = 1.0;

	enum class SampleVerdict
	{
		Use,
		Skip,
		Lost
	};

	inline SampleVerdict ClassifySamplePose(const vr::DriverPose_t& pose)
	{
		const bool running = pose.result == vr::TrackingResult_Running_OK;
		if (pose.poseIsValid && running) return SampleVerdict::Use;
		if (!pose.poseIsValid && !running) return SampleVerdict::Lost;
		return SampleVerdict::Skip;
	}

	inline const char* SampleVerdictName(SampleVerdict verdict)
	{
		switch (verdict) {
			case SampleVerdict::Use: return "ok";
			case SampleVerdict::Skip: return "skipped";
			case SampleVerdict::Lost: return "lost";
		}
		return "unknown";
	}

	inline void ApplyRuntimePoseState(vr::DriverPose_t& pose, bool runtimeConnected, bool runtimePoseValid,
	                                  vr::ETrackingResult runtimeResult)
	{
		if (runtimeConnected && runtimePoseValid) return;
		pose.deviceIsConnected = runtimeConnected;
		pose.poseIsValid = false;
		pose.result = runtimeResult;
	}

	inline void MarkPoseIfStale(vr::DriverPose_t& pose, double lastSampleSec, double nowSec)
	{
		if (nowSec - lastSampleSec <= kStalePoseSec) return;
		pose.poseIsValid = false;
		if (pose.result == vr::TrackingResult_Running_OK) pose.result = vr::TrackingResult_Running_OutOfRange;
	}

}
