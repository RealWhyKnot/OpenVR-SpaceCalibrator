#pragma once

#include <Eigen/Core>
#include <Eigen/Geometry>
#include <Windows.h>
#include <openvr.h>
#include <vector>
#include <deque>

#include "HandshakeOutcome.h"
#include "Protocol.h"
#include "StickSettings.h"
#include "VRState.h"

enum class CalibrationState
{
	None,
	Begin,
	Rotation,
	Translation,
	Editing,
	Continuous,
	ContinuousStandby,
};

struct StandbyDevice
{
	std::string trackingSystem;
	std::string model, serial;
};

struct DeviceSelection
{
	std::string referenceTrackingSystem;
	std::string targetTrackingSystem;
	int32_t referenceID = -1;
	int32_t targetID = -1;
	StandbyDevice referenceStandby;
	StandbyDevice targetStandby;
};

inline constexpr const char* kFailureSamplesMismatch =
    "The samples didn't line up. Hold the devices tighter together, or pick Slow speed, then try again.";
inline constexpr const char* kFailureTargetNotTracking =
    "Target device is not tracking. Check that both devices are on and tracking, then try again.";
inline constexpr const char* kFailureReferenceNotTracking =
    "Reference device is not tracking. Check that both devices are on and tracking, then try again.";
inline constexpr const char* kFailureMissingDevice = "Pick a reference and a target device, then start again.";

struct CalibrationContext
{
	CalibrationState state = CalibrationState::None;
	int32_t referenceID = -1, targetID = -1;

	static const size_t MAX_CONTROLLERS = 8;
	int32_t controllerIDs[MAX_CONTROLLERS];

	StandbyDevice targetStandby, referenceStandby;

	Eigen::Vector3d calibratedRotation;
	Eigen::Vector3d calibratedTranslation;
	double calibratedScale;

	std::string referenceTrackingSystem;
	std::string targetTrackingSystem;

	bool enabled = false;
	bool validProfile = false;
	bool clearOnLog = false;
	bool quashTargetInContinuous = false;
	double timeLastTick = 0, timeLastScan = 0, timeLastAssign = 0;
	bool ignoreOutliers = false;
	double wantedUpdateInterval = 1.0;
	float jitterThreshold = 3.0f;

	bool requireTriggerPressToApply = false;
	bool wasWaitingForTriggers = false;
	bool hasAppliedCalibrationResult = false;

	float xprev, yprev, zprev;

	float continuousCalibrationThreshold;
	float maxRelativeErrorThreshold = 0.005f;
	Eigen::Vector3d continuousCalibrationOffset;

	protocol::AlignmentSpeedParams alignmentSpeedParams;
	protocol::SmoothingParams smoothingParams;
	protocol::FingerSmoothingConfig fingerSmoothing;
	protocol::StickSmoothingConfig stickSmoothing{};
	bool stickOn[spacecal::stick::kHandCount] = {};
	bool stickLocked[spacecal::stick::kHandCount] = {};
	bool smoothControllers = false;
	bool enableStaticRecalibration;
	bool lockRelativePosition = false;

	Eigen::AffineCompact3d refToTargetPose = Eigen::AffineCompact3d::Identity();
	bool relativePosCalibrated = false;

	enum Speed
	{
		FAST = 0,
		SLOW = 1,
		VERY_SLOW = 2
	};
	Speed calibrationSpeed = FAST;

	vr::DriverPose_t devicePoses[vr::k_unMaxTrackedDeviceCount];

	CalibrationContext()
	{
		calibratedScale = 1.0;
		memset(devicePoses, 0, sizeof(devicePoses));
		ResetConfig();
	}

	void ResetConfig()
	{
		alignmentSpeedParams.thr_rot_tiny = 0.49f * (EIGEN_PI / 180.0f);
		alignmentSpeedParams.thr_rot_small = 0.5f * (EIGEN_PI / 180.0f);
		alignmentSpeedParams.thr_rot_large = 5.0f * (EIGEN_PI / 180.0f);

		alignmentSpeedParams.thr_trans_tiny = 0.98f / 1000.0;  // mm
		alignmentSpeedParams.thr_trans_small = 1.0f / 1000.0;  // mm
		alignmentSpeedParams.thr_trans_large = 20.0f / 1000.0; // mm

		alignmentSpeedParams.align_speed_tiny = 1.0f;
		alignmentSpeedParams.align_speed_small = 1.0f;
		alignmentSpeedParams.align_speed_large = 2.0f;

		continuousCalibrationThreshold = 1.5f;
		maxRelativeErrorThreshold = 0.005f;
		jitterThreshold = 3.0f;

		continuousCalibrationOffset = Eigen::Vector3d::Zero();

		enableStaticRecalibration = false;
		ResetSmoothingConfig();
	}

	void ResetSmoothingConfig()
	{
		smoothingParams.strength = 0;
		smoothControllers = false;
		fingerSmoothing = protocol::FingerSmoothingConfig{};
		fingerSmoothing.fingerMask = protocol::kAllFingersMask;
		for (int hand = 0; hand < spacecal::stick::kHandCount; ++hand) {
			stickSmoothing.sticks[hand] = spacecal::stick_settings::DefaultStick();
			stickOn[hand] = false;
			stickLocked[hand] = false;
		}
	}

	struct Chaperone
	{
		bool valid = false;
		bool autoApply = true;
		std::vector<vr::HmdQuad_t> geometry;
		vr::HmdMatrix34_t standingCenter = {
		    1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
		};
		vr::HmdVector2_t playSpaceSize = {0.0f, 0.0f};
	} chaperone;

	void ClearLogOnMessage() { clearOnLog = true; }

	void Clear()
	{
		chaperone.geometry.clear();
		chaperone.standingCenter = vr::HmdMatrix34_t();
		chaperone.playSpaceSize = vr::HmdVector2_t();
		chaperone.valid = false;

		calibratedRotation = Eigen::Vector3d();
		calibratedTranslation = Eigen::Vector3d();
		calibratedScale = 1.0;
		referenceTrackingSystem = "";
		targetTrackingSystem = "";
		enabled = false;
		validProfile = false;
		refToTargetPose = Eigen::AffineCompact3d::Identity();
		relativePosCalibrated = true;
	}

	size_t SampleCount()
	{
		switch (calibrationSpeed) {
			case FAST: return 100;
			case SLOW: return 250;
			case VERY_SLOW: return 500;
		}
		return 100;
	}

	struct Message
	{
		enum Type
		{
			String,
			Progress
		} type = String;

		Message(Type type) : type(type), progress(0), target(0) {}

		std::string str;
		int progress, target;
	};

	std::deque<Message> messages;
	std::string failureReason;

	DeviceSelection selectionBeforeCommit;
	bool hasSelectionBeforeCommit = false;

	void CommitSelection(const DeviceSelection& selection)
	{
		selectionBeforeCommit.referenceTrackingSystem = referenceTrackingSystem;
		selectionBeforeCommit.targetTrackingSystem = targetTrackingSystem;
		selectionBeforeCommit.referenceID = referenceID;
		selectionBeforeCommit.targetID = targetID;
		selectionBeforeCommit.referenceStandby = referenceStandby;
		selectionBeforeCommit.targetStandby = targetStandby;
		hasSelectionBeforeCommit = true;
		referenceTrackingSystem = selection.referenceTrackingSystem;
		targetTrackingSystem = selection.targetTrackingSystem;
		referenceID = selection.referenceID;
		targetID = selection.targetID;
		referenceStandby = selection.referenceStandby;
		targetStandby = selection.targetStandby;
	}

	void KeepCommittedSelection() { hasSelectionBeforeCommit = false; }

	void RestoreSelectionBeforeCommit()
	{
		if (!hasSelectionBeforeCommit) return;
		hasSelectionBeforeCommit = false;
		referenceTrackingSystem = selectionBeforeCommit.referenceTrackingSystem;
		targetTrackingSystem = selectionBeforeCommit.targetTrackingSystem;
		referenceID = selectionBeforeCommit.referenceID;
		targetID = selectionBeforeCommit.targetID;
		referenceStandby = selectionBeforeCommit.referenceStandby;
		targetStandby = selectionBeforeCommit.targetStandby;
		timeLastScan = -1.0e9;
		std::cerr << "Device selection restored to " << referenceTrackingSystem << " / " << targetTrackingSystem << "\n";
	}

	void Log(const std::string& msg)
	{
		if (clearOnLog) {
			messages.clear();
			clearOnLog = false;
		}

		if (messages.empty() || messages.back().type == Message::Progress) messages.push_back(Message(Message::String));

		OutputDebugStringA(msg.c_str());

		messages.back().str += msg;
		std::cerr << msg;

		while (messages.size() > 15)
			messages.pop_front();
	}

	void Progress(int current, int target)
	{
		if (messages.empty() || messages.back().type == Message::String) messages.push_back(Message(Message::Progress));

		messages.back().progress = current;
		messages.back().target = target;
	}

	bool TargetPoseIsValidSimple() const
	{
		return targetID >= 0 && targetID <= vr::k_unMaxTrackedDeviceCount && devicePoses[targetID].poseIsValid &&
		       devicePoses[targetID].result == vr::ETrackingResult::TrackingResult_Running_OK;
	}

	bool ReferencePoseIsValidSimple() const
	{
		return referenceID >= 0 && referenceID <= vr::k_unMaxTrackedDeviceCount && devicePoses[referenceID].poseIsValid &&
		       devicePoses[referenceID].result == vr::ETrackingResult::TrackingResult_Running_OK;
	}
};

extern CalibrationContext CalCtx;

inline void CancelCalibration()
{
	if (CalCtx.state != CalibrationState::Begin && CalCtx.state != CalibrationState::Rotation &&
	    CalCtx.state != CalibrationState::Translation) {
		return;
	}
	CalCtx.state = CalibrationState::None;
	CalCtx.failureReason.clear();
	CalCtx.Log("Calibration cancelled\n");
}

inline bool CopyLiveChaperoneBounds(vr::IVRChaperoneSetup& setup, CalibrationContext::Chaperone& chaperone)
{
	setup.RevertWorkingCopy();
	uint32_t quadCount = 0;
	setup.GetLiveCollisionBoundsInfo(nullptr, &quadCount);
	if (quadCount == 0) return false;
	std::vector<vr::HmdQuad_t> geometry(quadCount);
	setup.GetLiveCollisionBoundsInfo(geometry.data(), &quadCount);
	if (quadCount == 0) return false;
	if (quadCount < geometry.size()) geometry.resize(quadCount);
	chaperone.geometry = std::move(geometry);
	setup.GetWorkingStandingZeroPoseToRawTrackingPose(&chaperone.standingCenter);
	setup.GetWorkingPlayAreaSize(&chaperone.playSpaceSize.v[0], &chaperone.playSpaceSize.v[1]);
	chaperone.valid = true;
	return true;
}

bool TryConnectDriver(std::string& error);
spacecal::HandshakeOutcome LastDriverHandshake();
void DisconnectDriver();
const std::vector<KnownDevice>& GetKnownDevices();
void CalibrationTick(double time);
void StartCalibration();
void StartContinuousCalibration();
void EndContinuousCalibration();
void LoadChaperoneBounds();
void ApplyChaperoneBounds();

void ApplySmoothingSettings();
bool QuerySmoothingStats(uint32_t id, protocol::SmoothingStats& stats);

void PushCalibrationApplyTime();
void ShowCalibrationDebug(int r, int c);
void DebugApplyRandomOffset();