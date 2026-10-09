#pragma once

#include <cmath>
#include <cstdint>

namespace spacecal::stick {

	constexpr int kHandCount = 2;
	constexpr double kMaxRampSeconds = 6.0;
	constexpr double kLegacyFullPushSeconds = 4.0;
	constexpr double kLegacyFullReleaseSeconds = 1.5;
	constexpr double kTimeConstantsTo95Percent = 4.7439;
	constexpr double kBrakeToAccelRatio = 4.0;
	constexpr double kReverseHandoff = 0.02;
	constexpr double kSettleEpsilon = 1e-4;
	constexpr double kPumpAfterIdleSeconds = 0.02;

	struct AxisFilter
	{
		double stage1 = 0.0;
		double stage2 = 0.0;
		double target = 0.0;
		double velocity = 0.0;
	};

	struct Ramp
	{
		double pushSeconds = 0.0;
		double releaseSeconds = 0.0;
		double heldBack = 1.0;
	};

	inline double ClampRampSeconds(double seconds)
	{
		return seconds > 0.0 ? (seconds < kMaxRampSeconds ? seconds : kMaxRampSeconds) : 0.0;
	}

	inline Ramp RampFromSettings(uint16_t pushMs, uint16_t releaseMs, uint8_t strength)
	{
		return {ClampRampSeconds(pushMs / 1000.0), ClampRampSeconds(releaseMs / 1000.0), strength > 100 ? 1.0 : strength / 100.0};
	}

	inline uint16_t LegacyPushMs(uint8_t strength)
	{
		return (uint16_t)std::lround(kLegacyFullPushSeconds * 1000.0 * (strength > 100 ? 100 : strength) / 100.0);
	}

	inline uint16_t LegacyReleaseMs(uint8_t strength)
	{
		return (uint16_t)std::lround(kLegacyFullReleaseSeconds * 1000.0 * (strength > 100 ? 100 : strength) / 100.0);
	}

	inline bool PushIsInstant(const Ramp& ramp)
	{
		return !(ramp.pushSeconds > 0.0) || !(ramp.heldBack > 0.0);
	}

	inline bool ReleaseIsInstant(const Ramp& ramp)
	{
		return !(ramp.releaseSeconds > 0.0);
	}

	inline bool IsOff(const Ramp& ramp)
	{
		return PushIsInstant(ramp) && ReleaseIsInstant(ramp);
	}

	inline bool IsSettled(const AxisFilter& filter)
	{
		return std::fabs(filter.stage1 - filter.target) < kSettleEpsilon && std::fabs(filter.stage2 - filter.target) < kSettleEpsilon &&
		       std::fabs(filter.velocity) < kSettleEpsilon;
	}

	inline void Snap(AxisFilter& filter)
	{
		filter.stage1 = filter.target;
		filter.stage2 = filter.target;
		filter.velocity = 0.0;
	}

	inline bool IsPushing(const AxisFilter& filter)
	{
		if (filter.target == 0.0) return false;
		const double along = filter.target > 0.0 ? filter.stage2 : -filter.stage2;
		return along >= -kReverseHandoff && along < std::fabs(filter.target);
	}

	inline void LiftToPushFloor(AxisFilter& filter, const Ramp& ramp)
	{
		const double dir = filter.target > 0.0 ? 1.0 : -1.0;
		const double r = std::fabs(filter.target);
		const double floor = PushIsInstant(ramp) ? r : (1.0 - ramp.heldBack) * r;
		if (!(floor > 0.0) || dir * filter.stage2 >= floor) return;
		filter.stage2 = dir * floor;
		filter.stage1 = filter.stage2;
		if (floor >= r) filter.velocity = 0.0;
	}

	inline void DropToReleaseGoal(AxisFilter& filter)
	{
		const bool crossing = filter.target != 0.0 && filter.stage2 != 0.0 && (filter.target > 0.0) != (filter.stage2 > 0.0);
		filter.stage2 = crossing ? 0.0 : filter.target;
		filter.stage1 = filter.stage2;
		filter.velocity = 0.0;
	}

	inline void ApplyInstantSteps(AxisFilter& filter, const Ramp& ramp)
	{
		if (!IsPushing(filter) && ReleaseIsInstant(ramp)) DropToReleaseGoal(filter);
		if (IsPushing(filter)) LiftToPushFloor(filter, ramp);
	}

	inline void StepPush(AxisFilter& filter, double dt, const Ramp& ramp)
	{
		const double dir = filter.target > 0.0 ? 1.0 : -1.0;
		const double r = std::fabs(filter.target);
		double y = dir * filter.stage2;
		double v = dir * filter.velocity;
		if (v < 0.0) v = 0.0;
		const double a = 2.0 * (1.0 + 1.0 / kBrakeToAccelRatio) * ramp.heldBack * r / (ramp.pushSeconds * ramp.pushSeconds);
		const double b = kBrakeToAccelRatio * a;
		double remaining = dt;

		const double e0 = r - y;
		if (v * v < 2.0 * b * e0) {
			const double disc = v * v - a * (v * v - 2.0 * b * e0) / (a + b);
			const double toBrake = (std::sqrt(disc) - v) / a;
			const double t = remaining < toBrake ? remaining : toBrake;
			y += v * t + 0.5 * a * t * t;
			v += a * t;
			remaining -= t;
		}

		if (remaining > 0.0) {
			const double e = r - y;
			if (e <= 0.0 || v <= 0.0 || remaining >= 2.0 * e / v) {
				y = r;
				v = 0.0;
			}
			else {
				const double d = v * v / (2.0 * e);
				y += v * remaining - 0.5 * d * remaining * remaining;
				v -= d * remaining;
			}
		}

		if (y > r) y = r;
		filter.stage2 = dir * y;
		filter.stage1 = filter.stage2;
		filter.velocity = dir * v;
	}

	inline void StepRelease(AxisFilter& filter, double dt, double releaseSeconds)
	{
		filter.velocity = 0.0;
		const bool crossing = filter.target != 0.0 && filter.stage2 != 0.0 && (filter.target > 0.0) != (filter.stage2 > 0.0);
		const double goal = crossing ? 0.0 : filter.target;
		const double x = dt * kTimeConstantsTo95Percent / releaseSeconds;
		const double decay = std::exp(-x);
		const double e1 = filter.stage1 - goal;
		const double e2 = filter.stage2 - goal;
		filter.stage2 = goal + (e2 + e1 * x) * decay;
		filter.stage1 = goal + e1 * decay;
	}

	inline double Advance(AxisFilter& filter, double dt, const Ramp& ramp)
	{
		if (IsOff(ramp) || !std::isfinite(dt)) {
			Snap(filter);
			return filter.stage2;
		}
		ApplyInstantSteps(filter, ramp);
		if (dt > 0.0) {
			if (IsPushing(filter))
				StepPush(filter, dt, ramp);
			else if (!ReleaseIsInstant(ramp))
				StepRelease(filter, dt, ramp.releaseSeconds);
		}
		if (IsSettled(filter)) Snap(filter);
		return filter.stage2;
	}

	inline double OnDriverSample(AxisFilter& filter, double value, double dtSinceLastStep, const Ramp& ramp)
	{
		Advance(filter, dtSinceLastStep, ramp);
		filter.target = value;
		if (IsOff(ramp))
			Snap(filter);
		else
			ApplyInstantSteps(filter, ramp);
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
