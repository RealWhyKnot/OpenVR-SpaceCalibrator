#include "ServerTrackedDeviceProvider.h"
#include "Logging.h"
#include "InterfaceHookInjector.h"
#include "IsometryTransform.h"
#include "SkeletalHook.h"
#include "StickHook.h"

#include <cmath>
#include <limits>
#include <random>

vr::EVRInitError ServerTrackedDeviceProvider::Init(vr::IVRDriverContext* pDriverContext)
{
	TRACE("ServerTrackedDeviceProvider::Init()");
	VR_INIT_SERVER_DRIVER_CONTEXT(pDriverContext);

	HMODULE self = nullptr;
	if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(&InjectHooks),
	                        &self)) {
		LOG("could not pin the driver module in memory. Error: %lu", GetLastError());
	}

	for (uint32_t id = 0; id < vr::k_unMaxTrackedDeviceCount; ++id) {
		transforms[id] = DeviceTransform{};
		ipcDesired[id] = DesiredTransform{};
		ipcRejectLogged[id] = false;
		desiredLocks[id].Write(ipcDesired[id]);
	}

	ipcSettings = DriverSettings{};
	auto& alignmentSpeedParams = ipcSettings.alignment;

	alignmentSpeedParams.thr_rot_tiny = 0.1f * (EIGEN_PI / 180.0f);
	alignmentSpeedParams.thr_rot_small = 1.0f * (EIGEN_PI / 180.0f);
	alignmentSpeedParams.thr_rot_large = 5.0f * (EIGEN_PI / 180.0f);

	alignmentSpeedParams.thr_trans_tiny = 0.1f / 1000.0;   // mm
	alignmentSpeedParams.thr_trans_small = 1.0f / 1000.0;  // mm
	alignmentSpeedParams.thr_trans_large = 20.0f / 1000.0; // mm

	alignmentSpeedParams.align_speed_tiny = 0.05f;
	alignmentSpeedParams.align_speed_small = 0.2f;
	alignmentSpeedParams.align_speed_large = 2.0f;

	settingsLock.Write(ipcSettings);

	spacecal::skeletal_hook::Init(this);
	spacecal::stick_hook::Init();
	shmem.Create(OPENVR_SPACECALIBRATOR_SHMEM_NAME);
	InjectHooks(this, pDriverContext);
	server.Run();

	return vr::VRInitError_None;
}

void ServerTrackedDeviceProvider::Cleanup()
{
	TRACE("ServerTrackedDeviceProvider::Cleanup()");
	DisableHooks();
	const bool drained = RemoveHooks();
	if (drained) {
		spacecal::skeletal_hook::Shutdown();
		spacecal::stick_hook::Shutdown();
	}
	server.Stop();
	if (drained) shmem.Close();
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

	bool Finite(double v)
	{
		return std::fabs(v) <= std::numeric_limits<double>::max();
	}

	bool IsFinite(const IsoTransform& t)
	{
		return Finite(t.translation.x()) && Finite(t.translation.y()) && Finite(t.translation.z()) && Finite(t.rotation.w()) &&
		       Finite(t.rotation.x()) && Finite(t.rotation.y()) && Finite(t.rotation.z());
	}

	bool IsUsable(const protocol::SetDeviceTransform& t)
	{
		if (t.updateTranslation && !(Finite(t.translation.v[0]) && Finite(t.translation.v[1]) && Finite(t.translation.v[2]))) {
			return false;
		}
		if (t.updateRotation && !(Finite(t.rotation.w) && Finite(t.rotation.x) && Finite(t.rotation.y) && Finite(t.rotation.z))) {
			return false;
		}
		if (t.updateScale && !(Finite(t.scale) && t.scale > 0.0)) return false;
		return true;
	}
}


/**
 * This function heuristically evaluates the amount of drift between the src and target playspace transforms,
 * evaluated centered on the `pose` device transform. This is then used to control the speed of realignment.
 */
ServerTrackedDeviceProvider::DeltaSize
ServerTrackedDeviceProvider::GetTransformDeltaSize(const protocol::AlignmentSpeedParams& alignmentSpeedParams, DeltaSize prior_delta,
                                                   const IsoTransform& deviceWorldPose, const IsoTransform& src,
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

double ServerTrackedDeviceProvider::GetTransformRate(const protocol::AlignmentSpeedParams& alignmentSpeedParams, DeltaSize delta) const
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

	lerp *= GetTransformRate(device.settings.alignment, device.currentRate);
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

	const uint32_t id = newTransform.openVRID;
	if (!IsUsable(newTransform)) {
		if (!ipcRejectLogged[id]) {
			ipcRejectLogged[id] = true;
			LOG("ignoring a calibration for device %u with a non-finite value or a scale <= 0", id);
		}
		return;
	}

	DesiredTransform& d = ipcDesired[id];
	d.enabled = newTransform.enabled;

	if (newTransform.updateTranslation) {
		for (int i = 0; i < 3; ++i) {
			d.translation[i] = newTransform.translation.v[i];
		}
		if (!newTransform.lerp) ++d.snapTranslationGen;
	}

	if (newTransform.updateRotation) {
		d.rotation[0] = newTransform.rotation.w;
		d.rotation[1] = newTransform.rotation.x;
		d.rotation[2] = newTransform.rotation.y;
		d.rotation[3] = newTransform.rotation.z;
		if (!newTransform.lerp) ++d.snapRotationGen;
	}

	if (newTransform.updateScale) d.scale = newTransform.scale;

	d.quash = newTransform.quash;
	d.smooth = newTransform.smooth;
	desiredLocks[id].Write(d);
}

void ServerTrackedDeviceProvider::HandleSetAlignmentSpeedParams(const protocol::AlignmentSpeedParams params)
{
	ipcSettings.alignment = params;
	settingsLock.Write(ipcSettings);
}

void ServerTrackedDeviceProvider::HandleSetSmoothingParams(const protocol::SmoothingParams& params)
{
	const uint8_t strength = params.strength > 100 ? 100 : params.strength;
	const bool changed = ipcSettings.smoothingStrength != strength;
	ipcSettings.smoothingStrength = strength;
	ipcSettings.filter = spacecal::ParamsFromStrength(strength);
	ipcSettings.predictionScale = spacecal::PredictionScaleFromStrength(strength);
	if (changed) ++ipcSettings.filterResetGen;
	settingsLock.Write(ipcSettings);
	if (changed) {
		LOG("smoothing strength %u%% (cutoff=%.2fHz beta=%.1fHz/mps prediction=%.2f)", (unsigned)strength, ipcSettings.filter.minCutoffHz,
		    ipcSettings.filter.beta, ipcSettings.predictionScale);
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
	const uint64_t low = spacecal::skeletal::PackFingerLow(clamped);
	const uint64_t header = spacecal::skeletal::PackFingerHeader(clamped);
	const bool lowChanged = fingerCfgLowPacked.exchange(low, std::memory_order_acq_rel) != low;
	const bool headerChanged = fingerCfgHeaderPacked.exchange(header, std::memory_order_acq_rel) != header;

	const uint16_t reseedBits = spacecal::skeletal::ComputeFingerSmoothingReseedBits(previous, clamped);
	spacecal::skeletal_hook::MarkFingersNeedReseed(reseedBits);
	if (lowChanged || headerChanged) {
		LOG("finger smoothing strength=%u mask=0x%04x reseed=0x%04x", (unsigned)clamped.strength, (unsigned)clamped.fingerMask,
		    (unsigned)reseedBits);
	}
}

void ServerTrackedDeviceProvider::RunFrame()
{
	spacecal::stick_hook::Pump();
}

void ServerTrackedDeviceProvider::HandleGetSmoothingStats(const protocol::SmoothingStatsRequest& request, protocol::SmoothingStats& stats)
{
	memset(&stats, 0, sizeof stats);
	stats.openVRID = request.openVRID;
	if (request.openVRID >= vr::k_unMaxTrackedDeviceCount) {
		return;
	}
	const auto& tf = transforms[request.openVRID];
	stats.active = ipcSettings.smoothingStrength > 0 && ipcDesired[request.openVRID].smooth && tf.filter.initialized;
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

	const auto result = spacecal::Step(device.filter, device.settings.filter, rawPos, dt);
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

	const double scale = device.settings.predictionScale;
	for (int i = 0; i < 3; ++i) {
		devicePose.vecPosition[i] = device.filter.pos[i];
		devicePose.vecVelocity[i] *= scale;
		devicePose.vecAcceleration[i] *= scale;
		devicePose.vecAngularVelocity[i] *= scale;
		devicePose.vecAngularAcceleration[i] *= scale;
	}
	devicePose.poseTimeOffset *= scale;
}

void ServerTrackedDeviceProvider::SyncDeviceState(uint32_t openVRID, DeviceTransform& tf)
{
	const uint32_t filterResetGen = tf.settings.filterResetGen;
	if (settingsLock.ReadIfChanged(tf.settings, tf.settingsSeen) && tf.settings.filterResetGen != filterResetGen) {
		tf.filter.initialized = false;
	}

	if (!desiredLocks[openVRID].ReadIfChanged(tf.desired, tf.desiredSeen)) return;

	const DesiredTransform& d = tf.desired;
	tf.enabled = d.enabled;
	tf.quash = d.quash;
	tf.scale = d.scale;
	tf.targetTransform.translation = convert(d.translation);
	tf.targetTransform.rotation = Eigen::Quaterniond(d.rotation[0], d.rotation[1], d.rotation[2], d.rotation[3]);
	if (d.snapTranslationGen != tf.snapTranslationGen) {
		tf.snapTranslationGen = d.snapTranslationGen;
		tf.transform.translation = tf.targetTransform.translation;
	}
	if (d.snapRotationGen != tf.snapRotationGen) {
		tf.snapRotationGen = d.snapRotationGen;
		tf.transform.rotation = tf.targetTransform.rotation;
	}
	if (tf.smooth != d.smooth) {
		tf.filter.initialized = false;
	}
	tf.smooth = d.smooth;
}

bool ServerTrackedDeviceProvider::HandleDevicePoseUpdated(uint32_t openVRID, vr::DriverPose_t& pose)
{
	if (openVRID >= vr::k_unMaxTrackedDeviceCount) {
		return true;
	}

	auto& tf = transforms[openVRID];
	SyncDeviceState(openVRID, tf);
	const DriverSettings& settings = tf.settings;

	// Apply debug pose before anything else
	if (openVRID > 0) {
		const Eigen::Vector3d dbgPos = convert(pose.vecPosition) + convert(settings.debugTranslation);
		const Eigen::Quaterniond dbgRot =
		    convert(pose.qRotation) *
		    Eigen::Quaterniond(settings.debugRotation[0], settings.debugRotation[1], settings.debugRotation[2], settings.debugRotation[3]);
		pose.qRotation = convert(dbgRot);
		pose.vecPosition[0] = dbgPos(0);
		pose.vecPosition[1] = dbgPos(1);
		pose.vecPosition[2] = dbgPos(2);
	}

	shmem.SetPose(openVRID, pose);

	if (tf.smooth && settings.smoothingStrength > 0 && !tf.quash) {
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
		tf.currentRate = GetTransformDeltaSize(settings.alignment, tf.currentRate, deviceWorldPose, tf.transform, tf.targetTransform);

		BlendTransform(tf, deviceWorldPose);
		if (!IsFinite(tf.transform)) {
			tf.transform = tf.targetTransform;
		}
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

	for (int i = 0; i < 3; ++i) {
		ipcSettings.debugTranslation[i] = posOffset(i);
	}
	ipcSettings.debugRotation[0] = 1.0;
	ipcSettings.debugRotation[1] = 0.0;
	ipcSettings.debugRotation[2] = 0.0;
	ipcSettings.debugRotation[3] = 0.0;
	settingsLock.Write(ipcSettings);

	std::ostringstream oss;
	oss << "Applied random offset: " << posOffset << " from init " << init << '\n';
	LOG("%s", oss.str().c_str());
}