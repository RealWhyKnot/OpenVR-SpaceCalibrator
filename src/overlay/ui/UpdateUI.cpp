#include "stdafx.h"
#include "UiCommon.h"
#include "Configuration.h"
#include "Updater.h"
#include "Version.h"

#include <imgui/imgui.h>
#include "imgui_extensions.h"

void DrawUpdatePrompt()
{
	static bool shown = false;
	if (UpdaterCtx.state != UpdateState::Available) {
		shown = false;
	}
	else if (!shown) {
		ImGui::OpenPopup("Update available");
		shown = true;
	}

	auto& io = ImGui::GetIO();
	ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(620.0f, 0.0f), ImGuiCond_Always);
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
	                               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize;
	if (!ImGui::BeginPopupModal("Update available", nullptr, flags)) {
		return;
	}
	ImGuiStyle& style = ImGui::GetStyle();
	ImGui::Spacing();
	ImGui::Text("Space Calibrator %s", UpdaterCtx.available.tag.c_str());
	ImGui::TextDisabled("Installed: v" SPACECAL_VERSION_STRING " (" SPACECAL_CHANNEL ")");
	ImGui::Spacing();
	ImGui::TextWrapped("The update downloads now and installs itself the next time SteamVR closes.");
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();
	const ImVec2 button(175.0f, 0.0f);
	float total = button.x * 3.0f + style.ItemSpacing.x * 2.0f;
	ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - total);
	if (ImGui::Button("Later", button)) {
		UpdaterCtx.Later();
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button("Skip this version", button)) {
		UpdaterCtx.Skip();
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button("Update", button)) {
		UpdaterCtx.Update();
		ImGui::CloseCurrentPopup();
	}
	ImGui::Spacing();
	ImGui::EndPopup();
}

void DrawUpdatesPanel(ImVec2 panel_size)
{
	ImGui::BeginGroupPanel("Updates", panel_size);
	bool dev = UpdaterCtx.Channel() == spacecal::UpdateChannel::Dev;
	if (dev) {
		ImGui::TextDisabled("dev build: update checks are off");
	}
	else {
		if (ImGui::Checkbox("Check for updates when the overlay starts", &UpdaterCtx.settings.checkOnStartup)) {
			SaveUpdateSettings(UpdaterCtx.settings);
		}
		if (ImGui::Checkbox("Install updates automatically when SteamVR closes", &UpdaterCtx.settings.autoInstall)) {
			SaveUpdateSettings(UpdaterCtx.settings);
		}
		ImGui::BeginDisabled(UpdaterCtx.state == UpdateState::Checking);
		if (ImGui::Button("Check now")) {
			UpdaterCtx.StartCheck(true);
		}
		ImGui::EndDisabled();
		if (UpdaterCtx.state == UpdateState::Available) {
			ImGui::SameLine();
			if (ImGui::Button("Update")) {
				UpdaterCtx.Update();
			}
		}
		std::string status = UpdaterCtx.StatusText();
		if (!status.empty()) {
			ImGui::TextDisabled("%s", status.c_str());
		}
		if (!UpdaterCtx.lastRunNote.empty()) {
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.3f, 1.0f));
			ImGui::TextWrapped("%s", UpdaterCtx.lastRunNote.c_str());
			ImGui::PopStyleColor();
		}
	}
	ImGui::EndGroupPanel();
}
