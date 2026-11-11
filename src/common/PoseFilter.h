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

	inline constexpr double kRotationLeverM = 0.10;

	struct RotationFilter
	{
		bool initialized = false;
		double prevRawRot[4] = {1.0, 0.0, 0.0, 0.0};
		double rot[4] = {1.0, 0.0, 0.0, 0.0};
		double speedHat = 0.0;
		double angVel[3] = {0.0, 0.0, 0.0};
	};

	inline double Dot4(const double a[4], const double b[4])
	{
		return a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
	}

	inline bool Normalize4(double q[4])
	{
		const double norm = std::sqrt(Dot4(q, q));
		if (!std::isfinite(norm) || norm < 1e-9) return false;
		for (int i = 0; i < 4; ++i) {
			q[i] /= norm;
		}
		return true;
	}

	inline bool StepRotation(RotationFilter& s, const OneEuroParams& p, const double rawRot[4], const double rawAngVel[3], double dtSeconds,
	                         bool reseed)
	{
		double raw[4] = {rawRot[0], rawRot[1], rawRot[2], rawRot[3]};
		if (!Normalize4(raw) || !IsFinite3(rawAngVel)) {
			s.initialized = false;
			return false;
		}
		if (!s.initialized || reseed) {
			for (int i = 0; i < 4; ++i) {
				s.prevRawRot[i] = s.rot[i] = raw[i];
			}
			for (int i = 0; i < 3; ++i) {
				s.angVel[i] = rawAngVel[i];
			}
			s.speedHat = 0.0;
			s.initialized = true;
			return true;
		}

		const double dt = Clamp(dtSeconds, 0.001, 0.050);
		if (Dot4(raw, s.prevRawRot) < 0.0) {
			for (int i = 0; i < 4; ++i) {
				raw[i] = -raw[i];
			}
		}
		const double turned = 2.0 * std::acos(Clamp(Dot4(raw, s.prevRawRot), -1.0, 1.0));
		s.speedHat += AlphaFromCutoffHz(p.dCutoffHz, dt) * (turned / dt - s.speedHat);
		const double a = AlphaFromCutoffHz(p.minCutoffHz + p.beta * kRotationLeverM * s.speedHat, dt);

		double toRaw = Dot4(s.rot, raw);
		const double sign = toRaw < 0.0 ? -1.0 : 1.0;
		toRaw = Clamp(toRaw * sign, 0.0, 1.0);
		const double gap = std::acos(toRaw);
		double keep = 1.0 - a;
		double take = a;
		if (gap > 1e-6) {
			keep = std::sin((1.0 - a) * gap) / std::sin(gap);
			take = std::sin(a * gap) / std::sin(gap);
		}
		for (int i = 0; i < 4; ++i) {
			s.rot[i] = keep * s.rot[i] + sign * take * raw[i];
			s.prevRawRot[i] = raw[i];
		}
		if (!Normalize4(s.rot)) {
			s.initialized = false;
			return false;
		}
		for (int i = 0; i < 3; ++i) {
			s.angVel[i] += a * (rawAngVel[i] - s.angVel[i]);
		}
		return true;
	}

}
