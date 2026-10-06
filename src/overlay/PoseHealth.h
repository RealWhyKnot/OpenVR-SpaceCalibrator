#pragma once

#ifndef _OPENVR_API
#include <openvr_driver.h>
#endif

namespace spacecal {

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

}
