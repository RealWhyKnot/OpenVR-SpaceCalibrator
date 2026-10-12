#include "PoseHealth.h"

#include <cstdio>
#include <limits>

namespace {

	using namespace spacecal;

	int failures = 0;

#define CHECK(cond)                                                                                                                        \
	do {                                                                                                                                   \
		if (!(cond)) {                                                                                                                     \
			std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);                                                                     \
			++failures;                                                                                                                    \
		}                                                                                                                                  \
	} while (0)

	vr::DriverPose_t MakePose(bool valid, bool connected, vr::ETrackingResult result)
	{
		vr::DriverPose_t pose = {};
		pose.poseIsValid = valid;
		pose.deviceIsConnected = connected;
		pose.result = result;
		return pose;
	}

	void TestClassify()
	{
		CHECK(ClassifySamplePose(MakePose(true, true, vr::TrackingResult_Running_OK)) == SampleVerdict::Use);
		CHECK(ClassifySamplePose(MakePose(true, true, vr::TrackingResult_Running_OutOfRange)) == SampleVerdict::Skip);
		CHECK(ClassifySamplePose(MakePose(true, true, vr::TrackingResult_Calibrating_OutOfRange)) == SampleVerdict::Skip);
		CHECK(ClassifySamplePose(MakePose(false, true, vr::TrackingResult_Running_OK)) == SampleVerdict::Skip);
		CHECK(ClassifySamplePose(MakePose(false, false, vr::TrackingResult_Running_OutOfRange)) == SampleVerdict::Lost);
		CHECK(ClassifySamplePose(MakePose(false, true, vr::TrackingResult_Uninitialized)) == SampleVerdict::Lost);
	}

	void TestRuntimeState()
	{
		vr::DriverPose_t healthy = MakePose(true, true, vr::TrackingResult_Running_OK);
		ApplyRuntimePoseState(healthy, true, true, vr::TrackingResult_Running_OK);
		CHECK(ClassifySamplePose(healthy) == SampleVerdict::Use);

		vr::DriverPose_t disconnected = MakePose(true, true, vr::TrackingResult_Running_OK);
		ApplyRuntimePoseState(disconnected, false, false, vr::TrackingResult_Running_OutOfRange);
		CHECK(!disconnected.deviceIsConnected);
		CHECK(ClassifySamplePose(disconnected) == SampleVerdict::Lost);

		vr::DriverPose_t invalidButRunning = MakePose(true, true, vr::TrackingResult_Running_OK);
		ApplyRuntimePoseState(invalidButRunning, true, false, vr::TrackingResult_Running_OK);
		CHECK(ClassifySamplePose(invalidButRunning) == SampleVerdict::Skip);
	}

	void TestStale()
	{
		vr::DriverPose_t fresh = MakePose(true, true, vr::TrackingResult_Running_OK);
		MarkPoseIfStale(fresh, 10.0, 10.0 + kStalePoseSec);
		CHECK(ClassifySamplePose(fresh) == SampleVerdict::Use);

		vr::DriverPose_t frozen = MakePose(true, true, vr::TrackingResult_Running_OK);
		MarkPoseIfStale(frozen, 10.0, 10.0 + kStalePoseSec + 0.05);
		CHECK(!frozen.poseIsValid);
		CHECK(frozen.result == vr::TrackingResult_Running_OutOfRange);
		CHECK(ClassifySamplePose(frozen) == SampleVerdict::Lost);

		vr::DriverPose_t calibrating = MakePose(true, true, vr::TrackingResult_Calibrating_InProgress);
		MarkPoseIfStale(calibrating, 0.0, 5.0);
		CHECK(calibrating.result == vr::TrackingResult_Calibrating_InProgress);
		CHECK(ClassifySamplePose(calibrating) == SampleVerdict::Lost);
	}

	void TestNonFinitePoseIsSkipped()
	{
		const double nan = std::numeric_limits<double>::quiet_NaN();
		const double inf = std::numeric_limits<double>::infinity();
		vr::DriverPose_t healthy = MakePose(true, true, vr::TrackingResult_Running_OK);
		healthy.qRotation = {1, 0, 0, 0};
		healthy.qWorldFromDriverRotation = {1, 0, 0, 0};
		healthy.vecPosition[0] = 1.5;
		healthy.vecWorldFromDriverTranslation[2] = -0.25;
		CHECK(PoseIsFinite(healthy));
		CHECK(ClassifySamplePose(healthy) == SampleVerdict::Use);

		void (*const setters[])(vr::DriverPose_t&, double) = {
		    [](vr::DriverPose_t& p, double v) { p.vecPosition[0] = v; },
		    [](vr::DriverPose_t& p, double v) { p.vecPosition[2] = v; },
		    [](vr::DriverPose_t& p, double v) { p.vecWorldFromDriverTranslation[1] = v; },
		    [](vr::DriverPose_t& p, double v) { p.qRotation.w = v; },
		    [](vr::DriverPose_t& p, double v) { p.qRotation.z = v; },
		    [](vr::DriverPose_t& p, double v) { p.qWorldFromDriverRotation.x = v; },
		    [](vr::DriverPose_t& p, double v) { p.qWorldFromDriverRotation.w = v; },
		};
		for (auto set : setters) {
			for (double bad : {nan, inf, -inf}) {
				vr::DriverPose_t pose = healthy;
				set(pose, bad);
				CHECK(!PoseIsFinite(pose));
				CHECK(ClassifySamplePose(pose) == SampleVerdict::Skip);
			}
		}

		vr::DriverPose_t lost = MakePose(false, false, vr::TrackingResult_Running_OutOfRange);
		lost.vecPosition[1] = nan;
		CHECK(ClassifySamplePose(lost) == SampleVerdict::Lost);

		vr::DriverPose_t outOfRange = MakePose(true, true, vr::TrackingResult_Running_OutOfRange);
		outOfRange.qRotation.x = inf;
		CHECK(ClassifySamplePose(outOfRange) == SampleVerdict::Skip);
	}

}

int main()
{
	TestClassify();
	TestRuntimeState();
	TestStale();
	TestNonFinitePoseIsSkipped();

	if (failures == 0) {
		std::printf("posehealth_tests: all tests passed\n");
		return 0;
	}
	std::printf("posehealth_tests: %d failure(s)\n", failures);
	return 1;
}
