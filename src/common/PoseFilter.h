#pragma once

#include <cmath>
#include <cstdint>

namespace spacecal {

	inline constexpr double kPi = 3.14159265358979323846;
	inline constexpr double kResetGapSeconds = 0.25;
	inline constexpr double kPositionJumpM = 0.50;

	struct OneEuroParams
	{
		double minCutoffHz = 1.0;
		double beta = 0.05;
		double dCutoffHz = 1.0;
	};

	inline double Clamp(double v, double lo, double hi)
	{
		return v < lo ? lo : (v > hi ? hi : v);
	}

	inline double AlphaFromCutoffHz(double cutoffHz, double dtSeconds)
	{
		cutoffHz = Clamp(cutoffHz, 0.001, 120.0);
		dtSeconds = Clamp(dtSeconds, 0.001, 0.050);
		const double tau = 1.0 / (2.0 * kPi * cutoffHz);
		return dtSeconds / (dtSeconds + tau);
	}

	inline OneEuroParams ParamsFromStrength(uint8_t strength)
	{
		const double s01 = Clamp(strength / 100.0, 0.0, 1.0);
		const double inv = 1.0 - s01;
		OneEuroParams p;
		p.minCutoffHz = 0.45 + 16.0 * std::pow(inv, 1.8);
		p.beta = 6.0 + 18.0 * s01;
		p.dCutoffHz = 4.0;
		return p;
	}

	inline double PredictionScaleFromStrength(uint8_t strength)
	{
		const double inv = 1.0 - Clamp(strength / 100.0, 0.0, 1.0);
		return inv * inv;
	}

	inline bool IsFinite3(const double v[3])
	{
		return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
	}

	inline double Distance3(const double a[3], const double b[3])
	{
		const double dx = a[0] - b[0];
		const double dy = a[1] - b[1];
		const double dz = a[2] - b[2];
		return std::sqrt(dx * dx + dy * dy + dz * dz);
	}

	struct PoseFilter
	{
		bool initialized = false;
		double prevRawPos[3] = {0.0, 0.0, 0.0};
		double pos[3] = {0.0, 0.0, 0.0};
		double dxHat[3] = {0.0, 0.0, 0.0};
	};

	inline void Seed(PoseFilter& s, const double rawPos[3])
	{
		for (int i = 0; i < 3; ++i) {
			s.prevRawPos[i] = s.pos[i] = rawPos[i];
			s.dxHat[i] = 0.0;
		}
		s.initialized = true;
	}

	enum class StepResult
	{
		Ok,
		Seeded,
		Gap,
		Jump,
		Invalid
	};

	inline const char* StepResultName(StepResult r)
	{
		switch (r) {
			case StepResult::Ok: return "ok";
			case StepResult::Seeded: return "seeded";
			case StepResult::Gap: return "gap";
			case StepResult::Jump: return "jump";
			default: return "invalid";
		}
	}

	inline StepResult Step(PoseFilter& s, const OneEuroParams& p, const double rawPos[3], double dtSeconds)
	{
		if (!IsFinite3(rawPos)) {
			s.initialized = false;
			return StepResult::Invalid;
		}
		if (!s.initialized) {
			Seed(s, rawPos);
			return StepResult::Seeded;
		}
		if (!(dtSeconds > 0.0) || dtSeconds > kResetGapSeconds) {
			Seed(s, rawPos);
			return StepResult::Gap;
		}
		if (Distance3(rawPos, s.pos) > kPositionJumpM) {
			Seed(s, rawPos);
			return StepResult::Jump;
		}

		const double dt = Clamp(dtSeconds, 0.001, 0.050);
		const double ad = AlphaFromCutoffHz(p.dCutoffHz, dt);
		for (int i = 0; i < 3; ++i) {
			const double dx = (rawPos[i] - s.prevRawPos[i]) / dt;
			s.dxHat[i] += ad * (dx - s.dxHat[i]);
			const double a = AlphaFromCutoffHz(p.minCutoffHz + p.beta * std::fabs(s.dxHat[i]), dt);
			s.pos[i] += a * (rawPos[i] - s.pos[i]);
			s.prevRawPos[i] = rawPos[i];
		}
		return StepResult::Ok;
	}

}
