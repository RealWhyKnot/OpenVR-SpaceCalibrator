#include "stdafx.h"
#include "ui/UiCommon.h"
#include "Calibration.h"
#include "PoseFilter.h"
#include "StickSettings.h"
#include "StickSmoothingMath.h"

#include <imgui/imgui.h>
#include "imgui_extensions.h"

#include <string>
#include <vector>

static void SmoothingTooltip(const char* text)
{
	if (ImGui::IsItemHovered(0)) {
		ImGui::SetTooltip("%s", text);
	}
}

struct SmoothingStatsRow
{
	std::string label;
	protocol::SmoothingStats stats;
};

static void RefreshSmoothingStats(std::vector<SmoothingStatsRow>& rows)
{
	rows.clear();
	if (!vr::VRSystem()) {
		return;
	}
	char serial[vr::k_unMaxPropertyStringSize];
	for (uint32_t id = 0; id < vr::k_unMaxTrackedDeviceCount; ++id) {
		auto deviceClass = vr::VRSystem()->GetTrackedDeviceClass(id);
		if (deviceClass != vr::TrackedDeviceClass_GenericTracker && deviceClass != vr::TrackedDeviceClass_Controller) {
			continue;
		}
		protocol::SmoothingStats stats;
		if (!QuerySmoothingStats(id, stats) || !stats.active) {
			continue;
		}
		serial[0] = 0;
		vr::VRSystem()->GetStringTrackedDeviceProperty(id, vr::Prop_SerialNumber_String, serial, sizeof serial);
		rows.push_back({serial, stats});
	}
}

void DrawSmoothingPanel(ImVec2 panel_size)
{
	static std::vector<SmoothingStatsRow> rows;
	static double lastRefresh = -1.0;

	ImGui::BeginGroupPanel("Tracker smoothing", panel_size);

	int strength = CalCtx.smoothingParams.strength;
	ImGui::SliderInt("Smoothing", &strength, 0, 100, strength > 0 ? "%d%%" : "off", ImGuiSliderFlags_AlwaysClamp);
	bool changed = ImGui::IsItemDeactivatedAfterEdit();
	SmoothingTooltip("Steadies every tracker in the calibrated target space, in place. No extra devices are created.\n"
	                 "0 turns it off. Higher is calmer when the tracker is still; fast movement stays responsive at any setting.\n"
	                 "Takes effect immediately and is saved with the profile.");
	CalCtx.smoothingParams.strength = (uint8_t)strength;

	changed |= ImGui::Checkbox("Also smooth controllers", &CalCtx.smoothControllers);
	SmoothingTooltip("Smooth controllers from the target space too. Off by default: controller input is latency sensitive.");

	if (changed) {
		ApplySmoothingSettings();
	}

	if (CalCtx.smoothingParams.strength > 0) {
		const double now = ImGui::GetTime();
		if (now - lastRefresh > 0.5) {
			RefreshSmoothingStats(rows);
			lastRefresh = now;
		}
		if (rows.empty()) {
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			ImGui::TextWrapped("No smoothed devices yet. Smoothing follows the calibrated target space, so a profile must be active.");
			ImGui::PopStyleColor();
		}
		else if (ImGui::BeginTable("SmoothingStats", 4, ImGuiTableFlags_SizingStretchProp)) {
			ImGui::TableSetupColumn("Device");
			ImGui::TableSetupColumn("State");
			ImGui::TableSetupColumn("Position jitter (mm)");
			ImGui::TableSetupColumn("Reseeds");
			ImGui::TableHeadersRow();
			for (const auto& row : rows) {
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("%s", row.label.c_str());
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%s", spacecal::StepResultName((spacecal::StepResult)row.stats.lastResult));
				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%.2f -> %.2f", row.stats.rawJitterMm, row.stats.smoothJitterMm);
				ImGui::TableSetColumnIndex(3);
				ImGui::Text("%u", row.stats.reseeds);
			}
			ImGui::EndTable();
			SmoothingTooltip("Frame-to-frame movement, raw -> smoothed, averaged over the last second.\n"
			                 "Reseeds count gaps and jumps where the filter restarted from the raw pose.");
		}
	}

	ImGui::EndGroupPanel();
}

static bool StickSecondsSlider(const char* id, uint16_t& ms, double maxSeconds, int stepMs, const char* tooltip)
{
	float seconds = ms / 1000.0f;
	const char* format = ms == 0 ? "off" : (ms % 100 == 0 ? "%.1f s" : "%.2f s");
	ImGui::SetNextItemWidth(-FLT_MIN);
	if (ImGui::SliderFloat(id, &seconds, 0.0f, (float)maxSeconds, format, ImGuiSliderFlags_AlwaysClamp)) {
		ms = (uint16_t)(std::lround(seconds * 1000.0f / stepMs) * stepMs);
	}
	const bool changed = ImGui::IsItemDeactivatedAfterEdit();
	SmoothingTooltip(tooltip);
	return changed;
}

static bool StickStrengthSlider(uint16_t& pushMs)
{
	using namespace spacecal::stick_settings;
	int percent = StrengthForPushMs(pushMs);
	ImGui::SetNextItemWidth(-FLT_MIN);
	if (ImGui::SliderInt("##strength", &percent, kPercentStep, 100, pushMs > 0 ? "%d%%" : "no build-up", ImGuiSliderFlags_AlwaysClamp)) {
		pushMs = PushMsForStrength(SnapPercent(percent));
	}
	const bool changed = ImGui::IsItemDeactivatedAfterEdit();
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
		const double fullSeconds = spacecal::stick::kMaxPushSeconds;
		ImGui::SetTooltip("How gently speed builds up when you push the stick.\n"
		                  "Higher takes longer to reach full speed: 50%% takes %.0f s, 100%% takes %.0f s.",
		                  fullSeconds * 0.5, fullSeconds);
	}
	return changed;
}

static bool StickStartSpeedSlider(uint8_t& strength)
{
	using namespace spacecal::stick_settings;
	int start = 100 - ClampPercent(strength);
	ImGui::SetNextItemWidth(-FLT_MIN);
	if (ImGui::SliderInt("##start", &start, 0, 100, "%d%%", ImGuiSliderFlags_AlwaysClamp)) {
		strength = (uint8_t)(100 - SnapPercent(start));
	}
	const bool changed = ImGui::IsItemDeactivatedAfterEdit();
	SmoothingTooltip("How fast a push starts before it builds up.\n"
	                 "0% starts from a standstill. 40% starts at 40% speed and builds up the rest.\n"
	                 "Raise it if the start of a push doesn't move you in game.");
	return changed;
}

void DrawStickSmoothingPanel(ImVec2 panel_size)
{
	static const char* kStickLabels[2] = {"Left stick (movement)", "Right stick (turning)"};
	static const char* kStickNotes[2] = {"Turns acceleration on for this stick. Applies to every app, including SteamVR menus.",
	                                     "Turns acceleration on for this stick.\n"
	                                     "With snap turning, each snap fires later instead of turning more smoothly."};

	ImGui::BeginGroupPanel("Joystick acceleration", panel_size);

	ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
	ImGui::TextWrapped("Speed builds up and winds down gradually instead of jumping. Useful for filming.");
	ImGui::PopStyleColor();

	bool dirty = false;
	if (ImGui::BeginTable("stick_grid", 2, ImGuiTableFlags_SizingStretchProp)) {
		ImGui::TableSetupColumn("Stick", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Strength", ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableHeadersRow();
		for (int hand = 0; hand < 2; ++hand) {
			protocol::StickRampConfig& stick = CalCtx.stickSmoothing.sticks[hand];
			ImGui::PushID(hand);
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			if (ImGui::Checkbox(kStickLabels[hand], &CalCtx.stickOn[hand])) {
				if (CalCtx.stickOn[hand]) spacecal::stick_settings::PrepareToTurnOn(stick);
				dirty = true;
			}
			SmoothingTooltip(kStickNotes[hand]);
			ImGui::TableSetColumnIndex(1);
			ImGui::BeginDisabled(!CalCtx.stickOn[hand]);
			dirty |= StickStrengthSlider(stick.pushMs);
			ImGui::EndDisabled();
			ImGui::PopID();
		}
		ImGui::EndTable();
	}

	if (ImGui::TreeNodeEx("Fine-tune", ImGuiTreeNodeFlags_SpanAvailWidth)) {
		if (ImGui::BeginTable("stick_fine", 4, ImGuiTableFlags_SizingStretchProp)) {
			ImGui::TableSetupColumn("Stick", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Start speed", ImGuiTableColumnFlags_WidthStretch, 1.0f);
			ImGui::TableSetupColumn("Delay", ImGuiTableColumnFlags_WidthStretch, 1.0f);
			ImGui::TableSetupColumn("Stop", ImGuiTableColumnFlags_WidthStretch, 1.0f);
			ImGui::TableHeadersRow();
			for (int hand = 0; hand < 2; ++hand) {
				protocol::StickRampConfig& stick = CalCtx.stickSmoothing.sticks[hand];
				ImGui::PushID(hand);
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted(kStickLabels[hand]);
				ImGui::BeginDisabled(!CalCtx.stickOn[hand]);
				ImGui::TableSetColumnIndex(1);
				ImGui::BeginDisabled(stick.pushMs == 0);
				dirty |= StickStartSpeedSlider(stick.strength);
				ImGui::EndDisabled();
				ImGui::TableSetColumnIndex(2);
				ImGui::BeginDisabled(stick.pushMs == 0 || stick.strength == 0);
				dirty |= StickSecondsSlider("##delay", stick.delayMs, spacecal::stick::kMaxDelaySeconds, 100,
				                            "How long a push holds the start speed before it builds up.\n"
				                            "Taps shorter than this stay at the start speed, for small moves while framing a shot.\n"
				                            "At 0% start speed a short tap doesn't move at all.\n"
				                            "Off = builds up right away.");
				ImGui::EndDisabled();
				ImGui::TableSetColumnIndex(3);
				dirty |= StickSecondsSlider("##stop", stick.releaseMs, spacecal::stick::kMaxReleaseSeconds, 250,
				                            "How long the stick takes to glide to a stop after you let go.\nOff = stops instantly.");
				ImGui::EndDisabled();
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		ImGui::TreePop();
	}

	if (dirty) {
		ApplySmoothingSettings();
	}

	ImGui::EndGroupPanel();
}

void DrawFingerSmoothingPanel(ImVec2 panel_size)
{
	static const char* kFingerLabels[5] = {"Thumb", "Index", "Middle", "Ring", "Pinky"};
	static const char* kHandLabels[2] = {"Left", "Right"};

	ImGui::BeginGroupPanel("Finger smoothing", panel_size);

	ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
	ImGui::TextWrapped("Valve Index controllers only. Finger movement is smoothed before it reaches the game.");
	ImGui::PopStyleColor();

	bool dirty = false;

	int strength = CalCtx.fingerSmoothing.strength;
	ImGui::SliderInt("Strength##fingers", &strength, 0, 100, strength > 0 ? "%d%%" : "off", ImGuiSliderFlags_AlwaysClamp);
	dirty |= ImGui::IsItemDeactivatedAfterEdit();
	SmoothingTooltip("0 = no smoothing (each frame snaps to the incoming bones).\n"
	                 "50 = moderate, a good starting point.\n"
	                 "100 = heaviest smoothing. Fingers lag noticeably but never fully freeze.\n"
	                 "Applied to every enabled finger below; per-finger values override it.");
	CalCtx.fingerSmoothing.strength = (uint8_t)strength;

	ImGui::BeginDisabled(CalCtx.fingerSmoothing.strength == 0);

	if (ImGui::BeginTable("fingers_grid", 6, ImGuiTableFlags_SizingStretchProp)) {
		ImGui::TableSetupColumn("Hand");
		for (int f = 0; f < 5; ++f) {
			ImGui::TableSetupColumn(kFingerLabels[f]);
		}
		ImGui::TableHeadersRow();
		for (int hand = 0; hand < 2; ++hand) {
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted(kHandLabels[hand]);
			for (int f = 0; f < 5; ++f) {
				ImGui::TableSetColumnIndex(f + 1);
				const int bit = protocol::FingerBit(hand, f);
				bool enabled = ((CalCtx.fingerSmoothing.fingerMask >> bit) & 1u) != 0;
				ImGui::PushID(bit);
				if (ImGui::Checkbox("##finger", &enabled)) {
					if (enabled) {
						CalCtx.fingerSmoothing.fingerMask |= (uint16_t)(1u << bit);
					}
					else {
						CalCtx.fingerSmoothing.fingerMask &= (uint16_t)~(1u << bit);
					}
					dirty = true;
				}
				ImGui::PopID();
			}
		}
		ImGui::EndTable();
	}

	if (ImGui::Button("Enable all fingers")) {
		CalCtx.fingerSmoothing.fingerMask = protocol::kAllFingersMask;
		dirty = true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Disable all fingers")) {
		CalCtx.fingerSmoothing.fingerMask = 0;
		dirty = true;
	}

	if (ImGui::BeginTable("fingers_strength_grid", 6, ImGuiTableFlags_SizingStretchProp)) {
		ImGui::TableSetupColumn("Hand");
		for (int f = 0; f < 5; ++f) {
			ImGui::TableSetupColumn(kFingerLabels[f]);
		}
		ImGui::TableHeadersRow();
		for (int hand = 0; hand < 2; ++hand) {
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted(kHandLabels[hand]);
			for (int f = 0; f < 5; ++f) {
				ImGui::TableSetColumnIndex(f + 1);
				const int bit = protocol::FingerBit(hand, f);
				const bool fingerEnabled = ((CalCtx.fingerSmoothing.fingerMask >> bit) & 1u) != 0;
				int value = CalCtx.fingerSmoothing.perFinger[bit];
				ImGui::PushID(bit);
				ImGui::SetNextItemWidth(-FLT_MIN);
				ImGui::BeginDisabled(!fingerEnabled);
				if (ImGui::SliderInt("##perfinger", &value, 0, 100, value > 0 ? "%d" : "global")) {
					CalCtx.fingerSmoothing.perFinger[bit] = (uint8_t)value;
				}
				dirty |= ImGui::IsItemDeactivatedAfterEdit();
				ImGui::EndDisabled();
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
					ImGui::SetTooltip("%s %s\n0 = use the global strength (%d).", kHandLabels[hand], kFingerLabels[f],
					                  (int)CalCtx.fingerSmoothing.strength);
				}
				ImGui::PopID();
			}
		}
		ImGui::EndTable();
	}

	ImGui::EndDisabled();

	if (dirty) {
		ApplySmoothingSettings();
	}

	ImGui::EndGroupPanel();
}
