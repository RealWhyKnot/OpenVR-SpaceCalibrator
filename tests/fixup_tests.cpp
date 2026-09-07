#include "TrackingSystemFixups.h"

#include <cstdio>
#include <cstring>

namespace {

	int failures = 0;

#define CHECK(cond)                                                                                                                        \
	do {                                                                                                                                   \
		if (!(cond)) {                                                                                                                     \
			std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);                                                                     \
			++failures;                                                                                                                    \
		}                                                                                                                                  \
	} while (0)

	vr::HmdMatrix34_t CrystalEyeToHead(float ipd)
	{
		vr::HmdMatrix34_t m = {};
		m.m[0][0] = 1;
		m.m[1][1] = 1;
		m.m[2][2] = 1;
		m.m[0][3] = ipd;
		return m;
	}

	void TestCrystalHmdMatrix()
	{
		CHECK(IsPimaxCrystalEyeToHead(CrystalEyeToHead(0.032f)));
		CHECK(IsPimaxCrystalEyeToHead(CrystalEyeToHead(0.0f)));

		auto rotated = CrystalEyeToHead(0.032f);
		rotated.m[0][1] = 0.01f;
		CHECK(!IsPimaxCrystalEyeToHead(rotated));

		auto offsetY = CrystalEyeToHead(0.032f);
		offsetY.m[1][3] = 0.01f;
		CHECK(!IsPimaxCrystalEyeToHead(offsetY));

		auto offsetZ = CrystalEyeToHead(0.032f);
		offsetZ.m[2][3] = 0.01f;
		CHECK(!IsPimaxCrystalEyeToHead(offsetZ));
	}

	void TestCrystalControllerNames()
	{
		CHECK(IsPimaxCrystalController("{aapvr}crystal_controller_left", "lighthouse_dongle"));
		CHECK(!IsPimaxCrystalController("oculus_touch_left", "oculus_dongle"));
		CHECK(!IsPimaxCrystalController("{aapvr}crystal_controller_left", "oculus_dongle"));
		CHECK(!IsPimaxCrystalController("{aapvr}other_controller", "lighthouse_dongle"));
	}

	void TestFixupSystemNames()
	{
		CHECK(std::strcmp(kPimaxCrystalHmdSystem, "Pimax Crystal HMD") == 0);
		CHECK(std::strcmp(kPimaxCrystalControllerSystem, "Pimax Crystal Controllers") == 0);
	}

}

int main()
{
	TestCrystalHmdMatrix();
	TestCrystalControllerNames();
	TestFixupSystemNames();

	if (failures == 0) {
		std::printf("fixup_tests: all tests passed\n");
		return 0;
	}
	std::printf("fixup_tests: %d failure(s)\n", failures);
	return 1;
}
