#pragma once

#include "BleTypes.h"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace spacecal::basestations {

	enum class StatusTone
	{
		Idle,
		Ok,
		Pending,
		Error,
		Info
	};

	struct StatusLabel
	{
		const char* label;
		StatusTone tone;
	};

	struct OpenVrReference
	{
		std::string serial;
		std::string model;
	};

	struct Station
	{
		std::string serial;
		uint64_t address = 0;
		StationKind kind = StationKind::Unknown;
		uint8_t channel = 0;
		PowerState powerState = PowerState::Unknown;
		bool standbySupported = false;
		bool isFaulty = false;
		bool seenByBle = false;
		bool seenBySteamVr = false;
		std::string nickname;
	};

	inline bool V1NameMatchesSerial(std::string_view bleName, std::string_view serial)
	{
		constexpr std::string_view kPrefix = "HTC BS ";
		if (bleName.size() <= kPrefix.size() || serial.size() < 6) return false;
		const std::string_view suffix = bleName.substr(bleName.size() - 6);
		const std::string_view serialTail = serial.substr(serial.size() - 6);
		return suffix == serialTail;
	}

	struct NamedAdvert
	{
		uint64_t address = 0;
		std::string name;
		StationKind kind = StationKind::Unknown;
		std::optional<V2AdvertState> v2State;
	};

	inline std::vector<Station> MergeDiscovery(const std::vector<NamedAdvert>& adverts, const std::vector<OpenVrReference>& openvrRefs)
	{
		std::vector<Station> stations;
		auto findBySerial = [&stations](std::string_view serial) -> Station* {
			for (auto& s : stations) {
				if (s.serial == serial) return &s;
			}
			return nullptr;
		};

		for (const NamedAdvert& advert : adverts) {
			if (advert.kind == StationKind::Unknown) continue;
			Station station;
			station.serial = advert.name;
			station.address = advert.address;
			station.kind = advert.kind;
			station.seenByBle = true;
			if (advert.v2State) {
				station.channel = advert.v2State->channel;
				station.powerState = advert.v2State->powerState;
				station.isFaulty = advert.v2State->isFaulty;
				station.standbySupported = !advert.v2State->oldFirmware;
			}
			stations.push_back(std::move(station));
		}

		for (const OpenVrReference& ref : openvrRefs) {
			Station* match = findBySerial(ref.serial);
			if (!match) {
				for (auto& s : stations) {
					if (s.kind == StationKind::V1 && V1NameMatchesSerial(s.serial, ref.serial)) {
						match = &s;
						break;
					}
				}
			}
			if (match) {
				match->seenBySteamVr = true;
				if (match->kind == StationKind::V1) match->serial = ref.serial;
			}
			else {
				Station station;
				station.serial = ref.serial;
				station.seenBySteamVr = true;
				stations.push_back(std::move(station));
			}
		}

		std::sort(stations.begin(), stations.end(), [](const Station& a, const Station& b) { return a.serial < b.serial; });
		return stations;
	}

	inline StatusLabel PowerStateBadge(const Station& station)
	{
		const char* label = "State unknown";
		if (station.kind != StationKind::V2) {
			label = station.seenByBle ? "State unknown" : "Not seen over Bluetooth";
		}
		else {
			switch (station.powerState) {
				case PowerState::Sleeping: label = "Sleeping"; break;
				case PowerState::Standby: label = "Standby"; break;
				case PowerState::AwakeOldFirmware:
				case PowerState::AwakeFromSleep:
				case PowerState::AwakeFromStandby: label = "Active"; break;
				default: break;
			}
		}

		StatusTone tone = StatusTone::Idle;
		if (station.isFaulty) {
			tone = StatusTone::Error;
		}
		else if (station.kind == StationKind::V2) {
			if (IsAwake(station.powerState)) {
				tone = StatusTone::Ok;
			}
			else if (station.powerState == PowerState::Standby || station.powerState == PowerState::Sleeping) {
				tone = StatusTone::Pending;
			}
		}
		return {label, tone};
	}

	inline std::map<uint8_t, std::string> ChannelOccupancy(const std::vector<Station>& stations)
	{
		std::map<uint8_t, std::string> occupancy;
		for (const Station& s : stations) {
			if (s.kind != StationKind::V2 || s.channel < 1 || s.channel > 16) continue;
			occupancy.emplace(s.channel, s.serial);
		}
		return occupancy;
	}

	enum class ChannelSlot
	{
		Current,
		Taken,
		Free
	};

	inline ChannelSlot ClassifyChannelSlot(uint8_t candidate, const Station& self, const std::map<uint8_t, std::string>& occupancy)
	{
		if (candidate == self.channel) return ChannelSlot::Current;
		const auto it = occupancy.find(candidate);
		if (it != occupancy.end() && it->second != self.serial) return ChannelSlot::Taken;
		return ChannelSlot::Free;
	}

	inline bool ApplyNickname(std::map<std::string, std::string>& nicknames, const std::string& serial, std::string_view value)
	{
		if (value.empty()) return nicknames.erase(serial) > 0;
		auto [it, inserted] = nicknames.emplace(serial, value);
		if (inserted) return true;
		if (it->second == value) return false;
		it->second = value;
		return true;
	}

	inline std::set<uint8_t> ConflictingChannels(const std::vector<Station>& stations)
	{
		std::map<uint8_t, int> counts;
		for (const Station& s : stations) {
			if (s.kind == StationKind::V2 && s.channel >= 1 && s.channel <= 16) ++counts[s.channel];
		}
		std::set<uint8_t> conflicts;
		for (const auto& [channel, count] : counts) {
			if (count > 1) conflicts.insert(channel);
		}
		return conflicts;
	}

	inline std::map<std::string, uint8_t> AutoAssignChannels(const std::vector<Station>& stations)
	{
		std::set<uint8_t> used;
		for (const Station& s : stations) {
			if (s.kind == StationKind::V2 && s.channel >= 1 && s.channel <= 16) used.insert(s.channel);
		}
		const std::set<uint8_t> conflicts = ConflictingChannels(stations);

		std::map<std::string, uint8_t> assignments;
		std::set<uint8_t> seenConflictChannels;
		for (const Station& s : stations) {
			if (s.kind != StationKind::V2) continue;
			const bool invalid = s.channel < 1 || s.channel > 16;
			const bool conflicted = conflicts.count(s.channel) > 0;
			if (!invalid && !conflicted) continue;
			if (conflicted && seenConflictChannels.insert(s.channel).second) continue;
			for (uint8_t candidate = 1; candidate <= 16; ++candidate) {
				if (used.count(candidate) == 0) {
					assignments[s.serial] = candidate;
					used.insert(candidate);
					break;
				}
			}
		}
		return assignments;
	}

	enum class IntentPhase
	{
		Idle,
		Pending,
		Confirmed,
		Failed
	};

	struct PowerIntent
	{
		IntentPhase phase = IntentPhase::Idle;
		PowerCommand target = PowerCommand::Wake;
		int retriesLeft = 0;
	};

	inline bool IntentSatisfied(PowerCommand target, PowerState observed)
	{
		switch (target) {
			case PowerCommand::Wake: return IsAwake(observed);
			case PowerCommand::Standby: return observed == PowerState::Standby;
			case PowerCommand::Sleep: return observed == PowerState::Sleeping;
		}
		return false;
	}

	inline void BeginIntent(PowerIntent& intent, PowerCommand target, int retries)
	{
		intent.phase = IntentPhase::Pending;
		intent.target = target;
		intent.retriesLeft = retries;
	}

	enum class IntentAction
	{
		None,
		SendCommand,
		Done
	};

	inline IntentAction TickIntent(PowerIntent& intent, PowerState observed, bool observable)
	{
		if (intent.phase != IntentPhase::Pending) return IntentAction::None;
		if (!observable) {
			intent.phase = IntentPhase::Confirmed;
			return IntentAction::Done;
		}
		if (IntentSatisfied(intent.target, observed)) {
			intent.phase = IntentPhase::Confirmed;
			return IntentAction::Done;
		}
		if (intent.retriesLeft <= 0) {
			intent.phase = IntentPhase::Failed;
			return IntentAction::Done;
		}
		--intent.retriesLeft;
		return IntentAction::SendCommand;
	}

	struct AutomationSettings
	{
		bool powerManagement = false;
		bool wakeOnStart = false;
		bool sleepOnExit = false;
		bool useStandby = false;
	};

	enum class AutomationAction
	{
		None,
		WakeAll,
		StandbyAll,
		SleepAll
	};

	struct AutomationState
	{
		bool wakeFiredThisSession = false;
	};

	inline AutomationAction EvaluateAutomation(AutomationState& state, bool prevVrConnected, bool vrConnected,
	                                           const AutomationSettings& settings)
	{
		if (!settings.powerManagement) return AutomationAction::None;
		if (!prevVrConnected && vrConnected && settings.wakeOnStart) {
			state.wakeFiredThisSession = true;
			return AutomationAction::WakeAll;
		}
		if (prevVrConnected && !vrConnected && settings.sleepOnExit && state.wakeFiredThisSession) {
			state.wakeFiredThisSession = false;
			return settings.useStandby ? AutomationAction::StandbyAll : AutomationAction::SleepAll;
		}
		return AutomationAction::None;
	}

}
