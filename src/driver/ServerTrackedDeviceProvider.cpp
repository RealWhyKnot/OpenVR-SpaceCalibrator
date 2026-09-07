#include "ServerTrackedDeviceProvider.h"
#include "Logging.h"
#include "InterfaceHookInjector.h"
#include "IsometryTransform.h"
#include "SkeletalHook.h"

#include <random>

vr::EVRInitError ServerTrackedDeviceProvider::Init(vr::IVRDriverContext* pDriverContext)
{
	TRACE("ServerTrackedDeviceProvider::Init()");
	VR_INIT_SERVER_DRIVER_CONTEXT(pDriverContext);

	memset(transforms, 0, vr::k_unMaxTrackedDeviceCount * sizeof(DeviceTransform));
	memset(&alignmentSpeedParams, 0, sizeof alignmentSpeedParams);

	alignmentSpeedParams.thr_rot_tiny = 0.1f * (EIGEN_PI / 180.0f);
	alignmentSpeedParams.thr_rot_small = 1.0f * (EIGEN_PI / 180.0f);
	alignmentSpeedParams.thr_rot_large = 5.0f * (EIGEN_PI / 180.0f);

	alignmentSpeedParams.thr_trans_tiny = 0.1f / 1000.0;   // mm
	alignmentSpeedParams.thr_trans_small = 1.0f / 1000.0;  // mm
	alignmentSpeedParams.thr_trans_large = 20.0f / 1000.0; // mm

	alignmentSpeedParams.align_speed_tiny = 0.05f;
	alignmentSpeedParams.align_speed_small = 0.2f;
	alignmentSpeedParams.align_speed_large = 2.0f;

	spacecal::skeletal_hook::Init(this);
	InjectHooks(this, pDriverContext);
	server.Run();
	shmem.Create(OPENVR_SPACECALIBRATOR_SHMEM_NAME);

	debugTransform = Eigen::Vector3d::Zero();
	debugRotation = Eigen::Quaterniond::Identity();

	return vr::VRInitError_None;
}

void ServerTrackedDeviceProvider::Cleanup()
{
	TRACE("ServerTrackedDeviceProvider::Cleanup()");
	server.Stop();
	shmem.Close();
	spacecal::skeletal_hook::Shutdown();
	DisableHooks();
	VR_CLEANUP_SERVER_DRIVER_CONTEXT();
}

namespace {


	vr::HmdQuaternion_t convert(const Eigen::Quaterniond& q)
	{
		vr::HmdQuaternion_t result;
		result.w = q.w();
		result.x = q.x();
		result.y = q.y();
		result.z = q.z();
		return result;
	}

	vr::HmdVector3_t convert(const Eigen::Vector3d& v)
	{
		vr::HmdVector3_t result;
		result.v[0] = (float)v.x();
		result.v[1] = (float)v.y();
		result.v[2] = (float)v.z();
		return result;
	}

	Eigen::Quaterniond convert(const vr::HmdQuaternion_t& q)
	{
		return Eigen::Quaterniond(q.w, q.x, q.y, q.z);
	}

	Eigen::Vector3d convert(const vr::HmdVector3d_t& v)
	{
		return Eigen::Vector3d(v.v[0], v.v[1], v.v[2]);
	}

	Eigen::Vector3d convert(const double* arr)
	{
		return Eigen::Vector3d(arr[0], arr[1], arr[2]);
	}

	IsoTransform toIsoWorldTransform(const vr::DriverPose_t& pose)
	{
		Eigen::Quaterniond rot(pose.qWorldFromDriverRotation.w, pose.qWorldFromDriverRotation.x, pose.qWorldFromDriverRotation.y,
		                       pose.qWorldFromDriverRotation.z);
		Eigen::Vector3d trans(pose.vecWorldFromDriverTranslation[0], pose.vecWorldFromDriverTranslation[1],
		                      pose.vecWorldFromDriverTranslation[2]);

		return IsoTransform(rot, trans);
	}

	IsoTransform toIsoPose(const vr::DriverPose_t& pose)
	{
		auto worldXform = toIsoWorldTransform(pose);

		Eigen::Quaterniond rot(pose.qRotation.w, pose.qRotation.x, pose.qRotation.y, pose.qRotation.z);
		Eigen::Vector3d trans(pose.vecPosition[0], pose.vecPosition[1], pose.vecPosition[2]);

		return worldXform * IsoTransform(rot, trans);
	}
}


/**
 * This function heuristically evaluates the amount of drift between the src and target playspace transforms,
 * evaluated centered on the `pose` device transform. This is then used to control the speed of realignment.
 */
ServerTrackedDeviceProvider::DeltaSize ServerTrackedDeviceProvider::GetTransformDeltaSize(DeltaSize prior_delta,
                                                                                          const IsoTransform& deviceWorldPose,
                                                                                          const IsoTransform& src,
                                                                                          const IsoTransform& target) const
{
	const auto src_pose = src * deviceWorldPose;
	const auto target_pose = target * deviceWorldPose;

	const auto trans_delta = (src_pose.translation - target_pose.translation).squaredNorm();
	const auto rot_delta = src_pose.rotation.angularDistance(target_pose.rotation);

	DeltaSize trans_level, rot_level;

	if (trans_delta > alignmentSpeedParams.thr_trans_large)
		trans_level = DeltaSize::LARGE;
	else if (trans_delta > alignmentSpeedParams.thr_trans_small)
		trans_level = DeltaSize::SMALL;
	else
		trans_level = DeltaSize::TINY;

	if (rot_delta > alignmentSpeedParams.thr_rot_large)
		rot_level = DeltaSize::LARGE;
	else if (rot_delta > alignmentSpeedParams.thr_rot_small)
		rot_level = DeltaSize::SMALL;
	else
		rot_level = DeltaSize::TINY;

	if (trans_level == DeltaSize::TINY && rot_level == DeltaSize::TINY)
		return DeltaSize::TINY;
	else
		return std::max(prior_delta, std::max(trans_level, rot_level));
}

double ServerTrackedDeviceProvider::GetTransformRate(DeltaSize delta) const
{
	switch (delta) {
		case DeltaSize::TINY: return alignmentSpeedParams.align_speed_tiny;
		case DeltaSize::SMALL: return alignmentSpeedParams.align_speed_small;
		default: return alignmentSpeedParams.align_speed_large;
	}
}

/**
 * Smoothly interpolates the device active transform towards the target transform.
 */
void ServerTrackedDeviceProvider::BlendTransform(DeviceTransform& device, const IsoTransform& deviceWorldPose) const
{
	LARGE_INTEGER timestamp, freq;
	QueryPerformanceCounter(&timestamp);
	QueryPerformanceFrequency(&freq);

	double lerp = (timestamp.QuadPart - device.lastPoll.QuadPart) / (double)freq.QuadPart;
	device.lastPoll = timestamp;

	lerp *= GetTransformRate(device.currentRate);
	if (lerp > 1.0) lerp = 1.0;
	if (lerp < 0 || isnan(lerp)) lerp = 0;

	device.transform = device.transform.interpolateAround(lerp, device.targetTransform, deviceWorldPose.translation);
}

void ServerTrackedDeviceProvider::ApplyTransform(DeviceTransform& device, vr::DriverPose_t& devicePose) const
{
	auto deviceWorldTransform = toIsoWorldTransform(devicePose);
	deviceWorldTransform = device.transform * deviceWorldTransform;
	devicePose.vecWorldFromDriverTranslation[0] = deviceWorldTransform.translation(0);
	devicePose.vecWorldFromDriverTranslation[1] = deviceWorldTransform.translation(1);
	devicePose.vecWorldFromDriverTranslation[2] = deviceWorldTransform.translation(2);
	devicePose.qWorldFromDriverRotation = convert(deviceWorldTransform.rotation);
}


inline vr::HmdQuaternion_t operator*(const vr::HmdQuaternion_t& lhs, const vr::HmdQuaternion_t& rhs)
{
	return {(lhs.w * rhs.w) - (lhs.x * rhs.x) - (lhs.y * rhs.y) - (lhs.z * rhs.z),
	        (lhs.w * rhs.x) + (lhs.x * rhs.w) + (lhs.y * rhs.z) - (lhs.z * rhs.y),
	        (lhs.w * rhs.y) + (lhs.y * rhs.w) + (lhs.z * rhs.x) - (lhs.x * rhs.z),
	        (lhs.w * rhs.z) + (lhs.z * rhs.w) + (lhs.x * rhs.y) - (lhs.y * rhs.x)};
}

inline vr::HmdVector3d_t quaternionRotateVector(const vr::HmdQuaternion_t& quat, const double (&vector)[3])
{
	vr::HmdQuaternion_t vectorQuat = {0.0, vector[0], vector[1], vector[2]};
	vr::HmdQuaternion_t conjugate = {quat.w, -quat.x, -quat.y, -quat.z};
	auto rotatedVectorQuat = quat * vectorQuat * conjugate;
	return {rotatedVectorQuat.x, rotatedVectorQuat.y, rotatedVectorQuat.z};
}

void ServerTrackedDeviceProvider::SetDeviceTransform(const protocol::SetDeviceTransform& newTransform)
{
	if (newTransform.openVRID >= vr::k_unMaxTrackedDeviceCount) {
		return;
	}

	auto& tf = transforms[newTransform.openVRID];
	tf.enabled = newTransform.enabled;

	if (newTransform.updateTranslation) {
		tf.targetTransform.translation = convert(newTransform.translation);
		if (!newTransform.lerp) {
			tf.transform.translation = tf.targetTransform.translation;
		}
	}

	if (newTransform.updateRotation) {
		tf.targetTransform.rotation = convert(newTransform.rotation);

		if (!newTransform.lerp) {
			tf.transform.rotation = tf.targetTransform.rotation;
		}
	}

	if (newTransform.updateScale) tf.scale = newTransform.scale;

	tf.quash = newTransform.quash;
	if (tf.smooth != newTransform.smooth) {
		tf.filter.initialized = false;
	}
	tf.smooth = newTransform.smooth;
}

void ServerTrackedDeviceProvider::HandleSetSmoothingParams(const protocol::SmoothingParams& params)
{
	const uint8_t strength = params.strength > 100 ? 100 : params.strength;
	const bool changed = smoothingStrength != strength;
	smoothingStrength = strength;
	smoothingFilter = spacecal::ParamsFromStrength(strength);
	smoothingPredictionScale = spacecal::PredictionScaleFromStrength(strength);
	if (changed) {
		for (auto& tf : transforms) {
			tf.filter.initialized = false;
		}
		LOG("smoothing strength %u%% (cutoff=%.2fHz beta=%.1fHz/mps prediction=%.2f)", (unsigned)strength, smoothingFilter.minCutoffHz,
		    smoothingFilter.beta, smoothingPredictionScale);
	}
}

void ServerTrackedDeviceProvider::SetFingerSmoothingConfig(const protocol::FingerSmoothingConfig& config)
{
	protocol::FingerSmoothingConfig clamped = config;
	if (clamped.strength > 100) clamped.strength = 100;
	for (auto& value : clamped.perFinger) {
		if (value > 100) value = 100;
	}
	clamped.fingerMask &= protocol::kAllFingersMask;

	const protocol::FingerSmoothingConfig previous = GetFingerSmoothingConfig();
	fingerCfgLowPacked.exchange(spacecal::skeletal::PackFingerLow(clamped), std::memory_order_acq_rel);
	fingerCfgHeaderPacked.exchange(spacecal::skeletal::PackFingerHeader(clamped), std::memory_order_acq_rel);

	const uint16_t reseedBits = spacecal::skeletal::ComputeFingerSmoothingReseedBits(previous, clamped);
	spacecal::skeletal_hook::MarkFingersNeedReseed(reseedBits);
	LOG("finger smoothing strength=%u mask=0x%04x reseed=0x%04x", (unsigned)clamped.strength, (unsigned)clamped.fingerMask,
	    (unsigned)reseedBits);
}

void ServerTrackedDeviceProvider::HandleGetSmoothingStats(const protocol::SmoothingStatsRequest& request, protocol::SmoothingStats& stats)
{
	memset(&stats, 0, sizeof stats);
	stats.openVRID = request.openVRID;
	if (request.openVRID >= vr::k_unMaxTrackedDeviceCount) {
		return;
	}
	const auto& tf = transforms[request.openVRID];
	stats.active = smoothingStrength > 0 && tf.smooth && tf.filter.initialized;
	stats.lastResult = tf.lastResult;
	stats.reseeds = tf.reseeds;
	stats.rawJitterMm = std::sqrt(tf.rawPosJitter2) * 1000.0;
	stats.smoothJitterMm = std::sqrt(tf.smoothPosJitter2) * 1000.0;
}

void ServerTrackedDeviceProvider::ApplySmoothing(DeviceTransform& device, vr::DriverPose_t& devicePose)
{
	if (!devicePose.poseIsValid || !devicePose.deviceIsConnected || devicePose.result != vr::TrackingResult_Running_OK) {
		device.filter.initialized = false;
		return;
	}

	LARGE_INTEGER now, freq;
	QueryPerformanceCounter(&now);
	QueryPerformanceFrequency(&freq);
	const double dt = device.lastPoseQpc.QuadPart == 0 ? 0.0 : (now.QuadPart - device.lastPoseQpc.QuadPart) / (double)freq.QuadPart;
	device.lastPoseQpc = now;

	const double rawPos[3] = {devicePose.vecPosition[0], devicePose.vecPosition[1], devicePose.vecPosition[2]};
	double prevRawPos[3], prevPos[3];
	memcpy(prevRawPos, device.filter.prevRawPos, sizeof prevRawPos);
	memcpy(prevPos, device.filter.pos, sizeof prevPos);

	const auto result = spacecal::Step(device.filter, smoothingFilter, rawPos, dt);
	device.lastResult = (uint8_t)result;
	if (result == spacecal::StepResult::Jump || result == spacecal::StepResult::Gap) {
		device.reseeds++;
	}
	if (result == spacecal::StepResult::Invalid) {
		return;
	}

	if (result == spacecal::StepResult::Ok) {
		const double a = spacecal::Clamp(dt, 0.0, 1.0);
		const double dRaw = spacecal::Distance3(rawPos, prevRawPos);
		const double dSmooth = spacecal::Distance3(device.filter.pos, prevPos);
		device.rawPosJitter2 += a * (dRaw * dRaw - device.rawPosJitter2);
		device.smoothPosJitter2 += a * (dSmooth * dSmooth - device.smoothPosJitter2);
	}

	const double scale = smoothingPredictionScale;
	for (int i = 0; i < 3; ++i) {
		devicePose.vecPosition[i] = device.filter.pos[i];
		devicePose.vecVelocity[i] *= scale;
		devicePose.vecAcceleration[i] *= scale;
		devicePose.vecAngularVelocity[i] *= scale;
		devicePose.vecAngularAcceleration[i] *= scale;
	}
	devicePose.poseTimeOffset *= scale;
}

bool ServerTrackedDeviceProvider::HandleDevicePoseUpdated(uint32_t openVRID, vr::DriverPose_t& pose)
{
	if (openVRID >= vr::k_unMaxTrackedDeviceCount) {
		return true;
	}

	// Apply debug pose before anything else
	if (openVRID > 0) {
		auto dbgPos = convert(pose.vecPosition) + debugTransform;
		auto dbgRot = convert(pose.qRotation) * debugRotation;
		pose.qRotation = convert(dbgRot);
		pose.vecPosition[0] = dbgPos(0);
		pose.vecPosition[1] = dbgPos(1);
		pose.vecPosition[2] = dbgPos(2);
	}

	shmem.SetPose(openVRID, pose);

	auto& tf = transforms[openVRID];

	if (tf.smooth && smoothingStrength > 0 && !tf.quash) {
		ApplySmoothing(tf, pose);
	}

	if (tf.quash) {
		pose.vecPosition[0] = -pose.vecWorldFromDriverTranslation[0];
		pose.vecPosition[1] = -pose.vecWorldFromDriverTranslation[1] + 9001; // put it 9001m above the origin
		pose.vecPosition[2] = -pose.vecWorldFromDriverTranslation[2];
	}
	else if (tf.enabled) {
		// @TODO: Offset, scale, and re-offset
		pose.vecPosition[0] *= tf.scale;
		pose.vecPosition[1] *= tf.scale;
		pose.vecPosition[2] *= tf.scale;

		auto deviceWorldPose = toIsoPose(pose);
		tf.currentRate = GetTransformDeltaSize(tf.currentRate, deviceWorldPose, tf.transform, tf.targetTransform);
		double lerp = GetTransformRate(tf.currentRate);

		BlendTransform(tf, deviceWorldPose);
		ApplyTransform(tf, pose);
	}

	return true;
}

void ServerTrackedDeviceProvider::HandleApplyRandomOffset()
{
	std::random_device gen;
	std::uniform_real_distribution<double> d(-1, 1);
	auto init = Eigen::Vector3d(d(gen), d(gen), d(gen));
	auto posOffset = init * 0.25f;

	debugTransform = posOffset;
	debugRotation = Eigen::Quaterniond::Identity();

	std::ostringstream oss;
	oss << "Applied random offset: " << posOffset << " from init " << init << '\n';
	LOG("%s", oss.str().c_str());
}