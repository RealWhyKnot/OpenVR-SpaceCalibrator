#include "stdafx.h"
#include "ui/UiCommon.h"
#include "Calibration.h"

#include <imgui/imgui.h>
#include "imgui_extensions.h"

#include <string>

static void ScaledDragFloat(const char* label, double& f, double scale, double min, double max, int flags = ImGuiSliderFlags_AlwaysClamp)
{
	float v = (float)(f * scale);
	std::string labelStr = std::string(label);

	if (labelStr.size() > 2 && labelStr[0] == '#' && labelStr[1] == '#') {
		ImGui::SliderFloat(label, &v, (float)min, (float)max, "%1.2f", flags);
	}
	else {
		size_t visible = labelStr.find("##");
		ImGui::TextUnformatted(label, visible == std::string::npos ? nullptr : label + visible);
		ImGui::SameLine();
		ImGui::PushID((std::string(label) + "_id").c_str());
		constexpr uint32_t LABEL_CURSOR = 100;
		uint32_t cursorPosX = (int)ImGui::GetCursorPosX();
		uint32_t roundedPosition = ((cursorPosX + LABEL_CURSOR / 2) / LABEL_CURSOR) * LABEL_CURSOR;
		ImGui::SetCursorPosX((float)roundedPosition);
		ImGui::SliderFloat((std::string("##") + label).c_str(), &v, (float)min, (float)max, "%1.2f", flags);
		ImGui::PopID();
	}

	f = v / scale;
}

static void DrawVectorElement(const std::string id, const char* text, double* value, int defaultValue = 0,
                              const char* defaultValueStr = " 0 ")
{
	constexpr float CONTINUOUS_CALIBRATION_TRACKER_OFFSET_DELTA = 0.01f;

	ImGui::Text(text);

	ImGui::SameLine();

	ImGui::PushID((id + text + "_btn_reset").c_str());
	if (ImGui::Button(defaultValueStr)) {
		*value *= defaultValue;
	}
	ImGui::PopID();
	ImGui::SameLine();
	if (ImGui::ArrowButton((id + text + "_decrease").c_str(), ImGuiDir_Down)) {
		*value -= CONTINUOUS_CALIBRATION_TRACKER_OFFSET_DELTA;
	}
	ImGui::SameLine();
	ImGui::PushItemWidth(100);
	ImGui::PushID((id + text + "_text_field").c_str());
	ImGui::InputDouble("##label", value, 0, 0, "%.2f");
	ImGui::PopID();
	ImGui::PopItemWidth();
	ImGui::SameLine();
	if (ImGui::ArrowButton((id + text + "_increase").c_str(), ImGuiDir_Up)) {
		*value += CONTINUOUS_CALIBRATION_TRACKER_OFFSET_DELTA;
	}
}

void CCal_DrawSettings()
{
	ImVec2 panel_size{ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x, 0};

	ImGui::BeginGroupPanel("Tip", panel_size);
	ImGui::Text("Hover over settings to learn more about them!");
	ImGui::EndGroupPanel();

	{
		ImGui::BeginGroupPanel("Calibration speeds", panel_size);

		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		ImGui::TextWrapped(
		    "SpaceCalibrator uses up to three different speeds at which it drags the calibration back into "
		    "position when drift occurs. These settings control how far off the calibration should be before going back to low speed (for "
		    "Decel) or going to higher speeds (for Slow and Fast).");
		ImGui::PopStyleColor();

		{
			ImGui::BeginGroupPanel("Calibration speed", panel_size);

			auto speed = CalCtx.calibrationSpeed;

			ImGui::Columns(3, nullptr, false);
			if (ImGui::RadioButton(" Fast          ", speed == CalibrationContext::FAST)) {
				CalCtx.calibrationSpeed = CalibrationContext::FAST;
			}
			ImGui::NextColumn();
			if (ImGui::RadioButton(" Slow          ", speed == CalibrationContext::SLOW)) {
				CalCtx.calibrationSpeed = CalibrationContext::SLOW;
			}
			ImGui::NextColumn();
			if (ImGui::RadioButton(" Very Slow     ", speed == CalibrationContext::VERY_SLOW)) {
				CalCtx.calibrationSpeed = CalibrationContext::VERY_SLOW;
			}
			ImGui::Columns(1);

			ImGui::EndGroupPanel();
		}

		if (ImGui::BeginTable("SpeedThresholds", 3, 0)) {
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("Translation (mm)");
			ImGui::TableSetColumnIndex(2);
			ImGui::Text("Rotation (degrees)");

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("Decel");
			ImGui::TableSetColumnIndex(1);
			ScaledDragFloat("##TransDecel", CalCtx.alignmentSpeedParams.thr_trans_tiny, 1000.0, 0, 20.0);
			ImGui::TableSetColumnIndex(2);
			ScaledDragFloat("##RotDecel", CalCtx.alignmentSpeedParams.thr_rot_tiny, 180.0 / EIGEN_PI, 0, 5.0);

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("Slow");
			ImGui::TableSetColumnIndex(1);
			ScaledDragFloat("##TransSlow", CalCtx.alignmentSpeedParams.thr_trans_small, 1000.0,
			                CalCtx.alignmentSpeedParams.thr_trans_tiny * 1000.0, 20.0);
			ImGui::TableSetColumnIndex(2);
			ScaledDragFloat("##RotSlow", CalCtx.alignmentSpeedParams.thr_rot_small, 180.0 / EIGEN_PI,
			                CalCtx.alignmentSpeedParams.thr_rot_tiny * (180.0 / EIGEN_PI), 10.0);

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("Fast");
			ImGui::TableSetColumnIndex(1);
			ScaledDragFloat("##TransFast", CalCtx.alignmentSpeedParams.thr_trans_large, 1000.0,
			                CalCtx.alignmentSpeedParams.thr_trans_small * 1000.0, 50.0);
			ImGui::TableSetColumnIndex(2);
			ScaledDragFloat("##RotFast", CalCtx.alignmentSpeedParams.thr_rot_large, 180.0 / EIGEN_PI,
			                CalCtx.alignmentSpeedParams.thr_rot_small * (180.0 / EIGEN_PI), 20.0);

			ImGui::EndTable();
		}

		ImGui::EndGroupPanel();
	}

	{
		ImGui::BeginGroupPanel("Alignment speeds", panel_size);

		ScaledDragFloat("Decel", CalCtx.alignmentSpeedParams.align_speed_tiny, 1.0, 0, 2.0, 0);
		ScaledDragFloat("Slow", CalCtx.alignmentSpeedParams.align_speed_small, 1.0, 0, 2.0, 0);
		ScaledDragFloat("Fast", CalCtx.alignmentSpeedParams.align_speed_large, 1.0, 0, 2.0, 0);

		ImGui::EndGroupPanel();
	}

	{
		ImGui::BeginGroupPanel("Continuous calibration", panel_size);
		{
			ImGui::Text("Recalibration threshold");
			ImGui::SameLine();
			ImGui::PushID("recalibration_threshold");
			ImGui::SliderFloat("##recalibration_threshold_slider", &CalCtx.continuousCalibrationThreshold, 1.01f, 10.0f, "%1.1f", 0);
			if (ImGui::IsItemHovered(0)) {
				ImGui::SetTooltip(
				    "Controls how good the calibration must be before realigning the trackers.\n"
				    "Higher values cause calibration to happen less often, and may be useful for systems with lots of tracking drift.");
			}
			ImGui::PopID();

			ImGui::Text("Max relative error threshold");
			ImGui::SameLine();
			ImGui::PushID("max_relative_error_threshold");
			ImGui::SliderFloat("##max_relative_error_threshold_slider", &CalCtx.maxRelativeErrorThreshold, 0.01f, 1.0f, "%1.1f", 0);
			if (ImGui::IsItemHovered(0)) {
				ImGui::SetTooltip("Controls the maximum acceptable relative error. If the error from the relative calibration is too poor, "
				                  "the calibration will be discarded.");
			}
			ImGui::PopID();

			ImGui::Text("Jitter threshold");
			ImGui::SameLine();
			ImGui::PushID("jtter_threshold");
			ImGui::SliderFloat("##jitter_threshold_slider", &CalCtx.jitterThreshold, 0.1f, 10.0f, "%1.1f", 0);
			if (ImGui::IsItemHovered(0)) {
				ImGui::SetTooltip("Controls how much jitter will be allowed for calibration.\n"
				                  "Higher values allow worse tracking to calibrate, but may result in poorer tracking.");
			}
			ImGui::PopID();

			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			ImGui::TextWrapped("Controls how often SpaceCalibrator synchronises playspaces.");
			ImGui::PopStyleColor();
			if (ImGui::IsItemHovered(0)) {
				ImGui::SetTooltip(
				    "Controls how good the calibration must be before realigning the trackers.\n"
				    "Higher values cause calibration to happen less often, and may be useful for system with lots of tracking drift.");
			}
		}

		{
			ImVec2 panel_size_inner{panel_size.x - 11 * 2, 0};
			ImGui::BeginGroupPanel("Tracker offset", panel_size_inner);
			DrawVectorElement("cc_tracker_offset", "X", &CalCtx.continuousCalibrationOffset.x());
			DrawVectorElement("cc_tracker_offset", "Y", &CalCtx.continuousCalibrationOffset.y());
			DrawVectorElement("cc_tracker_offset", "Z", &CalCtx.continuousCalibrationOffset.z());
			ImGui::EndGroupPanel();
		}

		{
			ImVec2 panel_size_inner{panel_size.x - 11 * 2, 0};
			ImGui::BeginGroupPanel("Playspace scale", panel_size_inner);
			DrawVectorElement("cc_playspace_scale", "PLayspace Scale", &CalCtx.calibratedScale, 1, " 1 ");
			ImGui::EndGroupPanel();
		}

		ImGui::EndGroupPanel();
	}

	DrawUpdatesPanel(panel_size);

	ImGui::NewLine();
	ImGui::Indent();
	if (ImGui::Button("Reset settings")) {
		CalCtx.ResetConfig();
	}
	ImGui::Unindent();
	ImGui::NewLine();
}

void BuildProfileEditor()
{
	ImGuiStyle& style = ImGui::GetStyle();
	float width = ImGui::GetWindowContentRegionWidth() / 3.0f - style.FramePadding.x;
	float widthF = width - style.FramePadding.x;

	TextWithWidth("YawLabel", "Yaw", width);
	ImGui::SameLine();
	TextWithWidth("PitchLabel", "Pitch", width);
	ImGui::SameLine();
	TextWithWidth("RollLabel", "Roll", width);

	ImGui::PushItemWidth(widthF);
	ImGui::InputDouble("##Yaw", &CalCtx.calibratedRotation(1), 0.1, 1.0, "%.8f");
	ImGui::SameLine();
	ImGui::InputDouble("##Pitch", &CalCtx.calibratedRotation(2), 0.1, 1.0, "%.8f");
	ImGui::SameLine();
	ImGui::InputDouble("##Roll", &CalCtx.calibratedRotation(0), 0.1, 1.0, "%.8f");

	TextWithWidth("XLabel", "X", width);
	ImGui::SameLine();
	TextWithWidth("YLabel", "Y", width);
	ImGui::SameLine();
	TextWithWidth("ZLabel", "Z", width);

	ImGui::InputDouble("##X", &CalCtx.calibratedTranslation(0), 1.0, 10.0, "%.8f");
	ImGui::SameLine();
	ImGui::InputDouble("##Y", &CalCtx.calibratedTranslation(1), 1.0, 10.0, "%.8f");
	ImGui::SameLine();
	ImGui::InputDouble("##Z", &CalCtx.calibratedTranslation(2), 1.0, 10.0, "%.8f");

	TextWithWidth("ScaleLabel", "Scale", width);

	ImGui::InputDouble("##Scale", &CalCtx.calibratedScale, 0.0001, 0.01, "%.8f");
	ImGui::PopItemWidth();
}
