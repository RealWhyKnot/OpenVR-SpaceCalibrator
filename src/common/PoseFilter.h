#pragma once

#include <cmath>

namespace spacecal {

	inline constexpr double kPi = 3.14159265358979323846;
	inline constexpr double kResetGapSeconds = 0.25;
	inline constexpr double kPositionJumpM = 0.50;
	inline constexpr double kRotationJumpRad = 1.0471975511965976;

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

	inline double QuatDot(const double a[4], const double b[4])
	{
		return a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
	}

	inline bool QuatNormalize(const double q[4], double out[4])
	{
		if (!(std::isfinite(q[0]) && std::isfinite(q[1]) && std::isfinite(q[2]) && std::isfinite(q[3]))) {
			out[0] = 1.0;
			out[1] = out[2] = out[3] = 0.0;
			return false;
		}
		const double n2 = QuatDot(q, q);
		if (!(n2 > 1e-18)) {
			out[0] = 1.0;
			out[1] = out[2] = out[3] = 0.0;
			return false;
		}
		const double inv = 1.0 / std::sqrt(n2);
		for (int i = 0; i < 4; ++i) {
			out[i] = q[i] * inv;
		}
		return true;
	}

	inline void QuatSameHemisphere(const double reference[4], const double q[4], double out[4])
	{
		const double s = QuatDot(reference, q) < 0.0 ? -1.0 : 1.0;
		for (int i = 0; i < 4; ++i) {
			out[i] = q[i] * s;
		}
	}

	inline double QuatAngleRad(const double a[4], const double b[4])
	{
		const double d = Clamp(std::fabs(QuatDot(a, b)), 0.0, 1.0);
		return 2.0 * std::acos(d);
	}

	inline void QuatNlerpShortest(const double from[4], const double to[4], double alpha, double out[4])
	{
		double hemi[4];
		QuatSameHemisphere(from, to, hemi);
		alpha = Clamp(alpha, 0.0, 1.0);
		double blended[4];
		for (int i = 0; i < 4; ++i) {
			blended[i] = from[i] + alpha * (hemi[i] - from[i]);
		}
		if (!QuatNormalize(blended, out)) {
			for (int i = 0; i < 4; ++i) {
				out[i] = to[i];
			}
		}
	}

	inline void QuatMul(const double a[4], const double b[4], double out[4])
	{
		out[0] = a[0] * b[0] - a[1] * b[1] - a[2] * b[2] - a[3] * b[3];
		out[1] = a[0] * b[1] + a[1] * b[0] + a[2] * b[3] - a[3] * b[2];
		out[2] = a[0] * b[2] - a[1] * b[3] + a[2] * b[0] + a[3] * b[1];
		out[3] = a[0] * b[3] + a[1] * b[2] - a[2] * b[1] + a[3] * b[0];
	}

	struct PoseFilter
	{
		bool initialized = false;
		double prevRawPos[3] = {0.0, 0.0, 0.0};
		double prevRawRot[4] = {1.0, 0.0, 0.0, 0.0};
		double pos[3] = {0.0, 0.0, 0.0};
		double rot[4] = {1.0, 0.0, 0.0, 0.0};
		double dxHat[3] = {0.0, 0.0, 0.0};
		double omegaHat = 0.0;
		double vel[3] = {0.0, 0.0, 0.0};
		double angVel[3] = {0.0, 0.0, 0.0};
	};

	inline void Seed(PoseFilter& s, const double rawPos[3], const double rawRot[4])
	{
		for (int i = 0; i < 3; ++i) {
			s.prevRawPos[i] = s.pos[i] = rawPos[i];
			s.dxHat[i] = s.vel[i] = s.angVel[i] = 0.0;
		}
		double n[4];
		QuatNormalize(rawRot, n);
		for (int i = 0; i < 4; ++i) {
			s.prevRawRot[i] = s.rot[i] = n[i];
		}
		s.omegaHat = 0.0;
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

	inline StepResult Step(PoseFilter& s, const OneEuroParams& pp, const OneEuroParams& rp, const double rawPos[3],
	                       const double rawRotIn[4], double dtSeconds)
	{
		double rawRot[4];
		if (!QuatNormalize(rawRotIn, rawRot) || !IsFinite3(rawPos)) {
			s.initialized = false;
			for (int i = 0; i < 3; ++i) {
				s.vel[i] = s.angVel[i] = 0.0;
			}
			return StepResult::Invalid;
		}
		if (!s.initialized) {
			Seed(s, rawPos, rawRot);
			return StepResult::Seeded;
		}
		if (!(dtSeconds > 0.0) || dtSeconds > kResetGapSeconds) {
			Seed(s, rawPos, rawRot);
			return StepResult::Gap;
		}

		const double dt = Clamp(dtSeconds, 0.001, 0.050);
		QuatSameHemisphere(s.prevRawRot, rawRot, rawRot);
		if (Distance3(rawPos, s.pos) > kPositionJumpM || QuatAngleRad(rawRot, s.rot) > kRotationJumpRad) {
			Seed(s, rawPos, rawRot);
			return StepResult::Jump;
		}

		const double ad = AlphaFromCutoffHz(pp.dCutoffHz, dt);
		for (int i = 0; i < 3; ++i) {
			const double dx = (rawPos[i] - s.prevRawPos[i]) / dt;
			s.dxHat[i] += ad * (dx - s.dxHat[i]);
			const double a = AlphaFromCutoffHz(pp.minCutoffHz + pp.beta * std::fabs(s.dxHat[i]), dt);
			const double old = s.pos[i];
			s.pos[i] += a * (rawPos[i] - s.pos[i]);
			s.vel[i] = (s.pos[i] - old) / dt;
			s.prevRawPos[i] = rawPos[i];
		}

		const double omega = QuatAngleRad(rawRot, s.prevRawRot) / dt;
		const double adr = AlphaFromCutoffHz(rp.dCutoffHz, dt);
		s.omegaHat += adr * (omega - s.omegaHat);
		const double ra = AlphaFromCutoffHz(rp.minCutoffHz + rp.beta * s.omegaHat, dt);
		const double oldRot[4] = {s.rot[0], s.rot[1], s.rot[2], s.rot[3]};
		QuatNlerpShortest(oldRot, rawRot, ra, s.rot);
		const double conj[4] = {oldRot[0], -oldRot[1], -oldRot[2], -oldRot[3]};
		double dq[4];
		QuatMul(s.rot, conj, dq);
		QuatNormalize(dq, dq);
		if (dq[0] < 0.0) {
			for (int i = 0; i < 4; ++i) {
				dq[i] = -dq[i];
			}
		}
		const double sinHalf = std::sqrt(dq[1] * dq[1] + dq[2] * dq[2] + dq[3] * dq[3]);
		if (sinHalf > 1e-9) {
			const double k = 2.0 * std::atan2(sinHalf, dq[0]) / (sinHalf * dt);
			s.angVel[0] = dq[1] * k;
			s.angVel[1] = dq[2] * k;
			s.angVel[2] = dq[3] * k;
		}
		else {
			s.angVel[0] = s.angVel[1] = s.angVel[2] = 0.0;
		}
		for (int i = 0; i < 4; ++i) {
			s.prevRawRot[i] = rawRot[i];
		}
		return StepResult::Ok;
	}

}
