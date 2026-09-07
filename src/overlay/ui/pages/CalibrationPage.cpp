#include "stdafx.h"
#include "ui/UiCommon.h"
#include "AutoDetectController.h"
#include "Calibration.h"
#include "CalibrationMetrics.h"
#include "Configuration.h"
#include "VRSession.h"

#include <imgui/imgui.h>
#include "imgui_extensions.h"

void DrawSteamVRWarning()
{
	ImVec2 panel_size{ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x, 0};

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

void CCal_BasicInfo()
{
	if (ImGui::BeginTable("DeviceInfo", 2, 0)) {
		ImGui::TableSetupColumn("Reference device");
		ImGui::TableSetupColumn("Target device");
		ImGui::TableHeadersRow();

		const char* refTrackingSystem = GetPrettyTrackingSystemName(CalCtx.referenceStandby.trackingSystem);
		const char* targetTrackingSystem = GetPrettyTrackingSystemName(CalCtx.targetStandby.trackingSystem);

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::BeginGroup();
		ImGui::Text("%s / %s / %s", refTrackingSystem, CalCtx.referenceStandby.model.c_str(), CalCtx.referenceStandby.serial.c_str());
		const char* status;
		if (CalCtx.referenceID < 0) {
			ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, 0xFF000080);
			status = "NOT FOUND";
		}
		else if (!CalCtx.ReferencePoseIsValidSimple()) {
			ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, 0xFFFF0080);
			status = "NOT TRACKING";
		}
		else {
			status = "OK";
		}
		ImGui::Text("Status: %s", status);
		ImGui::EndGroup();

		ImGui::TableSetColumnIndex(1);
		ImGui::BeginGroup();
		ImGui::Text("%s / %s / %s", targetTrackingSystem, CalCtx.targetStandby.model.c_str(), CalCtx.targetStandby.serial.c_str());
		if (CalCtx.targetID < 0) {
			ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, 0xFF000080);
			status = "NOT FOUND";
		}
		else if (!CalCtx.TargetPoseIsValidSimple()) {
			ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, 0xFFFF0080);
			status = "NOT TRACKING";
		}
		else {
			status = "OK";
		}
		ImGui::Text("Status: %s", status);
		ImGui::EndGroup();

		ImGui::EndTable();
	}

	float width = ImGui::GetWindowContentRegionWidth(), scale = 1.0f;

	if (ImGui::BeginTable("##CCal_Cancel", Metrics::enableLogs ? 3 : 2, 0, ImVec2(width * scale, ImGui::GetTextLineHeight() * 2))) {
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		if (ImGui::Button("Cancel Continuous Calibration", ImVec2(-FLT_MIN, 0.0f))) {
			EndContinuousCalibration();
		}

		ImGui::TableSetColumnIndex(1);
		if (ImGui::Button("Debug: Force break calibration", ImVec2(-FLT_MIN, 0.0f))) {
			DebugApplyRandomOffset();
		}

		if (Metrics::enableLogs) {
			ImGui::TableSetColumnIndex(2);
			if (ImGui::Button("Debug: Mark logs", ImVec2(-FLT_MIN, 0.0f))) {
				Metrics::WriteLogAnnotation("MARK LOGS");
			}
		}

		ImGui::EndTable();
	}

	ImGui::Checkbox("Hide tracker", &CalCtx.quashTargetInContinuous);
	ImGui::SameLine();
	ImGui::Checkbox("Static recalibration", &CalCtx.enableStaticRecalibration);
	ImGui::SameLine();
	ImGui::Checkbox("Enable debug logs", &Metrics::enableLogs);
	ImGui::Checkbox("Lock relative transform", &CalCtx.lockRelativePosition);
	ImGui::SameLine();
	ImGui::Checkbox("Require triggers", &CalCtx.requireTriggerPressToApply);
	ImGui::SameLine();
	ImGui::Checkbox("Ignore outliers", &CalCtx.ignoreOutliers);

	ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 1));

	for (const auto& msg : CalCtx.messages) {
		if (msg.type == CalibrationContext::Message::String) {
			ImGui::TextWrapped("> %s", msg.str.c_str());
		}
	}

	ImGui::PopStyleColor();

	ShowCalibrationDebug(1, 3);
}

void BuildMenu(bool runningInOverlay)
{
	auto& io = ImGui::GetIO();
	ImGuiStyle& style = ImGui::GetStyle();
	ImGui::Text("");

	if (CalCtx.state == CalibrationState::None) {
		if (CalCtx.validProfile && !CalCtx.enabled) {
			ImGui::TextColored(ImVec4(0.8f, 0.2f, 0.2f, 1), "Reference (%s) HMD not detected, profile disabled",
			                   GetPrettyTrackingSystemName(CalCtx.referenceTrackingSystem));
			ImGui::Text("");
		}

		float width = ImGui::GetWindowContentRegionWidth(), scale = 1.0f;
		if (CalCtx.validProfile) {
			width -= style.FramePadding.x * 4.0f;
			scale = 1.0f / 4.0f;
		}

		ImGui::BeginDisabled(VRSess.state != VRConnectionState::Connected);

		if (ImGui::Button("Start Calibration", ImVec2(width * scale, ImGui::GetTextLineHeight() * 2))) {
			ImGui::OpenPopup("Calibration Progress");
			StartCalibration();
		}

		ImGui::SameLine();
		if (ImGui::Button("Continuous Calibration", ImVec2(width * scale, ImGui::GetTextLineHeight() * 2))) {
			StartContinuousCalibration();
		}

		if (CalCtx.validProfile) {
			ImGui::SameLine();
			if (ImGui::Button("Edit Calibration", ImVec2(width * scale, ImGui::GetTextLineHeight() * 2))) {
				CalCtx.state = CalibrationState::Editing;
			}

			ImGui::SameLine();
			if (ImGui::Button("Clear Calibration", ImVec2(width * scale, ImGui::GetTextLineHeight() * 2))) {
				CalCtx.Clear();
				SaveProfile(CalCtx);
			}
		}

		width = ImGui::GetWindowContentRegionWidth();
		scale = 1.0f;
		if (CalCtx.chaperone.valid) {
			width -= style.FramePadding.x * 2.0f;
			scale = 0.5;
		}

		ImGui::Text("");
		if (ImGui::Button("Copy Chaperone Bounds to profile", ImVec2(width * scale, ImGui::GetTextLineHeight() * 2))) {
			LoadChaperoneBounds();
			SaveProfile(CalCtx);
		}

		if (CalCtx.chaperone.valid) {
			ImGui::SameLine();
			if (ImGui::Button("Paste Chaperone Bounds", ImVec2(width * scale, ImGui::GetTextLineHeight() * 2))) {
				ApplyChaperoneBounds();
			}

			if (ImGui::Checkbox(" Paste Chaperone Bounds automatically when geometry resets", &CalCtx.chaperone.autoApply)) {
				SaveProfile(CalCtx);
			}
		}

		ImGui::EndDisabled();

		ImGui::Text("");
		auto speed = CalCtx.calibrationSpeed;

		ImGui::Columns(4, nullptr, false);
		ImGui::Text("Calibration Speed");

		ImGui::NextColumn();
		if (ImGui::RadioButton(" Fast          ", speed == CalibrationContext::FAST)) CalCtx.calibrationSpeed = CalibrationContext::FAST;

		ImGui::NextColumn();
		if (ImGui::RadioButton(" Slow          ", speed == CalibrationContext::SLOW)) CalCtx.calibrationSpeed = CalibrationContext::SLOW;

		ImGui::NextColumn();
		if (ImGui::RadioButton(" Very Slow     ", speed == CalibrationContext::VERY_SLOW))
			CalCtx.calibrationSpeed = CalibrationContext::VERY_SLOW;

		ImGui::Columns(1);

		ImGui::Text("");
		DrawSmoothingPanel(ImVec2(ImGui::GetWindowContentRegionWidth(), 0));
	}
	else if (CalCtx.state == CalibrationState::Editing) {
		BuildProfileEditor();

		if (ImGui::Button("Save Profile", ImVec2(ImGui::GetWindowContentRegionWidth(), ImGui::GetTextLineHeight() * 2))) {
			SaveProfile(CalCtx);
			CalCtx.state = CalibrationState::None;
		}
	}
	else {
		ImGui::Button("Calibration in progress...", ImVec2(ImGui::GetWindowContentRegionWidth(), ImGui::GetTextLineHeight() * 2));
	}

	ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x - 40.0f, io.DisplaySize.y - 40.0f), ImGuiCond_Always);
	if (ImGui::BeginPopupModal("Calibration Progress", nullptr, bareWindowFlags)) {
		ImGui::PushStyleColor(ImGuiCol_FrameBg, (ImVec4)ImVec4(0, 0, 0, 1));
		for (auto& message : CalCtx.messages) {
			switch (message.type) {
				case CalibrationContext::Message::String: ImGui::TextWrapped(message.str.c_str()); break;
				case CalibrationContext::Message::Progress:
					float fraction = (float)message.progress / (float)message.target;
					ImGui::Text("");
					ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f), "");
					ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetFontSize() - style.FramePadding.y * 2);
					ImGui::Text(" %d%%", (int)(fraction * 100));
					break;
			}
		}
		ImGui::PopStyleColor();

		if (CalCtx.state == CalibrationState::None) {
			ImGui::Text("");
			if (ImGui::Button("Close", ImVec2(ImGui::GetWindowContentRegionWidth(), ImGui::GetTextLineHeight() * 2)))
				ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}
}
