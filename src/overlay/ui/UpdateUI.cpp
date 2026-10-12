#include "stdafx.h"
#include "UiCommon.h"
#include "Widgets.h"
#include "Configuration.h"
#include "Updater.h"
#include "Version.h"

#include <imgui/imgui.h>
#include "imgui_extensions.h"

#include <cstdio>
#include <string>

static std::string UpdateFailureNote(const std::string& error)
{
	int code = 0;
	if (sscanf_s(error.c_str(), "request failed: %d", &code) == 1 && (code == 12002 || code == 12007 || code == 12029 || code == 12030)) {
		return "Couldn't reach GitHub to check for updates. Check your internet connection, then press Check now. (error " +
		       std::to_string(code) + ")";
	}
	const std::string helper = "could not start the update helper: ";
	if (error.rfind(helper, 0) == 0) {
		return "The update installer couldn't start (error " + error.substr(helper.size()) +
		       "). Press Check now to try again, or get the setup from the GitHub releases page.";
	}
	const std::string write = "could not write ";
	if (error.rfind(write, 0) == 0) {
		return "Couldn't save the update helper to " + error.substr(write.size()) + ". Press Check now to try again.";
	}
	return "Update check failed (" + error + "). Press Check now to try again.";
}

void DrawUpdatePrompt()
{
	static bool shown = false;
	if (UpdaterCtx.state != UpdateState::Available) {
		shown = false;
	}
	else if (!shown && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
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
	const ImVec2 button((ImGui::GetContentRegionAvail().x - style.ItemSpacing.x * 2.0f) / 3.0f, 0.0f);
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
		ImGui::TextDisabled("Installed: v" SPACECAL_VERSION_STRING " (" SPACECAL_CHANNEL ")");
		if (ImGui::Checkbox("Check for updates when the overlay starts", &UpdaterCtx.settings.checkOnStartup)) {
			SaveUpdateSettings(UpdaterCtx.settings);
		}
		if (ImGui::Checkbox("Install updates automatically when SteamVR closes", &UpdaterCtx.settings.autoInstall)) {
			SaveUpdateSettings(UpdaterCtx.settings);
		}
		ui::BeginDisabledLook(UpdaterCtx.state == UpdateState::Checking);
		if (ImGui::Button("Check now")) {
			UpdaterCtx.StartCheck(true);
		}
		ui::EndDisabledLook();
		if (UpdaterCtx.state == UpdateState::Available) {
			ImGui::SameLine();
			if (ImGui::Button("Update")) {
				UpdaterCtx.Update();
			}
		}
		if (UpdaterCtx.state == UpdateState::Failed) {
			ui::Note("update_failed", ui::NoteTone::Error, UpdateFailureNote(UpdaterCtx.error).c_str());
		}
		else {
			std::string status = UpdaterCtx.StatusText();
			if (!status.empty()) {
				ImGui::TextDisabled("%s", status.c_str());
			}
		}
		if (!UpdaterCtx.lastRunNote.empty()) {
			ui::Note("update_last_run", ui::NoteTone::Pending, UpdaterCtx.lastRunNote.c_str());
		}
	}
	ImGui::EndGroupPanel();
}
