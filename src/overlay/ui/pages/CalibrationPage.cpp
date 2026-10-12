#include "stdafx.h"
#include "ui/UiCommon.h"
#include "ui/Widgets.h"
#include "AutoDetectController.h"
#include "Calibration.h"
#include "CalibrationMetrics.h"
#include "Configuration.h"
#include "VRSession.h"

#include <imgui/imgui.h>
#include "imgui_extensions.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <string_view>

void DrawSteamVRWarning()
{
	ImVec2 panel_size{ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x, 0};

	if (VRSess.state == VRConnectionState::Connecting && VRSess.driverNotLoaded) {
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.3f, 1.0f));
		ImGui::BeginGroupPanel("Space Calibrator driver not loaded", panel_size);
		ImGui::PopStyleColor();
		ImGui::TextWrapped("SteamVR is running, but the Space Calibrator driver isn't answering.");
		ImGui::TextWrapped("Make sure the Space Calibrator add-on is on in SteamVR settings, then restart SteamVR. If it's already on, "
		                   "reinstall Space Calibrator.");
		ImGui::TextDisabled("Space Calibrator tries again every 2 seconds.");
		ImGui::EndGroupPanel();
		return;
	}

	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.3f, 1.0f));
	ImGui::BeginGroupPanel("SteamVR not connected", panel_size);
	ImGui::PopStyleColor();
	switch (VRSess.lastInitError) {
		case vr::VRInitError_None: ImGui::TextWrapped("Connecting to SteamVR..."); break;
		case vr::VRInitError_Init_NoServerForBackgroundApp:
			ImGui::TextWrapped("SteamVR isn't running. Settings are saved and apply once it starts.");
			break;
		case vr::VRInitError_Init_HmdNotFound:
		case vr::VRInitError_Init_HmdNotFoundPresenceFailed:
			ImGui::TextWrapped("SteamVR can't find your headset. Connect it, then start SteamVR.");
			break;
		default: ImGui::TextWrapped("%s", VRSess.statusText.c_str()); break;
	}
	if (VRSess.state == VRConnectionState::Connecting && !VRSess.statusText.empty()) {
		ImGui::TextWrapped("%s", VRSess.statusText.c_str());
	}
	ImGui::TextDisabled("Space Calibrator connects automatically when SteamVR is ready.");
	ImGui::EndGroupPanel();
}

void DrawAutoDetectCard()
{
	auto& detect = AutoDetectController::Get();
	ImVec2 panel_size{ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x, 0};

	if (detect.HasResult()) {
		ImGui::BeginGroupPanel("Tracker auto-detected", panel_size);
		ImGui::TextWrapped("Selected %s as the target and %s as the reference.", detect.ResultTargetLabel().c_str(),
		                   detect.ResultReferenceLabel().c_str());
		if (ImGui::Button("Undo auto-selection")) {
			detect.Undo();
		}
		ImGui::SameLine();
		if (ImGui::Button("OK")) {
			detect.DismissResult();
		}
		ImGui::EndGroupPanel();
		return;
	}

	if (!detect.Active()) return;

	ImGui::BeginGroupPanel("Looking for your tracker", panel_size);
	ImGui::TextWrapped("No calibration profile yet, so Space Calibrator is watching for a device pair moving together. Hold "
	                   "your tracker firmly against a controller and wave them around.");
	ImGui::ProgressBar((float)detect.Progress(), ImVec2(-FLT_MIN, 0.0f));
	if (ImGui::Button("Stop watching")) {
		detect.Cancel();
	}
	ImGui::EndGroupPanel();
}

static void DeviceStatusCell(bool found, bool tracking)
{
	const char* status = "OK";
	ui::NoteTone tone = ui::NoteTone::Ok;
	if (!found) {
		status = "Not found";
		tone = ui::NoteTone::Error;
	}
	else if (!tracking) {
		status = "Not tracking";
		tone = ui::NoteTone::Pending;
	}
	ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(ui::NoteTint(tone)));
	ImGui::Text("Status: %s", status);
}

void CCal_BasicInfo()
{
	if (ImGui::BeginTable("DeviceInfo", 2, ImGuiTableFlags_PadOuterX | ImGuiTableFlags_BordersInnerV)) {
		ImGui::TableSetupColumn("Reference device");
		ImGui::TableSetupColumn("Target device");
		ImGui::TableHeadersRow();

		const char* refTrackingSystem = GetPrettyTrackingSystemName(CalCtx.referenceStandby.trackingSystem);
		const char* targetTrackingSystem = GetPrettyTrackingSystemName(CalCtx.targetStandby.trackingSystem);

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::BeginGroup();
		ImGui::Text("%s / %s / %s", refTrackingSystem, CalCtx.referenceStandby.model.c_str(), CalCtx.referenceStandby.serial.c_str());
		DeviceStatusCell(CalCtx.referenceID >= 0, CalCtx.ReferencePoseIsValidSimple());
		ImGui::EndGroup();

		ImGui::TableSetColumnIndex(1);
		ImGui::BeginGroup();
		ImGui::Text("%s / %s / %s", targetTrackingSystem, CalCtx.targetStandby.model.c_str(), CalCtx.targetStandby.serial.c_str());
		DeviceStatusCell(CalCtx.targetID >= 0, CalCtx.TargetPoseIsValidSimple());
		ImGui::EndGroup();

		ImGui::EndTable();
	}

	float width = ImGui::GetWindowContentRegionWidth(), scale = 1.0f;

	if (ImGui::BeginTable("##CCal_Cancel", Metrics::enableLogs ? 3 : 1, 0, ImVec2(width * scale, ImGui::GetTextLineHeight() * 2))) {
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		if (ImGui::Button("Cancel continuous calibration", ImVec2(-FLT_MIN, 0.0f))) {
			EndContinuousCalibration();
		}

		if (Metrics::enableLogs) {
			ImGui::TableSetColumnIndex(1);
			if (ImGui::Button("Debug: Force break calibration", ImVec2(-FLT_MIN, 0.0f))) {
				DebugApplyRandomOffset();
			}

			ImGui::TableSetColumnIndex(2);
			if (ImGui::Button("Debug: Mark logs", ImVec2(-FLT_MIN, 0.0f))) {
				Metrics::WriteLogAnnotation("MARK LOGS");
			}
		}

		ImGui::EndTable();
	}

	ImGui::Checkbox("Hide tracker", &CalCtx.quashTargetInContinuous);
	ui::Tooltip("Moves the target tracker far above the play area during continuous calibration, so games don't see it.");
	ImGui::SameLine();
	ImGui::Checkbox("Static recalibration", &CalCtx.enableStaticRecalibration);
	ui::Tooltip("Also recalibrates from the saved offset between reference and target, which works while you stand still. Uses "
	            "Recalibration threshold and Max relative error threshold from Settings.");
	ImGui::SameLine();
	ImGui::Checkbox("Enable debug logs", &Metrics::enableLogs);
	ui::Tooltip("Writes calibration data to AppData\\LocalLow\\SpaceCalibrator\\Logs and shows two debug buttons. Logs older than a "
	            "day are deleted when a new one starts.");
	ImGui::Checkbox("Lock relative transform", &CalCtx.lockRelativePosition);
	ui::Tooltip("Recalibrates from the saved offset between reference and target. The motion-based solve only runs if that fails.");
	ImGui::SameLine();
	ImGui::Checkbox("Require triggers", &CalCtx.requireTriggerPressToApply);
	ui::Tooltip("After the first result, continuous calibration only updates while you hold the trigger on every controller from the "
	            "target system.");
	ImGui::SameLine();
	ImGui::Checkbox("Ignore outliers", &CalCtx.ignoreOutliers);
	ui::Tooltip("Leaves out samples whose rotation disagrees with the rest by more than about 16 degrees when solving rotation.");

	ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 1));

	for (const auto& msg : CalCtx.messages) {
		if (msg.type == CalibrationContext::Message::String) {
			ImGui::TextWrapped("> %s", msg.str.c_str());
		}
	}

	ImGui::PopStyleColor();

	ShowCalibrationDebug(1, 3);
}

namespace {

	struct EditSnapshot
	{
		Eigen::Vector3d rotation = Eigen::Vector3d::Zero();
		Eigen::Vector3d translation = Eigen::Vector3d::Zero();
		double scale = 1.0;
	};

	EditSnapshot editSnapshot;

	std::string_view LastLogLine()
	{
		std::string_view last;
		for (const auto& message : CalCtx.messages) {
			if (message.type != CalibrationContext::Message::String) continue;
			const std::string_view text(message.str);
			size_t start = 0;
			while (start < text.size()) {
				size_t end = text.find('\n', start);
				if (end == std::string_view::npos) end = text.size();
				if (end > start) last = text.substr(start, end - start);
				start = end + 1;
			}
		}
		return last;
	}

	float LastProgressFraction()
	{
		float fraction = 0.0f;
		for (const auto& message : CalCtx.messages) {
			if (message.type == CalibrationContext::Message::Progress && message.target > 0) {
				fraction = (float)message.progress / (float)message.target;
			}
		}
		return fraction;
	}

	void DrawCalibrationProgress()
	{
		const ImGuiIO& io = ImGui::GetIO();
		const ImGuiStyle& style = ImGui::GetStyle();
		ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(std::min(680.0f, io.DisplaySize.x - 40.0f), 0.0f), ImGuiCond_Always);
		ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0.0f, 0.0f, 0.0f, 0.6f));
		ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
		                               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize;
		if (ImGui::BeginPopupModal("Calibration progress", nullptr, flags)) {
			const float buttonHeight = ImGui::GetFrameHeight() * 1.3f;
			const float halfWidth = (ImGui::GetContentRegionAvail().x - style.ItemSpacing.x) * 0.5f;
			ImGui::Spacing();
			if (CalCtx.state != CalibrationState::None) {
				const std::string_view status = LastLogLine();
				ImGui::TextWrapped("Hold both devices firmly together and draw slow figure eights until the bar fills.");
				ImGui::Spacing();
				ImGui::ProgressBar(LastProgressFraction(), ImVec2(-FLT_MIN, buttonHeight));
				ImGui::TextDisabled("%.*s", (int)status.size(), status.data());
				ImGui::Spacing();
				if (ImGui::Button("Cancel", ImVec2(-FLT_MIN, buttonHeight))) {
					CancelCalibration();
					CalCtx.RestoreSelectionBeforeCommit();
					ImGui::CloseCurrentPopup();
				}
			}
			else if (CalCtx.failureReason.empty()) {
				ImGui::TextWrapped("Calibration finished and the profile is saved.");
				ImGui::Spacing();
				if (ImGui::Button("Close", ImVec2(-FLT_MIN, buttonHeight))) {
					CalCtx.KeepCommittedSelection();
					ImGui::CloseCurrentPopup();
				}
			}
			else {
				ImGui::TextUnformatted("Calibration didn't finish");
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
				ImGui::TextWrapped("%s", CalCtx.failureReason.c_str());
				ImGui::PopStyleColor();
				ImGui::Spacing();
				const bool canRetry = CalCtx.failureReason != kFailureMissingDevice;
				if (canRetry) {
					if (ImGui::Button("Try again", ImVec2(halfWidth, buttonHeight))) {
						StartCalibration();
					}
					ImGui::SameLine();
				}
				if (ImGui::Button("Close", ImVec2(canRetry ? halfWidth : -FLT_MIN, buttonHeight))) {
					CalCtx.RestoreSelectionBeforeCommit();
					ImGui::CloseCurrentPopup();
				}
			}
			ImGui::Spacing();
			ImGui::EndPopup();
		}
		ImGui::PopStyleColor(2);
	}

}

void BuildMenu(bool runningInOverlay)
{
	ImGuiStyle& style = ImGui::GetStyle();
	ImGui::Text("");

	if (CalCtx.state == CalibrationState::None) {
		if (CalCtx.validProfile && !CalCtx.enabled && VRSess.state == VRConnectionState::Connected) {
			char text[256];
			snprintf(text, sizeof text,
			         "No headset from the reference system (%s) is connected, so the profile is off. Connect that headset or "
			         "calibrate again.",
			         GetPrettyTrackingSystemName(CalCtx.referenceTrackingSystem));
			ui::Note("profile_disabled", ui::NoteTone::Error, text);
			ImGui::Text("");
		}

		const float avail = ImGui::GetContentRegionAvail().x;
		const int mainCount = CalCtx.validProfile ? 4 : 2;
		const float mainWidth = (avail - (mainCount - 1) * style.ItemSpacing.x) / mainCount;

		ui::BeginDisabledLook(VRSess.state != VRConnectionState::Connected);

		if (ImGui::Button("Start calibration", ImVec2(mainWidth, ImGui::GetTextLineHeight() * 2))) {
			CommitDeviceSelection();
			ImGui::OpenPopup("Calibration progress");
			StartCalibration();
		}

		ImGui::SameLine();
		if (ImGui::Button("Continuous calibration", ImVec2(mainWidth, ImGui::GetTextLineHeight() * 2))) {
			CommitDeviceSelection();
			StartContinuousCalibration();
		}

		if (CalCtx.validProfile) {
			ImGui::SameLine();
			if (ImGui::Button("Edit calibration", ImVec2(mainWidth, ImGui::GetTextLineHeight() * 2))) {
				editSnapshot = {CalCtx.calibratedRotation, CalCtx.calibratedTranslation, CalCtx.calibratedScale};
				CalCtx.state = CalibrationState::Editing;
			}

			ImGui::SameLine();
			if (ImGui::Button("Clear calibration", ImVec2(mainWidth, ImGui::GetTextLineHeight() * 2))) {
				ImGui::OpenPopup("Clear calibration?");
			}
		}

		const int chaperoneCount = CalCtx.chaperone.valid ? 2 : 1;
		const float chaperoneWidth = (avail - (chaperoneCount - 1) * style.ItemSpacing.x) / chaperoneCount;

		ImGui::Text("");
		if (ImGui::Button("Copy chaperone bounds to profile", ImVec2(chaperoneWidth, ImGui::GetTextLineHeight() * 2))) {
			LoadChaperoneBounds();
			SaveProfile(CalCtx);
		}

		if (CalCtx.chaperone.valid) {
			ImGui::SameLine();
			if (ImGui::Button("Paste chaperone bounds", ImVec2(chaperoneWidth, ImGui::GetTextLineHeight() * 2))) {
				ImGui::OpenPopup("Paste chaperone bounds?");
			}
		}

		ui::EndDisabledLook();

		if (CalCtx.chaperone.valid) {
			if (ImGui::Checkbox("Paste chaperone bounds automatically when geometry resets", &CalCtx.chaperone.autoApply)) {
				SaveProfile(CalCtx);
			}
		}

		if (ui::ConfirmPopup("Clear calibration?", "This deletes the saved calibration and any chaperone bounds stored with it.",
		                     "Clear calibration")) {
			CalCtx.Clear();
			SaveProfile(CalCtx);
		}
		if (ui::ConfirmPopup("Paste chaperone bounds?", "This replaces SteamVR's current boundary with the one saved in this profile.",
		                     "Paste bounds")) {
			ApplyChaperoneBounds();
		}

		ImGui::Text("");
		auto speed = CalCtx.calibrationSpeed;

		ImGui::Columns(4, nullptr, false);
		ImGui::AlignTextToFramePadding();
		ImGui::Text("Calibration speed");

		ImGui::NextColumn();
		if (ImGui::RadioButton("Fast", speed == CalibrationContext::FAST)) CalCtx.calibrationSpeed = CalibrationContext::FAST;
		ui::Tooltip(ui::kCalibrationSpeedTips[0]);

		ImGui::NextColumn();
		if (ImGui::RadioButton("Slow", speed == CalibrationContext::SLOW)) CalCtx.calibrationSpeed = CalibrationContext::SLOW;
		ui::Tooltip(ui::kCalibrationSpeedTips[1]);

		ImGui::NextColumn();
		if (ImGui::RadioButton("Very slow", speed == CalibrationContext::VERY_SLOW))
			CalCtx.calibrationSpeed = CalibrationContext::VERY_SLOW;
		ui::Tooltip(ui::kCalibrationSpeedTips[2]);

		ImGui::Columns(1);
		if (CalCtx.calibrationSpeed != speed) SaveProfile(CalCtx);

		ImGui::Text("");
		DrawSmoothingPanel(ImVec2(ImGui::GetWindowContentRegionWidth(), 0));
	}
	else if (CalCtx.state == CalibrationState::Editing) {
		BuildProfileEditor();

		const ImVec2 buttonSize((ImGui::GetContentRegionAvail().x - style.ItemSpacing.x) * 0.5f, ImGui::GetTextLineHeight() * 2);
		if (ImGui::Button("Save profile", buttonSize)) {
			SaveProfile(CalCtx);
			CalCtx.state = CalibrationState::None;
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", buttonSize)) {
			CalCtx.calibratedRotation = editSnapshot.rotation;
			CalCtx.calibratedTranslation = editSnapshot.translation;
			CalCtx.calibratedScale = editSnapshot.scale;
			CalCtx.state = CalibrationState::None;
			SaveProfile(CalCtx);
		}
		ui::Tooltip("Puts back the values from before you started editing.");
	}
	else {
		ImGui::Button("Calibration in progress...", ImVec2(ImGui::GetWindowContentRegionWidth(), ImGui::GetTextLineHeight() * 2));
	}

	DrawCalibrationProgress();
}
