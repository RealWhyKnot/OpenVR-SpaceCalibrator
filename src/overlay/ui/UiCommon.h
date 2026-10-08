#pragma once

#include "VRState.h"

#include <imgui/imgui.h>
#include <string>

inline constexpr ImGuiWindowFlags bareWindowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                                    ImGuiWindowFlags_NoCollapse;

inline void TextWithWidth(const char* label, const char* text, float width)
{
	ImGui::BeginChild(label, ImVec2(width, ImGui::GetTextLineHeightWithSpacing()));
	ImGui::TextUnformatted(text);
	ImGui::EndChild();
}

void ShowVersionLine();
void BuildContinuousCalDisplay();
void DrawUpdatePrompt();
void DrawUpdatesPanel(ImVec2 panel_size);
void DrawSmoothingPanel(ImVec2 panel_size);
void DrawFingerSmoothingPanel(ImVec2 panel_size);
void DrawStickSmoothingPanel(ImVec2 panel_size);
void CCal_BasicInfo();
void DrawAutoDetectCard();
void DrawSteamVRWarning();
void DrawDriverConflictPanel();
void DrawLearnPage();
void DrawAboutPage();
void DrawBaseStationsPage();
void CCal_DrawSettings();
void BuildMenu(bool runningInOverlay);
void BuildProfileEditor();
VRState LoadVRState();
void BuildSystemSelection(const VRState& state);
void BuildDeviceSelections(const VRState& state);
