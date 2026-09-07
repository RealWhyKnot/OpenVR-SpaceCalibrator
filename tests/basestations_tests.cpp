#include "basestations/BaseStationsLogic.h"
#include "basestations/BaseStationsSettings.h"

#include <cstdio>
#include <functional>

namespace {

	using namespace spacecal::basestations;

	int failures = 0;

#define CHECK(cond)                                                                                                                        \
	do {                                                                                                                                   \
		if (!(cond)) {                                                                                                                     \
			std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);                                                                     \
			++failures;                                                                                                                    \
		}                                                                                                                                  \
	} while (0)

	NamedAdvert MakeV2Advert(const std::string& name, uint8_t channel, PowerState state, bool faulty = false)
	{
		NamedAdvert advert;
		advert.address = std::hash<std::string>{}(name);
		advert.name = name;
		advert.kind = StationKind::V2;
		V2AdvertState v2;
		v2.channel = channel;
		v2.powerState = state;
		v2.isFaulty = faulty;
		advert.v2State = v2;
		return advert;
	}

	Station MakeStation(const std::string& serial, StationKind kind, uint8_t channel = 0, PowerState state = PowerState::Unknown)
	{
		Station station;
		station.serial = serial;
		station.kind = kind;
		station.channel = channel;
		station.powerState = state;
		station.seenByBle = kind != StationKind::Unknown;
		return station;
	}

	void TestAdvertNames()
	{
		CHECK(ClassifyAdvertName("LHB-12345678") == StationKind::V2);
		CHECK(ClassifyAdvertName("LHB-00000000") == StationKind::Unknown);
		CHECK(ClassifyAdvertName("HTC BS A1B2C3") == StationKind::V1);
		CHECK(ClassifyAdvertName("HTC BS 000000") == StationKind::Unknown);
		CHECK(ClassifyAdvertName("SomeHeadphones") == StationKind::Unknown);
		CHECK(ClassifyAdvertName("") == StationKind::Unknown);
	}

	void TestV2ManufacturerData()
	{
		const std::vector<uint8_t> data{0x00, 0x00, 0x0D, 0x00, 0x0B, 0x00, 0x00};
		const auto state = DecodeV2ManufacturerData(data);
		CHECK(state.has_value());
		CHECK(state->channel == 13);
		CHECK(state->powerState == PowerState::AwakeFromStandby);
		CHECK(!state->isFaulty);
		CHECK(!state->oldFirmware);

		const auto faulty = DecodeV2ManufacturerData({0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01});
		CHECK(faulty.has_value() && faulty->isFaulty && faulty->powerState == PowerState::Sleeping);

		const auto oldFw = DecodeV2ManufacturerData({0x00, 0x00, 0x02, 0x00, 0x03, 0x00, 0x00});
		CHECK(oldFw.has_value() && oldFw->oldFirmware && IsAwake(oldFw->powerState));

		CHECK(!DecodeV2ManufacturerData({0x00, 0x00, 0x01}).has_value());
		CHECK(!DecodeV2ManufacturerData({}).has_value());
	}

	void TestPowerWrites()
	{
		const auto wake = EncodeV2PowerWrites(PowerCommand::Wake, true);
		CHECK(wake.size() == 1 && wake[0] == std::vector<uint8_t>{0x01});

		const auto standby = EncodeV2PowerWrites(PowerCommand::Standby, true);
		CHECK(standby.size() == 1 && standby[0] == std::vector<uint8_t>{0x02});

		const auto sleep = EncodeV2PowerWrites(PowerCommand::Sleep, true);
		CHECK(sleep.size() == 2 && sleep[0] == std::vector<uint8_t>{0x01} && sleep[1] == std::vector<uint8_t>{0x00});

		CHECK(EncodeV2PowerWrites(PowerCommand::Standby, false) == sleep);

		CHECK(!EncodeV2ChannelWrite(0).has_value());
		CHECK(!EncodeV2ChannelWrite(17).has_value());
		const auto ch = EncodeV2ChannelWrite(16);
		CHECK(ch.has_value() && (*ch)[0] == 16);
	}

	void TestV1Packet()
	{
		const auto packet = EncodeV1PowerPacket(kV1CommandTimedOff, 0x0102, 0xAABBCCDDu);
		CHECK(packet.size() == 20);
		CHECK(packet[0] == 0x12);
		CHECK(packet[1] == kV1CommandTimedOff);
		CHECK(packet[2] == 0x01 && packet[3] == 0x02);
		CHECK(packet[4] == 0xDD && packet[5] == 0xCC && packet[6] == 0xBB && packet[7] == 0xAA);
		for (size_t i = 8; i < packet.size(); ++i)
			CHECK(packet[i] == 0x00);
	}

	void TestPowerStateBadge()
	{
		Station faulty = MakeStation("LHB-1", StationKind::V2, 1, PowerState::AwakeFromSleep);
		faulty.isFaulty = true;
		CHECK(PowerStateBadge(faulty).tone == StatusTone::Error);
		CHECK(std::string(PowerStateBadge(faulty).label) == "Active");

		Station v1Unseen = MakeStation("LHB-STEAMVR", StationKind::Unknown);
		v1Unseen.seenByBle = false;
		CHECK(std::string(PowerStateBadge(v1Unseen).label) == "Not seen over Bluetooth");

		CHECK(PowerStateBadge(MakeStation("LHB-2", StationKind::V2, 2, PowerState::Standby)).tone == StatusTone::Pending);
		for (const PowerState awake : {PowerState::AwakeOldFirmware, PowerState::AwakeFromSleep, PowerState::AwakeFromStandby}) {
			CHECK(PowerStateBadge(MakeStation("LHB-4", StationKind::V2, 4, awake)).tone == StatusTone::Ok);
		}
	}

	void TestChannels()
	{
		const std::vector<Station> stations = {
		    MakeStation("LHB-A", StationKind::V2, 3),
		    MakeStation("LHB-B", StationKind::V2, 0),
		    MakeStation("LHB-C", StationKind::V2, 17),
		    MakeStation("HTC BS XX1234", StationKind::V1, 5),
		};
		const auto occupancy = ChannelOccupancy(stations);
		CHECK(occupancy.size() == 1 && occupancy.at(3) == "LHB-A");

		const Station self = MakeStation("LHB-A", StationKind::V2, 3);
		const auto occ2 = ChannelOccupancy({self, MakeStation("LHB-B", StationKind::V2, 7)});
		CHECK(ClassifyChannelSlot(3, self, occ2) == ChannelSlot::Current);
		CHECK(ClassifyChannelSlot(7, self, occ2) == ChannelSlot::Taken);
		CHECK(ClassifyChannelSlot(8, self, occ2) == ChannelSlot::Free);
	}

	void TestMerge()
	{
		auto joined = MergeDiscovery({MakeV2Advert("LHB-AAAA1111", 3, PowerState::AwakeFromSleep)}, {{"LHB-AAAA1111", "Valve SR Imp"}});
		CHECK(joined.size() == 1 && joined[0].seenByBle && joined[0].seenBySteamVr && joined[0].channel == 3);

		auto unjoined = MergeDiscovery({MakeV2Advert("LHB-AAAA1111", 3, PowerState::Sleeping)}, {{"LHB-BBBB2222", "Valve SR Imp"}});
		CHECK(unjoined.size() == 2);

		NamedAdvert v1;
		v1.address = 42;
		v1.name = "HTC BS C3D4E5";
		v1.kind = StationKind::V1;
		auto v1Joined = MergeDiscovery({v1}, {{"LHB-11C3D4E5", "HTC V1"}});
		CHECK(v1Joined.size() == 1 && v1Joined[0].seenByBle && v1Joined[0].seenBySteamVr && v1Joined[0].serial == "LHB-11C3D4E5");
	}

	void TestConflictsAndAssign()
	{
		const auto stations = MergeDiscovery({MakeV2Advert("LHB-AAAA1111", 5, PowerState::AwakeFromSleep),
		                                      MakeV2Advert("LHB-BBBB2222", 5, PowerState::AwakeFromSleep),
		                                      MakeV2Advert("LHB-CCCC3333", 7, PowerState::AwakeFromSleep)},
		                                     {});
		const auto conflicts = ConflictingChannels(stations);
		CHECK(conflicts.size() == 1 && conflicts.count(5) == 1);

		const auto assignments = AutoAssignChannels(stations);
		CHECK(assignments.size() == 1);
		const uint8_t assigned = assignments.begin()->second;
		CHECK(assigned != 5 && assigned != 7 && assigned >= 1 && assigned <= 16);
	}

	void TestNicknames()
	{
		std::map<std::string, std::string> nicknames;
		CHECK(ApplyNickname(nicknames, "LHB-A", "Front left"));
		CHECK(nicknames.at("LHB-A") == "Front left");
		CHECK(!ApplyNickname(nicknames, "LHB-A", "Front left"));
		CHECK(ApplyNickname(nicknames, "LHB-A", "Desk"));
		CHECK(ApplyNickname(nicknames, "LHB-A", ""));
		CHECK(nicknames.empty());
		CHECK(!ApplyNickname(nicknames, "LHB-A", ""));
	}

	void TestIntents()
	{
		PowerIntent intent;
		BeginIntent(intent, PowerCommand::Wake, 2);
		CHECK(TickIntent(intent, PowerState::Sleeping, true) == IntentAction::SendCommand);
		CHECK(TickIntent(intent, PowerState::AwakeFromSleep, true) == IntentAction::Done);
		CHECK(intent.phase == IntentPhase::Confirmed);

		PowerIntent failing;
		BeginIntent(failing, PowerCommand::Sleep, 1);
		CHECK(TickIntent(failing, PowerState::AwakeFromSleep, true) == IntentAction::SendCommand);
		CHECK(TickIntent(failing, PowerState::AwakeFromSleep, true) == IntentAction::Done);
		CHECK(failing.phase == IntentPhase::Failed);

		PowerIntent blind;
		BeginIntent(blind, PowerCommand::Wake, 2);
		CHECK(TickIntent(blind, PowerState::Unknown, false) == IntentAction::Done);
		CHECK(blind.phase == IntentPhase::Confirmed);
	}

	void TestAutomation()
	{
		AutomationSettings settings;
		settings.powerManagement = true;
		settings.wakeOnStart = true;
		settings.sleepOnExit = true;

		AutomationState state;
		CHECK(EvaluateAutomation(state, false, false, settings) == AutomationAction::None);
		CHECK(EvaluateAutomation(state, false, true, settings) == AutomationAction::WakeAll);
		CHECK(EvaluateAutomation(state, true, true, settings) == AutomationAction::None);
		CHECK(EvaluateAutomation(state, true, false, settings) == AutomationAction::SleepAll);

		settings.useStandby = true;
		CHECK(EvaluateAutomation(state, false, true, settings) == AutomationAction::WakeAll);
		CHECK(EvaluateAutomation(state, true, false, settings) == AutomationAction::StandbyAll);

		settings.powerManagement = false;
		CHECK(EvaluateAutomation(state, false, true, settings) == AutomationAction::None);

		AutomationSettings noWake;
		noWake.powerManagement = true;
		noWake.sleepOnExit = true;
		AutomationState fresh;
		CHECK(EvaluateAutomation(fresh, false, true, noWake) == AutomationAction::None);
		CHECK(EvaluateAutomation(fresh, true, false, noWake) == AutomationAction::None);
	}

	void TestSettingsRoundTrip()
	{
		BaseStationsSettings settings;
		settings.automation.powerManagement = true;
		settings.automation.wakeOnStart = true;
		settings.automation.useStandby = true;
		settings.nicknames["LHB-AAAA1111"] = "Front left";
		settings.nicknames["LHB-BBBB2222"] = "Desk";

		const BaseStationsSettings loaded = ParseBaseStationsSettings(SerializeBaseStationsSettings(settings));
		CHECK(loaded.automation.powerManagement);
		CHECK(loaded.automation.wakeOnStart);
		CHECK(!loaded.automation.sleepOnExit);
		CHECK(loaded.automation.useStandby);
		CHECK(loaded.nicknames.size() == 2);
		CHECK(loaded.nicknames.at("LHB-AAAA1111") == "Front left");

		const BaseStationsSettings garbage = ParseBaseStationsSettings("not json");
		CHECK(!garbage.automation.powerManagement && garbage.nicknames.empty());
	}

}

int main()
{
	TestAdvertNames();
	TestV2ManufacturerData();
	TestPowerWrites();
	TestV1Packet();
	TestPowerStateBadge();
	TestChannels();
	TestMerge();
	TestConflictsAndAssign();
	TestNicknames();
	TestIntents();
	TestAutomation();
	TestSettingsRoundTrip();

	if (failures == 0) {
		std::printf("basestations_tests: all tests passed\n");
		return 0;
	}
	std::printf("basestations_tests: %d failure(s)\n", failures);
	return 1;
}
