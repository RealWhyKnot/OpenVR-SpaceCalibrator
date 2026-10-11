#include "Logging.h"
#include "Protocol.h"
#include "ServerTrackedDeviceProvider.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {

	int failures = 0;

#define CHECK(cond)                                                                                                                        \
	do {                                                                                                                                   \
		if (!(cond)) {                                                                                                                     \
			std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);                                                                     \
			++failures;                                                                                                                    \
		}                                                                                                                                  \
	} while (0)

	ServerTrackedDeviceProvider g_provider;

	constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

	vr::DriverPose_t TrackerPose()
	{
		vr::DriverPose_t p;
		std::memset(&p, 0, sizeof p);
		p.poseIsValid = true;
		p.deviceIsConnected = true;
		p.result = vr::TrackingResult_Running_OK;
		p.qWorldFromDriverRotation = {1, 0, 0, 0};
		p.qDriverFromHeadRotation = {1, 0, 0, 0};
		p.qRotation = {std::cos(0.15), 0.0, std::sin(0.15), 0.0};
		p.vecPosition[0] = 0.2;
		p.vecPosition[1] = 1.1;
		p.vecPosition[2] = -0.3;
		return p;
	}

	protocol::SetDeviceTransform Calibration(uint32_t id, double x, bool lerp)
	{
		protocol::SetDeviceTransform t(id, true, vr::HmdVector3d_t{x, -0.03, 0.4},
		                               vr::HmdQuaternion_t{std::cos(0.35), 0.0, std::sin(0.35), 0.0}, 1.0);
		t.lerp = lerp;
		return t;
	}

	protocol::AlignmentSpeedParams OverlayDefaultAlignment()
	{
		const double deg = 3.14159265358979323846 / 180.0;
		protocol::AlignmentSpeedParams p{};
		p.thr_rot_tiny = 0.49 * deg;
		p.thr_rot_small = 0.5 * deg;
		p.thr_rot_large = 5.0 * deg;
		p.thr_trans_tiny = 0.98 / 1000.0;
		p.thr_trans_small = 1.0 / 1000.0;
		p.thr_trans_large = 20.0 / 1000.0;
		p.align_speed_tiny = 1.0;
		p.align_speed_small = 1.0;
		p.align_speed_large = 2.0;
		return p;
	}

	bool Finite(const vr::DriverPose_t& p)
	{
		bool ok = std::isfinite(p.qWorldFromDriverRotation.w) && std::isfinite(p.qWorldFromDriverRotation.x) &&
		          std::isfinite(p.qWorldFromDriverRotation.y) && std::isfinite(p.qWorldFromDriverRotation.z);
		for (int i = 0; i < 3; ++i) {
			ok = ok && std::isfinite(p.vecWorldFromDriverTranslation[i]) && std::isfinite(p.vecPosition[i]);
		}
		return ok;
	}

	vr::DriverPose_t Update(uint32_t id)
	{
		vr::DriverPose_t pose = TrackerPose();
		g_provider.HandleDevicePoseUpdated(id, pose);
		return pose;
	}

	void TestFiniteCalibrationGivesFinitePoses()
	{
		g_provider.SetDeviceTransform(Calibration(5, 0.12, false));
		CHECK(Finite(Update(5)));
		g_provider.SetDeviceTransform(Calibration(5, 0.25, true));
		for (int i = 0; i < 20; ++i) {
			CHECK(Finite(Update(5)));
		}
	}

	void TestNonFiniteCalibrationIsIgnored()
	{
		g_provider.SetDeviceTransform(Calibration(6, 0.12, false));
		Update(6);
		protocol::SetDeviceTransform bad = Calibration(6, kNaN, true);
		bad.rotation = {kNaN, kNaN, kNaN, kNaN};
		g_provider.SetDeviceTransform(bad);
		const bool finite = Finite(Update(6));
		CHECK(finite);
		if (!finite) std::printf("  a NaN calibration from the overlay reaches the device pose\n");
	}

	void TestBlendRecoversOnceAFiniteCalibrationArrives()
	{
		g_provider.SetDeviceTransform(Calibration(7, 0.12, false));
		Update(7);
		g_provider.SetDeviceTransform(Calibration(7, kNaN, true));
		Update(7);
		Update(7);
		g_provider.SetDeviceTransform(Calibration(7, 0.12, true));
		int nanPoses = 0;
		for (int i = 0; i < 200; ++i) {
			if (!Finite(Update(7))) ++nanPoses;
		}
		CHECK(nanPoses == 0);
		if (nanPoses) std::printf("  %d of 200 poses after a good lerped calibration were still NaN\n", nanPoses);
		g_provider.SetDeviceTransform(Calibration(7, 0.12, false));
		CHECK(Finite(Update(7)));
	}

	bool SameWorldTransform(const vr::DriverPose_t& a, const vr::DriverPose_t& b, double tol)
	{
		const double qa[4] = {a.qWorldFromDriverRotation.w, a.qWorldFromDriverRotation.x, a.qWorldFromDriverRotation.y,
		                      a.qWorldFromDriverRotation.z};
		const double qb[4] = {b.qWorldFromDriverRotation.w, b.qWorldFromDriverRotation.x, b.qWorldFromDriverRotation.y,
		                      b.qWorldFromDriverRotation.z};
		for (int i = 0; i < 4; ++i) {
			if (!(std::fabs(qa[i] - qb[i]) <= tol)) return false;
		}
		for (int i = 0; i < 3; ++i) {
			if (!(std::fabs(a.vecWorldFromDriverTranslation[i] - b.vecWorldFromDriverTranslation[i]) <= tol)) return false;
		}
		return true;
	}

	void TestNonFiniteTrackerPoseDoesNotPoisonTheBlend()
	{
		g_provider.SetDeviceTransform(Calibration(8, 0.12, false));
		Update(8);
		const vr::DriverPose_t clean = Update(8);
		CHECK(Finite(clean));
		g_provider.SetDeviceTransform(Calibration(8, 0.12, true));
		vr::DriverPose_t broken = TrackerPose();
		broken.vecPosition[0] = kNaN;
		g_provider.HandleDevicePoseUpdated(8, broken);
		int nanPoses = 0;
		int drifted = 0;
		for (int i = 0; i < 50; ++i) {
			const vr::DriverPose_t next = Update(8);
			if (!Finite(next))
				++nanPoses;
			else if (!SameWorldTransform(next, clean, 1e-9))
				++drifted;
		}
		CHECK(nanPoses == 0);
		CHECK(drifted == 0);
		if (nanPoses || drifted) {
			std::printf("  after one NaN tracker pose: %d of 50 later poses NaN, %d moved off the calibration\n", nanPoses, drifted);
		}
	}

}

int main()
{
	LogFile = stdout;
	g_provider.HandleSetAlignmentSpeedParams(OverlayDefaultAlignment());
	TestFiniteCalibrationGivesFinitePoses();
	TestNonFiniteCalibrationIsIgnored();
	TestBlendRecoversOnceAFiniteCalibrationArrives();
	TestNonFiniteTrackerPoseDoesNotPoisonTheBlend();

	if (failures == 0) {
		std::printf("blend_tests: all tests passed\n");
		return 0;
	}
	std::printf("blend_tests: %d failure(s)\n", failures);
	return 1;
}
