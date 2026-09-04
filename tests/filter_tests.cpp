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

}

int main()
{
	TestConverges();
	TestBetaTracksFaster();
	TestReseeds();
	TestStrengthMapping();
	TestPredictionScale();
	TestLagAtSpeed();
	if (failures == 0) {
		std::printf("all filter tests passed\n");
	}
	return failures;
}
