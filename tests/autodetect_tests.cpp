#include "DeviceAutoDetect.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <string_view>
#include <vector>

namespace {

	using namespace spacecal::autodetect;

	int failures = 0;

#define CHECK(cond)                                                                                                                        \
	do {                                                                                                                                   \
		if (!(cond)) {                                                                                                                     \
			std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);                                                                     \
			++failures;                                                                                                                    \
		}                                                                                                                                  \
	} while (0)

	constexpr double kDt = 0.05;

	DeviceMotionInput Device(int id, std::string_view system, const Eigen::Vector3d& position, bool valid = true)
	{
		DeviceMotionInput input;
		input.id = id;
		input.valid = valid;
		input.position = position;
		input.trackingSystem = system;
		return input;
	}

	TickResult RunScenario(State& state, double seconds, const std::function<std::vector<DeviceMotionInput>(double t)>& frame)
	{
		TickResult last;
		const int ticks = (int)(seconds / kDt);
		for (int tick = 0; tick <= ticks; ++tick) {
			const double t = tick * kDt;
			last = Tick(state, frame(t), t, kDt, "oculus");
			if (last.phase == Phase::Succeeded || last.phase == Phase::Failed) break;
		}
		return last;
	}

	Eigen::Vector3d Wave(double t, double phase)
	{
		return Eigen::Vector3d(std::sin(t * 6.0 + phase) * 0.4, std::cos(t * 5.0 + phase) * 0.4, std::sin(t * 4.0) * 0.3);
	}

	void TestCorrelatedPairSucceeds()
	{
		State state;
		Begin(state, 0.0);
		const auto result = RunScenario(state, 6.0, [](double t) {
			return std::vector<DeviceMotionInput>{
			    Device(0, "oculus", Eigen::Vector3d(0, 1.6, 0)),
			    Device(1, "oculus", Wave(t, 0.0)),
			    Device(5, "lighthouse", Wave(t, 0.02) + Eigen::Vector3d(10, 0, 0)),
			};
		});
		CHECK(result.phase == Phase::Succeeded);
		CHECK(result.refId == 1);
		CHECK(result.targetId == 5);
	}

	void TestSameSystemPairIgnored()
	{
		State state;
		Begin(state, 0.0);
		const auto result = RunScenario(state, 6.0, [](double t) {
			return std::vector<DeviceMotionInput>{
			    Device(1, "oculus", Wave(t, 0.0)),
			    Device(2, "oculus", Wave(t, 0.02)),
			};
		});
		CHECK(result.phase == Phase::Failed);
	}

	void TestStationaryDevicesFail()
	{
		State state;
		Begin(state, 0.0);
		const auto result = RunScenario(state, 6.0, [](double) {
			return std::vector<DeviceMotionInput>{
			    Device(1, "oculus", Eigen::Vector3d(0, 1, 0)),
			    Device(5, "lighthouse", Eigen::Vector3d(1, 1, 0)),
			};
		});
		CHECK(result.phase == Phase::Failed);
	}

	void TestIndependentMotionFails()
	{
		State state;
		Begin(state, 0.0);
		const auto result = RunScenario(state, 6.0, [](double t) {
			return std::vector<DeviceMotionInput>{
			    Device(1, "oculus", Wave(t, 0.0)),
			    Device(5, "lighthouse", Wave(t * 3.7, 1.9) * 2.5),
			};
		});
		CHECK(result.phase == Phase::Failed);
	}

	void TestHmdSystemBecomesReference()
	{
		State state;
		Begin(state, 0.0);
		TickResult last;
		for (int tick = 0; tick <= 120; ++tick) {
			const double t = tick * kDt;
			const std::vector<DeviceMotionInput> inputs{
			    Device(5, "lighthouse", Wave(t, 0.02)),
			    Device(1, "oculus", Wave(t, 0.0) + Eigen::Vector3d(10, 0, 0)),
			};
			last = Tick(state, inputs, t, kDt, "oculus");
			if (last.phase == Phase::Succeeded || last.phase == Phase::Failed) break;
		}
		CHECK(last.phase == Phase::Succeeded);
		CHECK(last.refId == 1);
		CHECK(last.targetId == 5);
	}

	void TestSpeedsDecayWhenMotionStops()
	{
		State state;
		Begin(state, 0.0);
		int tick = 0;
		for (; tick <= 48; ++tick) {
			const double t = tick * kDt;
			Tick(state, {Device(1, "oculus", Wave(t, 0.0)), Device(5, "lighthouse", Eigen::Vector3d(1, 1, 1))}, t, kDt, "oculus");
		}
		const double peak = state.speeds[1];
		CHECK(peak > kMinVelocityThreshold);
		for (; tick <= 98; ++tick) {
			const double t = tick * kDt;
			Tick(state, {Device(1, "oculus", Wave(2.4, 0.0)), Device(5, "lighthouse", Eigen::Vector3d(1, 1, 1))}, t, kDt, "oculus");
		}
		CHECK(state.speeds[1] < peak * 0.75);
		CHECK(state.speeds[1] > 0.0);
	}

}

int main()
{
	TestCorrelatedPairSucceeds();
	TestSameSystemPairIgnored();
	TestStationaryDevicesFail();
	TestIndependentMotionFails();
	TestHmdSystemBecomesReference();
	TestSpeedsDecayWhenMotionStops();

	if (failures == 0) {
		std::printf("autodetect_tests: all tests passed\n");
		return 0;
	}
	std::printf("autodetect_tests: %d failure(s)\n", failures);
	return 1;
}
