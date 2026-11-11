#include "PoseFilter.h"

#include <cmath>
#include <cstdio>

namespace {

	int failures = 0;

#define CHECK(cond)                                                                                                                        \
	do {                                                                                                                                   \
		if (!(cond)) {                                                                                                                     \
			std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);                                                                     \
			++failures;                                                                                                                    \
		}                                                                                                                                  \
	} while (0)

	void TestConverges()
	{
		spacecal::PoseFilter f;
		spacecal::OneEuroParams p;
		const double pos[3] = {1.0, 2.0, 3.0};
		for (int i = 0; i < 500; ++i) {
			spacecal::Step(f, p, pos, 0.01);
		}
		CHECK(std::fabs(f.pos[0] - 1.0) < 1e-6);
		CHECK(std::fabs(f.pos[1] - 2.0) < 1e-6);
		CHECK(std::fabs(f.pos[2] - 3.0) < 1e-6);
	}

	void TestBetaTracksFaster()
	{
		spacecal::OneEuroParams slow;
		slow.beta = 0.0;
		spacecal::OneEuroParams fast;
		fast.beta = 50.0;
		spacecal::PoseFilter a;
		spacecal::PoseFilter b;
		const double origin[3] = {0.0, 0.0, 0.0};
		spacecal::Step(a, slow, origin, 0.01);
		spacecal::Step(b, fast, origin, 0.01);
		const double target[3] = {0.2, 0.0, 0.0};
		for (int i = 0; i < 5; ++i) {
			spacecal::Step(a, slow, target, 0.01);
			spacecal::Step(b, fast, target, 0.01);
		}
		CHECK(b.pos[0] > a.pos[0]);
	}

	void TestReseeds()
	{
		spacecal::PoseFilter f;
		spacecal::OneEuroParams p;
		const double a[3] = {0.0, 0.0, 0.0};
		using spacecal::StepResult;
		CHECK(spacecal::Step(f, p, a, 0.01) == StepResult::Seeded);
		CHECK(spacecal::Step(f, p, a, 0.01) == StepResult::Ok);
		CHECK(spacecal::Step(f, p, a, 0.3) == StepResult::Gap);
		CHECK(spacecal::Step(f, p, a, 0.01) == StepResult::Ok);
		const double nanPos[3] = {std::nan(""), 0.0, 0.0};
		CHECK(spacecal::Step(f, p, nanPos, 0.01) == StepResult::Invalid);
		CHECK(spacecal::Step(f, p, a, 0.01) == StepResult::Seeded);
		CHECK(spacecal::Step(f, p, a, 0.01) == StepResult::Ok);
		const double jump[3] = {0.6, 0.0, 0.0};
		CHECK(spacecal::Step(f, p, jump, 0.01) == StepResult::Jump);
		CHECK(std::fabs(f.pos[0] - 0.6) < 1e-12);
	}

	void TestStrengthMapping()
	{
		CHECK(std::fabs(spacecal::ParamsFromStrength(0).minCutoffHz - 16.45) < 1e-9);
		CHECK(std::fabs(spacecal::ParamsFromStrength(100).minCutoffHz - 0.45) < 1e-9);
		CHECK(std::fabs(spacecal::ParamsFromStrength(0).beta - 6.0) < 1e-9);
		CHECK(std::fabs(spacecal::ParamsFromStrength(100).beta - 24.0) < 1e-9);
		for (int s = 1; s <= 100; ++s) {
			const spacecal::OneEuroParams lo = spacecal::ParamsFromStrength((uint8_t)(s - 1));
			const spacecal::OneEuroParams hi = spacecal::ParamsFromStrength((uint8_t)s);
			CHECK(hi.minCutoffHz < lo.minCutoffHz);
			CHECK(hi.beta > lo.beta);
		}
		CHECK(std::fabs(spacecal::ParamsFromStrength(200).minCutoffHz - 0.45) < 1e-9);
	}

	void TestPredictionScale()
	{
		CHECK(std::fabs(spacecal::PredictionScaleFromStrength(0) - 1.0) < 1e-12);
		CHECK(std::fabs(spacecal::PredictionScaleFromStrength(50) - 0.25) < 1e-12);
		CHECK(std::fabs(spacecal::PredictionScaleFromStrength(100)) < 1e-12);
	}

	void TestLagAtSpeed()
	{
		const spacecal::OneEuroParams p = spacecal::ParamsFromStrength(50);
		spacecal::PoseFilter f;
		const double v = 1.0;
		const double dt = 0.004;
		double last = 0.0;
		for (int i = 0; i < 500; ++i) {
			last = v * dt * i;
			const double pos[3] = {last, 0.0, 0.0};
			spacecal::Step(f, p, pos, dt);
		}
		CHECK(std::fabs(last - f.pos[0]) < 0.015);
	}

	constexpr double kDegrees = spacecal::kPi / 180.0;
	const double kStill[3] = {0.0, 0.0, 0.0};

	void YawQuat(double angle, double out[4])
	{
		out[0] = std::cos(angle / 2.0);
		out[1] = 0.0;
		out[2] = std::sin(angle / 2.0);
		out[3] = 0.0;
	}

	double YawOf(const double q[4])
	{
		return 2.0 * std::atan2(q[2], q[0]);
	}

	void TestRotationConverges()
	{
		spacecal::RotationFilter f;
		const spacecal::OneEuroParams p = spacecal::ParamsFromStrength(100);
		double start[4], target[4];
		YawQuat(0.0, start);
		YawQuat(20.0 * kDegrees, target);
		const double spin[3] = {0.0, 0.3, 0.0};
		CHECK(spacecal::StepRotation(f, p, start, kStill, 0.004, false));
		for (int i = 0; i < 5000; ++i) {
			CHECK(spacecal::StepRotation(f, p, target, spin, 0.004, false));
		}
		CHECK(std::fabs(YawOf(f.rot) - 20.0 * kDegrees) < 1e-6);
		CHECK(std::fabs(f.angVel[1] - 0.3) < 1e-6);
	}

	void TestRotationCutsHandTremor()
	{
		const spacecal::OneEuroParams p = spacecal::ParamsFromStrength(100);
		spacecal::RotationFilter f;
		const double dt = 0.004;
		double rawSquared = 0.0, outSquared = 0.0, spinSquared = 0.0, spinOutSquared = 0.0;
		for (int i = 0; i < 1500; ++i) {
			const double phase = 2.0 * spacecal::kPi * 6.0 * dt * i;
			const double angle = kDegrees * std::sin(phase);
			const double spin[3] = {0.0, kDegrees * 2.0 * spacecal::kPi * 6.0 * std::cos(phase), 0.0};
			double q[4];
			YawQuat(angle, q);
			CHECK(spacecal::StepRotation(f, p, q, spin, dt, false));
			if (i < 250) continue;
			rawSquared += angle * angle;
			outSquared += YawOf(f.rot) * YawOf(f.rot);
			spinSquared += spin[1] * spin[1];
			spinOutSquared += f.angVel[1] * f.angVel[1];
		}
		const double kept = std::sqrt(outSquared / rawSquared);
		const double spinKept = std::sqrt(spinOutSquared / spinSquared);
		CHECK(kept < 0.4);
		CHECK(spinKept < 0.4);
		std::printf("  1 deg 6 Hz tremor at strength 100: %.0f%% of the rotation and %.0f%% of the spin gets through\n", kept * 100.0,
		            spinKept * 100.0);
	}

	void TestRotationFollowsAFastTurn()
	{
		const spacecal::OneEuroParams p = spacecal::ParamsFromStrength(100);
		spacecal::RotationFilter f;
		const double dt = 0.004;
		const double spin[3] = {0.0, 5.0, 0.0};
		double last = 0.0;
		for (int i = 0; i < 75; ++i) {
			last = 5.0 * dt * i;
			double q[4];
			YawQuat(last, q);
			CHECK(spacecal::StepRotation(f, p, q, spin, dt, false));
		}
		const double behind = last - YawOf(f.rot);
		CHECK(behind > 0.0 && behind < 0.12);
		CHECK(std::fabs(f.angVel[1] - 5.0) < 1e-9);
		std::printf("  5 rad/s turn at strength 100 trails by %.2f deg\n", behind / kDegrees);
	}

	void TestRotationReseedsAndRejectsBadInput()
	{
		const spacecal::OneEuroParams p = spacecal::ParamsFromStrength(100);
		spacecal::RotationFilter f;
		double a[4], b[4];
		YawQuat(0.1, a);
		YawQuat(0.9, b);
		CHECK(spacecal::StepRotation(f, p, a, kStill, 0.004, false));
		CHECK(spacecal::StepRotation(f, p, b, kStill, 0.004, true));
		CHECK(std::fabs(YawOf(f.rot) - 0.9) < 1e-12);

		const double zero[4] = {0.0, 0.0, 0.0, 0.0};
		CHECK(!spacecal::StepRotation(f, p, zero, kStill, 0.004, false));
		CHECK(!f.initialized);
		CHECK(spacecal::StepRotation(f, p, a, kStill, 0.004, false));
		CHECK(std::fabs(YawOf(f.rot) - 0.1) < 1e-12);

		const double badSpin[3] = {std::nan(""), 0.0, 0.0};
		CHECK(!spacecal::StepRotation(f, p, a, badSpin, 0.004, false));
		const double badRot[4] = {std::nan(""), 0.0, 0.0, 0.0};
		CHECK(!spacecal::StepRotation(f, p, badRot, kStill, 0.004, false));

		const double scaled[4] = {a[0] * 3.0, a[1] * 3.0, a[2] * 3.0, a[3] * 3.0};
		CHECK(spacecal::StepRotation(f, p, scaled, kStill, 0.004, false));
		CHECK(std::fabs(spacecal::Dot4(f.rot, f.rot) - 1.0) < 1e-12);
	}

	void TestRotationIgnoresQuaternionSign()
	{
		const spacecal::OneEuroParams p = spacecal::ParamsFromStrength(100);
		spacecal::RotationFilter f;
		double q[4];
		YawQuat(0.7, q);
		const double flipped[4] = {-q[0], -q[1], -q[2], -q[3]};
		for (int i = 0; i < 200; ++i) {
			CHECK(spacecal::StepRotation(f, p, i % 2 == 0 ? q : flipped, kStill, 0.004, false));
			CHECK(std::fabs(std::fabs(spacecal::Dot4(f.rot, q)) - 1.0) < 1e-12);
		}
		CHECK(f.speedHat < 1e-6);
	}

}

int main()
{
	TestConverges();
	TestBetaTracksFaster();
	TestReseeds();
	TestStrengthMapping();
	TestPredictionScale();
	TestLagAtSpeed();
	TestRotationConverges();
	TestRotationCutsHandTremor();
	TestRotationFollowsAFastTurn();
	TestRotationReseedsAndRejectsBadInput();
	TestRotationIgnoresQuaternionSign();
	if (failures == 0) {
		std::printf("all filter tests passed\n");
	}
	return failures;
}
