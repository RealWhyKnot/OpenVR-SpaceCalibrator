#pragma once

#ifndef _OPENVR_API
#include <openvr_driver.h>
#endif

#include <cmath>

namespace spacecal {

	inline constexpr double kStalePoseSec = 1.0;

	enum class SampleVerdict
	{
		Use,
		Skip,
		Lost
	};

	inline bool PoseIsFinite(const vr::DriverPose_t& pose)
	{
		const vr::HmdQuaternion_t& rot = pose.qRotation;
		const vr::HmdQuaternion_t& world = pose.qWorldFromDriverRotation;
		const double values[] = {pose.vecPosition[0],
		                         pose.vecPosition[1],
		                         pose.vecPosition[2],
		                         pose.vecWorldFromDriverTranslation[0],
		                         pose.vecWorldFromDriverTranslation[1],
		                         pose.vecWorldFromDriverTranslation[2],
		                         rot.w,
		                         rot.x,
		                         rot.y,
		                         rot.z,
		                         world.w,
		                         world.x,
		                         world.y,
		                         world.z};
		for (double value : values) {
			if (!std::isfinite(value)) return false;
		}
		return true;
	}

	inline SampleVerdict ClassifySamplePose(const vr::DriverPose_t& pose)
	{
		const bool running = pose.result == vr::TrackingResult_Running_OK;
		if (pose.poseIsValid && running) return PoseIsFinite(pose) ? SampleVerdict::Use : SampleVerdict::Skip;
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
