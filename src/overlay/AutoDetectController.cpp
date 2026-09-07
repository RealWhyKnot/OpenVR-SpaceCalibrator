#include "stdafx.h"
#include "AutoDetectController.h"
#include "VRSession.h"

#include <openvr.h>

#include <iostream>
#include <string_view>
#include <vector>

AutoDetectController& AutoDetectController::Get()
{
	static AutoDetectController instance;
	return instance;
}

bool AutoDetectController::ShouldRun() const
{
	if (cancelled_) return false;
	if (VRSess.state != VRConnectionState::Connected) return false;
	if (CalCtx.validProfile) return false;
	if (CalCtx.state != CalibrationState::None) return false;
	return cachedState_.trackingSystems.size() >= 2;
}

void AutoDetectController::Cancel()
{
	cancelled_ = true;
	running_ = false;
	spacecal::autodetect::Cancel(detect_);
}

void AutoDetectController::DismissResult()
{
	hasResult_ = false;
}

void AutoDetectController::Undo()
{
	CalCtx.referenceTrackingSystem = undo_.referenceTrackingSystem;
	CalCtx.targetTrackingSystem = undo_.targetTrackingSystem;
	CalCtx.referenceID = undo_.referenceID;
	CalCtx.targetID = undo_.targetID;
	CalCtx.referenceStandby = undo_.referenceStandby;
	CalCtx.targetStandby = undo_.targetStandby;
	hasResult_ = false;
	cancelled_ = true;
}

void AutoDetectController::ApplyResult(int refId, int targetId, const VRState& state)
{
	undo_.referenceTrackingSystem = CalCtx.referenceTrackingSystem;
	undo_.targetTrackingSystem = CalCtx.targetTrackingSystem;
	undo_.referenceID = CalCtx.referenceID;
	undo_.targetID = CalCtx.targetID;
	undo_.referenceStandby = CalCtx.referenceStandby;
	undo_.targetStandby = CalCtx.targetStandby;

	const VRDevice* ref = nullptr;
	const VRDevice* target = nullptr;
	for (const auto& device : state.devices) {
		if (device.id == refId) ref = &device;
		if (device.id == targetId) target = &device;
	}
	if (!ref || !target) return;

	CalCtx.referenceTrackingSystem = ref->trackingSystem;
	CalCtx.targetTrackingSystem = target->trackingSystem;
	CalCtx.referenceID = refId;
	CalCtx.targetID = targetId;
	CalCtx.referenceStandby = {ref->trackingSystem, ref->model, ref->serial};
	CalCtx.targetStandby = {target->trackingSystem, target->model, target->serial};

	resultReferenceLabel_ = ref->model + " | " + ref->serial;
	resultTargetLabel_ = target->model + " | " + target->serial;
	hasResult_ = true;

	std::cerr << "auto-detect selected reference '" << resultReferenceLabel_ << "' target '" << resultTargetLabel_ << "'\n";
}

void AutoDetectController::Tick(double time)
{
	const double dt = lastTickTime_ > 0.0 ? time - lastTickTime_ : 0.0;
	lastTickTime_ = time;

	if (time - lastStateRefresh_ >= 2.0) {
		lastStateRefresh_ = time;
		if (VRSess.state == VRConnectionState::Connected) {
			cachedState_ = VRState::Load();
		}
		else {
			cachedState_ = VRState{};
		}
	}

	if (!ShouldRun()) {
		if (running_) {
			running_ = false;
			spacecal::autodetect::Cancel(detect_);
		}
		return;
	}

	if (!running_) {
		running_ = true;
		spacecal::autodetect::Begin(detect_, time);
		std::cerr << "auto-detect watching for a moving tracker pair\n";
	}

	std::vector<spacecal::autodetect::DeviceMotionInput> inputs;
	inputs.reserve(cachedState_.devices.size());
	for (const auto& device : cachedState_.devices) {
		if (device.id < 0) continue;
		if (device.deviceClass != vr::TrackedDeviceClass_HMD && device.deviceClass != vr::TrackedDeviceClass_Controller &&
		    device.deviceClass != vr::TrackedDeviceClass_GenericTracker) {
			continue;
		}
		const auto& pose = CalCtx.devicePoses[device.id];
		spacecal::autodetect::DeviceMotionInput input;
		input.id = device.id;
		input.valid = pose.poseIsValid && pose.result == vr::ETrackingResult::TrackingResult_Running_OK;
		input.position = Eigen::Vector3d(pose.vecPosition[0], pose.vecPosition[1], pose.vecPosition[2]);
		input.trackingSystem = device.trackingSystem;
		inputs.push_back(input);
	}

	std::string_view hmdSystem;
	for (const auto& device : cachedState_.devices) {
		if (device.deviceClass == vr::TrackedDeviceClass_HMD) {
			hmdSystem = device.trackingSystem;
			break;
		}
	}

	const auto result = spacecal::autodetect::Tick(detect_, inputs, time, dt, hmdSystem);
	progress_ = result.fractionComplete;
	score_ = result.score;

	switch (result.phase) {
		case spacecal::autodetect::Phase::Succeeded:
			running_ = false;
			ApplyResult(result.refId, result.targetId, cachedState_);
			break;
		case spacecal::autodetect::Phase::Failed: running_ = false; break;
		default: break;
	}
}
