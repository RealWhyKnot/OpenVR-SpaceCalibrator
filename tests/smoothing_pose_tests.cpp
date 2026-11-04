#include "driver_test_support.h"

#include "Logging.h"
#include "Protocol.h"
#include "ServerTrackedDeviceProvider.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace driver_access {

	struct SmoothingProviderShmem
	{
		using type = protocol::DriverPoseShmem ServerTrackedDeviceProvider::*;
		friend type Get(SmoothingProviderShmem);
	};
	template struct Grant<SmoothingProviderShmem, &ServerTrackedDeviceProvider::shmem>;

}

namespace {

	constexpr uint32_t kTracker = 3;
	constexpr uint32_t kUnsmoothedDevice = 4;

	int failures = 0;

#define CHECK(cond)                                                                                                                        \
	do {                                                                                                                                   \
		if (!(cond)) {                                                                                                                     \
			std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);                                                                     \
			++failures;                                                                                                                    \
		}                                                                                                                                  \
	} while (0)

	ServerTrackedDeviceProvider g_provider;

	bool Near(double a, double b)
	{
		return std::fabs(a - b) < 1e-12;
	}

	vr::DriverPose_t MovingTrackerPose(double wobbleMeters)
	{
		vr::DriverPose_t pose;
		std::memset(&pose, 0, sizeof pose);
		pose.poseIsValid = true;
		pose.deviceIsConnected = true;
		pose.result = vr::TrackingResult_Running_OK;
		pose.qWorldFromDriverRotation = {1, 0, 0, 0};
		pose.qDriverFromHeadRotation = {1, 0, 0, 0};
		pose.qRotation = {std::cos(0.2), 0.0, std::sin(0.2), 0.0};
		pose.vecPosition[0] = 0.3 + wobbleMeters;
		pose.vecPosition[1] = 1.0;
		pose.vecPosition[2] = -0.2 - wobbleMeters;
		pose.vecVelocity[0] = 0.4;
		pose.vecVelocity[1] = 0.1;
		pose.vecVelocity[2] = -0.2;
		pose.vecAcceleration[0] = 1.0;
		pose.vecAcceleration[1] = 2.0;
		pose.vecAcceleration[2] = 3.0;
		pose.vecAngularVelocity[0] = 1.5;
		pose.vecAngularVelocity[1] = -0.5;
		pose.vecAngularVelocity[2] = 2.0;
		pose.vecAngularAcceleration[0] = 3.0;
		pose.vecAngularAcceleration[1] = 1.0;
		pose.vecAngularAcceleration[2] = -2.0;
		pose.poseTimeOffset = -0.016;
		return pose;
	}

	void UseSmoothing(uint8_t strength)
	{
		protocol::SetDeviceTransform transform(kTracker, false);
		transform.smooth = true;
		g_provider.SetDeviceTransform(transform);
		protocol::SmoothingParams params{strength};
		g_provider.HandleSetSmoothingParams(params);
	}

	void CheckRotationUntouched(const vr::DriverPose_t& before, const vr::DriverPose_t& after)
	{
		CHECK(Near(after.qRotation.w, before.qRotation.w));
		CHECK(Near(after.qRotation.x, before.qRotation.x));
		CHECK(Near(after.qRotation.y, before.qRotation.y));
		CHECK(Near(after.qRotation.z, before.qRotation.z));
		for (int i = 0; i < 3; ++i) {
			CHECK(Near(after.vecAngularVelocity[i], before.vecAngularVelocity[i]));
			CHECK(Near(after.vecAngularAcceleration[i], before.vecAngularAcceleration[i]));
		}
		CHECK(Near(after.poseTimeOffset, before.poseTimeOffset));
	}

	void TestHalfStrengthKeepsRotationPrediction()
	{
		UseSmoothing(50);
		const vr::DriverPose_t raw = MovingTrackerPose(0.0);
		vr::DriverPose_t pose = raw;
		g_provider.HandleDevicePoseUpdated(kTracker, pose);

		CheckRotationUntouched(raw, pose);
		for (int i = 0; i < 3; ++i) {
			CHECK(Near(pose.vecVelocity[i], raw.vecVelocity[i] * 0.25));
			CHECK(Near(pose.vecAcceleration[i], raw.vecAcceleration[i] * 0.25));
		}
	}

	void TestFullStrengthKeepsRotationPrediction()
	{
		UseSmoothing(100);
		const vr::DriverPose_t raw = MovingTrackerPose(0.0);
		vr::DriverPose_t pose = raw;
		g_provider.HandleDevicePoseUpdated(kTracker, pose);

		CheckRotationUntouched(raw, pose);
		for (int i = 0; i < 3; ++i) {
			CHECK(Near(pose.vecVelocity[i], 0.0));
			CHECK(Near(pose.vecAcceleration[i], 0.0));
		}
	}

	void TestCalibrationReadsRawPoses()
	{
		auto& shmem = g_provider.*Get(driver_access::SmoothingProviderShmem{});
		const std::string name = "SpaceCalibratorSmoothingTest" + std::to_string(GetCurrentProcessId());
		CHECK(shmem.Create(name.c_str()));
		protocol::DriverPoseShmem reader;
		reader.Open(name.c_str());
		reader.ReadNewPoses([](const protocol::DriverPoseShmem::AugmentedPose&) {});

		UseSmoothing(100);
		int published = 0;
		for (int frame = 0; frame < 40; ++frame) {
			const double wobble = (frame % 2 == 0 ? 1.0 : -1.0) * 0.0004;
			const vr::DriverPose_t raw = MovingTrackerPose(wobble);
			vr::DriverPose_t pose = raw;
			g_provider.HandleDevicePoseUpdated(kTracker, pose);
			CHECK(Near(pose.vecVelocity[0], 0.0));

			reader.ReadNewPoses([&](const protocol::DriverPoseShmem::AugmentedPose& sample) {
				if (sample.deviceId != (int)kTracker) return;
				++published;
				for (int i = 0; i < 3; ++i) {
					CHECK(Near(sample.pose.vecPosition[i], raw.vecPosition[i]));
					CHECK(Near(sample.pose.vecVelocity[i], raw.vecVelocity[i]));
					CHECK(Near(sample.pose.vecAngularVelocity[i], raw.vecAngularVelocity[i]));
				}
				CHECK(Near(sample.pose.poseTimeOffset, raw.poseTimeOffset));
			});
		}
		CHECK(published == 40);
	}

	void TestDeviceWithoutSmoothFlagPassesThrough()
	{
		g_provider.SetDeviceTransform(protocol::SetDeviceTransform(kUnsmoothedDevice, false));
		protocol::SmoothingParams params{100};
		g_provider.HandleSetSmoothingParams(params);
		const vr::DriverPose_t raw = MovingTrackerPose(0.0);
		vr::DriverPose_t pose = raw;
		g_provider.HandleDevicePoseUpdated(kUnsmoothedDevice, pose);
		CHECK(std::memcmp(&pose, &raw, sizeof pose) == 0);
	}

}

int main()
{
	LogFile = stdout;

	TestHalfStrengthKeepsRotationPrediction();
	TestFullStrengthKeepsRotationPrediction();
	TestCalibrationReadsRawPoses();
	TestDeviceWithoutSmoothFlagPassesThrough();

	if (failures == 0) {
		std::printf("smoothing_pose_tests: all tests passed\n");
		return 0;
	}
	std::printf("smoothing_pose_tests: %d failure(s)\n", failures);
	return 1;
}
