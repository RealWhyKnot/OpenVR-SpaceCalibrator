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

	Ramp Legacy(uint8_t strength)
	{
		return RampFromSettings(LegacyPushMs(strength), LegacyReleaseMs(strength), 100);
	}

	Ramp Custom(double pushSeconds, double heldBack, double releaseSeconds)
	{
		return {pushSeconds, releaseSeconds, heldBack};
	}

	const Ramp kFull = Legacy(100);
	const Ramp kHalf = Legacy(50);

	double Step(AxisFilter& filter, double target, double dt, const Ramp& ramp)
	{
		filter.target = target;
		return Advance(filter, dt, ramp);
	}

	double Run(AxisFilter& filter, double target, double seconds, double dt, const Ramp& ramp)
	{
		const int steps = (int)std::lround(seconds / dt);
		double out = filter.stage2;
		for (int i = 0; i < steps; ++i) {
			out = Step(filter, target, dt, ramp);
		}
		return out;
	}

	double PushCurve(double p)
	{
		if (p >= 1.0) return 1.0;
		return p < 0.8 ? 1.25 * p * p : 1.0 - 5.0 * (1.0 - p) * (1.0 - p);
	}

	void TestSettingsMapping()
	{
		const Ramp ramp = RampFromSettings(2400, 900, 60);
		CHECK(std::fabs(ramp.pushSeconds - 2.4) < 1e-12);
		CHECK(std::fabs(ramp.releaseSeconds - 0.9) < 1e-12);
		CHECK(std::fabs(ramp.heldBack - 0.6) < 1e-12);
		const Ramp capped = RampFromSettings(65535, 65535, 255);
		CHECK(capped.pushSeconds == kMaxRampSeconds);
		CHECK(capped.releaseSeconds == kMaxRampSeconds);
		CHECK(capped.heldBack == 1.0);
		CHECK(IsOff(RampFromSettings(0, 0, 100)));
		CHECK(IsOff(RampFromSettings(3000, 0, 0)));
		CHECK(!IsOff(RampFromSettings(0, 900, 100)));
		CHECK(!IsOff(RampFromSettings(3000, 0, 100)));
	}

	void TestLegacySliderKeepsItsFeel()
	{
		CHECK(LegacyPushMs(0) == 0);
		CHECK(LegacyReleaseMs(0) == 0);
		CHECK(IsOff(Legacy(0)));
		CHECK(LegacyPushMs(50) == 2000);
		CHECK(LegacyReleaseMs(50) == 750);
		CHECK(LegacyPushMs(100) == 4000);
		CHECK(LegacyReleaseMs(100) == 1500);
		CHECK(LegacyPushMs(255) == 4000);
		CHECK(LegacyPushMs(33) == 1320);
		CHECK(LegacyReleaseMs(33) == 495);
		CHECK(kFull.heldBack == 1.0);
	}

	void TestOffPassesThrough()
	{
		AxisFilter filter;
		CHECK(Step(filter, 0.7, 0.01, Ramp{}) == 0.7);
		CHECK(IsSettled(filter));
		CHECK(Step(filter, -1.0, 0.0, Ramp{}) == -1.0);
		CHECK(OnDriverSample(filter, 0.3, 5.0, Ramp{}) == 0.3);
		CHECK(filter.target == 0.3);
	}

	void TestGentleStart()
	{
		AxisFilter filter;
		const double halfSecond = Run(filter, 1.0, 0.5, 0.005, kFull);
		CHECK(halfSecond > 0.0);
		CHECK(halfSecond < 0.02);
		const double oneSecond = Run(filter, 1.0, 0.5, 0.005, kFull);
		CHECK(oneSecond < 0.08);
	}

	void TestSpeedsUpFasterTheLongerItIsHeld()
	{
		AxisFilter filter;
		double previous = 0.0;
		double previousGain = 0.0;
		for (int i = 1; i <= 32; ++i) {
			const double out = Step(filter, 1.0, 0.1, kFull);
			const double gain = out - previous;
			CHECK(gain > previousGain);
			previous = out;
			previousGain = gain;
		}
		for (int i = 33; i <= 40; ++i) {
			const double out = Step(filter, 1.0, 0.1, kFull);
			const double gain = out - previous;
			CHECK(gain < previousGain);
			CHECK(gain >= 0.0);
			previous = out;
			previousGain = gain;
		}
		CHECK(previous == 1.0);
	}

	void TestFollowsThePushCurveAndLandsAtPushTime()
	{
		for (uint8_t strength : {10, 50, 100}) {
			const Ramp ramp = Legacy(strength);
			const double T = ramp.pushSeconds;
			for (double p : {0.25, 0.5, 0.79, 0.81, 0.9, 0.99}) {
				AxisFilter filter;
				const double out = Run(filter, 1.0, p * T, T / 1000.0, ramp);
				CHECK(std::fabs(out - PushCurve(p)) < 1e-9);
			}
			AxisFilter full;
			CHECK(Run(full, 1.0, T, T / 1000.0, ramp) == 1.0);
			CHECK(IsSettled(full));
		}
	}

	void TestPartialPushTakesTheSameTime()
	{
		AxisFilter half;
		CHECK(std::fabs(Run(half, 0.5, 2.0, 0.01, kFull) - 0.5 * PushCurve(0.5)) < 1e-9);
		CHECK(Run(half, 0.5, 2.0, 0.01, kFull) == 0.5);
	}

	void TestDiagonalPushKeepsItsDirection()
	{
		AxisFilter x, y;
		for (int i = 0; i < 400; ++i) {
			const double ox = Step(x, -0.2, 0.011, kFull);
			const double oy = Step(y, 1.0, 0.011, kFull);
			if (oy > 1e-6) CHECK(std::fabs(ox / oy + 0.2) < 1e-9);
		}
		CHECK(x.stage2 == -0.2);
		CHECK(y.stage2 == 1.0);
	}

	void TestUpdateRateDoesNotChangeTheCurve()
	{
		for (double seconds : {0.36, 1.8}) {
			AxisFilter fast, slow, single;
			const double atFast = Run(fast, 1.0, seconds, 0.001, kHalf);
			const double atSlow = Run(slow, 1.0, seconds, 0.012, kHalf);
			const double atOnce = Step(single, 1.0, seconds, kHalf);
			CHECK(std::fabs(atFast - atSlow) < 1e-9);
			CHECK(std::fabs(atFast - atOnce) < 1e-9);
			CHECK(std::fabs(fast.velocity - single.velocity) < 1e-9);
		}
	}

	void TestReleaseKeepsTodaysStop()
	{
		AxisFilter filter;
		Run(filter, 1.0, 10.0, 0.011, kFull);
		CHECK(filter.stage2 == 1.0);
		AxisFilter released = filter;
		const double atRelease = Run(released, 0.0, kFull.releaseSeconds, kFull.releaseSeconds / 1000.0, kFull);
		CHECK(std::fabs(atRelease - 0.05) < 1e-3);
	}

	void TestEasesOutWithoutCrossingZero()
	{
		AxisFilter filter;
		Run(filter, 1.0, 10.0, 0.011, kFull);
		CHECK(filter.stage2 == 1.0);
		double previous = 1.0;
		bool crossed = false;
		bool rose = false;
		for (int i = 0; i < 800; ++i) {
			const double out = Step(filter, 0.0, 0.011, kFull);
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
		AxisFilter filter;
		Run(filter, 1.0, 2.0, 0.011, kFull);
		double peak = filter.stage2;
		for (int i = 0; i < 800; ++i) {
			const double out = Step(filter, 0.0, 0.011, kFull);
			CHECK(out >= 0.0);
			if (out > peak) peak = out;
		}
		CHECK(peak < 1.0);
		CHECK(filter.stage2 == 0.0);
	}

	void TestReversalSlowsDownThenRampsUpTheOtherWay()
	{
		AxisFilter filter;
		Run(filter, 1.0, 10.0, 0.011, kFull);
		const double dt = 0.011;
		const double maxStep = (4.6 / kFull.pushSeconds + 2.0 * kTimeConstantsTo95Percent / kFull.releaseSeconds) * dt;
		double previous = filter.stage2;
		double handoffAt = -1.0;
		double oneSecondAfterHandoff = 0.0;
		for (int i = 1; i <= 1200; ++i) {
			const double out = Step(filter, -1.0, dt, kFull);
			CHECK(out <= previous);
			CHECK(previous - out <= maxStep);
			if (handoffAt < 0.0 && std::fabs(out) < kReverseHandoff) handoffAt = i * dt;
			if (handoffAt > 0.0 && oneSecondAfterHandoff == 0.0 && i * dt >= handoffAt + 1.0) oneSecondAfterHandoff = out;
			previous = out;
		}
		CHECK(handoffAt > 1.0);
		CHECK(oneSecondAfterHandoff < 0.0);
		CHECK(oneSecondAfterHandoff > -0.1);
		CHECK(filter.stage2 == -1.0);
	}

	void TestRandomInputNeverJumps()
	{
		std::mt19937 rng(1234);
		std::uniform_real_distribution<double> target(-1.0, 1.0);
		std::uniform_real_distribution<double> dt(0.0005, 0.05);
		std::uniform_int_distribution<int> hold(1, 60);
		const Ramp ramp = Legacy(60);
		const double maxRate = 4.6 / ramp.pushSeconds + 2.0 * kTimeConstantsTo95Percent / ramp.releaseSeconds;
		AxisFilter filter;
		bool outside = false;
		bool jumped = false;
		for (int i = 0; i < 4000; ++i) {
			const double goal = target(rng);
			for (int n = hold(rng); n > 0; --n) {
				const double before = filter.stage2;
				const double step = dt(rng);
				const double out = Step(filter, goal, step, ramp);
				if (out < -1.0 || out > 1.0) outside = true;
				if (std::fabs(out - before) > maxRate * step + kSettleEpsilon) jumped = true;
			}
		}
		CHECK(!outside);
		CHECK(!jumped);
	}

	void TestDroppingStrengthMidRampSnaps()
	{
		AxisFilter filter;
		Run(filter, 1.0, 0.2, 0.011, kFull);
		CHECK(!IsSettled(filter));
		CHECK(Advance(filter, 0.011, Ramp{}) == 1.0);
		CHECK(IsSettled(filter));
		CHECK(filter.velocity == 0.0);
	}

	void TestZeroAndBadTimeSteps()
	{
		AxisFilter filter;
		Run(filter, 1.0, 0.3, 0.011, kFull);
		const double before = filter.stage2;
		CHECK(Step(filter, 1.0, 0.0, kFull) == before);
		CHECK(Step(filter, 1.0, -0.5, kFull) == before);
		CHECK(Step(filter, 0.25, NAN, kFull) == 0.25);
	}

	void TestPumpDecision()
	{
		AxisFilter settled;
		Step(settled, 0.5, 0.0, Ramp{});
		CHECK(!NeedsPump(settled, 1.0));

		AxisFilter moving;
		Step(moving, 1.0, 0.1, kFull);
		CHECK(!NeedsPump(moving, 0.005));
		CHECK(NeedsPump(moving, kPumpAfterIdleSeconds));
		CHECK(NeedsPump(moving, 0.5));
	}

	void TestFirstPushAfterLongIdleStillRamps()
	{
		AxisFilter filter;
		CHECK(OnDriverSample(filter, 0.0, 0.0, kFull) == 0.0);
		CHECK(OnDriverSample(filter, 1.0, 10.0, kFull) == 0.0);
		CHECK(filter.target == 1.0);
		const double afterFrame = Advance(filter, 0.011, kFull);
		CHECK(afterFrame > 0.0);
		CHECK(afterFrame < 0.001);
	}

	void TestDriverSamplesMatchHeldTarget()
	{
		AxisFilter sampled;
		OnDriverSample(sampled, 1.0, 0.0, kHalf);
		double viaSamples = 0.0;
		for (int i = 0; i < 40; ++i) {
			viaSamples = OnDriverSample(sampled, 1.0, 0.009, kHalf);
		}
		AxisFilter held;
		const double viaHold = Run(held, 1.0, 0.36, 0.009, kHalf);
		CHECK(std::fabs(viaSamples - viaHold) < 1e-9);
	}

	void TestStrengthStartsPartwayUpAndStillLandsAtTheTimer()
	{
		const Ramp ramp = Custom(3.0, 0.6, 1.125);
		for (double p : {0.25, 0.5, 0.79, 0.81, 0.9, 0.99}) {
			AxisFilter filter;
			CHECK(std::fabs(OnDriverSample(filter, 1.0, 0.0, ramp) - 0.4) < 1e-12);
			const double out = Run(filter, 1.0, p * 3.0, 3.0 / 1000.0, ramp);
			CHECK(std::fabs(out - (0.4 + 0.6 * PushCurve(p))) < 1e-9);
		}
		AxisFilter full;
		CHECK(Run(full, 1.0, 3.0, 3.0 / 1000.0, ramp) == 1.0);
		CHECK(IsSettled(full));
	}

	void TestStrengthScalesWithPartialPushes()
	{
		const Ramp ramp = Custom(2.0, 0.3, 1.0);
		AxisFilter filter;
		CHECK(std::fabs(OnDriverSample(filter, -0.5, 0.0, ramp) + 0.35) < 1e-12);
		Run(filter, -0.5, 2.0, 0.01, ramp);
		CHECK(filter.stage2 == -0.5);
		CHECK(std::fabs(OnDriverSample(filter, -1.0, 0.01, ramp) + 0.7) < 1e-12);
		CHECK(Run(filter, -1.0, 2.0, 0.01, ramp) == -1.0);
	}

	void TestStrengthKeepsDiagonalDirection()
	{
		const Ramp ramp = Custom(3.0, 0.6, 1.0);
		AxisFilter x, y;
		OnDriverSample(x, -0.2, 0.0, ramp);
		OnDriverSample(y, 1.0, 0.0, ramp);
		CHECK(std::fabs(x.stage2 / y.stage2 + 0.2) < 1e-12);
		for (int i = 0; i < 400; ++i) {
			const double ox = Step(x, -0.2, 0.011, ramp);
			const double oy = Step(y, 1.0, 0.011, ramp);
			CHECK(std::fabs(ox / oy + 0.2) < 1e-9);
		}
		CHECK(x.stage2 == -0.2);
		CHECK(y.stage2 == 1.0);
	}

	void TestInstantPushStillGlidesToAStop()
	{
		for (const Ramp& ramp : {Custom(0.0, 1.0, 1.5), Custom(3.0, 0.0, 1.5)}) {
			AxisFilter filter;
			CHECK(OnDriverSample(filter, 1.0, 0.0, ramp) == 1.0);
			CHECK(IsSettled(filter));
			CHECK(OnDriverSample(filter, 0.0, 0.01, ramp) == 1.0);
			const double atStop = Run(filter, 0.0, 1.5, 1.5 / 1000.0, ramp);
			CHECK(std::fabs(atStop - 0.05) < 1e-3);
		}
	}

	void TestInstantStopStillRampsThePush()
	{
		const Ramp ramp = Custom(4.0, 1.0, 0.0);
		AxisFilter filter;
		CHECK(std::fabs(Run(filter, 1.0, 2.0, 0.004, ramp) - PushCurve(0.5)) < 1e-9);
		CHECK(OnDriverSample(filter, 0.0, 0.004, ramp) == 0.0);
		CHECK(IsSettled(filter));
		Run(filter, 1.0, 4.0, 0.004, ramp);
		CHECK(filter.stage2 == 1.0);
		CHECK(OnDriverSample(filter, 0.4, 0.004, ramp) == 0.4);
	}

	void TestInstantStopReversesStraightIntoTheNewPush()
	{
		const Ramp ramp = Custom(2.0, 0.5, 0.0);
		AxisFilter filter;
		Run(filter, 1.0, 2.0, 0.01, ramp);
		CHECK(filter.stage2 == 1.0);
		CHECK(std::fabs(OnDriverSample(filter, -1.0, 0.01, ramp) + 0.5) < 1e-12);
		const double after = Step(filter, -1.0, 0.5, ramp);
		CHECK(after < -0.5);
		CHECK(after > -1.0);
	}

	void TestRandomInputStaysInRangeForEverySetting()
	{
		std::mt19937 rng(99);
		std::uniform_real_distribution<double> target(-1.0, 1.0);
		std::uniform_real_distribution<double> dt(0.0005, 0.05);
		std::uniform_int_distribution<int> hold(1, 60);
		const Ramp ramps[] = {Custom(3.0, 0.6, 1.0), Custom(0.0, 1.0, 2.0), Custom(6.0, 0.25, 0.0), Custom(1.0, 0.0, 0.5)};
		for (const Ramp& ramp : ramps) {
			AxisFilter filter;
			bool outside = false;
			for (int i = 0; i < 2000; ++i) {
				const double goal = target(rng);
				for (int n = hold(rng); n > 0; --n) {
					const double out = OnDriverSample(filter, goal, dt(rng), ramp);
					if (!(out >= -1.0 && out <= 1.0)) outside = true;
				}
			}
			CHECK(!outside);
			OnDriverSample(filter, -0.75, 0.01, ramp);
			CHECK(Run(filter, -0.75, 15.0, 0.01, ramp) == -0.75);
			CHECK(IsSettled(filter));
		}
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
	TestSettingsMapping();
	TestLegacySliderKeepsItsFeel();
	TestOffPassesThrough();
	TestGentleStart();
	TestSpeedsUpFasterTheLongerItIsHeld();
	TestFollowsThePushCurveAndLandsAtPushTime();
	TestPartialPushTakesTheSameTime();
	TestDiagonalPushKeepsItsDirection();
	TestUpdateRateDoesNotChangeTheCurve();
	TestReleaseKeepsTodaysStop();
	TestEasesOutWithoutCrossingZero();
	TestReleaseMidRampStaysOnItsSide();
	TestReversalSlowsDownThenRampsUpTheOtherWay();
	TestRandomInputNeverJumps();
	TestDroppingStrengthMidRampSnaps();
	TestZeroAndBadTimeSteps();
	TestPumpDecision();
	TestFirstPushAfterLongIdleStillRamps();
	TestDriverSamplesMatchHeldTarget();
	TestStrengthStartsPartwayUpAndStillLandsAtTheTimer();
	TestStrengthScalesWithPartialPushes();
	TestStrengthKeepsDiagonalDirection();
	TestInstantPushStillGlidesToAStop();
	TestInstantStopStillRampsThePush();
	TestInstantStopReversesStraightIntoTheNewPush();
	TestRandomInputStaysInRangeForEverySetting();
	TestStickPaths();

	if (failures == 0) {
		std::printf("stick_tests: all tests passed\n");
		return 0;
	}
	std::printf("stick_tests: %d failure(s)\n", failures);
	return 1;
}
