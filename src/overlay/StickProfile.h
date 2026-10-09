#pragma once

#include "Protocol.h"
#include "StickSmoothingMath.h"

#include <picojson.h>

#include <cmath>
#include <cstdint>

namespace spacecal::stick_profile {

	inline uint8_t ClampPercent(double value)
	{
		return (uint8_t)(value > 0.0 ? (value < 100.0 ? value : 100.0) : 0.0);
	}

	inline uint16_t ClampRampMs(double ms)
	{
		return (uint16_t)std::lround(spacecal::stick::ClampRampSeconds(ms / 1000.0) * 1000.0);
	}

	inline void Load(protocol::StickRampConfig& stick, picojson::value& value)
	{
		if (value.is<double>()) {
			const uint8_t legacy = ClampPercent(value.get<double>());
			stick.pushMs = spacecal::stick::LegacyPushMs(legacy);
			stick.releaseMs = spacecal::stick::LegacyReleaseMs(legacy);
			stick.strength = 100;
			return;
		}
		if (!value.is<picojson::object>()) {
			return;
		}
		auto& ramp = value.get<picojson::object>();
		if (ramp["timer_ms"].is<double>()) {
			stick.pushMs = ClampRampMs(ramp["timer_ms"].get<double>());
		}
		if (ramp["strength"].is<double>()) {
			stick.strength = ClampPercent(ramp["strength"].get<double>());
		}
		if (ramp["stop_ms"].is<double>()) {
			stick.releaseMs = ClampRampMs(ramp["stop_ms"].get<double>());
		}
	}

	inline picojson::value Save(const protocol::StickRampConfig& stick)
	{
		picojson::object ramp;
		const double timerMs = stick.pushMs;
		ramp["timer_ms"].set<double>(timerMs);
		const double strength = stick.strength;
		ramp["strength"].set<double>(strength);
		const double stopMs = stick.releaseMs;
		ramp["stop_ms"].set<double>(stopMs);
		return picojson::value(ramp);
	}

}
