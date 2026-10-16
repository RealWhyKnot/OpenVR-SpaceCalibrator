#include "stdafx.h"
#include "ui/UiCommon.h"
#include "ui/IconGlyphs.h"
#include "ui/Style.h"
#include "ui/Widgets.h"
#include "basestations/BaseStationsController.h"

#include <imgui/imgui.h>
#include "imgui_extensions.h"

#include <openvr.h>

#include <cstdio>
#include <map>
#include <string>

namespace {

	using namespace spacecal::basestations;

	constexpr float kChannelButtonSize = 44.0f;

	std::string nickEditSerial;
	char nickBuffer[64] = {};

	struct PendingChannel
	{
		bool open = false;
		std::string serial;
		std::string text;
		uint8_t channel = 0;
	};
	PendingChannel pendingChannel;
	std::map<std::string, uint8_t> pendingFix;
	std::string pendingFixText;

	std::string StationName(const Station& station)
	{
		return station.nickname.empty() ? station.serial : station.nickname;
	}

	std::string ChannelMoveText(const Station& station, uint8_t channel)
	{
		char text[160];
		if (station.channel >= 1 && station.channel <= 16) {
			snprintf(text, sizeof text, "%s moves from channel %u to channel %u.", StationName(station).c_str(), (unsigned)station.channel,
			         (unsigned)channel);
		}
		else {
			snprintf(text, sizeof text, "%s moves to channel %u.", StationName(station).c_str(), (unsigned)channel);
		}
		return text;
	}

	ImVec4 ToneColor(StatusTone tone)
	{
		switch (tone) {
			case StatusTone::Ok: return ImVec4(0.35f, 0.85f, 0.45f, 1.0f);
			case StatusTone::Pending: return ImVec4(1.0f, 0.75f, 0.3f, 1.0f);
			case StatusTone::Error: return ImVec4(0.9f, 0.4f, 0.4f, 1.0f);
			case StatusTone::Info: return ImVec4(0.55f, 0.75f, 1.0f, 1.0f);
			default: return ImVec4(0.72f, 0.72f, 0.72f, 1.0f);
		}
	}

	void Tooltip(const char* text)
	{
		ui::Tooltip(text, ImGuiHoveredFlags_AllowWhenDisabled);
	}

	bool ReadSteamVrNativePowerManagement()
	{
		auto* settings = vr::VRSettings();
		if (!settings) return false;
		vr::EVRSettingsError err = vr::VRSettingsError_None;
		const bool bluetooth = settings->GetBool(vr::k_pch_Lighthouse_Section, vr::k_pch_Lighthouse_EnableBluetooth_Bool, &err);
		if (err != vr::VRSettingsError_None || !bluetooth) return false;
		char managed[256] = {};
		err = vr::VRSettingsError_None;
		settings->GetString(vr::k_pch_Lighthouse_Section, vr::k_pch_Lighthouse_PowerManagedBaseStations2_String, managed, sizeof managed,
		                    &err);
		return err == vr::VRSettingsError_None && managed[0] != '\0';
	}

	bool SteamVrNativePowerManagementLooksActive()
	{
		constexpr double kRecheckSeconds = 1.0;
		static bool active = false;
		static bool checked = false;
		static double checkedAt = 0.0;
		const double now = ImGui::GetTime();
		if (!checked || now < checkedAt || now - checkedAt >= kRecheckSeconds) {
			active = ReadSteamVrNativePowerManagement();
			checked = true;
			checkedAt = now;
		}
		return active;
	}

	void DrawChannelGrid(BaseStationsController& ctl, const Station& station, const std::map<uint8_t, std::string>& occupancy)
	{
		ImGui::Spacing();
		ImGui::TextUnformatted("Channel");
		Tooltip("Every 2.0 station in the room needs its own channel.");
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, kChannelButtonSize * 0.5f);
		for (uint8_t ch = 1; ch <= 16; ++ch) {
			if (((ch - 1) % 8) != 0) ImGui::SameLine();
			char label[16];
			snprintf(label, sizeof label, "%u##ch%u", ch, ch);
			const ImVec2 size(kChannelButtonSize, kChannelButtonSize);
			switch (ClassifyChannelSlot(ch, station, occupancy)) {
				case ChannelSlot::Current: {
					const ImVec4 current(0.14f, 0.48f, 0.26f, 1.0f);
					ImGui::PushStyleColor(ImGuiCol_Button, current);
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, current);
					ImGui::PushStyleColor(ImGuiCol_ButtonActive, current);
					ImGui::Button(label, size);
					ImGui::PopStyleColor(3);
					Tooltip("Current channel.");
					break;
				}
				case ChannelSlot::Taken: {
					ui::BeginDisabledLook(true);
					ImGui::Button(label, size);
					ui::EndDisabledLook();
					const auto it = occupancy.find(ch);
					const std::string why = "Used by " + (it != occupancy.end() ? it->second : std::string("another station"));
					Tooltip(why.c_str());
					break;
				}
				case ChannelSlot::Free:
					if (ImGui::Button(label, size)) {
						pendingChannel.open = true;
						pendingChannel.serial = station.serial;
						pendingChannel.channel = ch;
						pendingChannel.text = ChannelMoveText(station, ch);
					}
					break;
			}
		}
		ImGui::PopStyleVar();
	}

	void DrawNicknameRow(BaseStationsController& ctl, const Station& station)
	{
		ImGui::Spacing();
		if (nickEditSerial != station.serial) {
			if (ImGui::Button(ICON_MI_EDIT " Edit name")) {
				nickEditSerial = station.serial;
				snprintf(nickBuffer, sizeof nickBuffer, "%s", station.nickname.c_str());
			}
			Tooltip("Set a nickname for this station.");
			return;
		}
		ImGui::SetNextItemWidth(240.0f);
		const bool entered = ImGui::InputTextWithHint("##nickname", station.serial.c_str(), nickBuffer, sizeof nickBuffer,
		                                              ImGuiInputTextFlags_EnterReturnsTrue);
		ImGui::SameLine();
		const bool saved = ImGui::Button(ICON_MI_SAVE " Save");
		if (entered || saved) {
			ctl.SetNickname(station.serial, nickBuffer);
			nickEditSerial.clear();
		}
	}

	void DrawStationCard(BaseStationsController& ctl, const Station& station, const std::map<uint8_t, std::string>& occupancy)
	{
		ImGui::PushID(station.serial.c_str());
		if (ui::BeginCard("station")) {
			const std::string title = station.nickname.empty() ? station.serial : station.nickname;
			ImGui::PushFont(ui::fontHeading);
			ImGui::TextUnformatted(title.c_str());
			ImGui::PopFont();

			ui::PillText(station.kind == StationKind::V1   ? "1.0"
			             : station.kind == StationKind::V2 ? "2.0"
			                                               : "?",
			             ToneColor(StatusTone::Info));
			if (station.isFaulty) {
				ImGui::SameLine();
				ui::PillText("Faulty", ToneColor(StatusTone::Error));
				Tooltip("The station is advertising a fault (red status light).");
			}
			if (station.seenBySteamVr) {
				ImGui::SameLine();
				ui::PillText("Used by SteamVR", ToneColor(StatusTone::Ok));
			}
			ImGui::SameLine();
			const StatusLabel power = PowerStateBadge(station);
			ui::PillText(power.label, ToneColor(power.tone));

			const bool unseen = !station.seenByBle;
			const bool notV2 = station.kind != StationKind::V2;

			ui::BeginDisabledLook(unseen);
			if (ImGui::Button(ICON_MI_POWER " Wake")) ctl.RequestPower(station, PowerCommand::Wake);
			ui::EndDisabledLook();
			if (unseen) Tooltip("Not seen over Bluetooth yet.");

			ImGui::SameLine();
			ui::BeginDisabledLook(unseen || notV2 || !station.standbySupported);
			if (ImGui::Button(ICON_MI_HISTORY " Standby")) ctl.RequestPower(station, PowerCommand::Standby);
			ui::EndDisabledLook();
			Tooltip(unseen ? "Not seen over Bluetooth yet." : "Standby needs a 2.0 station on current firmware.");

			ImGui::SameLine();
			ui::BeginDisabledLook(unseen);
			if (ImGui::Button(ICON_MI_BEDTIME " Sleep")) ctl.RequestPower(station, PowerCommand::Sleep);
			ui::EndDisabledLook();
			if (unseen) Tooltip("Not seen over Bluetooth yet.");

			ImGui::SameLine();
			ui::BeginDisabledLook(unseen || notV2);
			if (ImGui::Button(ICON_MI_LIGHTBULB " Identify")) ctl.RequestIdentify(station);
			ui::EndDisabledLook();
			Tooltip(unseen ? "Not seen over Bluetooth yet." : "Identify needs a 2.0 station.");

			if (station.kind == StationKind::V2) DrawChannelGrid(ctl, station, occupancy);
			DrawNicknameRow(ctl, station);
		}
		ui::EndCard();
		ImGui::PopID();
		ImGui::Spacing();
	}

	void DrawAutomationSettings(BaseStationsController& ctl)
	{
		auto& automation = ctl.Settings().automation;
		ui::TextHeading("Power automation");
		bool changed = false;
		changed |= ImGui::Checkbox("Enable power automation", &automation.powerManagement);
		Tooltip("Turns on the options below. Not recommended if other SteamVR PCs share these stations.");
		if (automation.powerManagement) {
			ImGui::Indent();
			changed |= ImGui::Checkbox("Wake on SteamVR start", &automation.wakeOnStart);
			Tooltip("Wakes every station seen over Bluetooth during the first minute after Space Calibrator connects to SteamVR.");
			changed |= ImGui::Checkbox("Power down on SteamVR exit", &automation.sleepOnExit);
			Tooltip("Powers stations down when SteamVR closes. Only runs in sessions where Wake on SteamVR start fired.");
			changed |= ImGui::Checkbox("Use standby instead of sleep", &automation.useStandby);
			Tooltip("Power down and Sleep all use standby. Standby wakes faster but needs 2.0 stations on current firmware; others "
			        "fall back to sleep.");
			ImGui::Unindent();
		}
		if (changed) ctl.SaveSettings();

		if (automation.powerManagement && SteamVrNativePowerManagementLooksActive()) {
			ui::Note("power_conflict", ui::NoteTone::Pending,
			         "SteamVR is also managing station power, so the two can fight. Turn off Enable power automation here, or turn "
			         "off base station power management in SteamVR settings.");
		}
	}

}

void DrawBaseStationsPage()
{
	auto& ctl = BaseStationsController::Get();
	ctl.EnsureStarted();

	ui::TextHeading("Base stations");

	const bool bluetoothFailed = ctl.BluetoothFailed();
	if (bluetoothFailed) {
		ui::Note("bluetooth_failed", ui::NoteTone::Error,
		         "Bluetooth LE isn't available, so stations can't be controlled. Turn Bluetooth on in Windows or plug in an adapter, "
		         "then restart Space Calibrator.");
		ImGui::Spacing();
	}

	ui::BeginDisabledLook(bluetoothFailed);
	if (ImGui::Button(ICON_MI_POWER " Wake all")) ctl.RequestPowerAll(PowerCommand::Wake);
	ui::EndDisabledLook();
	Tooltip(bluetoothFailed ? "Needs Bluetooth LE." : "Wake every station in range.");
	ImGui::SameLine();
	ui::BeginDisabledLook(bluetoothFailed);
	if (ImGui::Button(ICON_MI_BEDTIME " Sleep all")) {
		ctl.RequestPowerAll(ctl.Settings().automation.useStandby ? PowerCommand::Standby : PowerCommand::Sleep);
	}
	ui::EndDisabledLook();
	Tooltip(bluetoothFailed ? "Needs Bluetooth LE." : "Power down every station in range.");
	ImGui::Spacing();

	const auto& stations = ctl.Stations();
	if (stations.empty() && !bluetoothFailed) {
		ImGui::TextDisabled("Listening for base stations over Bluetooth...");
		ImGui::Spacing();
	}

	const auto conflicts = ConflictingChannels(stations);
	if (!conflicts.empty()) {
		bool fixClicked = false;
		if (ui::BeginNote("channel_conflict", ui::NoteTone::Error)) {
			ImGui::TextWrapped("Two or more 2.0 stations share a channel, so they can't both track. Fix channels moves the extras to "
			                   "free channels.");
			fixClicked = ImGui::Button("Fix channels");
			ui::EndNote();
		}
		if (fixClicked) {
			pendingFix = AutoAssignChannels(stations);
			pendingFixText.clear();
			for (const auto& [serial, channel] : pendingFix) {
				for (const Station& station : stations) {
					if (station.serial != serial) continue;
					if (!pendingFixText.empty()) pendingFixText += '\n';
					pendingFixText += ChannelMoveText(station, channel);
				}
			}
			if (!pendingFix.empty()) ImGui::OpenPopup("Fix channels?");
		}
		ImGui::Spacing();
	}
	if (ui::ConfirmPopup("Fix channels?", pendingFixText.c_str(), "Fix channels")) {
		for (const auto& [serial, channel] : pendingFix) {
			ctl.RequestChannel(serial, channel);
		}
	}

	if (!nickEditSerial.empty()) {
		bool editTargetVisible = false;
		for (const Station& station : stations) {
			if (station.serial == nickEditSerial) {
				editTargetVisible = true;
				break;
			}
		}
		if (!editTargetVisible) nickEditSerial.clear();
	}

	const auto occupancy = ChannelOccupancy(stations);
	for (const Station& station : stations) {
		DrawStationCard(ctl, station, occupancy);
	}

	if (pendingChannel.open) {
		pendingChannel.open = false;
		ImGui::OpenPopup("Change channel?");
	}
	if (ui::ConfirmPopup("Change channel?", pendingChannel.text.c_str(), "Change channel")) {
		ctl.RequestChannel(pendingChannel.serial, pendingChannel.channel);
	}

	ImGui::Spacing();
	DrawAutomationSettings(ctl);
}
