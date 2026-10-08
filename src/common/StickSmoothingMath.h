#pragma once

#include <cmath>
#include <cstdint>

namespace spacecal::stick {

	constexpr int kHandCount = 2;
	constexpr double kFullStrengthRampSeconds = 1.5;
	constexpr double kTimeConstantsTo95Percent = 4.7439;
	constexpr double kSettleEpsilon = 1e-4;
	constexpr double kPumpAfterIdleSeconds = 0.02;

	struct AxisFilter
	{
		double stage1 = 0.0;
		double stage2 = 0.0;
		double target = 0.0;
	};

	inline double RampSecondsFromStrength(uint8_t strength)
	{
		const double s = strength > 100 ? 1.0 : strength / 100.0;
		return kFullStrengthRampSeconds * s;
	}

	inline bool IsSettled(const AxisFilter& filter)
	{
		return std::fabs(filter.stage1 - filter.target) < kSettleEpsilon && std::fabs(filter.stage2 - filter.target) < kSettleEpsilon;
	}

	inline void Snap(AxisFilter& filter)
	{
		filter.stage1 = filter.target;
		filter.stage2 = filter.target;
	}

	inline double Advance(AxisFilter& filter, double dt, double rampSeconds)
	{
		if (rampSeconds <= 0.0 || !std::isfinite(dt)) {
			Snap(filter);
			return filter.stage2;
		}
		if (dt > 0.0) {
			const double x = dt * kTimeConstantsTo95Percent / rampSeconds;
			const double decay = std::exp(-x);
			const double e1 = filter.stage1 - filter.target;
			const double e2 = filter.stage2 - filter.target;
			filter.stage2 = filter.target + (e2 + e1 * x) * decay;
			filter.stage1 = filter.target + e1 * decay;
		}
		if (IsSettled(filter)) Snap(filter);
		return filter.stage2;
	}

	inline double OnDriverSample(AxisFilter& filter, double value, double dtSinceLastStep, double rampSeconds)
	{
		Advance(filter, dtSinceLastStep, rampSeconds);
		filter.target = value;
		if (rampSeconds <= 0.0) Snap(filter);
		return filter.stage2;
	}

	inline bool NeedsPump(const AxisFilter& filter, double secondsSinceDriverUpdate)
	{
		return secondsSinceDriverUpdate >= kPumpAfterIdleSeconds && !IsSettled(filter);
	}

	inline bool IsStickAxisPath(const char* path)
	{
		if (!path) return false;
		static const char* const kPaths[] = {"/input/joystick/x", "/input/joystick/y", "/input/thumbstick/x", "/input/thumbstick/y"};
		for (const char* candidate : kPaths) {
			const char* a = path;
			const char* b = candidate;
			while (*a && *b) {
				const char lower = (*a >= 'A' && *a <= 'Z') ? static_cast<char>(*a - 'A' + 'a') : *a;
				if (lower != *b) break;
				++a;
				++b;
			}
			if (*a == 0 && *b == 0) return true;
		}
		return false;
	}

}
