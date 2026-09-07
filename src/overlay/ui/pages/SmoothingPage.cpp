#include "stdafx.h"
#include "ui/UiCommon.h"
#include "Calibration.h"
#include "PoseFilter.h"

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
	bool changed = ImGui::SliderInt("Smoothing", &strength, 0, 100, strength > 0 ? "%d%%" : "off", ImGuiSliderFlags_AlwaysClamp);
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
			ImGui::TextDisabled("No smoothed devices yet. Smoothing follows the calibrated target space, so a profile must be active.");
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
