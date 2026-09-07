#pragma once

#include "VRState.h"

#include <imgui/imgui.h>
#include <string>

inline constexpr ImGuiWindowFlags bareWindowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                                    ImGuiWindowFlags_NoCollapse;

inline const char* GetPrettyTrackingSystemName(const std::string& value)
{
	// To comply with SteamVR branding guidelines (page 29), we rename devices under lighthouse tracking to SteamVR Tracking.
	if (value == "lighthouse" || value == "aapvr") {
		return "SteamVR Tracking";
	}
	return value.c_str();
}

inline void TextWithWidth(const char* label, const char* text, float width)
{
	ImGui::BeginChild(label, ImVec2(width, ImGui::GetTextLineHeightWithSpacing()));
	ImGui::Text(text);
	ImGui::EndChild();
}

void ShowVersionLine();
void BuildContinuousCalDisplay();
void DrawUpdatePrompt();
void DrawUpdatesPanel(ImVec2 panel_size);
void DrawSmoothingPanel(ImVec2 panel_size);
void CCal_BasicInfo();
void DrawSteamVRWarning();
void CCal_DrawSettings();
void BuildMenu(bool runningInOverlay);
void BuildProfileEditor();
VRState LoadVRState();
void BuildSystemSelection(const VRState& state);
void BuildDeviceSelections(const VRState& state);
