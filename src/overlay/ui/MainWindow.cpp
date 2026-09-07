#include "stdafx.h"
#include "UserInterface.h"
#include "UiCommon.h"
#include "Sidebar.h"
#include "Style.h"
#include "Widgets.h"
#include "Calibration.h"
#include "CalibrationMetrics.h"
#include "Updater.h"
#include "Version.h"
#include "VRSession.h"

#include <imgui/imgui.h>
#include "imgui_extensions.h"

static bool runningInOverlay;

static void DrawCalibrationPage()
{
	bool continuousCalibration = CalCtx.state == CalibrationState::Continuous || CalCtx.state == CalibrationState::ContinuousStandby;

	if (VRSess.state != VRConnectionState::Connected) {
		DrawSteamVRWarning();
	}

	if (continuousCalibration) {
		BuildContinuousCalDisplay();
	}
	else {
		auto state = LoadVRState();

		ImGui::BeginDisabled(CalCtx.state == CalibrationState::Continuous);
		BuildSystemSelection(state);
		BuildDeviceSelections(state);
		ImGui::EndDisabled();
		BuildMenu(runningInOverlay);
	}
}

static void DrawSmoothingPage()
{
	ui::TextHeading("Smoothing");
	ImVec2 panel_size{ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x, 0};
	DrawSmoothingPanel(panel_size);
}

void BuildMainWindow(bool runningInOverlay_)
{
	runningInOverlay = runningInOverlay_;
	UpdaterCtx.Poll();

	auto& io = ImGui::GetIO();

	ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
	ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	bool open = ImGui::Begin("SpaceCalibrator", nullptr, bareWindowFlags);
	ImGui::PopStyleVar();
	if (!open) {
		ImGui::End();
		return;
	}

	ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImGui::GetStyleColorVec4(ImGuiCol_Button));

	ui::DrawSidebar();
	ImGui::SameLine(0.0f, 0.0f);

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 14.0f));
	ImGui::BeginChild("content", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing()), ImGuiChildFlags_AlwaysUseWindowPadding);
	ImGui::PopStyleVar();

	switch (ui::CurrentPage()) {
		case ui::Page::Calibration: DrawCalibrationPage(); break;
		case ui::Page::Smoothing: DrawSmoothingPage(); break;
		case ui::Page::Settings: CCal_DrawSettings(); break;
		case ui::Page::Learn: DrawLearnPage(); break;
		case ui::Page::About: DrawAboutPage(); break;
	}

	DrawUpdatePrompt();

	ImGui::EndChild();

	ShowVersionLine();

	ImGui::PopStyleColor();
	ImGui::End();
}

void ShowVersionLine()
{
	ImGui::SetNextWindowPos(ImVec2(10.0f, ImGui::GetWindowHeight() - ImGui::GetFrameHeightWithSpacing()));
	if (!ImGui::BeginChild("bottom line", ImVec2(ImGui::GetWindowWidth() - 20.0f, ImGui::GetFrameHeightWithSpacing() * 2),
	                       ImGuiChildFlags_None)) {
		ImGui::EndChild();
		return;
	}
	ImGui::Text("Space Calibrator v" SPACECAL_VERSION_STRING " (" SPACECAL_CHANNEL ")");
	if (runningInOverlay) {
		ImGui::SameLine();
		ImGui::Text("- close VR overlay to use mouse");
	}
	std::string updateStatus = UpdaterCtx.StatusText();
	if (!updateStatus.empty()) {
		ImGui::SameLine();
		ImGui::TextDisabled("- %s", updateStatus.c_str());
	}
	ImGui::EndChild();
}

void BuildContinuousCalDisplay()
{
	if (ImGui::BeginTabBar("CCalTabs", 0)) {
		if (ImGui::BeginTabItem("Status")) {
			CCal_BasicInfo();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("More Graphs")) {
			ShowCalibrationDebug(2, 3);
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}
}
