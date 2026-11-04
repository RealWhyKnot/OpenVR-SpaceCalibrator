#include "stdafx.h"
#include "ui/UiCommon.h"
#include "ui/Widgets.h"
#include "Calibration.h"
#include "PoseHealth.h"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include "imgui_extensions.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace {

	constexpr int kVisibleRows = 8;
	constexpr int kTooltipChars = 200;
	constexpr double kRadToDeg = 57.295779513082321;
	constexpr double kFixedLimit = 100000.0;

	struct Column
	{
		const char* name;
		const char* tip;
		const char* widest;
	};

	constexpr Column kColumns[] = {
	    {"Device", nullptr, nullptr},
	    {"Tracking",
		 "The driver's tracking state. A green cell means calibration uses this device's samples, amber means it skips them and red "
		 "means it counts the device as lost.",
		 "Not connected"},
	    {"Time offset",
		 "When the pose was measured, in milliseconds, relative to when the driver sent it. Negative means it was already that old.",
		 "-999.9"},
	    {"Speed", "How fast the driver says the device is moving, in metres per second.", "99.99"},
	    {"Turn", "How fast the driver says the device is turning, in degrees per second.", "9999"},
	    {"World offset",
		 "The driver's own offset from its tracking space to the SteamVR world: how far it moves poses, in metres, and how far it turns "
		 "them, in degrees.",
		 "99.99 m, 180 deg"},
	};
	constexpr int kColumnCount = (int)(sizeof(kColumns) / sizeof(kColumns[0]));

	const char* const kTrackingNames[] = {
	    "OK",           "Out of range", "Calibrating", "Calibrating, out of range", "Rotation only", "Uninitialized", "Not connected",
	    "Pose invalid", "Not finite"};

	const char* Number(char* out, size_t size, double value, int decimals)
	{
		if (std::isnan(value)) {
			snprintf(out, size, "nan");
		}
		else if (std::isinf(value)) {
			snprintf(out, size, value > 0 ? "inf" : "-inf");
		}
		else if (std::fabs(value) < kFixedLimit) {
			snprintf(out, size, "%.*f", decimals, value);
		}
		else {
			snprintf(out, size, "%.1e", value);
		}
		return out;
	}

	double RotationDegrees(const vr::HmdQuaternion_t& q)
	{
		const double norm = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
		if (!std::isfinite(norm) || norm <= 0.0) return std::nan("");
		return 2.0 * std::acos(std::clamp(std::fabs(q.w) / norm, 0.0, 1.0)) * kRadToDeg;
	}

	const char* TrackingText(const vr::DriverPose_t& pose, char* out, size_t size)
	{
		if (spacecal::ClassifySamplePose(pose) == spacecal::SampleVerdict::Use) return kTrackingNames[0];
		if (!pose.deviceIsConnected) return kTrackingNames[6];
		if (pose.result == vr::TrackingResult_Running_OK) return pose.poseIsValid ? kTrackingNames[8] : kTrackingNames[7];
		switch (pose.result) {
			case vr::TrackingResult_Running_OutOfRange: return kTrackingNames[1];
			case vr::TrackingResult_Calibrating_InProgress: return kTrackingNames[2];
			case vr::TrackingResult_Calibrating_OutOfRange: return kTrackingNames[3];
			case vr::TrackingResult_Fallback_RotationOnly: return kTrackingNames[4];
			case vr::TrackingResult_Uninitialized: return kTrackingNames[5];
			default: snprintf(out, size, "Code %d", (int)pose.result); return out;
		}
	}

	ui::NoteTone TrackingTone(spacecal::SampleVerdict verdict)
	{
		switch (verdict) {
			case spacecal::SampleVerdict::Use: return ui::NoteTone::Ok;
			case spacecal::SampleVerdict::Skip: return ui::NoteTone::Pending;
			default: return ui::NoteTone::Error;
		}
	}

	const char* VerdictText(spacecal::SampleVerdict verdict)
	{
		switch (verdict) {
			case spacecal::SampleVerdict::Use: return "Calibration uses its samples.";
			case spacecal::SampleVerdict::Skip: return "Calibration skips its samples.";
			default: return "Calibration counts it as lost.";
		}
	}

	const char* YesNo(bool value)
	{
		return value ? "yes" : "no";
	}

	float ColumnWidth(const Column& column)
	{
		return std::max(ImGui::CalcTextSize(column.name).x, ImGui::CalcTextSize(column.widest).x);
	}

	int CappedLength(const std::string& text)
	{
		return (int)std::min(text.size(), (size_t)kTooltipChars);
	}

	const char* CapSuffix(const std::string& text)
	{
		return text.size() > (size_t)kTooltipChars ? "..." : "";
	}

	void FormattedTooltip(const char* format, ...)
	{
		if (!ImGui::IsItemHovered()) return;
		char text[640];
		va_list args;
		va_start(args, format);
		vsnprintf(text, sizeof text, format, args);
		va_end(args);
		ui::Tooltip(text);
	}

	void VectorTooltip(const double (&value)[3], double scale, const char* unit)
	{
		if (!ImGui::IsItemHovered()) return;
		char x[32], y[32], z[32];
		FormattedTooltip("x %s, y %s, z %s %s", Number(x, sizeof x, value[0] * scale, 3), Number(y, sizeof y, value[1] * scale, 3),
		                 Number(z, sizeof z, value[2] * scale, 3), unit);
	}

	void EllipsizedText(const char* text)
	{
		const ImVec2 pos = ImGui::GetCursorScreenPos();
		const ImVec2 size(ImGui::GetContentRegionAvail().x, ImGui::GetTextLineHeight());
		ImGui::Dummy(size);
		if (!ImGui::IsItemVisible()) return;
		const ImVec2 max(pos.x + size.x, pos.y + size.y);
		const ImVec2 textSize = ImGui::CalcTextSize(text);
		ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), pos, max, max.x, max.x, text, nullptr, &textSize);
	}

	void DrawDeviceRow(const VRDevice& device, const vr::DriverPose_t& pose)
	{
		char label[256];
		if (device.model.empty()) {
			snprintf(label, sizeof label, "%s", device.serial.c_str());
		}
		else {
			snprintf(label, sizeof label, "%s | %s", device.model.c_str(), device.serial.c_str());
		}
		char text[64], other[64], cell[160];

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		EllipsizedText(label);
		FormattedTooltip("%.*s%s\n%.*s%s\nDevice %d", CappedLength(device.model), device.model.c_str(), CapSuffix(device.model),
		                 CappedLength(device.serial), device.serial.c_str(), CapSuffix(device.serial), device.id);

		const spacecal::SampleVerdict verdict = spacecal::ClassifySamplePose(pose);
		ImGui::TableSetColumnIndex(1);
		ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(ui::NoteTint(TrackingTone(verdict))));
		const char* tracking = TrackingText(pose, text, sizeof text);
		EllipsizedText(tracking);
		FormattedTooltip("%s\n%s\nPose valid: %s, connected: %s, drifts in yaw: %s\nResult code %d", tracking, VerdictText(verdict),
		                 YesNo(pose.poseIsValid), YesNo(pose.deviceIsConnected), YesNo(pose.willDriftInYaw), (int)pose.result);

		ImGui::TableSetColumnIndex(2);
		EllipsizedText(Number(text, sizeof text, pose.poseTimeOffset * 1000.0, 1));
		FormattedTooltip("%s ms", Number(other, sizeof other, pose.poseTimeOffset * 1000.0, 3));

		ImGui::TableSetColumnIndex(3);
		const double speed = std::hypot(pose.vecVelocity[0], pose.vecVelocity[1], pose.vecVelocity[2]);
		EllipsizedText(Number(text, sizeof text, speed, 2));
		VectorTooltip(pose.vecVelocity, 1.0, "m/s");

		ImGui::TableSetColumnIndex(4);
		const double turn = std::hypot(pose.vecAngularVelocity[0], pose.vecAngularVelocity[1], pose.vecAngularVelocity[2]) * kRadToDeg;
		EllipsizedText(Number(text, sizeof text, turn, 0));
		VectorTooltip(pose.vecAngularVelocity, kRadToDeg, "deg/s");

		ImGui::TableSetColumnIndex(5);
		const double* t = pose.vecWorldFromDriverTranslation;
		const vr::HmdQuaternion_t& q = pose.qWorldFromDriverRotation;
		snprintf(cell, sizeof cell, "%s m, %s deg", Number(text, sizeof text, std::hypot(t[0], t[1], t[2]), 2),
		         Number(other, sizeof other, RotationDegrees(q), 0));
		EllipsizedText(cell);
		if (ImGui::IsItemHovered()) {
			char x[32], y[32], z[32], w[32], qx[32], qy[32], qz[32];
			FormattedTooltip("Moves poses by x %s, y %s, z %s m\nTurns them by w %s, x %s, y %s, z %s", Number(x, sizeof x, t[0], 3),
			                 Number(y, sizeof y, t[1], 3), Number(z, sizeof z, t[2], 3), Number(w, sizeof w, q.w, 4),
			                 Number(qx, sizeof qx, q.x, 4), Number(qy, sizeof qy, q.y, 4), Number(qz, sizeof qz, q.z, 4));
		}
	}

}

void DrawRawPosePanel(ImVec2 panel_size)
{
	ImGui::BeginGroupPanel("Device poses", panel_size);
	const bool open = ImGui::TreeNodeEx("Show raw poses", ImGuiTreeNodeFlags_SpanAvailWidth);
	ui::Tooltip("What each device's driver reports, before smoothing and before this app's offset. Use it to see why calibration "
	            "skips a tracker.");
	if (open) {
		const VRState& state = LoadVRState();
		int rows = 0;
		for (const VRDevice& device : state.devices) {
			if (device.id >= 0 && device.id < (int)vr::k_unMaxTrackedDeviceCount) ++rows;
		}
		if (rows == 0) {
			ImGui::TextDisabled("No devices yet.");
		}
		else {
			const float rowHeight = ImGui::GetTextLineHeight() + ImGui::GetStyle().CellPadding.y * 2.0f;
			ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchProp;
			ImVec2 outer(0.0f, 0.0f);
			if (rows > kVisibleRows) {
				flags |= ImGuiTableFlags_ScrollY;
				outer.y = rowHeight * (kVisibleRows + 1);
			}
			if (ImGui::BeginTable("RawPoses", kColumnCount, flags, outer)) {
				ImGui::TableSetupScrollFreeze(0, 1);
				ImGui::TableSetupColumn(kColumns[0].name, ImGuiTableColumnFlags_WidthStretch, 1.0f);
				for (int column = 1; column < kColumnCount; ++column)
					ImGui::TableSetupColumn(kColumns[column].name, ImGuiTableColumnFlags_WidthFixed, ColumnWidth(kColumns[column]));
				ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
				for (int column = 0; column < kColumnCount; ++column) {
					ImGui::TableSetColumnIndex(column);
					ImGui::TableHeader(kColumns[column].name);
					if (kColumns[column].tip) ui::Tooltip(kColumns[column].tip);
				}
				for (const VRDevice& device : state.devices) {
					if (device.id < 0 || device.id >= (int)vr::k_unMaxTrackedDeviceCount) continue;
					DrawDeviceRow(device, CalCtx.devicePoses[device.id]);
				}
				ImGui::EndTable();
			}
		}
		ImGui::TreePop();
	}
	ImGui::EndGroupPanel();
}
