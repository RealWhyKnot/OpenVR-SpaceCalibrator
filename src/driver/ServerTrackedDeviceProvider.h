#pragma once

#define EIGEN_MPL2_ONLY

#include "IPCServer.h"
#include "Protocol.h"
#include "PoseFilter.h"
#include "FingerConfigPacking.h"
#include "IsometryTransform.h"
#include "Seqlock.h"

#include <Eigen/Dense>

#include <openvr_driver.h>

#include <atomic>


class ServerTrackedDeviceProvider : public vr::IServerTrackedDeviceProvider
{
public:
	////// Start vr::IServerTrackedDeviceProvider functions

	/** initializes the driver. This will be called before any other methods are called. */
	virtual vr::EVRInitError Init(vr::IVRDriverContext* pDriverContext) override;

	/** cleans up the driver right before it is unloaded */
	virtual void Cleanup() override;

	/** Returns the version of the ITrackedDeviceServerDriver interface used by this driver */
	virtual const char* const* GetInterfaceVersions() { return vr::k_InterfaceVersions; }

	/** Allows the driver do to some work in the main loop of the server. */
	virtual void RunFrame() override;

	/** Returns true if the driver wants to block Standby mode. */
	virtual bool ShouldBlockStandbyMode() { return false; }

	/** Called when the system is entering Standby mode. The driver should switch itself into whatever sort of low-power
	 * state it has. */
	virtual void EnterStandby() {}

	/** Called when the system is leaving Standby mode. The driver should switch itself back to
	full operation. */
	virtual void LeaveStandby() {}

	////// End vr::IServerTrackedDeviceProvider functions

	ServerTrackedDeviceProvider() : server(this) {}
	void SetDeviceTransform(const protocol::SetDeviceTransform& newTransform);
	bool HandleDevicePoseUpdated(uint32_t openVRID, vr::DriverPose_t& pose);
	void HandleApplyRandomOffset();
	void HandleSetAlignmentSpeedParams(const protocol::AlignmentSpeedParams params);
	void HandleSetSmoothingParams(const protocol::SmoothingParams& params);
	void HandleGetSmoothingStats(const protocol::SmoothingStatsRequest& request, protocol::SmoothingStats& stats);
	void SetFingerSmoothingConfig(const protocol::FingerSmoothingConfig& config);

	protocol::FingerSmoothingConfig GetFingerSmoothingConfig() const
	{
		const uint64_t header = fingerCfgHeaderPacked.load(std::memory_order_acquire);
		const uint64_t low = fingerCfgLowPacked.load(std::memory_order_acquire);
		return spacecal::skeletal::UnpackFingerSmoothing(header, low);
	}

private:
	IPCServer server;
	protocol::DriverPoseShmem shmem;

	enum DeltaSize
	{
		TINY,
		SMALL,
		LARGE
	};

	struct DesiredTransform
	{
		bool enabled = false;
		bool quash = false;
		bool smooth = false;
		double translation[3] = {0.0, 0.0, 0.0};
		double rotation[4] = {1.0, 0.0, 0.0, 0.0};
		double scale = 1.0;
		uint32_t snapTranslationGen = 0;
		uint32_t snapRotationGen = 0;
	};

	struct DriverSettings
	{
		protocol::AlignmentSpeedParams alignment{};
		uint8_t smoothingStrength = 0;
		spacecal::OneEuroParams filter;
		double predictionScale = 1.0;
		double debugTranslation[3] = {0.0, 0.0, 0.0};
		double debugRotation[4] = {1.0, 0.0, 0.0, 0.0};
		uint32_t filterResetGen = 0;
	};

	struct DeviceTransform
	{
		bool enabled = false;
		bool quash = false;
		IsoTransform transform, targetTransform;
		double scale = 1.0;
		LARGE_INTEGER lastPoll;
		DeltaSize currentRate = DeltaSize::TINY;
		bool smooth = false;
		spacecal::PoseFilter filter;
		LARGE_INTEGER lastPoseQpc;
		uint32_t reseeds = 0;
		uint8_t lastResult = 0;
		double rawPosJitter2 = 0.0;
		double smoothPosJitter2 = 0.0;
		uint32_t desiredSeen = 0;
		uint32_t settingsSeen = 0;
		uint32_t snapTranslationGen = 0;
		uint32_t snapRotationGen = 0;
		DesiredTransform desired;
		DriverSettings settings;
	};

	DeviceTransform transforms[vr::k_unMaxTrackedDeviceCount];

	DeltaSize currentDeltaSpeed[vr::k_unMaxTrackedDeviceCount];

	DesiredTransform ipcDesired[vr::k_unMaxTrackedDeviceCount];
	bool ipcRejectLogged[vr::k_unMaxTrackedDeviceCount] = {};
	spacecal::Seqlock<DesiredTransform> desiredLocks[vr::k_unMaxTrackedDeviceCount];
	DriverSettings ipcSettings;
	spacecal::Seqlock<DriverSettings> settingsLock;

	mutable std::atomic<uint64_t> fingerCfgHeaderPacked{0};
	mutable std::atomic<uint64_t> fingerCfgLowPacked{0};

	void SyncDeviceState(uint32_t openVRID, DeviceTransform& device);
	void ApplySmoothing(DeviceTransform& device, vr::DriverPose_t& devicePose);

	DeltaSize GetTransformDeltaSize(const protocol::AlignmentSpeedParams& params, DeltaSize prior_delta,
	                                const IsoTransform& deviceWorldPose, const IsoTransform& src, const IsoTransform& target) const;

	double GetTransformRate(const protocol::AlignmentSpeedParams& params, DeltaSize delta) const;

	void BlendTransform(DeviceTransform& device, const IsoTransform& deviceWorldPose) const;
	void ApplyTransform(DeviceTransform& device, vr::DriverPose_t& devicePose) const;
};