#include "stdafx.h"
#include "ui/UiCommon.h"
#include "ui/Widgets.h"
#include "Calibration.h"
#include "Configuration.h"
#include "LegacyInstall.h"

#include <imgui/imgui.h>
#include "imgui_extensions.h"

#include <shellapi.h>

#include <cmath>
#include <cstring>
#include <string>

static bool AcceptValue(double& value, double candidate, bool positive)
{
	if (!std::isfinite(candidate) || (positive && candidate <= 0.0)) return false;
	value = candidate;
	return true;
}

static double SquaredToMm(double squaredMetres)
{
	return std::sqrt(squaredMetres > 0.0 ? squaredMetres : 0.0) * 1000.0;
}

static bool SquaredDistanceSlider(const char* id, double& squaredMetres, double minMm, double maxMm, const char* tooltip)
{
	float mm = (float)SquaredToMm(squaredMetres);
	if (ImGui::SliderFloat(id, &mm, (float)minMm, (float)maxMm, "%.1f mm", ImGuiSliderFlags_AlwaysClamp)) {
		const double metres = mm / 1000.0;
		AcceptValue(squaredMetres, metres * metres, false);
	}
	const bool commit = ImGui::IsItemDeactivatedAfterEdit();
	ui::Tooltip(tooltip);
	return commit;
}

static bool ScaledDragFloat(const char* label, double& f, double scale, double min, double max, int flags, const char* format,
                            const char* tooltip)
{
	float v = (float)(f * scale);
	bool changed;

	if (label[0] == '#' && label[1] == '#' && label[2] != 0) {
		changed = ImGui::SliderFloat(label, &v, (float)min, (float)max, format, flags);
	}
	else {
		ImGui::TextUnformatted(label, std::strstr(label, "##"));
		ImGui::SameLine();
		ImGui::PushID(label);
		constexpr uint32_t LABEL_CURSOR = 100;
		uint32_t cursorPosX = (int)ImGui::GetCursorPosX();
		uint32_t roundedPosition = ((cursorPosX + LABEL_CURSOR / 2) / LABEL_CURSOR) * LABEL_CURSOR;
		ImGui::SetCursorPosX((float)roundedPosition);
		changed = ImGui::SliderFloat("##slider", &v, (float)min, (float)max, format, flags);
		ImGui::PopID();
	}

	if (changed) {
		AcceptValue(f, v / scale, false);
	}
	const bool commit = ImGui::IsItemDeactivatedAfterEdit();
	ui::Tooltip(tooltip);
	return commit;
}

static bool DrawVectorElement(const char* id, const char* text, double* value, const char* format, const char* tooltip,
                              int defaultValue = 0, const char* defaultValueStr = " 0 ", bool positive = false)
{
	constexpr float CONTINUOUS_CALIBRATION_TRACKER_OFFSET_DELTA = 0.01f;
	bool commit = false;

	ImGui::TextUnformatted(text);
	ui::Tooltip(tooltip);

	ImGui::SameLine();

	ImGui::PushID(id);
	ImGui::PushID(text);
	if (ImGui::Button(defaultValueStr)) {
		*value = defaultValue;
		commit = true;
	}
	ui::Tooltip(tooltip);
	ImGui::SameLine();
	if (ImGui::ArrowButton("decrease", ImGuiDir_Down)) {
		commit |= AcceptValue(*value, *value - CONTINUOUS_CALIBRATION_TRACKER_OFFSET_DELTA, positive);
	}
	ui::Tooltip(tooltip);
	ImGui::SameLine();
	ImGui::PushItemWidth(100);
	double edited = *value;
	if (ImGui::InputDouble("##label", &edited, 0, 0, format)) {
		AcceptValue(*value, edited, positive);
	}
	commit |= ImGui::IsItemDeactivatedAfterEdit();
	ui::Tooltip(tooltip);
	ImGui::PopItemWidth();
	ImGui::SameLine();
	if (ImGui::ArrowButton("increase", ImGuiDir_Up)) {
		commit |= AcceptValue(*value, *value + CONTINUOUS_CALIBRATION_TRACKER_OFFSET_DELTA, positive);
	}
	ui::Tooltip(tooltip);
	ImGui::PopID();
	ImGui::PopID();
	return commit;
}

static void InputProfileValue(const char* label, double* value, double step, double stepFast, bool positive = false)
{
	double edited = *value;
	if (ImGui::InputDouble(label, &edited, step, stepFast, "%.8f")) {
		AcceptValue(*value, edited, positive);
	}
}

static void DrawLegacyInstallPanel(ImVec2 panel_size)
{
	static bool detected = false;
	static LegacyInstallInfo info;
	static bool queued = false;
	static std::string queueError;
	if (!detected) {
		info = DetectLegacyInstalls();
		detected = true;
	}
	if (!info.AnyRemovable() && !info.steamAppInstalled) return;

	ImGui::BeginGroupPanel("Old Space Calibrator versions", panel_size);

	if (info.AnyRemovable()) {
		if (info.runtimeDriverDirs.empty()) {
			ImGui::TextWrapped("An old Space Calibrator install is still on disk. SteamVR doesn't load it, so removing it just frees "
			                   "space.");
		}
		else {
			ImGui::TextWrapped("An old Space Calibrator driver is installed inside the SteamVR runtime. SteamVR loads it alongside this "
			                   "one, which breaks calibration.");
		}
		if (!info.uninstallExe.empty())
			ImGui::TextDisabled("Installer: %s", info.uninstallExe.c_str());
		else if (!info.programFilesDir.empty())
			ImGui::TextDisabled("Folder: %s", info.programFilesDir.c_str());
		for (const auto& dir : info.runtimeDriverDirs) {
			ImGui::TextDisabled("SteamVR driver copy: %s", dir.c_str());
		}
		if (queued) {
			ImGui::TextWrapped("Removal is queued and runs after SteamVR closes.");
		}
		else {
			if (ImGui::Button("Remove when SteamVR closes")) {
				queueError.clear();
				queued = QueueLegacyUninstall(info, queueError);
			}
			ui::Tooltip("Asks for administrator rights now, then waits in the background and removes the old install once SteamVR "
			            "has closed.");
			if (!queueError.empty()) {
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.4f, 0.4f, 1.0f));
				ImGui::TextWrapped("%s", queueError.c_str());
				ImGui::PopStyleColor();
			}
		}
	}

	if (info.steamAppInstalled) {
		ImGui::TextWrapped("The Steam release of Space Calibrator is also installed. Uninstall it through Steam so the two don't fight.");
		if (ImGui::Button("Open Steam to uninstall")) {
			ShellExecuteW(nullptr, L"open", L"steam://uninstall/3368750", nullptr, nullptr, SW_SHOWNORMAL);
		}
	}

	ImGui::EndGroupPanel();
}

void CCal_DrawSettings()
{
	ImVec2 panel_size{ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x, 0};
	bool save = false;

	DrawDriverConflictPanel();

	{
		ImGui::BeginGroupPanel("Calibration speeds", panel_size);

		{
			ImGui::BeginGroupPanel("Calibration speed", ImVec2(-FLT_MIN, 0));

			auto speed = CalCtx.calibrationSpeed;

			ImGui::Columns(3, nullptr, false);
			if (ImGui::RadioButton("Fast", speed == CalibrationContext::FAST)) {
				CalCtx.calibrationSpeed = CalibrationContext::FAST;
			}
			ui::Tooltip(ui::kCalibrationSpeedTips[0]);
			ImGui::NextColumn();
			if (ImGui::RadioButton("Slow", speed == CalibrationContext::SLOW)) {
				CalCtx.calibrationSpeed = CalibrationContext::SLOW;
			}
			ui::Tooltip(ui::kCalibrationSpeedTips[1]);
			ImGui::NextColumn();
			if (ImGui::RadioButton("Very slow", speed == CalibrationContext::VERY_SLOW)) {
				CalCtx.calibrationSpeed = CalibrationContext::VERY_SLOW;
			}
			ui::Tooltip(ui::kCalibrationSpeedTips[2]);
			ImGui::Columns(1);
			save |= CalCtx.calibrationSpeed != speed;

			ImGui::EndGroupPanel();
		}

		if (ImGui::BeginTable("SpeedThresholds", 3, 0)) {
			constexpr double kDegrees = 180.0 / EIGEN_PI;
			constexpr int kClamp = ImGuiSliderFlags_AlwaysClamp;
			const double maxSlowMm = SquaredToMm(0.020);
			const double maxFastMm = SquaredToMm(0.050);
			auto& params = CalCtx.alignmentSpeedParams;

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("Translation");
			ImGui::TableSetColumnIndex(2);
			ImGui::Text("Rotation");

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("Slow");
			ImGui::TableSetColumnIndex(1);
			save |= SquaredDistanceSlider("##TransSlow", params.thr_trans_small, 0.0, maxSlowMm,
			                              "Trackers speed up to Slow when a new calibration is more than this far from the applied one.");
			ImGui::TableSetColumnIndex(2);
			save |= ScaledDragFloat("##RotSlow", params.thr_rot_small, kDegrees, 0.0, 10.0, kClamp, "%.2f deg",
			                        "Trackers speed up to Slow when a new calibration is turned more than this many degrees from the "
			                        "applied one.");

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("Fast");
			ImGui::TableSetColumnIndex(1);
			save |= SquaredDistanceSlider("##TransFast", params.thr_trans_large, SquaredToMm(params.thr_trans_small), maxFastMm,
			                              "Trackers speed up to Fast when a new calibration is more than this far from the applied one. "
			                              "They stay fast until the gap is under both Slow thresholds.");
			ImGui::TableSetColumnIndex(2);
			save |= ScaledDragFloat("##RotFast", params.thr_rot_large, kDegrees, params.thr_rot_small * kDegrees, 20.0, kClamp, "%.2f deg",
			                        "Trackers speed up to Fast when a new calibration is turned more than this many degrees from the "
			                        "applied one. They stay fast until the gap is under both Slow thresholds.");

			ImGui::EndTable();
		}

		ImGui::EndGroupPanel();
	}

	{
		ImGui::BeginGroupPanel("Alignment speeds", panel_size);

		save |= ScaledDragFloat("Decel", CalCtx.alignmentSpeedParams.align_speed_tiny, 1.0, 0, 2.0, 0, "%1.2f",
		                        "During continuous calibration, how fast trackers glide to a new calibration while the gap is under "
		                        "both Slow thresholds. 1.0 closes about 63% of the gap per second. 0 stops them moving.");
		save |= ScaledDragFloat("Slow", CalCtx.alignmentSpeedParams.align_speed_small, 1.0, 0, 2.0, 0, "%1.2f",
		                        "During continuous calibration, how fast trackers glide to a new calibration after the gap passes a "
		                        "Slow threshold. 1.0 closes about 63% of the gap per second.");
		save |= ScaledDragFloat("Fast", CalCtx.alignmentSpeedParams.align_speed_large, 1.0, 0, 2.0, 0, "%1.2f",
		                        "During continuous calibration, how fast trackers glide to a new calibration after the gap passes a "
		                        "Fast threshold. 2.0 closes about 86% of the gap per second.");

		ImGui::EndGroupPanel();
	}

	{
		ImGui::BeginGroupPanel("Continuous calibration", panel_size);
		const float sliderLabelWidth = ImGui::CalcTextSize("Max relative error threshold").x + ImGui::GetStyle().ItemSpacing.x * 2.0f;
		{
			ImGui::Text("Recalibration threshold");
			ImGui::SameLine(sliderLabelWidth);
			ImGui::SetNextItemWidth(-FLT_MIN);
			ImGui::PushID("recalibration_threshold");
			ImGui::SliderFloat("##recalibration_threshold_slider", &CalCtx.continuousCalibrationThreshold, 1.01f, 10.0f, "%.2fx",
			                   ImGuiSliderFlags_AlwaysClamp);
			save |= ImGui::IsItemDeactivatedAfterEdit();
			ui::Tooltip("A new result replaces the current calibration only if its error is this many times lower. Higher values "
			            "recalibrate less often.");
			ImGui::PopID();

			ImGui::Text("Max relative error threshold");
			ImGui::SameLine(sliderLabelWidth);
			ImGui::SetNextItemWidth(-FLT_MIN);
			ImGui::PushID("max_relative_error_threshold");
			float maxErrorMm = CalCtx.maxRelativeErrorThreshold * 1000.0f;
			if (ImGui::SliderFloat("##max_relative_error_threshold_slider", &maxErrorMm, 1.0f, 1000.0f, "%.1f mm",
			                       ImGuiSliderFlags_AlwaysClamp)) {
				CalCtx.maxRelativeErrorThreshold = maxErrorMm / 1000.0f;
			}
			save |= ImGui::IsItemDeactivatedAfterEdit();
			ui::Tooltip("Only used with static recalibration. A static result with more error than this is thrown away.");
			ImGui::PopID();
		}

		{
			static const char* const kOffsetTip =
			    "During continuous calibration, shifts the calibrated trackers by this many metres on this axis. The arrows step 1 cm "
			    "and 0 resets it.";
			ImGui::BeginGroupPanel("Tracker offset", ImVec2(-FLT_MIN, 0));
			save |= DrawVectorElement("cc_tracker_offset", "X", &CalCtx.continuousCalibrationOffset.x(), "%.2f m", kOffsetTip);
			save |= DrawVectorElement("cc_tracker_offset", "Y", &CalCtx.continuousCalibrationOffset.y(), "%.2f m", kOffsetTip);
			save |= DrawVectorElement("cc_tracker_offset", "Z", &CalCtx.continuousCalibrationOffset.z(), "%.2f m", kOffsetTip);
			ImGui::EndGroupPanel();
		}

		{
			ImGui::BeginGroupPanel("Playspace scale", ImVec2(-FLT_MIN, 0));
			save |= DrawVectorElement("cc_playspace_scale", "Playspace scale", &CalCtx.calibratedScale, "%.2f",
			                          "Multiplies target device positions by this before the calibration is applied. 1 means no "
			                          "scaling, and the 1 button resets it.",
			                          1, " 1 ", true);
			ImGui::EndGroupPanel();
		}

		ImGui::EndGroupPanel();
	}

	DrawUpdatesPanel(panel_size);

	DrawLegacyInstallPanel(panel_size);

	ImGui::NewLine();
	ImGui::Indent();
	if (ImGui::Button("Reset settings")) {
		ImGui::OpenPopup("Reset settings?");
	}
	ui::Tooltip("Puts thresholds, alignment speeds, tracker offset, static recalibration and everything on the Smoothing page back to "
	            "defaults. Your calibration, calibration speed and playspace scale stay.");
	ImGui::Unindent();
	if (ui::ConfirmPopup("Reset settings?",
	                     "Resets thresholds, alignment speeds, tracker offset, static recalibration and the Smoothing page.",
	                     "Reset settings")) {
		CalCtx.ResetConfig();
		save = true;
	}
	ImGui::NewLine();

	if (save) {
		SaveProfile(CalCtx);
	}
}

void BuildProfileEditor()
{
	ImGuiStyle& style = ImGui::GetStyle();
	float width = ImGui::GetWindowContentRegionWidth() / 3.0f - style.FramePadding.x;
	float widthF = width - style.FramePadding.x;

	TextWithWidth("YawLabel", "Yaw (degrees)", width);
	ImGui::SameLine();
	TextWithWidth("PitchLabel", "Pitch (degrees)", width);
	ImGui::SameLine();
	TextWithWidth("RollLabel", "Roll (degrees)", width);

	ImGui::PushItemWidth(widthF);
	InputProfileValue("##Yaw", &CalCtx.calibratedRotation(1), 0.1, 1.0);
	ImGui::SameLine();
	InputProfileValue("##Pitch", &CalCtx.calibratedRotation(2), 0.1, 1.0);
	ImGui::SameLine();
	InputProfileValue("##Roll", &CalCtx.calibratedRotation(0), 0.1, 1.0);

	TextWithWidth("XLabel", "X (cm)", width);
	ImGui::SameLine();
	TextWithWidth("YLabel", "Y (cm)", width);
	ImGui::SameLine();
	TextWithWidth("ZLabel", "Z (cm)", width);

	InputProfileValue("##X", &CalCtx.calibratedTranslation(0), 1.0, 10.0);
	ImGui::SameLine();
	InputProfileValue("##Y", &CalCtx.calibratedTranslation(1), 1.0, 10.0);
	ImGui::SameLine();
	InputProfileValue("##Z", &CalCtx.calibratedTranslation(2), 1.0, 10.0);

	TextWithWidth("ScaleLabel", "Scale", width);

	InputProfileValue("##Scale", &CalCtx.calibratedScale, 0.0001, 0.01, true);
	ImGui::PopItemWidth();
}
