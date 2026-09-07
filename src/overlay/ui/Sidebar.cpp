#include "stdafx.h"
#include "Sidebar.h"
#include "IconGlyphs.h"
#include "Widgets.h"
#include "VRSession.h"

#include <imgui/imgui.h>

#include <string>

namespace ui {

	static Page currentPage = Page::Calibration;

	Page CurrentPage()
	{
		return currentPage;
	}

	void SetPage(Page page)
	{
		currentPage = page;
	}

	struct SidebarEntry
	{
		Page page;
		const char* icon;
		const char* label;
	};

	static const SidebarEntry kEntries[] = {
	    {Page::Calibration, ICON_MI_MY_LOCATION, "Calibration"},
	    {Page::BaseStations, ICON_MI_SENSORS, "Base stations"},
	    {Page::Smoothing, ICON_MI_WAVES, "Smoothing"},
	    {Page::Settings, ICON_MI_SETTINGS, "Settings"},
	    {Page::Learn, ICON_MI_SCHOOL, "Learn"},
	    {Page::About, ICON_MI_INFO, "About"},
	};

	static void DrawStatusPill()
	{
		switch (VRSess.state) {
			case VRConnectionState::Connected: PillText(ICON_MI_CHECK " SteamVR", ImVec4(0.35f, 0.85f, 0.45f, 1.0f)); break;
			case VRConnectionState::Connecting: PillText(ICON_MI_SYNC " Connecting", ImVec4(1.0f, 0.75f, 0.3f, 1.0f)); break;
			default: PillText(ICON_MI_CLOSE " SteamVR off", ImVec4(0.9f, 0.4f, 0.4f, 1.0f)); break;
		}
	}

	void DrawSidebar()
	{
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.055f, 0.065f, 0.075f, 1.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 12.0f));
		ImGui::BeginChild("sidebar", ImVec2(220.0f, -ImGui::GetFrameHeightWithSpacing()), ImGuiChildFlags_AlwaysUseWindowPadding);
		ImGui::PopStyleVar();

		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 10.0f));
		for (const auto& entry : kEntries) {
			const bool active = currentPage == entry.page;
			if (active) {
				ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
			}
			else {
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
			}
			std::string text = std::string(entry.icon) + "  " + entry.label;
			if (ImGui::Button(text.c_str(), ImVec2(-FLT_MIN, 0.0f))) {
				currentPage = entry.page;
			}
			ImGui::PopStyleColor();
		}
		ImGui::PopStyleVar();

		ImGui::SetCursorPosY(ImGui::GetWindowHeight() - ImGui::GetFrameHeightWithSpacing() - 10.0f);
		DrawStatusPill();

		ImGui::EndChild();
		ImGui::PopStyleColor();
	}

}
