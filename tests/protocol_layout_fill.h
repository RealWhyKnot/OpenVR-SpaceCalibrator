#pragma once

#include "protocol_layout.h"

namespace {

	WireLayout FillWireLayout()
	{
		WireLayout w{};
		w.requestSize = sizeof(protocol::Request);
		w.requestAlign = alignof(protocol::Request);
		w.requestPayloadOffset = offsetof(protocol::Request, setDeviceTransform);
		w.responseSize = sizeof(protocol::Response);
		w.responseAlign = alignof(protocol::Response);
		w.responsePayloadOffset = offsetof(protocol::Response, smoothingStats);
		w.transformSize = sizeof(protocol::SetDeviceTransform);
		w.transformTranslation = offsetof(protocol::SetDeviceTransform, translation);
		w.transformRotation = offsetof(protocol::SetDeviceTransform, rotation);
		w.transformScale = offsetof(protocol::SetDeviceTransform, scale);
		w.transformLerp = offsetof(protocol::SetDeviceTransform, lerp);
		w.transformQuash = offsetof(protocol::SetDeviceTransform, quash);
		w.transformSmooth = offsetof(protocol::SetDeviceTransform, smooth);
		w.alignmentSize = sizeof(protocol::AlignmentSpeedParams);
		w.smoothingSize = sizeof(protocol::SmoothingParams);
		w.statsRequestSize = sizeof(protocol::SmoothingStatsRequest);
		w.fingerSize = sizeof(protocol::FingerSmoothingConfig);
		w.fingerPerFinger = offsetof(protocol::FingerSmoothingConfig, perFinger);
		w.fingerMask = offsetof(protocol::FingerSmoothingConfig, fingerMask);
		w.stickSize = sizeof(protocol::StickSmoothingConfig);
		w.stickRampSize = sizeof(protocol::StickRampConfig);
		w.stickRelease = offsetof(protocol::StickRampConfig, releaseMs);
		w.stickStrength = offsetof(protocol::StickRampConfig, strength);
		w.stickDelay = offsetof(protocol::StickRampConfig, delayMs);
		w.statsSize = sizeof(protocol::SmoothingStats);
		w.statsActive = offsetof(protocol::SmoothingStats, active);
		w.statsReseeds = offsetof(protocol::SmoothingStats, reseeds);
		w.statsRawJitter = offsetof(protocol::SmoothingStats, rawJitterMm);
		w.statsSmoothJitter = offsetof(protocol::SmoothingStats, smoothJitterMm);
		w.poseSize = sizeof(vr::DriverPose_t);
		w.poseAlign = alignof(vr::DriverPose_t);
		w.poseFields[0] = offsetof(vr::DriverPose_t, poseTimeOffset);
		w.poseFields[1] = offsetof(vr::DriverPose_t, qWorldFromDriverRotation);
		w.poseFields[2] = offsetof(vr::DriverPose_t, vecWorldFromDriverTranslation);
		w.poseFields[3] = offsetof(vr::DriverPose_t, qDriverFromHeadRotation);
		w.poseFields[4] = offsetof(vr::DriverPose_t, vecDriverFromHeadTranslation);
		w.poseFields[5] = offsetof(vr::DriverPose_t, vecPosition);
		w.poseFields[6] = offsetof(vr::DriverPose_t, vecVelocity);
		w.poseFields[7] = offsetof(vr::DriverPose_t, vecAcceleration);
		w.poseFields[8] = offsetof(vr::DriverPose_t, qRotation);
		w.poseFields[9] = offsetof(vr::DriverPose_t, vecAngularVelocity);
		w.poseFields[10] = offsetof(vr::DriverPose_t, vecAngularAcceleration);
		w.poseFields[11] = offsetof(vr::DriverPose_t, result);
		w.poseFields[12] = offsetof(vr::DriverPose_t, poseIsValid);
		w.poseFields[13] = offsetof(vr::DriverPose_t, willDriftInYaw);
		w.poseFields[14] = offsetof(vr::DriverPose_t, shouldApplyHeadModel);
		w.poseFields[15] = offsetof(vr::DriverPose_t, deviceIsConnected);
		w.augmentedSize = sizeof(protocol::DriverPoseShmem::AugmentedPose);
		w.augmentedDeviceId = offsetof(protocol::DriverPoseShmem::AugmentedPose, deviceId);
		w.augmentedPose = offsetof(protocol::DriverPoseShmem::AugmentedPose, pose);
		w.version = protocol::Version;
		w.requestTypes[0] = protocol::RequestInvalid;
		w.requestTypes[1] = protocol::RequestHandshake;
		w.requestTypes[2] = protocol::RequestSetDeviceTransform;
		w.requestTypes[3] = protocol::RequestSetAlignmentSpeedParams;
		w.requestTypes[4] = protocol::RequestDebugOffset;
		w.requestTypes[5] = protocol::RequestSetSmoothingParams;
		w.requestTypes[6] = protocol::RequestGetSmoothingStats;
		w.requestTypes[7] = protocol::RequestSetFingerSmoothing;
		w.requestTypes[8] = protocol::RequestSetStickSmoothing;
		w.responseTypes[0] = protocol::ResponseInvalid;
		w.responseTypes[1] = protocol::ResponseHandshake;
		w.responseTypes[2] = protocol::ResponseSuccess;
		w.responseTypes[3] = protocol::ResponseSmoothingStats;
		w.fingerMaskAll = protocol::kAllFingersMask;
		w.pipeName = OPENVR_SPACECALIBRATOR_PIPE_NAME;
		w.shmemName = OPENVR_SPACECALIBRATOR_SHMEM_NAME;
		return w;
	}

}
