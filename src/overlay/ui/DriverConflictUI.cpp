#include "stdafx.h"
#include "UiCommon.h"
#include "DriverConflictState.h"
#include "VRSession.h"

#include <shellapi.h>

#include <imgui/imgui.h>
#include "imgui_extensions.h"

namespace {

	const ImVec4 blockingColor(0.95f, 0.45f, 0.45f, 1.0f);
	const ImVec4 warningColor(1.0f, 0.75f, 0.3f, 1.0f);

	void OpenInExplorer(const std::string& path)
	{
		const int wide = MultiByteToWideChar(CP_ACP, 0, path.c_str(), -1, nullptr, 0);
		if (wide <= 0) return;
		std::wstring widePath(wide - 1, L'\0');
		MultiByteToWideChar(CP_ACP, 0, path.c_str(), -1, &widePath[0], wide);
		ShellExecuteW(nullptr, L"explore", widePath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	}

	void DrawRivals(DriverConflictState& ctx)
	{
		for (size_t i = 0; i < ctx.report.rivals.size(); ++i) {
			const spacecal::RivalDriver& rival = ctx.report.rivals[i];
			ImGui::TextDisabled("%s", rival.path.c_str());
			if (rival.kind == spacecal::RivalKind::RuntimeFolder) {
				ImGui::TextWrapped("SteamVR loads that one because of where it sits, so it can't be unregistered. Remove or rename the "
				                   "folder, or use the removal panel on the Settings page to delete it.");
				ImGui::PushID(static_cast<int>(i));
				if (ImGui::Button("Open folder")) {
					OpenInExplorer(rival.path);
				}
				ImGui::PopID();
			}
		}
	}

} // namespace

void DrawDriverConflictPanel()
{
	DriverConflictState& ctx = DriverConflictCtx;
	const bool blocking = ctx.report.tier == spacecal::ConflictTier::Blocking;
	if (ctx.report.tier == spacecal::ConflictTier::None) return;
	if (!blocking && ctx.dismissed) return;

	ImVec2 panel_size{ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x, 0};

	ImGui::PushStyleColor(ImGuiCol_Text, blocking ? blockingColor : warningColor);
	ImGui::BeginGroupPanel(blocking ? "Wrong Space Calibrator driver" : "Another Space Calibrator driver is installed", panel_size);
	ImGui::PopStyleColor();

	if (blocking && ctx.report.ownDriverStale) {
		ImGui::TextWrapped("The driver answering SteamVR is from a different build than this overlay, and it's the copy that ships with "
		                   "this install. Close SteamVR and run install.ps1 again, or reinstall Space Calibrator.");
		const std::string dll = ctx.OwnDriverDllPath();
		if (!dll.empty()) {
			const std::string stamp = ctx.OwnDriverDllStamp();
			if (stamp.empty()) {
				ImGui::TextDisabled("%s", dll.c_str());
			}
			else {
				ImGui::TextDisabled("%s (%s)", dll.c_str(), stamp.c_str());
			}
		}
	}
	else if (blocking) {
		ImGui::TextWrapped("A different Space Calibrator driver is registered with SteamVR and answered first, so calibration can't run.");
		DrawRivals(ctx);
		if (!ctx.report.ownRegistered) {
			ImGui::TextWrapped("This copy isn't registered with SteamVR either. Run install.ps1 once the other one is gone.");
		}
	}
	else {
		ImGui::TextWrapped("A second Space Calibrator driver is registered with SteamVR. Calibration works right now, but either driver "
		                   "can answer on any reconnect, and the wrong one breaks it.");
		DrawRivals(ctx);
	}

	if (blocking && !VRSess.statusText.empty()) {
		ImGui::TextDisabled("%s", VRSess.statusText.c_str());
	}

	if (ctx.fixQueued) {
		ImGui::TextWrapped("Queued. The other driver is unregistered once SteamVR closes.");
	}
	else if (ctx.report.CanUnregister()) {
		if (ImGui::Button("Unregister the other driver")) {
			ctx.QueueUnregister();
		}
		if (ImGui::IsItemHovered(0)) {
			ImGui::SetTooltip("Takes it out of SteamVR's driver list once SteamVR closes. No files are deleted and no administrator "
			                  "rights are needed.");
		}
		if (!blocking) {
			ImGui::SameLine();
			if (ImGui::Button("Not now")) {
				ctx.dismissed = true;
			}
		}
	}

	if (!ctx.fixError.empty()) {
		ImGui::PushStyleColor(ImGuiCol_Text, blockingColor);
		ImGui::TextWrapped("%s", ctx.fixError.c_str());
		ImGui::PopStyleColor();
	}

	ImGui::EndGroupPanel();
}
