#include "PoseHookGuard.h"

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

	void TestClassify()
	{
		const vr::DriverPose_t pose = {};
		CHECK(ClassifyPoseUpdate(nullptr, sizeof(vr::DriverPose_t)) == PoseHookAction::Drop);
		CHECK(ClassifyPoseUpdate(nullptr, 0) == PoseHookAction::Drop);
		CHECK(ClassifyPoseUpdate(&pose, sizeof(vr::DriverPose_t)) == PoseHookAction::Process);
		CHECK(ClassifyPoseUpdate(&pose, sizeof(vr::DriverPose_t) - 8) == PoseHookAction::Forward);
		CHECK(ClassifyPoseUpdate(&pose, sizeof(vr::DriverPose_t) + 8) == PoseHookAction::Forward);
	}

}

int main()
{
	TestClassify();

	if (failures == 0) {
		std::printf("posehook_tests: all tests passed\n");
		return 0;
	}
	std::printf("posehook_tests: %d failure(s)\n", failures);
	return 1;
}
