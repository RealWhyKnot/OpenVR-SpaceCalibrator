#pragma once

#include <Eigen/Dense>

#include <array>
#include <cmath>
#include <cstddef>
#include <string_view>
#include <vector>

namespace spacecal::autodetect {

	inline constexpr double kDurationSec = 5.0;
	inline constexpr int kMinimumScore = 40;
	inline constexpr double kMinVelocityThreshold = 0.1;
	inline constexpr double kMaxVelocityDiff = 0.15;
	inline constexpr std::size_t kMaxDevices = 64;

	struct DeviceMotionInput
	{
		int id = -1;
		bool valid = false;
		Eigen::Vector3d position = Eigen::Vector3d::Zero();
		std::string_view trackingSystem;
	};

	enum class Phase
	{
		Inactive,
		InProgress,
		Succeeded,
		Failed
	};

	struct TickResult
	{
		Phase phase = Phase::Inactive;
		double fractionComplete = 0.0;
		int score = 0;
		int refId = -1;
		int targetId = -1;
	};

	struct State
	{
		bool active = false;
		double startTime = 0.0;
		std::array<double, kMaxDevices> speeds{};
		std::array<Eigen::Vector3d, kMaxDevices> lastPos{};
		std::array<bool, kMaxDevices> lastValid{};
		int candidateA = -1;
		int candidateB = -1;
		int score = 0;
	};

	inline void Begin(State& state, double time)
	{
		state = State{};
		state.active = true;
		state.startTime = time;
	}

	inline void Cancel(State& state)
	{
		state = State{};
	}

	inline TickResult Tick(State& state, const std::vector<DeviceMotionInput>& inputs, double time, double dt,
	                       std::string_view hmdTrackingSystem)
	{
		TickResult result;
		if (!state.active) return result;
		result.phase = Phase::InProgress;
		result.fractionComplete = kDurationSec > 0.0 ? (time - state.startTime) / kDurationSec : 1.0;

		std::array<bool, kMaxDevices> seen{};
		std::array<std::string_view, kMaxDevices> systems{};
		if (dt > 0.0) {
			const double decay = std::exp(-dt / kDurationSec);
			for (std::size_t i = 0; i < kMaxDevices; ++i) {
				state.speeds[i] *= decay;
			}
			for (const DeviceMotionInput& input : inputs) {
				if (input.id < 0 || static_cast<std::size_t>(input.id) >= kMaxDevices) continue;
				const auto idx = static_cast<std::size_t>(input.id);
				seen[idx] = input.valid;
				systems[idx] = input.trackingSystem;
				if (input.valid && state.lastValid[idx]) {
					state.speeds[idx] += (input.position - state.lastPos[idx]).squaredNorm() / dt;
				}
				state.lastPos[idx] = input.position;
				state.lastValid[idx] = input.valid;
			}
			for (std::size_t i = 0; i < kMaxDevices; ++i) {
				if (!seen[i]) state.lastValid[i] = false;
			}
		}

		int bestA = -1;
		int bestB = -1;
		double bestDiff = kMaxVelocityDiff;
		for (std::size_t i = 0; i < kMaxDevices; ++i) {
			if (!seen[i] || state.speeds[i] < kMinVelocityThreshold) continue;
			for (std::size_t j = i + 1; j < kMaxDevices; ++j) {
				if (!seen[j] || state.speeds[j] < kMinVelocityThreshold) continue;
				if (systems[i] == systems[j]) continue;
				const double diff = std::abs(state.speeds[i] - state.speeds[j]);
				if (diff < bestDiff) {
					bestDiff = diff;
					bestA = static_cast<int>(i);
					bestB = static_cast<int>(j);
				}
			}
		}

		if (bestA >= 0) {
			if (bestA == state.candidateA && bestB == state.candidateB) {
				++state.score;
			}
			else if (state.score > 0) {
				--state.score;
			}
			else {
				state.candidateA = bestA;
				state.candidateB = bestB;
				state.score = 0;
			}
		}
		else if (state.score > 0) {
			--state.score;
		}

		result.score = state.score;
		if (time - state.startTime < kDurationSec) return result;

		const bool havePair = state.candidateA >= 0 && state.candidateB >= 0;
		if (!havePair || state.score <= kMinimumScore) {
			result.phase = Phase::Failed;
			state.active = false;
			return result;
		}

		int refId = state.candidateA;
		int targetId = state.candidateB;
		if (!hmdTrackingSystem.empty() && systems[static_cast<std::size_t>(state.candidateB)] == hmdTrackingSystem &&
		    systems[static_cast<std::size_t>(state.candidateA)] != hmdTrackingSystem) {
			refId = state.candidateB;
			targetId = state.candidateA;
		}
		result.phase = Phase::Succeeded;
		result.refId = refId;
		result.targetId = targetId;
		result.fractionComplete = 1.0;
		state.active = false;
		return result;
	}

}
