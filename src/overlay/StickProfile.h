#pragma once

#include "Protocol.h"
#include "StickSettings.h"
#include "StickSmoothingMath.h"

#include <picojson.h>

#include <cmath>
#include <cstdint>

namespace spacecal::stick_profile {

	inline uint8_t ClampPercent(double value)
	{
		return (uint8_t)(value > 0.0 ? (value < 100.0 ? value : 100.0) : 0.0);
	}

	inline uint16_t ClampMs(double ms, double maxSeconds)
	{
		return (uint16_t)std::lround(spacecal::stick::ClampSeconds(ms / 1000.0, maxSeconds) * 1000.0);
	}

	inline void Load(protocol::StickRampConfig& stick, bool& on, bool& locked, picojson::value& value)
	{
		locked = false;
		if (value.is<double>()) {
			const uint8_t legacy = ClampPercent(value.get<double>());
			stick.pushMs = spacecal::stick::LegacyPushMs(legacy);
			stick.releaseMs = spacecal::stick::LegacyReleaseMs(legacy);
			stick.delayMs = 0;
			stick.strength = (uint8_t)(100 - spacecal::stick_settings::kDefaultStartPercent);
			on = true;
		}
		else if (value.is<picojson::object>()) {
			auto& ramp = value.get<picojson::object>();
			bool timed = false;
			if (ramp["timer_ms"].is<double>()) {
				stick.pushMs = ClampMs(ramp["timer_ms"].get<double>(), spacecal::stick::kMaxPushSeconds);
				timed |= stick.pushMs > 0;
			}
			if (ramp["strength"].is<double>()) {
				stick.strength = ClampPercent(ramp["strength"].get<double>());
			}
			if (ramp["delay_ms"].is<double>()) {
				stick.delayMs = ClampMs(ramp["delay_ms"].get<double>(), spacecal::stick::kMaxDelaySeconds);
			}
			if (ramp["stop_ms"].is<double>()) {
				stick.releaseMs = ClampMs(ramp["stop_ms"].get<double>(), spacecal::stick::kMaxReleaseSeconds);
				timed |= stick.releaseMs > 0;
			}
			if (ramp["start_pct"].is<double>()) {
				stick.strength = (uint8_t)(100 - ClampPercent(ramp["start_pct"].get<double>()));
			}
			else if (stick.strength == 100) {
				stick.strength = (uint8_t)(100 - spacecal::stick_settings::kDefaultStartPercent);
			}
			on = ramp["on"].is<bool>() ? ramp["on"].get<bool>() : timed;
			if (ramp["locked_pct"].is<double>()) {
				stick.lockedPct = spacecal::stick_settings::ClampLockedPercent(ClampPercent(ramp["locked_pct"].get<double>()));
			}
			locked = ramp["locked"].is<bool>() && ramp["locked"].get<bool>();
		}
		if (stick.lockedPct < spacecal::stick_settings::kMinLockedPercent || stick.lockedPct > 100) {
			stick.lockedPct = (uint8_t)spacecal::stick_settings::kDefaultLockedPercent;
		}
		if (!locked && spacecal::stick_settings::IsEmpty(stick)) {
			stick = spacecal::stick_settings::DefaultStick();
			on = false;
			locked = false;
		}
	}

	inline picojson::value Save(const protocol::StickRampConfig& stick, bool on, bool locked)
	{
		picojson::object ramp;
		ramp["on"].set<bool>(on);
		ramp["locked"].set<bool>(locked);
		const double lockedPct = spacecal::stick_settings::ClampLockedPercent(stick.lockedPct);
		ramp["locked_pct"].set<double>(lockedPct);
		const double timerMs = stick.pushMs;
		ramp["timer_ms"].set<double>(timerMs);
		const double strength = stick.strength;
		ramp["strength"].set<double>(strength);
		const double startPct = 100 - (stick.strength > 100 ? 100 : stick.strength);
		ramp["start_pct"].set<double>(startPct);
		const double delayMs = stick.delayMs;
		ramp["delay_ms"].set<double>(delayMs);
		const double stopMs = stick.releaseMs;
		ramp["stop_ms"].set<double>(stopMs);
		return picojson::value(ramp);
	}

}
