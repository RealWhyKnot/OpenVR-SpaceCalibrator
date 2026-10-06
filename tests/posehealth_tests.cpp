#include "PoseHealth.h"

#include <cstdio>

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

}

int main()
{
	TestClassify();
	TestRuntimeState();
	TestStale();

	if (failures == 0) {
		std::printf("posehealth_tests: all tests passed\n");
		return 0;
	}
	std::printf("posehealth_tests: %d failure(s)\n", failures);
	return 1;
}
