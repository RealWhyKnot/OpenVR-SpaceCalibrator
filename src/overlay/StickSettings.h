#pragma once

#include "Protocol.h"
#include "StickSmoothingMath.h"

#include <cmath>
#include <cstdint>

namespace spacecal::stick_settings {

	constexpr int kPercentStep = 5;
	constexpr int kDefaultStrengthPercent = 50;
	constexpr int kDefaultStartPercent = 25;
	constexpr uint16_t kDefaultStopMs = 1500;
	constexpr int kMinLockedPercent = 25;
	constexpr int kDefaultLockedPercent = 50;

	inline int ClampPercent(int percent)
	{
		return percent > 0 ? (percent < 100 ? percent : 100) : 0;
	}

	inline int SnapPercent(int percent)
	{
		return ClampPercent((int)std::lround(percent / (double)kPercentStep) * kPercentStep);
	}

	inline uint8_t ClampLockedPercent(int percent)
	{
		return (uint8_t)(percent > kMinLockedPercent ? (percent < 100 ? percent : 100) : kMinLockedPercent);
	}

	inline uint16_t PushMsForStrength(int percent)
	{
		return (uint16_t)std::lround(spacecal::stick::kMaxPushSeconds * 10.0 * ClampPercent(percent));
	}

	inline int StrengthForPushMs(uint16_t pushMs)
	{
		return ClampPercent((int)std::lround(pushMs / (spacecal::stick::kMaxPushSeconds * 10.0)));
	}

	inline protocol::StickRampConfig DefaultStick()
	{
		protocol::StickRampConfig stick{};
		stick.pushMs = PushMsForStrength(kDefaultStrengthPercent);
		stick.releaseMs = kDefaultStopMs;
		stick.strength = (uint8_t)(100 - kDefaultStartPercent);
		stick.lockedPct = (uint8_t)kDefaultLockedPercent;
		return stick;
	}

	inline bool IsEmpty(const protocol::StickRampConfig& stick)
	{
		return stick.pushMs == 0 && stick.releaseMs == 0;
	}

	inline void PrepareToTurnOn(protocol::StickRampConfig& stick, bool locked)
	{
		if (!locked && IsEmpty(stick)) stick = DefaultStick();
	}

	inline protocol::StickSmoothingConfig ForDriver(const protocol::StickSmoothingConfig& config,
	                                                const bool on[spacecal::stick::kHandCount],
	                                                const bool locked[spacecal::stick::kHandCount])
	{
		protocol::StickSmoothingConfig sent = config;
		for (int hand = 0; hand < spacecal::stick::kHandCount; ++hand) {
			if (!on[hand]) {
				sent.sticks[hand].pushMs = 0;
				sent.sticks[hand].releaseMs = 0;
			}
			sent.sticks[hand].lockedPct = on[hand] && locked[hand] ? ClampLockedPercent(config.sticks[hand].lockedPct) : 0;
		}
		return sent;
	}

}
