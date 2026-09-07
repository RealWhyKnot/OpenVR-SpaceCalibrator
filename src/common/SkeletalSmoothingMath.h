#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

#include <openvr_driver.h>

namespace spacecal::skeletal {

	constexpr uint32_t kFingerBoneCount = 31;
	constexpr int kFingersPerHand = 5;
	constexpr int kMotionRangeCount = 2;
	constexpr float kMaxSmoothStrength = 0.95f;

	inline int MotionRangeIndex(int motionRange)
	{
		return motionRange == static_cast<int>(vr::VRSkeletalMotionRange_WithoutController) ? 1 : 0;
	}

	inline double ComputeRateHz(uint64_t count, double elapsedSec)
	{
		if (elapsedSec <= 0.0) return 0.0;
		return static_cast<double>(count) / elapsedSec;
	}

	inline float Lerpf(float a, float b, float t)
	{
		return a + (b - a) * t;
	}

	inline vr::HmdQuaternionf_t SlerpQuat(const vr::HmdQuaternionf_t& a, vr::HmdQuaternionf_t b, float t)
	{
		float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
		if (dot < 0.0f) {
			b.x = -b.x;
			b.y = -b.y;
			b.z = -b.z;
			b.w = -b.w;
			dot = -dot;
		}

		if (dot > 0.9995f) {
			vr::HmdQuaternionf_t r{Lerpf(a.w, b.w, t), Lerpf(a.x, b.x, t), Lerpf(a.y, b.y, t), Lerpf(a.z, b.z, t)};
			float len = std::sqrt(r.w * r.w + r.x * r.x + r.y * r.y + r.z * r.z);
			if (len > 0.0f) {
				float inv = 1.0f / len;
				r.w *= inv;
				r.x *= inv;
				r.y *= inv;
				r.z *= inv;
			}
			return r;
		}

		float theta_0 = std::acos(dot);
		float sin_theta_0 = std::sin(theta_0);
		float theta = theta_0 * t;
		float sin_theta = std::sin(theta);
		float s1 = std::cos(theta) - dot * sin_theta / sin_theta_0;
		float s2 = sin_theta / sin_theta_0;
		return vr::HmdQuaternionf_t{s1 * a.w + s2 * b.w, s1 * a.x + s2 * b.x, s1 * a.y + s2 * b.y, s1 * a.z + s2 * b.z};
	}

	inline int FingerIndexForBone(uint32_t bone)
	{
		if (bone >= 2 && bone <= 5) return 0;
		if (bone >= 6 && bone <= 10) return 1;
		if (bone >= 11 && bone <= 15) return 2;
		if (bone >= 16 && bone <= 20) return 3;
		if (bone >= 21 && bone <= 25) return 4;
		return -1;
	}

	inline float SmoothnessToAlpha(uint8_t smoothness)
	{
		float s = static_cast<float>(smoothness);
		if (s > 100.0f) s = 100.0f;
		return 1.0f - (s / 100.0f) * kMaxSmoothStrength;
	}

	struct FingerFrameState
	{
		bool initialized = false;
		bool reseed_pending[kFingersPerHand] = {};
		vr::VRBoneTransform_t previous[kFingerBoneCount] = {};
	};

	struct FingerFrameResult
	{
		bool seeded = false;
		bool reseeded = false;
		bool appliedSmoothing = false;
		float maxPosDelta = 0.0f;
		int maxPosDeltaBone = -1;
		float minQuatDot = 1.0f;
		int minQuatDotBone = -1;
	};

	inline FingerFrameResult SmoothFingerFrame(FingerFrameState& state, const vr::VRBoneTransform_t* input, uint32_t count, int handBase,
	                                           uint16_t fingerMask, const float alphaPerFinger[kFingersPerHand],
	                                           vr::VRBoneTransform_t* output)
	{
		FingerFrameResult result{};
		if (!input || !output || !alphaPerFinger || count != kFingerBoneCount) {
			return result;
		}

		if (!state.initialized) {
			std::memcpy(state.previous, input, sizeof(state.previous));
			std::memcpy(output, input, sizeof(state.previous));
			state.initialized = true;
			result.seeded = true;
			return result;
		}

		bool reseedConsumed[kFingersPerHand] = {};

		for (uint32_t i = 0; i < kFingerBoneCount; ++i) {
			const int finger = FingerIndexForBone(i);
			const bool inMask = finger >= 0 && (((fingerMask >> (handBase + finger)) & 1u) != 0);
			if (!inMask) {
				output[i] = input[i];
				state.previous[i] = input[i];
				continue;
			}

			if (state.reseed_pending[finger]) {
				output[i] = input[i];
				state.previous[i] = input[i];
				reseedConsumed[finger] = true;
				result.reseeded = true;
				continue;
			}

			const float alpha = alphaPerFinger[finger];
			if (alpha >= 1.0f) {
				output[i] = input[i];
				state.previous[i] = input[i];
				continue;
			}

			const auto& in = input[i];
			const auto& prev = state.previous[i];
			vr::VRBoneTransform_t out{};
			out.position.v[0] = Lerpf(prev.position.v[0], in.position.v[0], alpha);
			out.position.v[1] = Lerpf(prev.position.v[1], in.position.v[1], alpha);
			out.position.v[2] = Lerpf(prev.position.v[2], in.position.v[2], alpha);
			out.position.v[3] = in.position.v[3];
			out.orientation = SlerpQuat(prev.orientation, in.orientation, alpha);

			const float dx = out.position.v[0] - prev.position.v[0];
			const float dy = out.position.v[1] - prev.position.v[1];
			const float dz = out.position.v[2] - prev.position.v[2];
			const float posDelta = std::sqrt(dx * dx + dy * dy + dz * dz);
			if (posDelta > result.maxPosDelta) {
				result.maxPosDelta = posDelta;
				result.maxPosDeltaBone = static_cast<int>(i);
			}

			const float qd = out.orientation.w * prev.orientation.w + out.orientation.x * prev.orientation.x +
			                 out.orientation.y * prev.orientation.y + out.orientation.z * prev.orientation.z;
			const float absQd = qd < 0.0f ? -qd : qd;
			if (absQd < result.minQuatDot) {
				result.minQuatDot = absQd;
				result.minQuatDotBone = static_cast<int>(i);
			}

			output[i] = out;
			state.previous[i] = out;
			result.appliedSmoothing = true;
		}

		for (int f = 0; f < kFingersPerHand; ++f) {
			if (reseedConsumed[f]) state.reseed_pending[f] = false;
		}
		return result;
	}

}
