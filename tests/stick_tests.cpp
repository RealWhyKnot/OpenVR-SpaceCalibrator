#include "StickSmoothingMath.h"

#include <cmath>
#include <cstdio>
#include <random>

namespace {

	using namespace spacecal::stick;

	int failures = 0;

#define CHECK(cond)                                                                                                                        \
	do {                                                                                                                                   \
		if (!(cond)) {                                                                                                                     \
			std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);                                                                     \
			++failures;                                                                                                                    \
		}                                                                                                                                  \
	} while (0)

	double Step(AxisFilter& filter, double target, double dt, double ramp)
	{
		filter.target = target;
		return Advance(filter, dt, ramp);
	}

	double Run(AxisFilter& filter, double target, double seconds, double dt, double ramp)
	{
		const int steps = (int)std::lround(seconds / dt);
		double out = filter.stage2;
		for (int i = 0; i < steps; ++i) {
			out = Step(filter, target, dt, ramp);
		}
		return out;
	}

	void TestStrengthMapping()
	{
		CHECK(RampSecondsFromStrength(0) == 0.0);
		CHECK(std::fabs(RampSecondsFromStrength(50) - 0.75) < 1e-12);
		CHECK(std::fabs(RampSecondsFromStrength(100) - 1.5) < 1e-12);
		CHECK(std::fabs(RampSecondsFromStrength(255) - 1.5) < 1e-12);
	}

	void TestOffPassesThrough()
	{
		AxisFilter filter;
		CHECK(Step(filter, 0.7, 0.01, 0.0) == 0.7);
		CHECK(IsSettled(filter));
		CHECK(Step(filter, -1.0, 0.0, 0.0) == -1.0);
		CHECK(OnDriverSample(filter, 0.3, 5.0, 0.0) == 0.3);
		CHECK(filter.target == 0.3);
	}

	void TestGentleStart()
	{
		const double ramp = 1.5;
		AxisFilter filter;
		double previous = 0.0;
		for (int i = 1; i <= 9; ++i) {
			const double out = Step(filter, 1.0, 0.005, ramp);
			const double linear = (0.005 * i) / ramp;
			CHECK(out < 0.5 * linear);
			CHECK(out > previous);
			previous = out;
		}
	}

	void TestReachesNinetyFivePercentAtRampTime()
	{
		for (uint8_t strength : {10, 50, 100}) {
			const double ramp = RampSecondsFromStrength(strength);
			AxisFilter early;
			const double beforeRamp = Run(early, 1.0, ramp * 0.9, ramp / 900.0, ramp);
			CHECK(beforeRamp < 0.95);
			AxisFilter full;
			const double atRamp = Run(full, 1.0, ramp, ramp / 1000.0, ramp);
			CHECK(std::fabs(atRamp - 0.95) < 1e-3);
		}
	}

	void TestUpdateRateDoesNotChangeTheCurve()
	{
		const double ramp = 0.75;
		AxisFilter fast, slow, single;
		const double atFast = Run(fast, 1.0, 0.36, 0.001, ramp);
		const double atSlow = Run(slow, 1.0, 0.36, 0.012, ramp);
		const double atOnce = Step(single, 1.0, 0.36, ramp);
		CHECK(std::fabs(atFast - atSlow) < 1e-9);
		CHECK(std::fabs(atFast - atOnce) < 1e-9);
		CHECK(std::fabs(fast.stage1 - single.stage1) < 1e-9);
	}

	void TestEasesOutWithoutCrossingZero()
	{
		const double ramp = 1.0;
		AxisFilter filter;
		Run(filter, 1.0, 10.0, 0.011, ramp);
		CHECK(filter.stage2 == 1.0);
		double previous = 1.0;
		bool crossed = false;
		bool rose = false;
		for (int i = 0; i < 400; ++i) {
			const double out = Step(filter, 0.0, 0.011, ramp);
			if (out < 0.0) crossed = true;
			if (out > previous) rose = true;
			previous = out;
		}
		CHECK(!crossed);
		CHECK(!rose);
		CHECK(previous == 0.0);
	}

	void TestReleaseMidRampStaysOnItsSide()
	{
		const double ramp = 1.5;
		AxisFilter filter;
		Run(filter, 1.0, 0.4, 0.011, ramp);
		double peak = filter.stage2;
		for (int i = 0; i < 500; ++i) {
			const double out = Step(filter, 0.0, 0.011, ramp);
			CHECK(out >= 0.0);
			if (out > peak) peak = out;
		}
		CHECK(peak < 1.0);
		CHECK(filter.stage2 == 0.0);
	}

	void TestOutputStaysInsideStickRange()
	{
		std::mt19937 rng(1234);
		std::uniform_real_distribution<double> target(-1.0, 1.0);
		std::uniform_real_distribution<double> dt(0.0005, 0.05);
		AxisFilter filter;
		bool outside = false;
		for (int i = 0; i < 20000; ++i) {
			const double out = Step(filter, target(rng), dt(rng), 0.9);
			if (out < -1.0 || out > 1.0) outside = true;
		}
		CHECK(!outside);
	}

	void TestDroppingStrengthMidRampSnaps()
	{
		AxisFilter filter;
		Run(filter, 1.0, 0.2, 0.011, 1.5);
		CHECK(!IsSettled(filter));
		CHECK(Advance(filter, 0.011, 0.0) == 1.0);
		CHECK(IsSettled(filter));
	}

	void TestZeroAndBadTimeSteps()
	{
		AxisFilter filter;
		Run(filter, 1.0, 0.3, 0.011, 1.5);
		const double before = filter.stage2;
		CHECK(Step(filter, 1.0, 0.0, 1.5) == before);
		CHECK(Step(filter, 1.0, -0.5, 1.5) == before);
		CHECK(Step(filter, 0.25, NAN, 1.5) == 0.25);
	}

	void TestPumpDecision()
	{
		AxisFilter settled;
		Step(settled, 0.5, 0.0, 0.0);
		CHECK(!NeedsPump(settled, 1.0));

		AxisFilter moving;
		Step(moving, 1.0, 0.1, 1.5);
		CHECK(!NeedsPump(moving, 0.005));
		CHECK(NeedsPump(moving, kPumpAfterIdleSeconds));
		CHECK(NeedsPump(moving, 0.5));
	}

	void TestFirstPushAfterLongIdleStillRamps()
	{
		const double ramp = 1.5;
		AxisFilter filter;
		CHECK(OnDriverSample(filter, 0.0, 0.0, ramp) == 0.0);
		CHECK(OnDriverSample(filter, 1.0, 10.0, ramp) == 0.0);
		CHECK(filter.target == 1.0);
		const double afterFrame = Advance(filter, 0.011, ramp);
		CHECK(afterFrame > 0.0);
		CHECK(afterFrame < 0.001);
	}

	void TestDriverSamplesMatchHeldTarget()
	{
		const double ramp = 0.75;
		AxisFilter sampled;
		OnDriverSample(sampled, 1.0, 0.0, ramp);
		double viaSamples = 0.0;
		for (int i = 0; i < 40; ++i) {
			viaSamples = OnDriverSample(sampled, 1.0, 0.009, ramp);
		}
		AxisFilter held;
		const double viaHold = Run(held, 1.0, 0.36, 0.009, ramp);
		CHECK(std::fabs(viaSamples - viaHold) < 1e-9);
	}

	void TestStickPaths()
	{
		CHECK(IsStickAxisPath("/input/joystick/x"));
		CHECK(IsStickAxisPath("/input/joystick/y"));
		CHECK(IsStickAxisPath("/input/thumbstick/x"));
		CHECK(IsStickAxisPath("/input/thumbstick/y"));
		CHECK(IsStickAxisPath("/input/Thumbstick/X"));
		CHECK(!IsStickAxisPath("/input/joystick/click"));
		CHECK(!IsStickAxisPath("/input/joystick/xx"));
		CHECK(!IsStickAxisPath("/input/joystick"));
		CHECK(!IsStickAxisPath("/input/trackpad/x"));
		CHECK(!IsStickAxisPath("/input/trigger/value"));
		CHECK(!IsStickAxisPath(""));
		CHECK(!IsStickAxisPath(nullptr));
	}

}

int main()
{
	TestStrengthMapping();
	TestOffPassesThrough();
	TestGentleStart();
	TestReachesNinetyFivePercentAtRampTime();
	TestUpdateRateDoesNotChangeTheCurve();
	TestEasesOutWithoutCrossingZero();
	TestReleaseMidRampStaysOnItsSide();
	TestOutputStaysInsideStickRange();
	TestDroppingStrengthMidRampSnaps();
	TestZeroAndBadTimeSteps();
	TestPumpDecision();
	TestFirstPushAfterLongIdleStillRamps();
	TestDriverSamplesMatchHeldTarget();
	TestStickPaths();

	if (failures == 0) {
		std::printf("stick_tests: all tests passed\n");
		return 0;
	}
	std::printf("stick_tests: %d failure(s)\n", failures);
	return 1;
}
