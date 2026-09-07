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

	ImVec4 ToneColor(StatusTone tone)
	{
		switch (tone) {
			case StatusTone::Ok: return ImVec4(0.35f, 0.85f, 0.45f, 1.0f);
			case StatusTone::Pending: return ImVec4(1.0f, 0.75f, 0.3f, 1.0f);
			case StatusTone::Error: return ImVec4(0.9f, 0.4f, 0.4f, 1.0f);
			case StatusTone::Info: return ImVec4(0.55f, 0.75f, 1.0f, 1.0f);
			default: return ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
		}
	}

	void Tooltip(const char* text)
	{
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
			ImGui::SetTooltip("%s", text);
		}
	}

	bool SteamVrNativePowerManagementLooksActive()
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
					const ImVec4 current = ToneColor(StatusTone::Ok);
					ImGui::PushStyleColor(ImGuiCol_Button, current);
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, current);
					ImGui::PushStyleColor(ImGuiCol_ButtonActive, current);
					ImGui::Button(label, size);
					ImGui::PopStyleColor(3);
					Tooltip("Current channel.");
					break;
				}
				case ChannelSlot::Taken: {
					ImGui::BeginDisabled(true);
					ImGui::Button(label, size);
					ImGui::EndDisabled();
					const auto it = occupancy.find(ch);
					const std::string why = "Used by " + (it != occupancy.end() ? it->second : std::string("another station"));
					Tooltip(why.c_str());
					break;
				}
				case ChannelSlot::Free:
					if (ImGui::Button(label, size)) {
						ctl.RequestChannel(station.serial, ch);
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

			ImGui::BeginDisabled(unseen);
			if (ImGui::Button(ICON_MI_POWER " Wake")) ctl.RequestPower(station, PowerCommand::Wake);
			ImGui::EndDisabled();
			if (unseen) Tooltip("Not seen over Bluetooth yet.");

			ImGui::SameLine();
			ImGui::BeginDisabled(unseen || notV2 || !station.standbySupported);
			if (ImGui::Button(ICON_MI_HISTORY " Standby")) ctl.RequestPower(station, PowerCommand::Standby);
			ImGui::EndDisabled();
			Tooltip(unseen ? "Not seen over Bluetooth yet." : "Standby needs a 2.0 station on current firmware.");

			ImGui::SameLine();
			ImGui::BeginDisabled(unseen);
			if (ImGui::Button(ICON_MI_BEDTIME " Sleep")) ctl.RequestPower(station, PowerCommand::Sleep);
			ImGui::EndDisabled();
			if (unseen) Tooltip("Not seen over Bluetooth yet.");

			ImGui::SameLine();
			ImGui::BeginDisabled(unseen || notV2);
			if (ImGui::Button(ICON_MI_LIGHTBULB " Identify")) ctl.RequestIdentify(station);
			ImGui::EndDisabled();
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
		changed |= ui::CheckboxWithDescription("Enable power automation",
		                                       "Wake stations when the VR session starts and power them down when it ends. Not "
		                                       "recommended for tracking spaces shared with other SteamVR machines.",
		                                       &automation.powerManagement);
		ImGui::BeginDisabled(!automation.powerManagement);
		changed |= ui::CheckboxWithDescription("Wake on SteamVR start", nullptr, &automation.wakeOnStart);
		changed |= ui::CheckboxWithDescription("Power down on SteamVR exit", nullptr, &automation.sleepOnExit);
		changed |= ui::CheckboxWithDescription("Use standby instead of sleep",
		                                       "Standby wakes faster but needs 2.0 stations on current firmware; others fall back "
		                                       "to sleep.",
		                                       &automation.useStandby);
		ImGui::EndDisabled();
		if (changed) ctl.SaveSettings();

		if (automation.powerManagement && SteamVrNativePowerManagementLooksActive()) {
			ImGui::PushStyleColor(ImGuiCol_Text, ToneColor(StatusTone::Pending));
			ImGui::TextWrapped("SteamVR also manages station power. Both managers are active; disable one so they don't fight.");
			ImGui::PopStyleColor();
		}
	}

}

void DrawBaseStationsPage()
{
	auto& ctl = BaseStationsController::Get();
	ctl.EnsureStarted();

	ui::TextHeading("Base stations");

	if (ctl.BluetoothFailed()) {
		ImGui::PushStyleColor(ImGuiCol_Text, ToneColor(StatusTone::Error));
		ImGui::TextWrapped("No usable Bluetooth LE adapter was found. Station control needs one.");
		ImGui::PopStyleColor();
		ImGui::Spacing();
	}

	if (ImGui::Button(ICON_MI_POWER " Wake all")) ctl.RequestPowerAll(PowerCommand::Wake);
	Tooltip("Wake every station in range.");
	ImGui::SameLine();
	if (ImGui::Button(ICON_MI_BEDTIME " Sleep all")) {
		ctl.RequestPowerAll(ctl.Settings().automation.useStandby ? PowerCommand::Standby : PowerCommand::Sleep);
	}
	Tooltip("Power down every station in range.");
	ImGui::Spacing();

	const auto& stations = ctl.Stations();
	if (stations.empty()) {
		ImGui::TextDisabled("Listening for base stations over Bluetooth...");
		ImGui::Spacing();
	}

	const auto conflicts = ConflictingChannels(stations);
	if (!conflicts.empty()) {
		ImGui::PushStyleColor(ImGuiCol_Text, ToneColor(StatusTone::Error));
		ImGui::TextWrapped("Channel conflict: two or more 2.0 stations share a channel.");
		ImGui::PopStyleColor();
		if (ImGui::Button("Fix channels")) {
			for (const auto& [serial, channel] : AutoAssignChannels(stations)) {
				ctl.RequestChannel(serial, channel);
			}
		}
		ImGui::Spacing();
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

	ImGui::Spacing();
	DrawAutomationSettings(ctl);
}
