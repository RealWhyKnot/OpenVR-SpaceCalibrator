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

}

int main()
{
	TestClassify();

	if (failures == 0) {
		std::printf("posehealth_tests: all tests passed\n");
		return 0;
	}
	std::printf("posehealth_tests: %d failure(s)\n", failures);
	return 1;
}
