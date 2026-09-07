#include "stdafx.h"
#include "Calibration.h"
#include "CalibrationMetrics.h"
#include "Configuration.h"
#include "IPCClient.h"
#include "CalibrationCalc.h"
#include "CalibrationMath.h"
#include "VRState.h"

#include <string>
#include <vector>
#include <iostream>

#include <Eigen/Dense>
#include <GLFW/glfw3.h>

CalibrationContext CalCtx;
IPCClient Driver;
static protocol::DriverPoseShmem shmem;

namespace {
	CalibrationCalc calibration;

	Pose ConvertPose(const vr::DriverPose_t& driverPose)
	{
		Eigen::Quaterniond driverToWorldQ(driverPose.qWorldFromDriverRotation.w, driverPose.qWorldFromDriverRotation.x,
		                                  driverPose.qWorldFromDriverRotation.y, driverPose.qWorldFromDriverRotation.z);
		Eigen::Vector3d driverToWorldV(driverPose.vecWorldFromDriverTranslation[0], driverPose.vecWorldFromDriverTranslation[1],
		                               driverPose.vecWorldFromDriverTranslation[2]);

		Eigen::Quaterniond driverRot = driverToWorldQ * Eigen::Quaterniond(driverPose.qRotation.w, driverPose.qRotation.x,
		                                                                   driverPose.qRotation.y, driverPose.qRotation.z);

		Eigen::Vector3d driverPos = driverToWorldV + driverToWorldQ * Eigen::Vector3d(driverPose.vecPosition[0], driverPose.vecPosition[1],
		                                                                              driverPose.vecPosition[2]);

		Eigen::AffineCompact3d xform = Eigen::Translation3d(driverPos) * driverRot;

		return Pose(xform);
	}

	bool CollectSample(const CalibrationContext& ctx)
	{
		vr::DriverPose_t reference, target;
		reference.poseIsValid = false;
		reference.result = vr::ETrackingResult::TrackingResult_Uninitialized;
		target.poseIsValid = false;
		target.result = vr::ETrackingResult::TrackingResult_Uninitialized;

		reference = ctx.devicePoses[ctx.referenceID];
		target = ctx.devicePoses[ctx.targetID];

		bool ok = true;
		if (!reference.poseIsValid && reference.result != vr::ETrackingResult::TrackingResult_Running_OK) {
			CalCtx.Log("Reference device is not tracking\n");
			ok = false;
		}
		if (!target.poseIsValid && target.result != vr::ETrackingResult::TrackingResult_Running_OK) {
			CalCtx.Log("Target device is not tracking\n");
			ok = false;
		}
		if (!ok) {
			if (CalCtx.state != CalibrationState::Continuous) {
				CalCtx.Log("Aborting calibration!\n");
				CalCtx.state = CalibrationState::None;
			}
			return false;
		}

		// Apply tracker offsets
		if (CalCtx.state == CalibrationState::Continuous || CalCtx.state == CalibrationState::ContinuousStandby) {
			reference.vecPosition[0] += ctx.continuousCalibrationOffset.x();
			reference.vecPosition[1] += ctx.continuousCalibrationOffset.y();
			reference.vecPosition[2] += ctx.continuousCalibrationOffset.z();
		}

		calibration.PushSample(Sample(ConvertPose(reference), ConvertPose(target), glfwGetTime()));

		return true;
	}

	bool AssignTargets()
	{
		auto state = VRState::Load();

		if (CalCtx.referenceID < 0) {
			CalCtx.referenceID =
			    state.FindDevice(CalCtx.referenceStandby.trackingSystem, CalCtx.referenceStandby.model, CalCtx.referenceStandby.serial);
		}

		if (CalCtx.targetID < 0) {
			CalCtx.targetID =
			    state.FindDevice(CalCtx.targetStandby.trackingSystem, CalCtx.targetStandby.model, CalCtx.targetStandby.serial);
		}

		for (int i = 0; i < CalCtx.MAX_CONTROLLERS; i++) {
			if (i < state.devices.size() && state.devices[i].trackingSystem == CalCtx.targetTrackingSystem &&
			    state.devices[i].deviceClass == vr::TrackedDeviceClass_Controller &&
			    (state.devices[i].controllerRole == vr::TrackedControllerRole_LeftHand ||
			     state.devices[i].controllerRole == vr::TrackedControllerRole_RightHand)) {
				CalCtx.controllerIDs[i] = state.devices[i].id;
			}
			else {
				CalCtx.controllerIDs[i] = -1;
			}
		}

		return CalCtx.referenceID >= 0 && CalCtx.targetID >= 0;
	}
}

void InitCalibrator()
{
	Driver.Connect();
	shmem.Open(OPENVR_SPACECALIBRATOR_SHMEM_NAME);
}

void ResetAndDisableOffsets(uint32_t id)
{
	vr::HmdVector3d_t zeroV;
	zeroV.v[0] = zeroV.v[1] = zeroV.v[2] = 0;

	vr::HmdQuaternion_t zeroQ;
	zeroQ.x = 0;
	zeroQ.y = 0;
	zeroQ.z = 0;
	zeroQ.w = 1;

	protocol::Request req(protocol::RequestSetDeviceTransform);
	req.setDeviceTransform = {id, false, zeroV, zeroQ, 1.0};
	Driver.SendBlocking(req);
}

static_assert(vr::k_unTrackedDeviceIndex_Hmd == 0, "HMD index expected to be 0");

void ScanAndApplyProfile(CalibrationContext& ctx)
{
	std::unique_ptr<char[]> buffer_array(new char[vr::k_unMaxPropertyStringSize]);
	char* buffer = buffer_array.get();
	ctx.enabled = ctx.validProfile;

	protocol::Request setParamsReq(protocol::RequestSetAlignmentSpeedParams);
	setParamsReq.setAlignmentSpeedParams = ctx.alignmentSpeedParams;
	Driver.SendBlocking(setParamsReq);

	protocol::Request setSmoothingReq(protocol::RequestSetSmoothingParams);
	setSmoothingReq.setSmoothingParams = ctx.smoothingParams;
	Driver.SendBlocking(setSmoothingReq);

	for (uint32_t id = 0; id < vr::k_unMaxTrackedDeviceCount; ++id) {
		auto deviceClass = vr::VRSystem()->GetTrackedDeviceClass(id);
		if (deviceClass == vr::TrackedDeviceClass_Invalid) continue;

		/*if (deviceClass == vr::TrackedDeviceClass_HMD) // for debugging unexpected universe switches
		{
		    vr::ETrackedPropertyError err = vr::TrackedProp_Success;
		    auto universeId = vr::VRSystem()->GetUint64TrackedDeviceProperty(id, vr::Prop_CurrentUniverseId_Uint64, &err);
		    printf("uid %d err %d\n", universeId, err);
		    ResetAndDisableOffsets(id);
		    continue;
		}*/

		if (!ctx.enabled) {
			ResetAndDisableOffsets(id);
			continue;
		}

		vr::ETrackedPropertyError err = vr::TrackedProp_Success;
		vr::VRSystem()->GetStringTrackedDeviceProperty(id, vr::Prop_TrackingSystemName_String, buffer, vr::k_unMaxPropertyStringSize, &err);

		if (err != vr::TrackedProp_Success) {
			ResetAndDisableOffsets(id);
			continue;
		}

		std::string trackingSystem(buffer);

		if (id == vr::k_unTrackedDeviceIndex_Hmd) {
			// auto p = ctx.devicePoses[id].mDeviceToAbsoluteTracking.m;
			// printf("HMD %d: %f %f %f\n", id, p[0][3], p[1][3], p[2][3]);

			// Check if the current HMD is a Pimax crystal
			if (trackingSystem == "aapvr") {
				// HMD is a Pimax HMD
				vr::HmdMatrix34_t eyeToHeadLeft = vr::VRSystem()->GetEyeToHeadTransform(vr::Eye_Left);
				// Crystal's projection matrix is constant 0s or 1s except for [0][3], which stores the IPD offset from the nose
				bool isCrystalHmd = eyeToHeadLeft.m[0][0] == 1 && eyeToHeadLeft.m[0][1] == 0 && eyeToHeadLeft.m[0][2] == 0 && // IPD
				                    eyeToHeadLeft.m[1][0] == 0 && eyeToHeadLeft.m[1][1] == 1 && eyeToHeadLeft.m[1][2] == 0 &&
				                    eyeToHeadLeft.m[1][3] == 0 && eyeToHeadLeft.m[2][0] == 0 && eyeToHeadLeft.m[2][1] == 0 &&
				                    eyeToHeadLeft.m[2][2] == 1 && eyeToHeadLeft.m[2][3] == 0;

				if (isCrystalHmd) {
					// Move it outside the aapvr system ; we treat aapvr as if it were lighthouse
					trackingSystem = "Pimax Crystal HMD";
				}
			}

			if (trackingSystem != ctx.referenceTrackingSystem) {
				// Currently using an HMD with a different tracking system than the calibration.
				ctx.enabled = false;
			}

			ResetAndDisableOffsets(id);
			continue;
		}

		// Detect Pimax crystal controllers and separate them too
		if (deviceClass == vr::TrackedDeviceClass_Controller) {
			if (trackingSystem == "oculus") {
				vr::VRSystem()->GetStringTrackedDeviceProperty(id, vr::Prop_RenderModelName_String, buffer, vr::k_unMaxPropertyStringSize,
				                                               &err);
				std::string renderModel(buffer);
				vr::VRSystem()->GetStringTrackedDeviceProperty(id, vr::Prop_ConnectedWirelessDongle_String, buffer,
				                                               vr::k_unMaxPropertyStringSize, &err);
				std::string connectedWirelessDongle(buffer);

				// Check if the controller claims its an oculus controller but also pimax
				if (renderModel.find("{aapvr}") != std::string::npos && renderModel.find("crystal") != std::string::npos &&
				    connectedWirelessDongle.find("lighthouse") != std::string::npos) {
					trackingSystem = "Pimax Crystal Controllers";
				}
			}
		}

		if (trackingSystem != ctx.targetTrackingSystem) {
			ResetAndDisableOffsets(id);
			continue;
		}

		protocol::Request req(protocol::RequestSetDeviceTransform);
		req.setDeviceTransform = {id, true, VRTranslationVec(ctx.calibratedTranslation), VRRotationQuat(ctx.calibratedRotation),
		                          ctx.calibratedScale};
		req.setDeviceTransform.lerp = CalCtx.state == CalibrationState::Continuous;
		req.setDeviceTransform.quash =
		    CalCtx.state == CalibrationState::Continuous && id == CalCtx.targetID && CalCtx.quashTargetInContinuous;
		req.setDeviceTransform.smooth = deviceClass == vr::TrackedDeviceClass_GenericTracker ||
		                                (ctx.smoothControllers && deviceClass == vr::TrackedDeviceClass_Controller);

		Driver.SendBlocking(req);
	}

	if (ctx.enabled && ctx.chaperone.valid && ctx.chaperone.autoApply) {
		uint32_t quadCount = 0;
		vr::VRChaperoneSetup()->GetLiveCollisionBoundsInfo(nullptr, &quadCount);

		// Heuristic: when SteamVR resets to a blank-ish chaperone, it uses empty geometry,
		// but manual adjustments (e.g. via a play space mover) will not touch geometry.
		if (quadCount != ctx.chaperone.geometry.size()) {
			ApplyChaperoneBounds();
		}
	}
}

void ApplySmoothingSettings()
{
	ScanAndApplyProfile(CalCtx);
	SaveProfile(CalCtx);
}

bool QuerySmoothingStats(uint32_t id, protocol::SmoothingStats& stats)
{
	protocol::Request req(protocol::RequestGetSmoothingStats);
	req.getSmoothingStats.openVRID = id;
	auto response = Driver.SendBlocking(req);
	if (response.type != protocol::ResponseSmoothingStats) {
		return false;
	}
	stats = response.smoothingStats;
	return true;
}

void StartCalibration()
{
	CalCtx.hasAppliedCalibrationResult = false;
	AssignTargets();
	CalCtx.state = CalibrationState::Begin;
	CalCtx.wantedUpdateInterval = 0.0;
	CalCtx.messages.clear();
	calibration.Clear();
	Metrics::WriteLogAnnotation("StartCalibration");
}

void StartContinuousCalibration()
{
	CalCtx.hasAppliedCalibrationResult = false;
	AssignTargets();
	StartCalibration();
	CalCtx.state = CalibrationState::Continuous;
	calibration.setRelativeTransformation(CalCtx.refToTargetPose, CalCtx.relativePosCalibrated);
	calibration.lockRelativePosition = CalCtx.lockRelativePosition;
	if (CalCtx.lockRelativePosition) {
		CalCtx.Log("Relative position locked");
	}
	else {
		CalCtx.Log("Collecting initial samples...");
	}
	Metrics::WriteLogAnnotation("StartContinuousCalibration");
}

void EndContinuousCalibration()
{
	CalCtx.state = CalibrationState::None;
	CalCtx.relativePosCalibrated = false;
	SaveProfile(CalCtx);
	Metrics::WriteLogAnnotation("EndContinuousCalibration");
}

void CalibrationTick(double time)
{
	if (!vr::VRSystem()) return;

	auto& ctx = CalCtx;
	if ((time - ctx.timeLastTick) < 0.05) return;

	if (ctx.state == CalibrationState::Continuous || ctx.state == CalibrationState::ContinuousStandby) {
		ctx.ClearLogOnMessage();

		if (CalCtx.requireTriggerPressToApply && (time - ctx.timeLastAssign) > 10) {
			// rescan devices every 10 seconds or so if we are using controller data
			ctx.timeLastAssign = time;
			AssignTargets();
		}
	}

	ctx.timeLastTick = time;
	shmem.ReadNewPoses([&](const protocol::DriverPoseShmem::AugmentedPose& augmented_pose) {
		if (augmented_pose.deviceId >= 0 && augmented_pose.deviceId <= vr::k_unMaxTrackedDeviceCount) {
			ctx.devicePoses[augmented_pose.deviceId] = augmented_pose.pose;
		}
	});

	// check for non-updating headset tracking space (caused by quest out of bounds or taken off head for example) and abort everything for
	// this tick
	auto p = ctx.devicePoses[vr::k_unTrackedDeviceIndex_Hmd].vecPosition;
	if ((p[0] == 0.0 && p[1] == 0.0 && p[2] == 0.0) || (ctx.xprev == p[0] && ctx.yprev == p[1] && ctx.zprev == p[2])) {
		// std::cerr << "HMD tracking didn't update, skipping update" << std::endl;
		return;
	}
	ctx.xprev = (float)p[0];
	ctx.yprev = (float)p[1];
	ctx.zprev = (float)p[2];

	if (ctx.state == CalibrationState::None || ctx.state == CalibrationState::ContinuousStandby ||
	    (ctx.state == CalibrationState::Continuous && !calibration.isValid())) {
		if ((time - ctx.timeLastScan) >= 1.0) {
			ScanAndApplyProfile(ctx);
			ctx.timeLastScan = time;
		}
	}

	if (ctx.state == CalibrationState::ContinuousStandby) {
		if (AssignTargets()) {
			StartContinuousCalibration();
		}
		else {
			ctx.wantedUpdateInterval = 0.5;
			ctx.Log("Waiting for devices...\n");
			return;
		}
	}

	if (ctx.state == CalibrationState::None) {
		ctx.wantedUpdateInterval = 1.0;
		return;
	}

	if (ctx.state == CalibrationState::Editing) {
		ctx.wantedUpdateInterval = 0.1;

		if ((time - ctx.timeLastScan) >= 0.1) {
			ScanAndApplyProfile(ctx);
			ctx.timeLastScan = time;
		}
		return;
	}

	bool ok = true;

	if (ctx.referenceID == -1 || ctx.referenceID >= vr::k_unMaxTrackedDeviceCount) {
		CalCtx.Log("Missing reference device\n");
		ok = false;
	}
	if (ctx.targetID == -1 || ctx.targetID >= vr::k_unMaxTrackedDeviceCount) {
		CalCtx.Log("Missing target device\n");
		ok = false;
	}

	if (ctx.state == CalibrationState::Begin) {
		char referenceSerial[256], targetSerial[256];
		referenceSerial[0] = targetSerial[0] = 0;
		vr::VRSystem()->GetStringTrackedDeviceProperty(ctx.referenceID, vr::Prop_SerialNumber_String, referenceSerial, 256);
		vr::VRSystem()->GetStringTrackedDeviceProperty(ctx.targetID, vr::Prop_SerialNumber_String, targetSerial, 256);

		char buf[256];
		snprintf(buf, sizeof buf, "Reference device ID: %d, serial: %s\n", ctx.referenceID, referenceSerial);
		CalCtx.Log(buf);
		snprintf(buf, sizeof buf, "Target device ID: %d, serial %s\n", ctx.targetID, targetSerial);
		CalCtx.Log(buf);

		ScanAndApplyProfile(ctx);

		Metrics::jitterRef.Push(calibration.ReferenceJitter());
		Metrics::jitterRef.Push(calibration.TargetJitter());

		if (!CalCtx.ReferencePoseIsValidSimple()) {
			CalCtx.Log("Reference device is not tracking\n");
			ok = false;
		}

		if (!CalCtx.TargetPoseIsValidSimple()) {
			CalCtx.Log("Target device is not tracking\n");
			ok = false;
		}

		// @TOOD: Determine if the tracking is jittery
		if (calibration.ReferenceJitter() > ctx.jitterThreshold) {
			CalCtx.Log("Reference device is not tracking\n");
			ok = false;
		}
		if (calibration.TargetJitter() > ctx.jitterThreshold) {
			CalCtx.Log("Target device is not tracking\n");
			ok = false;
		}

		if (ok) {
			// ResetAndDisableOffsets(ctx.targetID);
			ctx.state = CalibrationState::Rotation;
			ctx.wantedUpdateInterval = 0.0;

			CalCtx.Log("Starting calibration...\n");
			return;
		}
	}

	if (!ok) {
		if (ctx.state != CalibrationState::Continuous) {
			ctx.state = CalibrationState::None;

			CalCtx.Log("Aborting calibration!\n");
		}
		return;
	}

	if (!CollectSample(ctx)) {
		return;
	}

	CalCtx.Progress((int)calibration.SampleCount(), (int)CalCtx.SampleCount());

	if (calibration.SampleCount() < CalCtx.SampleCount()) return;
	while (calibration.SampleCount() > CalCtx.SampleCount())
		calibration.ShiftSample();

	if (CalCtx.state == CalibrationState::Continuous && CalCtx.requireTriggerPressToApply && CalCtx.hasAppliedCalibrationResult) {
		bool triggerPressed = true;
		vr::VRControllerState_t state;
		for (int i = 0; i < CalCtx.MAX_CONTROLLERS; i++) {
			if (CalCtx.controllerIDs[i] >= 0) {
				vr::VRSystem()->GetControllerState(CalCtx.controllerIDs[i], &state, sizeof(state));
				triggerPressed &= state.rAxis[vr::k_eControllerAxis_TrackPad /* matches trigger on Index controllers?? */].x > 0.75f ||
				                  state.rAxis[vr::k_eControllerAxis_Trigger].x > 0.75f;
				// printf("Controller %d tracpad: %f\n", i, state.rAxis[vr::k_eControllerAxis_TrackPad].x);
				// printf("Controller %d trigger: %f\n", i, state.rAxis[vr::k_eControllerAxis_Trigger].x);
				if (!triggerPressed) {
					break;
				}
			}
		}

		if (!triggerPressed) {
			CalCtx.Log("Waiting for trigger press...\n");
			CalCtx.wasWaitingForTriggers = true;
			return;
		}

		if (CalCtx.wasWaitingForTriggers) {
			CalCtx.Log("Triggers pressed, continuing calibration...\n");
			CalCtx.wasWaitingForTriggers = false;
		}
	}

	LARGE_INTEGER start_time;
	QueryPerformanceCounter(&start_time);

	bool lerp = false;

	if (CalCtx.state == CalibrationState::Continuous) {
		CalCtx.messages.clear();
		calibration.enableStaticRecalibration = CalCtx.enableStaticRecalibration;
		calibration.lockRelativePosition = CalCtx.lockRelativePosition;
		calibration.ComputeIncremental(lerp, CalCtx.continuousCalibrationThreshold, CalCtx.maxRelativeErrorThreshold,
		                               CalCtx.ignoreOutliers);
	}
	else {
		calibration.enableStaticRecalibration = false;
		calibration.ComputeOneshot(CalCtx.ignoreOutliers);
	}

	if (calibration.isValid()) {
		ctx.calibratedRotation = calibration.EulerRotation();
		ctx.calibratedTranslation = calibration.Transformation().translation() * 100.0; // convert to cm units for profile storage
		ctx.refToTargetPose = calibration.RelativeTransformation();
		ctx.relativePosCalibrated = calibration.isRelativeTransformationCalibrated();

		auto vrTrans = VRTranslationVec(ctx.calibratedTranslation);
		auto vrRot = VRRotationQuat(Eigen::Quaterniond(calibration.Transformation().rotation()));

		ctx.validProfile = true;
		SaveProfile(ctx);

		ScanAndApplyProfile(ctx);

		CalCtx.hasAppliedCalibrationResult = true;

		CalCtx.Log("Finished calibration, profile saved\n");
	}
	else {
		CalCtx.Log("Calibration failed.\n");
	}

	LARGE_INTEGER end_time;
	QueryPerformanceCounter(&end_time);
	LARGE_INTEGER freq;
	QueryPerformanceFrequency(&freq);
	double duration = (end_time.QuadPart - start_time.QuadPart) / (double)freq.QuadPart;
	Metrics::computationTime.Push(duration * 1000.0);

	Metrics::WriteLogEntry();

	if (CalCtx.state != CalibrationState::Continuous) {
		ctx.state = CalibrationState::None;
		calibration.Clear();
	}
	else {
		size_t drop_samples = CalCtx.SampleCount() / 10;
		for (int i = 0; i < drop_samples; i++) {
			calibration.ShiftSample();
		}
	}
}

void LoadChaperoneBounds()
{
	vr::VRChaperoneSetup()->RevertWorkingCopy();

	uint32_t quadCount = 0;
	vr::VRChaperoneSetup()->GetLiveCollisionBoundsInfo(nullptr, &quadCount);

	CalCtx.chaperone.geometry.resize(quadCount);
	vr::VRChaperoneSetup()->GetLiveCollisionBoundsInfo(&CalCtx.chaperone.geometry[0], &quadCount);
	vr::VRChaperoneSetup()->GetWorkingStandingZeroPoseToRawTrackingPose(&CalCtx.chaperone.standingCenter);
	vr::VRChaperoneSetup()->GetWorkingPlayAreaSize(&CalCtx.chaperone.playSpaceSize.v[0], &CalCtx.chaperone.playSpaceSize.v[1]);
	CalCtx.chaperone.valid = true;
}

void ApplyChaperoneBounds()
{
	vr::VRChaperoneSetup()->RevertWorkingCopy();
	vr::VRChaperoneSetup()->SetWorkingCollisionBoundsInfo(&CalCtx.chaperone.geometry[0], (uint32_t)CalCtx.chaperone.geometry.size());
	vr::VRChaperoneSetup()->SetWorkingStandingZeroPoseToRawTrackingPose(&CalCtx.chaperone.standingCenter);
	vr::VRChaperoneSetup()->SetWorkingPlayAreaSize(CalCtx.chaperone.playSpaceSize.v[0], CalCtx.chaperone.playSpaceSize.v[1]);
	vr::VRChaperoneSetup()->CommitWorkingCopy(vr::EChaperoneConfigFile_Live);
}

void DebugApplyRandomOffset()
{
	protocol::Request req(protocol::RequestDebugOffset);
	Driver.SendBlocking(req);
}