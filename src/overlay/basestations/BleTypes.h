#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace spacecal::basestations {

	inline constexpr std::string_view kV2ServiceUuid = "00001523-1212-efde-1523-785feabcd124";
	inline constexpr std::string_view kV2PowerCharUuid = "00001525-1212-efde-1523-785feabcd124";
	inline constexpr std::string_view kV2ChannelCharUuid = "00001524-1212-efde-1523-785feabcd124";
	inline constexpr std::string_view kV2IdentifyCharUuid = "00008421-1212-efde-1523-785feabcd124";

	inline constexpr std::string_view kV1ServiceUuid = "0000cb00-0000-1000-8000-00805f9b34fb";
	inline constexpr std::string_view kV1PowerCharUuid = "0000cb01-0000-1000-8000-00805f9b34fb";

	inline constexpr uint8_t kV1CommandOn = 0x00;
	inline constexpr uint8_t kV1CommandTimedOff = 0x01;
	inline constexpr uint8_t kV1CommandWake = 0x02;
	inline constexpr uint32_t kV1GenericId = 0xFFFFFFFFu;
	inline constexpr uint16_t kV1SleepTimeoutSec = 4;

	enum class StationKind
	{
		Unknown,
		V1,
		V2
	};

	enum class PowerState : uint8_t
	{
		Sleeping = 0x00,
		Standby = 0x02,
		AwakeOldFirmware = 0x03,
		AwakeFromSleep = 0x09,
		AwakeFromStandby = 0x0B,
		Unknown = 0xFF
	};

	inline bool IsAwake(PowerState state)
	{
		return state == PowerState::AwakeOldFirmware || state == PowerState::AwakeFromSleep || state == PowerState::AwakeFromStandby;
	}

	inline PowerState PowerStateFromByte(uint8_t value)
	{
		switch (value) {
			case 0x00: return PowerState::Sleeping;
			case 0x02: return PowerState::Standby;
			case 0x03: return PowerState::AwakeOldFirmware;
			case 0x09: return PowerState::AwakeFromSleep;
			case 0x0B: return PowerState::AwakeFromStandby;
			default: return PowerState::Unknown;
		}
	}

	inline StationKind ClassifyAdvertName(std::string_view name)
	{
		constexpr std::string_view kV2Prefix = "LHB-";
		constexpr std::string_view kV2BootPlaceholder = "LHB-00000000";
		constexpr std::string_view kV1Prefix = "HTC BS ";
		constexpr std::string_view kV1BootPlaceholder = "HTC BS 000000";
		if (name.rfind(kV2Prefix, 0) == 0) {
			return name == kV2BootPlaceholder ? StationKind::Unknown : StationKind::V2;
		}
		if (name.rfind(kV1Prefix, 0) == 0) {
			return name == kV1BootPlaceholder ? StationKind::Unknown : StationKind::V1;
		}
		return StationKind::Unknown;
	}

	struct V2AdvertState
	{
		uint8_t channel = 0;
		PowerState powerState = PowerState::Unknown;
		bool isFaulty = false;
		bool oldFirmware = false;
	};

	inline std::optional<V2AdvertState> DecodeV2ManufacturerData(const std::vector<uint8_t>& data)
	{
		if (data.size() < 7) return std::nullopt;
		V2AdvertState state;
		state.channel = data[2];
		state.powerState = PowerStateFromByte(data[4]);
		state.isFaulty = data[6] == 1;
		state.oldFirmware = state.powerState == PowerState::AwakeOldFirmware;
		return state;
	}

	enum class PowerCommand
	{
		Wake,
		Standby,
		Sleep
	};

	inline std::vector<std::vector<uint8_t>> EncodeV2PowerWrites(PowerCommand command, bool standbySupported)
	{
		switch (command) {
			case PowerCommand::Wake: return {{0x01}};
			case PowerCommand::Standby:
				if (standbySupported) return {{0x02}};
				[[fallthrough]];
			case PowerCommand::Sleep:
			default: return {{0x01}, {0x00}};
		}
	}

	inline std::optional<std::array<uint8_t, 1>> EncodeV2ChannelWrite(int channel)
	{
		if (channel < 1 || channel > 16) return std::nullopt;
		return std::array<uint8_t, 1>{static_cast<uint8_t>(channel)};
	}

	inline std::array<uint8_t, 1> EncodeV2IdentifyWrite()
	{
		return {0x01};
	}

	inline std::array<uint8_t, 20> EncodeV1PowerPacket(uint8_t command, uint16_t timeoutSec, uint32_t id)
	{
		std::array<uint8_t, 20> packet{};
		packet[0] = 0x12;
		packet[1] = command;
		packet[2] = static_cast<uint8_t>(timeoutSec >> 8);
		packet[3] = static_cast<uint8_t>(timeoutSec & 0xFF);
		packet[4] = static_cast<uint8_t>(id & 0xFF);
		packet[5] = static_cast<uint8_t>((id >> 8) & 0xFF);
		packet[6] = static_cast<uint8_t>((id >> 16) & 0xFF);
		packet[7] = static_cast<uint8_t>((id >> 24) & 0xFF);
		return packet;
	}

	struct BleAdvert
	{
		uint64_t address = 0;
		std::string_view name;
		StationKind kind = StationKind::Unknown;
		std::optional<V2AdvertState> v2State;
	};

}
