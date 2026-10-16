#include "stdafx.h"
#include "ui/UiCommon.h"
#include "ui/Widgets.h"
#include "AppWindow.h"
#include "Calibration.h"
#include "VRSession.h"

#include <imgui/imgui.h>
#include "imgui_extensions.h"

#include <algorithm>
#include <string>
#include <vector>

namespace {

	constexpr double kVRStateCacheSeconds = 1.0;
	constexpr double kIdentifySeconds = 0.5;
	constexpr double kIdentifyPulseSpacingSeconds = 0.005;
	constexpr double kIdentifyPulseGapMicroseconds = 1000.0;
	constexpr double kIdentifyMinPulseMicroseconds = 2000.0;
	constexpr double kIdentifyMaxPulseMicroseconds = 3999.0;

	struct IdentifyPulses
	{
		bool active = false;
		double startedAt = 0.0;
		double lastPulseAt = 0.0;
		int32_t referenceID = -1;
		int32_t targetID = -1;
	};

	VRState cachedState;
	bool cacheValid = false;
	double cachedAt = 0.0;
	VRConnectionState cachedConnection = VRConnectionState::Disconnected;
	CalibrationState cachedCalibration = CalibrationState::None;

	DeviceSelection pending;
	DeviceSelection synced;
	bool selectionSynced = false;

	IdentifyPulses identify;

	bool SameStandby(const StandbyDevice& a, const StandbyDevice& b)
	{
		return a.trackingSystem == b.trackingSystem && a.model == b.model && a.serial == b.serial;
	}

	bool SameSelection(const DeviceSelection& selection, const CalibrationContext& ctx)
	{
		return selection.referenceID == ctx.referenceID && selection.targetID == ctx.targetID &&
		       selection.referenceTrackingSystem == ctx.referenceTrackingSystem &&
		       selection.targetTrackingSystem == ctx.targetTrackingSystem &&
		       SameStandby(selection.referenceStandby, ctx.referenceStandby) && SameStandby(selection.targetStandby, ctx.targetStandby);
	}

	void CopySelection(DeviceSelection& selection, const CalibrationContext& ctx)
	{
		selection.referenceTrackingSystem = ctx.referenceTrackingSystem;
		selection.targetTrackingSystem = ctx.targetTrackingSystem;
		selection.referenceID = ctx.referenceID;
		selection.targetID = ctx.targetID;
		selection.referenceStandby = ctx.referenceStandby;
		selection.targetStandby = ctx.targetStandby;
	}

	DeviceSelection& PendingSelection()
	{
		if (!selectionSynced || !SameSelection(synced, CalCtx)) {
			CopySelection(synced, CalCtx);
			pending = synced;
			selectionSynced = true;
		}
		return pending;
	}

	void AssignIfDifferent(std::string& target, const char* value)
	{
		if (target != value) target = value;
	}

	const char* DeviceLabel(const VRDevice& device, bool notConnected)
	{
		static std::string label;
		label.clear();
		label += device.model;
		if (!label.empty()) label += " | ";
		label += device.serial;
		if (notConnected) label += VRSess.state == VRConnectionState::Connected ? " (not connected)" : " (offline)";
		return label.c_str();
	}

	void PlaceholderCombo(const char* id, const char* preview)
	{
		ImGui::BeginDisabled();
		if (ImGui::BeginCombo(id, preview)) ImGui::EndCombo();
		ImGui::EndDisabled();
	}

	const char* StandbyLabel(const StandbyDevice& device)
	{
		static std::string label;
		label.assign("< ");
		label += device.model;
		label += " | ";
		label += device.serial;
		label += " >";
		return label.c_str();
	}

	unsigned short IdentifyPulseMicroseconds(double gapSeconds)
	{
		const double fill = gapSeconds * 1.0e6 - kIdentifyPulseGapMicroseconds;
		return (unsigned short)std::clamp(fill, kIdentifyMinPulseMicroseconds, kIdentifyMaxPulseMicroseconds);
	}

	void PulseIdentifiedDevices(double now, double gapSeconds)
	{
		const unsigned short duration = IdentifyPulseMicroseconds(gapSeconds);
		if (auto* system = vr::VRSystem()) {
			system->TriggerHapticPulse(identify.targetID, 0, duration);
			system->TriggerHapticPulse(identify.referenceID, 0, duration);
		}
		identify.lastPulseAt = now;
	}

}

void BuildSystemSelection(const VRState& state)
{
	DeviceSelection& selection = PendingSelection();

	ImGuiStyle& style = ImGui::GetStyle();
	float paneWidth = ImGui::GetWindowContentRegionWidth() / 2 - style.FramePadding.x;

	TextWithWidth("ReferenceSystemLabel", "Reference space", paneWidth);
	ImGui::SameLine();
	TextWithWidth("TargetSystemLabel", "Target space", paneWidth);

	if (state.trackingSystems.empty()) {
		ImGui::PushItemWidth(paneWidth);
		PlaceholderCombo("##ReferenceTrackingSystem", "No devices");
		ImGui::SameLine();
		PlaceholderCombo("##TargetTrackingSystem", "No devices");
		ImGui::PopItemWidth();
		return;
	}

	int currentReferenceSystem = -1;
	int currentTargetSystem = -1;
	int firstReferenceSystemNotTargetSystem = -1;

	static std::vector<const char*> referenceSystems;
	static std::vector<const char*> referenceSystemsUi;
	referenceSystems.clear();
	referenceSystemsUi.clear();
	for (const std::string& str : state.trackingSystems) {
		if (str == selection.referenceTrackingSystem) {
			currentReferenceSystem = (int)referenceSystems.size();
		}
		else if (firstReferenceSystemNotTargetSystem == -1 && str != selection.targetTrackingSystem) {
			firstReferenceSystemNotTargetSystem = (int)referenceSystems.size();
		}
		referenceSystems.push_back(str.c_str());
		referenceSystemsUi.push_back(GetPrettyTrackingSystemName(str));
	}

	if (currentReferenceSystem == -1 && selection.referenceTrackingSystem == "") {
		if (CalCtx.state == CalibrationState::ContinuousStandby) {
			auto iter = std::find(state.trackingSystems.begin(), state.trackingSystems.end(), selection.referenceStandby.trackingSystem);
			if (iter != state.trackingSystems.end()) {
				currentReferenceSystem = (int)(iter - state.trackingSystems.begin());
			}
		}
		else {
			currentReferenceSystem = firstReferenceSystemNotTargetSystem;
		}
	}

	ImGui::PushItemWidth(paneWidth);
	ImGui::Combo("##ReferenceTrackingSystem", &currentReferenceSystem, &referenceSystemsUi[0], (int)referenceSystemsUi.size());

	if (currentReferenceSystem != -1 && currentReferenceSystem < (int)referenceSystems.size()) {
		AssignIfDifferent(selection.referenceTrackingSystem, referenceSystems[currentReferenceSystem]);
		if (selection.referenceTrackingSystem == selection.targetTrackingSystem) selection.targetTrackingSystem.clear();
	}

	if (selection.targetTrackingSystem == "") {
		if (CalCtx.state == CalibrationState::ContinuousStandby) {
			auto iter = std::find(state.trackingSystems.begin(), state.trackingSystems.end(), selection.targetStandby.trackingSystem);
			if (iter != state.trackingSystems.end()) {
				currentTargetSystem = (int)(iter - state.trackingSystems.begin());
			}
		}
		else {
			currentTargetSystem = 0;
		}
	}

	static std::vector<const char*> targetSystems;
	static std::vector<const char*> targetSystemsUi;
	targetSystems.clear();
	targetSystemsUi.clear();
	for (const std::string& str : state.trackingSystems) {
		if (str != selection.referenceTrackingSystem) {
			if (str != "" && str == selection.targetTrackingSystem) currentTargetSystem = (int)targetSystems.size();
			targetSystems.push_back(str.c_str());
			targetSystemsUi.push_back(GetPrettyTrackingSystemName(str));
		}
	}

	ImGui::SameLine();
	if (!targetSystemsUi.empty()) {
		ImGui::Combo("##TargetTrackingSystem", &currentTargetSystem, targetSystemsUi.data(), (int)targetSystemsUi.size());
	}
	else {
		PlaceholderCombo("##TargetTrackingSystem", "No second tracking system");
	}

	if (currentTargetSystem != -1 && currentTargetSystem < (int)targetSystems.size()) {
		AssignIfDifferent(selection.targetTrackingSystem, targetSystems[currentTargetSystem]);
	}

	ImGui::PopItemWidth();
}

static bool DeviceIsSelectable(const VRDevice& device)
{
	return device.id >= 0 && vr::VRSystem() && vr::VRSystem()->IsTrackedDeviceConnected(device.id);
}

static void BuildDeviceSelection(const VRState& state, int& initialSelected, const std::string& system, StandbyDevice& standbyDevice)
{
	int selected = initialSelected;
	if (system.empty()) {
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		ImGui::TextWrapped(state.trackingSystems.empty()
		                       ? "Turn on your headset, controllers and trackers."
							   : "Turn on the trackers you want to line up. Only one tracking system is connected.");
		ImGui::PopStyleColor();
		return;
	}
	ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), "Devices from: %s", GetPrettyTrackingSystemName(system));

	if (selected != -1) {
		bool matched = false;
		for (auto& device : state.devices) {
			if (device.trackingSystem != system) continue;

			if (selected == device.id) {
				matched = true;
				break;
			}
		}

		if (!matched) {
			// Device is no longer present.
			selected = -1;
		}
	}

	bool standby = CalCtx.state == CalibrationState::ContinuousStandby;

	if (selected == -1 && !standby) {
		for (auto& device : state.devices) {
			if (device.trackingSystem != system) continue;
			if (!DeviceIsSelectable(device)) continue;

			if (device.controllerRole == vr::TrackedControllerRole_LeftHand) {
				selected = device.id;
				break;
			}
		}

		if (selected == -1) {
			for (auto& device : state.devices) {
				if (device.trackingSystem != system) continue;
				if (!DeviceIsSelectable(device)) continue;

				selected = device.id;
				break;
			}
		}
	}

	if (selected == -1 && standby) {
		bool present = false;
		for (auto& device : state.devices) {
			if (device.trackingSystem != system) continue;

			if (standbyDevice.model != device.model) continue;
			if (standbyDevice.serial != device.serial) continue;

			present = true;
			break;
		}

		if (!present) {
			ImGui::PushID("standby");
			ImGui::Selectable(StandbyLabel(standbyDevice), true);
			ImGui::PopID();
		}
	}

	for (auto& device : state.devices) {
		if (device.trackingSystem != system) continue;

		ImGui::PushID(device.serial.c_str());
		ImGui::PushID(device.id);
		if (!DeviceIsSelectable(device)) {
			ImGui::PushStyleColor(ImGuiCol_Text, VRSess.state == VRConnectionState::Connected
			                                         ? ImVec4(0.9f, 0.35f, 0.35f, 1.0f)
													 : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			ImGui::Selectable(DeviceLabel(device, true), device.id >= 0 && selected == device.id, ImGuiSelectableFlags_Disabled);
			ImGui::PopStyleColor();
		}
		else if (ImGui::Selectable(DeviceLabel(device, false), selected == device.id)) {
			selected = device.id;
		}
		ImGui::PopID();
		ImGui::PopID();
	}
	if (selected != initialSelected) {
		const auto& device = std::find_if(state.devices.begin(), state.devices.end(), [&](const auto& d) { return d.id == selected; });
		if (device == state.devices.end()) return;

		initialSelected = selected;
		standbyDevice.trackingSystem = system;
		standbyDevice.model = device->model;
		standbyDevice.serial = device->serial;
	}
}

void BuildDeviceSelections(const VRState& state)
{
	DeviceSelection& selection = PendingSelection();

	ImGuiStyle& style = ImGui::GetStyle();
	ImVec2 paneSize(ImGui::GetWindowContentRegionWidth() / 2 - style.FramePadding.x,
	                ImGui::GetTextLineHeightWithSpacing() * 5 + style.ItemSpacing.y * 4);

	ImGui::BeginChild("left device pane", paneSize, ImGuiChildFlags_Borders);
	BuildDeviceSelection(state, selection.referenceID, selection.referenceTrackingSystem, selection.referenceStandby);
	ImGui::EndChild();

	ImGui::SameLine();

	ImGui::BeginChild("right device pane", paneSize, ImGuiChildFlags_Borders);
	BuildDeviceSelection(state, selection.targetID, selection.targetTrackingSystem, selection.targetStandby);
	ImGui::EndChild();

	ui::BeginDisabledLook(VRSess.state != VRConnectionState::Connected || selection.referenceID < 0 || selection.targetID < 0);
	if (ImGui::Button("Identify selected devices (vibrates)",
	                  ImVec2(ImGui::GetWindowContentRegionWidth(), ImGui::GetTextLineHeightWithSpacing() + 4.0f))) {
		const double now = ImGui::GetTime();
		identify.active = true;
		identify.startedAt = now;
		identify.referenceID = selection.referenceID;
		identify.targetID = selection.targetID;
		PulseIdentifiedDevices(now, 0.0);
		RequestImmediateRedraw();
	}
	ui::EndDisabledLook();
}

void TickIdentifyPulses()
{
	if (!identify.active) return;
	const double now = ImGui::GetTime();
	if (now < identify.startedAt || now - identify.startedAt >= kIdentifySeconds) {
		identify.active = false;
		return;
	}
	const double gap = now - identify.lastPulseAt;
	if (gap >= kIdentifyPulseSpacingSeconds) {
		PulseIdentifiedDevices(now, gap);
	}
	RequestImmediateRedraw();
}

void CommitDeviceSelection()
{
	CalCtx.CommitSelection(PendingSelection());
	CopySelection(synced, CalCtx);
}

void InvalidateVRStateCache()
{
	cacheValid = false;
}

static VRState BuildVRState()
{
	VRState state = VRState::Load();
	std::erase_if(state.devices, [](const VRDevice& device) { return device.deviceClass == vr::TrackedDeviceClass_DisplayRedirect; });
	state.DropIgnoredTrackingSystems();
	auto& trackingSystems = state.trackingSystems;

	if (VRSess.state != VRConnectionState::Connected) {
		for (const auto& known : GetKnownDevices()) {
			if (known.deviceClass == vr::TrackedDeviceClass_DisplayRedirect) continue;
			if (IsIgnoredTrackingSystem(known.trackingSystem)) continue;
			const bool listed = std::any_of(state.devices.begin(), state.devices.end(), [&](const VRDevice& d) {
				return d.trackingSystem == known.trackingSystem && d.serial == known.serial;
			});
			if (listed) continue;
			auto existing = std::find(trackingSystems.begin(), trackingSystems.end(), known.trackingSystem);
			if (existing == trackingSystems.end()) {
				trackingSystems.push_back(known.trackingSystem);
			}
			VRDevice device;
			device.deviceClass = known.deviceClass;
			device.model = known.model;
			device.serial = known.serial;
			device.trackingSystem = known.trackingSystem;
			state.devices.push_back(device);
		}
	}

	// Inject entries for continuous calibration targets which have yet to load

	if (CalCtx.state == CalibrationState::ContinuousStandby) {
		auto existing = std::find(trackingSystems.begin(), trackingSystems.end(), CalCtx.referenceTrackingSystem);
		if (existing == trackingSystems.end()) {
			trackingSystems.push_back(CalCtx.referenceTrackingSystem);
		}

		existing = std::find(trackingSystems.begin(), trackingSystems.end(), CalCtx.targetTrackingSystem);
		if (existing == trackingSystems.end()) {
			trackingSystems.push_back(CalCtx.targetTrackingSystem);
		}
	}

	return state;
}

const VRState& LoadVRState()
{
	const double now = ImGui::GetTime();
	const bool fresh = cacheValid && now >= cachedAt && now - cachedAt < kVRStateCacheSeconds && cachedConnection == VRSess.state &&
	                   cachedCalibration == CalCtx.state;
	if (!fresh) {
		cachedState = BuildVRState();
		cacheValid = true;
		cachedAt = now;
		cachedConnection = VRSess.state;
		cachedCalibration = CalCtx.state;
	}
	return cachedState;
}
