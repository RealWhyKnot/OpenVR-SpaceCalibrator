#pragma once

#include "Protocol.h"

#include <cstdint>
#include <cstring>

namespace spacecal::skeletal {

	inline uint64_t PackFingerHeader(const protocol::FingerSmoothingConfig& cfg)
	{
		uint64_t packed = 0;
		uint8_t* b = reinterpret_cast<uint8_t*>(&packed);
		b[0] = cfg.strength;
		b[1] = static_cast<uint8_t>(cfg.fingerMask & 0xFF);
		b[2] = static_cast<uint8_t>((cfg.fingerMask >> 8) & 0xFF);
		b[3] = cfg.perFinger[8];
		b[4] = cfg.perFinger[9];
		return packed;
	}

	inline uint64_t PackFingerLow(const protocol::FingerSmoothingConfig& cfg)
	{
		uint64_t packed = 0;
		std::memcpy(&packed, cfg.perFinger, 8);
		return packed;
	}

	inline protocol::FingerSmoothingConfig UnpackFingerSmoothing(uint64_t header, uint64_t low)
	{
		protocol::FingerSmoothingConfig cfg{};
		const uint8_t* b = reinterpret_cast<const uint8_t*>(&header);
		cfg.strength = b[0];
		cfg.fingerMask = static_cast<uint16_t>(b[1] | (static_cast<uint16_t>(b[2]) << 8));
		std::memcpy(cfg.perFinger, &low, 8);
		cfg.perFinger[8] = b[3];
		cfg.perFinger[9] = b[4];
		return cfg;
	}

	inline bool IsFingerSmoothed(const protocol::FingerSmoothingConfig& cfg, int idx)
	{
		if (((cfg.fingerMask >> idx) & 1u) == 0) return false;
		const uint8_t perFinger = cfg.perFinger[idx];
		const uint8_t effective = perFinger != 0 ? perFinger : cfg.strength;
		return effective != 0;
	}

	inline uint16_t ComputeFingerSmoothingReseedBits(const protocol::FingerSmoothingConfig& prev,
	                                                 const protocol::FingerSmoothingConfig& next)
	{
		uint16_t reseedBits = 0;
		for (int i = 0; i < 10; ++i) {
			const bool wasOn = IsFingerSmoothed(prev, i);
			const bool isOn = IsFingerSmoothed(next, i);
			if (!wasOn && isOn) reseedBits |= static_cast<uint16_t>(1u << i);
		}
		return reseedBits;
	}

}
