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

	void QuatAboutY(double rad, double out[4])
	{
		out[0] = std::cos(rad * 0.5);
		out[1] = 0.0;
		out[2] = std::sin(rad * 0.5);
		out[3] = 0.0;
	}

	void TestConverges()
	{
		spacecal::PoseFilter f;
		spacecal::OneEuroParams p;
		const double pos[3] = {1.0, 2.0, 3.0};
		double rot[4];
		QuatAboutY(0.5, rot);
		for (int i = 0; i < 500; ++i) {
			spacecal::Step(f, p, p, pos, rot, 0.01);
		}
		CHECK(std::fabs(f.pos[0] - 1.0) < 1e-6);
		CHECK(std::fabs(f.pos[1] - 2.0) < 1e-6);
		CHECK(std::fabs(f.pos[2] - 3.0) < 1e-6);
		CHECK(std::fabs(spacecal::QuatDot(f.rot, f.rot) - 1.0) < 1e-9);
		CHECK(spacecal::QuatAngleRad(f.rot, rot) < 1e-6);
		CHECK(std::fabs(f.vel[0]) < 1e-6);
	}

	void TestHemisphereFlip()
	{
		spacecal::PoseFilter f;
		spacecal::OneEuroParams p;
		const double pos[3] = {0.0, 0.0, 0.0};
		double rot[4];
		QuatAboutY(0.5, rot);
		spacecal::Step(f, p, p, pos, rot, 0.01);
		spacecal::Step(f, p, p, pos, rot, 0.01);
		const double neg[4] = {-rot[0], -rot[1], -rot[2], -rot[3]};
		CHECK(spacecal::Step(f, p, p, pos, neg, 0.01) == spacecal::StepResult::Ok);
		CHECK(spacecal::QuatAngleRad(f.rot, rot) < 1e-6);
		const double av = std::sqrt(f.angVel[0] * f.angVel[0] + f.angVel[1] * f.angVel[1] + f.angVel[2] * f.angVel[2]);
		CHECK(av < 1e-6);
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
		const double rot[4] = {1.0, 0.0, 0.0, 0.0};
		spacecal::Step(a, slow, slow, origin, rot, 0.01);
		spacecal::Step(b, fast, fast, origin, rot, 0.01);
		const double target[3] = {0.2, 0.0, 0.0};
		for (int i = 0; i < 5; ++i) {
			spacecal::Step(a, slow, slow, target, rot, 0.01);
			spacecal::Step(b, fast, fast, target, rot, 0.01);
		}
		CHECK(b.pos[0] > a.pos[0]);
	}

	void TestVelocityConsistency()
	{
		spacecal::PoseFilter f;
		spacecal::OneEuroParams p;
		const double rot[4] = {1.0, 0.0, 0.0, 0.0};
		const double v = 0.5;
		const double dt = 0.005;
		for (int i = 0; i < 400; ++i) {
			const double pos[3] = {v * dt * i, 0.0, 0.0};
			spacecal::Step(f, p, p, pos, rot, dt);
		}
		CHECK(std::fabs(f.vel[0] - v) < 0.05 * v);
	}

	void TestReseeds()
	{
		spacecal::PoseFilter f;
		spacecal::OneEuroParams p;
		const double rot[4] = {1.0, 0.0, 0.0, 0.0};
		const double a[3] = {0.0, 0.0, 0.0};
		using spacecal::StepResult;
		CHECK(spacecal::Step(f, p, p, a, rot, 0.01) == StepResult::Seeded);
		CHECK(spacecal::Step(f, p, p, a, rot, 0.01) == StepResult::Ok);
		CHECK(spacecal::Step(f, p, p, a, rot, 0.3) == StepResult::Gap);
		CHECK(spacecal::Step(f, p, p, a, rot, 0.01) == StepResult::Ok);
		const double nanPos[3] = {std::nan(""), 0.0, 0.0};
		CHECK(spacecal::Step(f, p, p, nanPos, rot, 0.01) == StepResult::Invalid);
		CHECK(spacecal::Step(f, p, p, a, rot, 0.01) == StepResult::Seeded);
		CHECK(spacecal::Step(f, p, p, a, rot, 0.01) == StepResult::Ok);
		const double jump[3] = {0.6, 0.0, 0.0};
		CHECK(spacecal::Step(f, p, p, jump, rot, 0.01) == StepResult::Jump);
		CHECK(std::fabs(f.pos[0] - 0.6) < 1e-12);
		CHECK(f.vel[0] == 0.0);
	}

	void TestAngularVelocity()
	{
		spacecal::PoseFilter f;
		spacecal::OneEuroParams p;
		const double origin[3] = {0.0, 0.0, 0.0};
		const double rate = 1.0;
		const double dt = 0.005;
		for (int i = 0; i < 400; ++i) {
			double rot[4];
			QuatAboutY(rate * dt * i, rot);
			spacecal::Step(f, p, p, origin, rot, dt);
		}
		CHECK(std::fabs(f.angVel[1] - rate) < 0.1 * rate);
		CHECK(std::fabs(f.angVel[0]) < 0.02);
		CHECK(std::fabs(f.angVel[2]) < 0.02);
	}

}

int main()
{
	TestConverges();
	TestHemisphereFlip();
	TestBetaTracksFaster();
	TestVelocityConsistency();
	TestReseeds();
	TestAngularVelocity();
	if (failures == 0) {
		std::printf("all filter tests passed\n");
	}
	return failures;
}
