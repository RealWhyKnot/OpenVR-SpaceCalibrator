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
		return RampFromSettings(LegacyPushMs(strength), LegacyReleaseMs(strength), 100, 0);
	}

	Ramp Custom(double pushSeconds, double heldBack, double releaseSeconds, double delaySeconds = 0.0)
	{
		return {pushSeconds, releaseSeconds, heldBack, delaySeconds};
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
		const Ramp ramp = RampFromSettings(2400, 900, 60, 350);
		CHECK(std::fabs(ramp.pushSeconds - 2.4) < 1e-12);
		CHECK(std::fabs(ramp.releaseSeconds - 0.9) < 1e-12);
		CHECK(std::fabs(ramp.heldBack - 0.6) < 1e-12);
		CHECK(std::fabs(ramp.delaySeconds - 0.35) < 1e-12);
		const Ramp capped = RampFromSettings(65535, 65535, 255, 65535);
		CHECK(capped.pushSeconds == kMaxPushSeconds);
		CHECK(capped.releaseSeconds == kMaxReleaseSeconds);
		const Ramp longest = RampFromSettings(8000, 6000, 100, 0);
		CHECK(longest.pushSeconds == 8.0);
		CHECK(longest.releaseSeconds == 6.0);
		const Ramp over = RampFromSettings(8100, 6100, 100, 0);
		CHECK(over.pushSeconds == 8.0);
		CHECK(over.releaseSeconds == 6.0);
		CHECK(capped.heldBack == 1.0);
		CHECK(capped.delaySeconds == kMaxDelaySeconds);
		CHECK(IsOff(RampFromSettings(0, 0, 100, 0)));
		CHECK(IsOff(RampFromSettings(3000, 0, 0, 500)));
		CHECK(IsOff(RampFromSettings(0, 0, 100, 500)));
		CHECK(!IsOff(RampFromSettings(0, 900, 100, 0)));
		CHECK(!IsOff(RampFromSettings(3000, 0, 100, 0)));
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
		for (const Ramp& ramp : {Legacy(10), Legacy(50), Legacy(100), RampFromSettings(8000, 1500, 100, 0)}) {
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

	void TestReversalStopsThenBuildsUpLikeAFreshPush()
	{
		for (const Ramp& ramp : {kFull, Custom(4.0, 0.75, 1.5), Custom(2.0, 0.5, 1.0, 0.3)}) {
			AxisFilter reversed;
			Run(reversed, 1.0, 10.0, 0.011, ramp);
			CHECK(reversed.stage2 == 1.0);
			AxisFilter fresh;
			for (int i = 1; i <= 800; ++i) {
				const double out = Step(reversed, -1.0, 0.011, ramp);
				const double expected = Step(fresh, -1.0, 0.011, ramp);
				CHECK(out <= 0.0);
				CHECK(std::fabs(out - expected) < 1e-12);
			}
			CHECK(reversed.stage2 == -1.0);
		}
	}

	void TestReversalFromADriverSampleStopsAtOnce()
	{
		const Ramp ramp = Custom(4.0, 0.75, 1.5);
		AxisFilter filter;
		Run(filter, 0.8, 10.0, 0.011, ramp);
		CHECK(std::fabs(OnDriverSample(filter, -0.8, 0.011, ramp) + 0.2) < 1e-12);
		AxisFilter partial;
		Run(partial, 1.0, 10.0, 0.011, kFull);
		CHECK(OnDriverSample(partial, -0.5, 0.011, kFull) == 0.0);
	}

	void TestRestNoiseAfterReleaseStillGlides()
	{
		for (double rest : {-0.004, -0.026, -0.09}) {
			AxisFilter noisy;
			Run(noisy, 1.0, 10.0, 0.011, kFull);
			AxisFilter clean = noisy;
			for (int i = 0; i < 40; ++i) {
				const double out = Step(noisy, rest, 0.011, kFull);
				const double expected = Step(clean, 0.0, 0.011, kFull);
				CHECK(std::fabs(out - expected) < 1e-12);
			}
			CHECK(noisy.stage2 > 0.5);
			Run(noisy, rest, 10.0, 0.011, kFull);
			CHECK(noisy.stage2 <= kReverseHandoff);
			CHECK(noisy.stage2 >= rest - 1e-9);
		}
	}

	void TestSmallestDeliberateReversalStopsAtOnce()
	{
		AxisFilter filter;
		Run(filter, 1.0, 10.0, 0.011, kFull);
		CHECK(OnDriverSample(filter, -kReverseThreshold, 0.011, kFull) == 0.0);
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
				const double bound = maxRate * step + kSettleEpsilon;
				const bool stoppedForReversal =
				    (goal > 0.0) != (before > 0.0) && std::fabs(goal) >= kReverseThreshold && std::fabs(out) <= bound;
				if (std::fabs(out - before) > bound && !stoppedForReversal) jumped = true;
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
		const Ramp ramps[] = {Custom(3.0, 0.6, 1.0), Custom(0.0, 1.0, 2.0),      Custom(6.0, 0.25, 0.0),
		                      Custom(1.0, 0.0, 0.5), Custom(2.0, 0.7, 1.0, 0.4), Custom(4.0, 1.0, 0.0, 2.0)};
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

	void TestDelayHoldsTheStartSpeedThenRampsOnTime()
	{
		const Ramp ramp = Custom(3.0, 0.6, 1.0, 0.5);
		for (double p : {0.25, 0.5, 0.81, 0.99}) {
			AxisFilter filter;
			CHECK(std::fabs(OnDriverSample(filter, 1.0, 0.0, ramp) - 0.4) < 1e-12);
			CHECK(std::fabs(Run(filter, 1.0, 0.5, 0.005, ramp) - 0.4) < 1e-12);
			const double out = Run(filter, 1.0, p * 3.0, 3.0 / 1000.0, ramp);
			CHECK(std::fabs(out - (0.4 + 0.6 * PushCurve(p))) < 1e-9);
		}
		AxisFilter full;
		OnDriverSample(full, 1.0, 0.0, ramp);
		CHECK(Run(full, 1.0, 3.5, 0.005, ramp) == 1.0);
		CHECK(IsSettled(full));
	}

	void TestQuickTapStaysAtTheStartSpeed()
	{
		const Ramp ramp = Custom(3.0, 0.7, 1.0, 0.4);
		AxisFilter filter;
		CHECK(std::fabs(OnDriverSample(filter, -1.0, 0.0, ramp) + 0.3) < 1e-12);
		for (int i = 0; i < 30; ++i) {
			CHECK(std::fabs(OnDriverSample(filter, -1.0, 0.011, ramp) + 0.3) < 1e-12);
		}
		CHECK(std::fabs(OnDriverSample(filter, 0.0, 0.011, ramp) + 0.3) < 1e-12);
		const double released = OnDriverSample(filter, 0.0, 0.011, ramp);
		CHECK(released > -0.3);
		CHECK(released < 0.0);
		CHECK(Run(filter, 0.0, 3.0, 0.011, ramp) == 0.0);
	}

	void TestDelayStartsOverForEachPush()
	{
		const Ramp ramp = Custom(2.0, 0.5, 0.5, 0.3);
		AxisFilter filter;
		OnDriverSample(filter, 1.0, 0.0, ramp);
		Run(filter, 1.0, 2.5, 0.01, ramp);
		CHECK(filter.stage2 == 1.0);
		Run(filter, 0.0, 2.0, 0.01, ramp);
		CHECK(filter.stage2 == 0.0);
		OnDriverSample(filter, 1.0, 0.01, ramp);
		CHECK(std::fabs(Run(filter, 1.0, 0.25, 0.01, ramp) - 0.5) < 1e-12);

		Run(filter, 1.0, 2.5, 0.01, ramp);
		CHECK(filter.stage2 == 1.0);
		double out = 1.0;
		for (int i = 0; i < 400 && out > 0.0; ++i) {
			out = OnDriverSample(filter, -1.0, 0.01, ramp);
		}
		CHECK(std::fabs(out + 0.5) < 1e-12);
		CHECK(std::fabs(Run(filter, -1.0, 0.25, 0.01, ramp) + 0.5) < 1e-12);
		CHECK(Run(filter, -1.0, 0.2, 0.01, ramp) < -0.5);
	}

	void TestDelayDoesNotRestartMidRamp()
	{
		const Ramp ramp = Custom(3.0, 1.0, 1.0, 0.3);
		AxisFilter filter;
		OnDriverSample(filter, 1.0, 0.0, ramp);
		Run(filter, 1.0, 1.5, 0.01, ramp);
		const double before = filter.stage2;
		CHECK(before > 0.0);
		CHECK(OnDriverSample(filter, 0.9, 0.01, ramp) > before);
		CHECK(Step(filter, 0.9, 0.01, ramp) > before);
	}

	void TestDelayDoesNotDependOnUpdateRate()
	{
		const Ramp ramp = Custom(2.0, 0.8, 1.0, 0.45);
		for (double seconds : {0.15, 0.45, 1.35}) {
			AxisFilter fast, slow, single;
			const double atFast = Run(fast, 1.0, seconds, 0.001, ramp);
			const double atSlow = Run(slow, 1.0, seconds, 0.015, ramp);
			const double atOnce = Step(single, 1.0, seconds, ramp);
			CHECK(std::fabs(atFast - atSlow) < 1e-9);
			CHECK(std::fabs(atFast - atOnce) < 1e-9);
		}
	}

	Ramp Locked(uint8_t percent, uint16_t stopMs = 1500)
	{
		return RampFromSettings(4000, stopMs, 75, 500, percent);
	}

	Ramp LockedTurn(uint8_t percent, uint16_t stopMs = 1500)
	{
		return RampFromSettings(4000, stopMs, 75, 500, percent, true);
	}

	double Hold(AxisFilter& filter, double value, double seconds, double dt, const Ramp& ramp)
	{
		const int steps = (int)std::lround(seconds / dt);
		double out = Output(filter, ramp);
		for (int i = 0; i < steps; ++i) {
			out = OnDriverSample(filter, value, dt, ramp);
		}
		return out;
	}

	void TestLockedSettingsKeepTheBuildUp()
	{
		const Ramp ramp = Locked(60, 900);
		CHECK(ramp.pushSeconds == 4.0);
		CHECK(std::fabs(ramp.heldBack - 0.75) < 1e-12);
		CHECK(std::fabs(ramp.delaySeconds - 0.5) < 1e-12);
		CHECK(std::fabs(ramp.scale - 0.6) < 1e-12);
		CHECK(std::fabs(ramp.releaseSeconds - 0.9) < 1e-12);
		CHECK(Locked(255).scale == 1.0);
		const Ramp buildUp = Locked(0);
		CHECK(buildUp.scale == 1.0);
		CHECK(buildUp.pushSeconds == 4.0);
		CHECK(std::fabs(buildUp.heldBack - 0.75) < 1e-12);
		CHECK(std::fabs(buildUp.delaySeconds - 0.5) < 1e-12);
		CHECK(!IsOff(Locked(50, 0)));
		CHECK(!IsOff(Locked(50)));
		CHECK(IsOff(RampFromSettings(0, 0, 75, 500, 50)));
		CHECK(IsOff(RampFromSettings(0, 0, 75, 500, 50, true)));
	}

	void TestLockedBuildsUpToTheLockOnTimeAndHolds()
	{
		for (int percent : {25, 40, 50, 80, 100}) {
			const double lock = percent / 100.0;
			const double start = lock < 0.25 ? lock : 0.25;
			for (const Ramp& ramp : {Locked((uint8_t)percent), LockedTurn((uint8_t)percent)}) {
				AxisFilter filter;
				const double dt = 1.0 / 90.0;
				CHECK(std::fabs(OnDriverSample(filter, 1.0, 0.0, ramp) - start) < 1e-12);
				CHECK(std::fabs(Hold(filter, 1.0, 0.4, dt, ramp) - start) < 1e-12);
				double previous = Output(filter, ramp);
				bool rising = true;
				for (int i = 0; i < 360; ++i) {
					const double out = OnDriverSample(filter, 1.0, dt, ramp);
					rising &= out >= previous - 1e-12 && out <= lock + 1e-12;
					previous = out;
				}
				CHECK(rising);
				if (lock > start) CHECK(previous < lock - 1e-4);
				CHECK(std::fabs(Hold(filter, 1.0, 0.1, dt, ramp) - lock) < 1e-9);
				double worst = 0.0;
				for (int i = 0; i < 60 * 90; ++i) {
					worst = std::fmax(worst, std::fabs(OnDriverSample(filter, 1.0, dt, ramp) - lock));
				}
				CHECK(worst < 1e-12);
				CHECK(IsSettled(filter));
			}
		}
	}

	void TestLockedBuildUpFollowsThePushCurve()
	{
		for (int percent : {40, 60, 90}) {
			const double lock = percent / 100.0;
			const Ramp ramp = RampFromSettings(4000, 1500, 100, 0, (uint8_t)percent);
			const Ramp turn = RampFromSettings(4000, 1500, 100, 0, (uint8_t)percent, true);
			AxisFilter move, turning;
			OnDriverSample(move, 1.0, 0.0, ramp);
			OnDriverSample(turning, 1.0, 0.0, turn);
			double worst = 0.0;
			for (int i = 1; i <= 400; ++i) {
				const double expected = lock * PushCurve(i / 400.0);
				worst = std::fmax(worst, std::fabs(OnDriverSample(move, 1.0, 0.01, ramp) - expected));
				worst = std::fmax(worst, std::fabs(OnDriverSample(turning, 1.0, 0.01, turn) - expected));
			}
			CHECK(worst < 1e-9);
		}
	}

	void TestLockedStartSpeedStopsAtTheLock()
	{
		for (const Ramp& ramp : {RampFromSettings(4000, 1500, 60, 500, 30), RampFromSettings(4000, 1500, 60, 500, 30, true)}) {
			AxisFilter filter;
			CHECK(std::fabs(OnDriverSample(filter, 1.0, 0.0, ramp) - 0.3) < 1e-12);
			CHECK(std::fabs(Hold(filter, 1.0, 2.0, 0.011, ramp) - 0.3) < 1e-12);
		}
		for (const Ramp& ramp : {RampFromSettings(4000, 1500, 60, 0, 50), RampFromSettings(4000, 1500, 60, 0, 50, true)}) {
			AxisFilter filter;
			CHECK(std::fabs(OnDriverSample(filter, 1.0, 0.0, ramp) - 0.4) < 1e-12);
		}
	}

	void TestLockedBuildUpScalesPartialPushesAndKeepsDiagonals()
	{
		const Ramp ramp = Locked(50);
		AxisFilter single;
		CHECK(std::fabs(OnDriverSample(single, 0.6, 0.0, ramp) - 0.15) < 1e-12);
		CHECK(std::fabs(Hold(single, 0.6, 6.0, 0.011, ramp) - 0.3) < 1e-9);
		AxisFilter negative;
		CHECK(std::fabs(OnDriverSample(negative, -0.8, 0.0, ramp) + 0.2) < 1e-12);
		CHECK(std::fabs(Hold(negative, -0.8, 6.0, 0.011, ramp) + 0.4) < 1e-9);
		AxisFilter x, y;
		const double want = std::atan2(0.95, 0.3);
		double worstAngle = 0.0;
		double outX = 0.0, outY = 0.0;
		for (int i = 0; i <= 600; ++i) {
			outX = OnDriverSample(x, 0.3, i ? 0.01 : 0.0, ramp);
			outY = OnDriverSample(y, 0.95, i ? 0.01 : 0.0, ramp);
			worstAngle = std::fmax(worstAngle, std::fabs(std::atan2(outY, outX) - want));
		}
		CHECK(worstAngle < 1e-12);
		CHECK(std::fabs(std::hypot(outX, outY) - 0.5 * std::hypot(0.3, 0.95)) < 1e-9);
	}

	void TestLockedBuildUpDoesNotDependOnUpdateRate()
	{
		for (const Ramp& ramp : {Locked(60), LockedTurn(60)}) {
			for (double seconds : {0.3, 1.35, 2.7, 5.0}) {
				AxisFilter fast, slow, single;
				OnDriverSample(fast, 1.0, 0.0, ramp);
				OnDriverSample(slow, 1.0, 0.0, ramp);
				OnDriverSample(single, 1.0, 0.0, ramp);
				const double atFast = Hold(fast, 1.0, seconds, 0.001, ramp);
				const double atSlow = Hold(slow, 1.0, seconds, seconds / 20.0, ramp);
				const double atOnce = OnDriverSample(single, 1.0, seconds, ramp);
				CHECK(std::fabs(atFast - atSlow) < 1e-9);
				CHECK(std::fabs(atFast - atOnce) < 1e-9);
			}
		}
	}

	void TestLockedStopGlidesFromTheLockedSpeed()
	{
		const Ramp ramp = Locked(50);
		AxisFilter filter;
		Run(filter, 1.0, 6.0, 0.011, ramp);
		double previous = Output(filter, ramp);
		CHECK(std::fabs(previous - 0.5) < 1e-12);
		const double dt = ramp.releaseSeconds / 1000.0;
		bool monotonic = true;
		for (int i = 0; i < 1000; ++i) {
			const double out = Step(filter, 0.0, dt, ramp);
			monotonic &= out <= previous && out >= 0.0;
			previous = out;
		}
		CHECK(monotonic);
		CHECK(std::fabs(previous - 0.025) < 1e-3);

		const Ramp instant = Locked(50, 0);
		AxisFilter stopped;
		Run(stopped, 1.0, 1.0, 0.011, instant);
		CHECK(Step(stopped, 0.0, 0.011, instant) == 0.0);
	}

	void TestLockedReversalStopsThenBuildsUpAgain()
	{
		for (const Ramp& ramp : {Locked(50), LockedTurn(50)}) {
			AxisFilter filter;
			Hold(filter, 1.0, 6.0, 0.011, ramp);
			CHECK(std::fabs(OnDriverSample(filter, -1.0, 0.011, ramp) + 0.25) < 1e-12);
			CHECK(std::fabs(Hold(filter, -1.0, 6.0, 0.011, ramp) + 0.5) < 1e-9);
		}
	}

	void TestLockedStopDoesNotDependOnUpdateRate()
	{
		const Ramp ramp = Locked(40);
		AxisFilter fast, slow;
		Run(fast, 1.0, 6.0, 0.011, ramp);
		Run(slow, 1.0, 6.0, 0.011, ramp);
		const double atFast = Run(fast, 0.0, 0.6, 0.001, ramp);
		const double atSlow = Run(slow, 0.0, 0.6, 0.012, ramp);
		CHECK(atFast > 0.0 && atFast < 0.4);
		CHECK(std::fabs(atFast - atSlow) < 1e-9);
	}

	void TestLockedRandomInputStaysUnderTheLock()
	{
		std::mt19937 rng(7);
		std::uniform_real_distribution<double> value(-1.0, 1.0);
		std::uniform_real_distribution<double> dt(0.0, 0.03);
		for (int percent : {25, 60, 100}) {
			for (const Ramp& ramp : {Locked((uint8_t)percent), LockedTurn((uint8_t)percent)}) {
				const double lock = percent / 100.0;
				AxisFilter filter;
				bool inRange = true;
				for (int i = 0; i < 20000; ++i) {
					const double out = OnDriverSample(filter, value(rng), dt(rng), ramp);
					inRange &= std::isfinite(out) && std::fabs(out) <= lock + 1e-12;
				}
				CHECK(inRange);
			}
		}
	}

	void TestLockedTurnCapsInsteadOfScaling()
	{
		const Ramp ramp = LockedTurn(60, 900);
		CHECK(ramp.scale == 1.0);
		CHECK(std::fabs(ramp.limit - 0.6) < 1e-12);
		CHECK(ramp.pushSeconds == 4.0);
		CHECK(std::fabs(ramp.delaySeconds - 0.5) < 1e-12);
		CHECK(std::fabs(ramp.releaseSeconds - 0.9) < 1e-12);
		CHECK(Locked(60).limit == 1.0);
		CHECK(LockedTurn(255).limit == 1.0);
		const Ramp buildUp = LockedTurn(0);
		CHECK(buildUp.scale == 1.0 && buildUp.limit == 1.0);
		CHECK(buildUp.pushSeconds == 4.0);
		CHECK(!IsOff(LockedTurn(50, 0)));
		CHECK(!IsOff(LockedTurn(50)));
	}

	void TestLockedTurnBuildsUpLikeBuildUpUnderTheLock()
	{
		for (int strength : {75, 70, 90, 45, 5}) {
			const double startSpeed = (100 - strength) / 100.0;
			for (int percent : {25, 50, 80, 100}) {
				const double lock = percent / 100.0;
				for (uint16_t stopMs : {(uint16_t)1500, (uint16_t)0}) {
					const Ramp ramp = RampFromSettings(4000, stopMs, (uint8_t)strength, 500, (uint8_t)percent, true);
					const Ramp buildUp = RampFromSettings(4000, stopMs, (uint8_t)strength, 500);
					bool same = true;
					bool settles = true;
					for (int step = 1; step <= 20; ++step) {
						const double push = step / 20.0;
						AxisFilter right, left, plain;
						const double start = OnDriverSample(right, push, 0.0, ramp);
						OnDriverSample(left, -push, 0.0, ramp);
						OnDriverSample(plain, push, 0.0, buildUp);
						const double expectedStart = startSpeed * push < lock ? startSpeed * push : lock;
						same &= std::fabs(start - expectedStart) < 1e-12;
						for (int i = 0; i < 600; ++i) {
							const double outRight = OnDriverSample(right, push, 0.01, ramp);
							const double outLeft = OnDriverSample(left, -push, 0.01, ramp);
							const double outPlain = OnDriverSample(plain, push, 0.01, buildUp);
							same &= outLeft == -outRight;
							if (push <= lock) same &= outRight == outPlain;
						}
						settles &= std::fabs(Output(right, ramp) - (push < lock ? push : lock)) < 1e-9;
					}
					CHECK(same);
					CHECK(settles);
				}
			}
		}
	}

	void TestLockedTurnStopGlidesFromTheCap()
	{
		const Ramp ramp = LockedTurn(50);
		AxisFilter filter;
		for (int i = 0; i < 500; ++i)
			OnDriverSample(filter, 1.0, 0.011, ramp);
		double previous = Output(filter, ramp);
		CHECK(std::fabs(previous - 0.5) < 1e-12);
		CHECK(std::fabs(OnDriverSample(filter, 0.0, 0.011, ramp) - 0.5) < 1e-12);
		const double dt = ramp.releaseSeconds / 1000.0;
		bool monotonic = true;
		for (int i = 0; i < 1000; ++i) {
			const double out = Advance(filter, dt, ramp);
			monotonic &= out <= previous && out >= 0.0;
			previous = out;
		}
		CHECK(monotonic);
		CHECK(std::fabs(previous - 0.025) < 1e-3);

		AxisFilter reversing;
		for (int i = 0; i < 500; ++i)
			OnDriverSample(reversing, 1.0, 0.011, ramp);
		CHECK(std::fabs(OnDriverSample(reversing, -1.0, 0.011, ramp) + 0.25) < 1e-12);
	}

	void TestBuildUpOutputIsTheFilterItself()
	{
		std::mt19937 rng(11);
		std::uniform_real_distribution<double> value(-1.0, 1.0);
		std::uniform_real_distribution<double> dt(0.0, 0.03);
		for (const Ramp& ramp : {kFull, kHalf, Locked(0), Custom(2.0, 0.6, 0.9, 0.3)}) {
			AxisFilter filter;
			bool same = true;
			for (int i = 0; i < 5000; ++i) {
				const double out = OnDriverSample(filter, value(rng), dt(rng), ramp);
				same &= out == filter.stage2;
			}
			CHECK(same);
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
	TestReversalStopsThenBuildsUpLikeAFreshPush();
	TestReversalFromADriverSampleStopsAtOnce();
	TestRestNoiseAfterReleaseStillGlides();
	TestSmallestDeliberateReversalStopsAtOnce();
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
	TestDelayHoldsTheStartSpeedThenRampsOnTime();
	TestQuickTapStaysAtTheStartSpeed();
	TestDelayStartsOverForEachPush();
	TestDelayDoesNotRestartMidRamp();
	TestDelayDoesNotDependOnUpdateRate();
	TestLockedSettingsKeepTheBuildUp();
	TestLockedBuildsUpToTheLockOnTimeAndHolds();
	TestLockedBuildUpFollowsThePushCurve();
	TestLockedStartSpeedStopsAtTheLock();
	TestLockedBuildUpScalesPartialPushesAndKeepsDiagonals();
	TestLockedBuildUpDoesNotDependOnUpdateRate();
	TestLockedStopGlidesFromTheLockedSpeed();
	TestLockedReversalStopsThenBuildsUpAgain();
	TestLockedStopDoesNotDependOnUpdateRate();
	TestLockedRandomInputStaysUnderTheLock();
	TestLockedTurnCapsInsteadOfScaling();
	TestLockedTurnBuildsUpLikeBuildUpUnderTheLock();
	TestLockedTurnStopGlidesFromTheCap();
	TestBuildUpOutputIsTheFilterItself();
	TestStickPaths();

	if (failures == 0) {
		std::printf("stick_tests: all tests passed\n");
		return 0;
	}
	std::printf("stick_tests: %d failure(s)\n", failures);
	return 1;
}
