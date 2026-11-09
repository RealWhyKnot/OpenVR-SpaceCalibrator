#include "stdafx.h"
#include "UiCommon.h"
#include "Widgets.h"
#include "DriverConflictState.h"
#include "VRSession.h"
#include "Version.h"

#include <shellapi.h>

#include <string>

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

	void DisabledWrapped(const std::string& text)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		ImGui::TextWrapped("%s", text.c_str());
		ImGui::PopStyleColor();
	}

	const std::string& DriverDateLine(const std::string& stamp)
	{
		static std::string cachedStamp;
		static std::string line;
		if (line.empty() || stamp != cachedStamp) {
			cachedStamp = stamp;
			line = "Driver file date " + stamp + ", overlay build " SPACECAL_VERSION_STRING;
		}
		return line;
	}

	void DrawRivals(DriverConflictState& ctx)
	{
		for (size_t i = 0; i < ctx.report.rivals.size(); ++i) {
			const spacecal::RivalDriver& rival = ctx.report.rivals[i];
			DisabledWrapped(rival.path);
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

	void DrawStaleDriverNote(const DriverConflictState& ctx)
	{
		const char* text = nullptr;
		ui::NoteTone tone = ui::NoteTone::Pending;
		switch (ctx.staleFix) {
			case spacecal::StaleDriverFix::None: return;
			case spacecal::StaleDriverFix::PendingOnClose: text = "A newer build is staged. It goes in when SteamVR closes."; break;
			case spacecal::StaleDriverFix::LoadsOnRestart:
				text = "SteamVR is running a Space Calibrator driver from another build. The matching one is already installed and loads "
				       "next time SteamVR starts.";
				break;
			case spacecal::StaleDriverFix::NeedsInstall:
				text = "SteamVR is running a Space Calibrator driver from another build, and the installed one doesn't match this app "
				       "either. Close SteamVR and reinstall Space Calibrator. If you built it from source, run build.ps1 again.";
				tone = ui::NoteTone::Error;
				break;
		}
		ui::Note("##staledriver", tone, text);
		if (ImGui::IsItemHovered(0)) {
			ImGui::SetTooltip("Loaded driver %s\nInstalled driver %s\nThis app %s", ctx.builds.loaded.empty() ? "unknown" : ctx.builds.loaded.c_str(),
			                  ctx.builds.onDisk.empty() ? "unknown" : ctx.builds.onDisk.c_str(), ctx.builds.app.c_str());
		}
	}

} // namespace

void DrawDriverConflictPanel()
{
	DriverConflictState& ctx = DriverConflictCtx;
	DrawStaleDriverNote(ctx);
	const bool blocking = ctx.report.tier == spacecal::ConflictTier::Blocking;
	if (ctx.report.tier == spacecal::ConflictTier::None) return;
	if (!blocking && ctx.dismissed) return;

	ImVec2 panel_size{ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x, 0};

	ImGui::PushStyleColor(ImGuiCol_Text, blocking ? blockingColor : warningColor);
	ImGui::BeginGroupPanel(blocking ? "Wrong Space Calibrator driver" : "Another Space Calibrator driver is installed", panel_size);
	ImGui::PopStyleColor();

	if (blocking && ctx.report.ownDriverStale) {
		ImGui::TextWrapped("SteamVR is running this install's own driver, but it's from a different build than this app. Close SteamVR "
		                   "and reinstall Space Calibrator. If you built it from source, run install.ps1 again instead.");
		const std::string dll = ctx.OwnDriverDllPath();
		if (!dll.empty()) {
			const std::string stamp = ctx.OwnDriverDllStamp();
			if (!stamp.empty()) {
				DisabledWrapped(DriverDateLine(stamp));
			}
			DisabledWrapped(dll);
		}
	}
	else if (blocking) {
		ImGui::TextWrapped("A different Space Calibrator driver is registered with SteamVR and answered first, so calibration can't run.");
		DrawRivals(ctx);
		if (!ctx.report.ownRegistered) {
			ImGui::TextWrapped("This copy isn't registered with SteamVR either. Once the other driver is gone, reinstall Space "
			                   "Calibrator. If you built it from source, run install.ps1 instead.");
		}
	}
	else {
		ImGui::TextWrapped("A second Space Calibrator driver is registered with SteamVR. Calibration works right now, but either driver "
		                   "can answer on any reconnect, and the wrong one breaks it.");
		DrawRivals(ctx);
	}

	if (blocking && !VRSess.statusText.empty()) {
		DisabledWrapped(VRSess.statusText);
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
