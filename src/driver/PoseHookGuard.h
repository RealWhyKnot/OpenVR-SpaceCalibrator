#pragma once

#include <openvr_driver.h>

#include <cstdint>

namespace spacecal {

	enum class PoseHookAction
	{
		Drop,
		Forward,
		Process
	};

	inline PoseHookAction ClassifyPoseUpdate(const vr::DriverPose_t* pose, uint32_t poseStructSize)
	{
		if (pose == nullptr) return PoseHookAction::Drop;
		if (poseStructSize != sizeof(vr::DriverPose_t)) return PoseHookAction::Forward;
		return PoseHookAction::Process;
	}

}
