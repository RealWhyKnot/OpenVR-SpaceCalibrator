#include "perf_harness.h"

#include <iostream>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <implot/implot.h>
#include <implot/implot_internal.h>

#include "AutoDetectController.h"
#include "Calibration.h"
#include "CalibrationMath.h"
#include "CalibrationMetrics.h"
#include "Configuration.h"
#include "DriverConflictState.h"
#include "DriverRequestCache.h"
#include "StickSettings.h"
#include "EmbeddedFonts.h"
#include "LegacyInstall.h"
#include "Updater.h"
#include "UserInterface.h"
#include "VRSession.h"
#include "VRState.h"
#include "basestations/BaseStationsController.h"
#include "imgui_extensions.h"
#include "ui/IconGlyphs.h"
#include "ui/Sidebar.h"
#include "ui/Style.h"
#include "ui/UiCommon.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace fakevr {

	struct Device
	{
		vr::ETrackedDeviceClass deviceClass = vr::TrackedDeviceClass_Invalid;
		std::string system;
		std::string model;
		std::string serial;
		std::string renderModel;
		std::string dongle;
		int32_t role = vr::TrackedControllerRole_Invalid;
		bool connected = false;
	};

	struct Calls
	{
		uint64_t deviceClass = 0;
		uint64_t connected = 0;
		uint64_t stringProp = 0;
		uint64_t otherProp = 0;
		uint64_t settings = 0;
		uint64_t other = 0;

		uint64_t Total() const { return deviceClass + connected + stringProp + otherProp + settings + other; }
	};

	Device devices[vr::k_unMaxTrackedDeviceCount];
	Calls calls;
	std::vector<vr::TrackedDeviceIndex_t> haptics;
	std::vector<unsigned short> hapticMicroseconds;
	bool runtimeUp = true;
	uint32_t initToken = 1;

	const Device* Find(vr::TrackedDeviceIndex_t id)
	{
		if (id >= vr::k_unMaxTrackedDeviceCount || devices[id].deviceClass == vr::TrackedDeviceClass_Invalid) return nullptr;
		return &devices[id];
	}

	template <class T> T Unknown(const Device* d, vr::ETrackedPropertyError* err)
	{
		if (err) *err = d ? vr::TrackedProp_UnknownProperty : vr::TrackedProp_InvalidDevice;
		return T{};
	}

	class System final : public vr::IVRSystem
	{
	public:
		void GetRecommendedRenderTargetSize(uint32_t* w, uint32_t* h) override
		{
			++calls.other;
			*w = *h = 0;
		}
		vr::HmdMatrix44_t GetProjectionMatrix(vr::EVREye, float, float) override
		{
			++calls.other;
			return {};
		}
		void GetProjectionRaw(vr::EVREye, float*, float*, float*, float*) override { ++calls.other; }
		bool ComputeDistortion(vr::EVREye, float, float, vr::DistortionCoordinates_t*) override
		{
			++calls.other;
			return false;
		}
		vr::HmdMatrix34_t GetEyeToHeadTransform(vr::EVREye) override
		{
			++calls.other;
			return {};
		}
		bool GetTimeSinceLastVsync(float*, uint64_t*) override
		{
			++calls.other;
			return false;
		}
		int32_t GetD3D9AdapterIndex() override
		{
			++calls.other;
			return -1;
		}
		void GetDXGIOutputInfo(int32_t*) override { ++calls.other; }
		void GetOutputDevice(uint64_t*, vr::ETextureType, VkInstance_T*) override { ++calls.other; }
		bool IsDisplayOnDesktop() override
		{
			++calls.other;
			return false;
		}
		bool SetDisplayVisibility(bool) override
		{
			++calls.other;
			return false;
		}
		void GetDeviceToAbsoluteTrackingPose(vr::ETrackingUniverseOrigin, float, vr::TrackedDevicePose_t*, uint32_t) override
		{
			++calls.other;
		}
		vr::HmdMatrix34_t GetSeatedZeroPoseToStandingAbsoluteTrackingPose() override
		{
			++calls.other;
			return {};
		}
		vr::HmdMatrix34_t GetRawZeroPoseToStandingAbsoluteTrackingPose() override
		{
			++calls.other;
			return {};
		}
		uint32_t GetSortedTrackedDeviceIndicesOfClass(vr::ETrackedDeviceClass, vr::TrackedDeviceIndex_t*, uint32_t,
		                                              vr::TrackedDeviceIndex_t) override
		{
			++calls.other;
			return 0;
		}
		vr::EDeviceActivityLevel GetTrackedDeviceActivityLevel(vr::TrackedDeviceIndex_t) override
		{
			++calls.other;
			return vr::k_EDeviceActivityLevel_Unknown;
		}
		void ApplyTransform(vr::TrackedDevicePose_t*, const vr::TrackedDevicePose_t*, const vr::HmdMatrix34_t*) override { ++calls.other; }
		vr::TrackedDeviceIndex_t GetTrackedDeviceIndexForControllerRole(vr::ETrackedControllerRole) override
		{
			++calls.other;
			return vr::k_unTrackedDeviceIndexInvalid;
		}
		vr::ETrackedControllerRole GetControllerRoleForTrackedDeviceIndex(vr::TrackedDeviceIndex_t) override
		{
			++calls.other;
			return vr::TrackedControllerRole_Invalid;
		}
		vr::ETrackedDeviceClass GetTrackedDeviceClass(vr::TrackedDeviceIndex_t id) override
		{
			++calls.deviceClass;
			const Device* d = Find(id);
			return d ? d->deviceClass : vr::TrackedDeviceClass_Invalid;
		}
		bool IsTrackedDeviceConnected(vr::TrackedDeviceIndex_t id) override
		{
			++calls.connected;
			const Device* d = Find(id);
			return d && d->connected;
		}
		bool GetBoolTrackedDeviceProperty(vr::TrackedDeviceIndex_t id, vr::ETrackedDeviceProperty, vr::ETrackedPropertyError* err) override
		{
			++calls.otherProp;
			return Unknown<bool>(Find(id), err);
		}
		float GetFloatTrackedDeviceProperty(vr::TrackedDeviceIndex_t id, vr::ETrackedDeviceProperty,
		                                    vr::ETrackedPropertyError* err) override
		{
			++calls.otherProp;
			return Unknown<float>(Find(id), err);
		}
		int32_t GetInt32TrackedDeviceProperty(vr::TrackedDeviceIndex_t id, vr::ETrackedDeviceProperty prop,
		                                      vr::ETrackedPropertyError* err) override
		{
			++calls.otherProp;
			const Device* d = Find(id);
			if (d && prop == vr::Prop_ControllerRoleHint_Int32) {
				if (err) *err = vr::TrackedProp_Success;
				return d->role;
			}
			return Unknown<int32_t>(d, err);
		}
		uint64_t GetUint64TrackedDeviceProperty(vr::TrackedDeviceIndex_t id, vr::ETrackedDeviceProperty,
		                                        vr::ETrackedPropertyError* err) override
		{
			++calls.otherProp;
			return Unknown<uint64_t>(Find(id), err);
		}
		vr::HmdMatrix34_t GetMatrix34TrackedDeviceProperty(vr::TrackedDeviceIndex_t id, vr::ETrackedDeviceProperty,
		                                                   vr::ETrackedPropertyError* err) override
		{
			++calls.otherProp;
			return Unknown<vr::HmdMatrix34_t>(Find(id), err);
		}
		uint32_t GetArrayTrackedDeviceProperty(vr::TrackedDeviceIndex_t id, vr::ETrackedDeviceProperty, vr::PropertyTypeTag_t, void*,
		                                       uint32_t, vr::ETrackedPropertyError* err) override
		{
			++calls.otherProp;
			return Unknown<uint32_t>(Find(id), err);
		}
		uint32_t GetStringTrackedDeviceProperty(vr::TrackedDeviceIndex_t id, vr::ETrackedDeviceProperty prop, char* value, uint32_t size,
		                                        vr::ETrackedPropertyError* err) override
		{
			++calls.stringProp;
			const Device* d = Find(id);
			const std::string* text = nullptr;
			if (d) {
				switch (prop) {
					case vr::Prop_TrackingSystemName_String: text = &d->system; break;
					case vr::Prop_ModelNumber_String: text = &d->model; break;
					case vr::Prop_SerialNumber_String: text = &d->serial; break;
					case vr::Prop_RenderModelName_String: text = &d->renderModel; break;
					case vr::Prop_ConnectedWirelessDongle_String: text = &d->dongle; break;
					default: break;
				}
			}
			if (!text) {
				if (value && size) value[0] = 0;
				return Unknown<uint32_t>(d, err);
			}
			if (err) *err = vr::TrackedProp_Success;
			const uint32_t needed = (uint32_t)text->size() + 1;
			if (value && size) {
				const uint32_t n = std::min(needed, size) - 1;
				std::memcpy(value, text->data(), n);
				value[n] = 0;
			}
			return needed;
		}
		const char* GetPropErrorNameFromEnum(vr::ETrackedPropertyError) override { return ""; }
		bool PollNextEvent(vr::VREvent_t*, uint32_t) override
		{
			++calls.other;
			return false;
		}
		bool PollNextEventWithPose(vr::ETrackingUniverseOrigin, vr::VREvent_t*, uint32_t, vr::TrackedDevicePose_t*) override
		{
			++calls.other;
			return false;
		}
		const char* GetEventTypeNameFromEnum(vr::EVREventType) override { return ""; }
		vr::HiddenAreaMesh_t GetHiddenAreaMesh(vr::EVREye, vr::EHiddenAreaMeshType) override
		{
			++calls.other;
			return {};
		}
		bool GetControllerState(vr::TrackedDeviceIndex_t, vr::VRControllerState_t*, uint32_t) override
		{
			++calls.other;
			return false;
		}
		bool GetControllerStateWithPose(vr::ETrackingUniverseOrigin, vr::TrackedDeviceIndex_t, vr::VRControllerState_t*, uint32_t,
		                                vr::TrackedDevicePose_t*) override
		{
			++calls.other;
			return false;
		}
		void TriggerHapticPulse(vr::TrackedDeviceIndex_t id, uint32_t, unsigned short microseconds) override
		{
			++calls.other;
			haptics.push_back(id);
			hapticMicroseconds.push_back(microseconds);
		}
		const char* GetButtonIdNameFromEnum(vr::EVRButtonId) override { return ""; }
		const char* GetControllerAxisTypeNameFromEnum(vr::EVRControllerAxisType) override { return ""; }
		bool IsInputAvailable() override
		{
			++calls.other;
			return true;
		}
		bool IsSteamVRDrawingControllers() override
		{
			++calls.other;
			return false;
		}
		bool ShouldApplicationPause() override
		{
			++calls.other;
			return false;
		}
		bool ShouldApplicationReduceRenderingWork() override
		{
			++calls.other;
			return false;
		}
		vr::EVRFirmwareError PerformFirmwareUpdate(vr::TrackedDeviceIndex_t) override
		{
			++calls.other;
			return vr::VRFirmwareError_None;
		}
		void AcknowledgeQuit_Exiting() override { ++calls.other; }
		uint32_t GetAppContainerFilePaths(char*, uint32_t) override
		{
			++calls.other;
			return 0;
		}
		const char* GetRuntimeVersion() override { return "fake"; }
	};

	class Settings final : public vr::IVRSettings
	{
	public:
		const char* GetSettingsErrorNameFromEnum(vr::EVRSettingsError) override { return ""; }
		void SetBool(const char*, const char*, bool, vr::EVRSettingsError* err) override { Ok(err); }
		void SetInt32(const char*, const char*, int32_t, vr::EVRSettingsError* err) override { Ok(err); }
		void SetFloat(const char*, const char*, float, vr::EVRSettingsError* err) override { Ok(err); }
		void SetString(const char*, const char*, const char*, vr::EVRSettingsError* err) override { Ok(err); }
		bool GetBool(const char*, const char*, vr::EVRSettingsError* err) override
		{
			Ok(err);
			return true;
		}
		int32_t GetInt32(const char*, const char*, vr::EVRSettingsError* err) override
		{
			Ok(err);
			return 0;
		}
		float GetFloat(const char*, const char*, vr::EVRSettingsError* err) override
		{
			Ok(err);
			return 0.0f;
		}
		void GetString(const char*, const char*, char* value, uint32_t len, vr::EVRSettingsError* err) override
		{
			Ok(err);
			if (value && len) value[0] = 0;
		}
		void RemoveSection(const char*, vr::EVRSettingsError* err) override { Ok(err); }
		void RemoveKeyInSection(const char*, const char*, vr::EVRSettingsError* err) override { Ok(err); }

	private:
		static void Ok(vr::EVRSettingsError* err)
		{
			++calls.settings;
			if (err) *err = vr::VRSettingsError_None;
		}
	};

	System system;
	Settings settings;

	void Clear()
	{
		for (auto& d : devices)
			d = Device{};
	}

	void Set(uint32_t id, vr::ETrackedDeviceClass cls, const char* system, const char* model, const char* serial,
	         int32_t role = vr::TrackedControllerRole_Invalid)
	{
		Device& d = devices[id];
		d.deviceClass = cls;
		d.system = system;
		d.model = model;
		d.serial = serial;
		d.role = role;
		d.connected = true;
	}

	void MixedSetup()
	{
		Clear();
		Set(0, vr::TrackedDeviceClass_HMD, "oculus", "Oculus Quest 3", "2G0YC1ZF8J0123");
		Set(1, vr::TrackedDeviceClass_Controller, "oculus", "Oculus Quest 3 (Left Controller)", "2G0YC1ZF8J0123_Controller_Left",
		    vr::TrackedControllerRole_LeftHand);
		Set(2, vr::TrackedDeviceClass_Controller, "oculus", "Oculus Quest 3 (Right Controller)", "2G0YC1ZF8J0123_Controller_Right",
		    vr::TrackedControllerRole_RightHand);
		Set(3, vr::TrackedDeviceClass_TrackingReference, "lighthouse", "Valve SR Imp", "LHB-4DE2A1F0");
		Set(4, vr::TrackedDeviceClass_TrackingReference, "lighthouse", "Valve SR Imp", "LHB-9C31B7E2");
		Set(5, vr::TrackedDeviceClass_GenericTracker, "lighthouse", "VIVE Tracker 3.0 MV", "LHR-1A2B3C4D");
		Set(6, vr::TrackedDeviceClass_GenericTracker, "lighthouse", "VIVE Tracker 3.0 MV", "LHR-5E6F7A8B");
		Set(7, vr::TrackedDeviceClass_GenericTracker, "lighthouse", "VIVE Tracker 3.0 MV", "LHR-9C0D1E2F");
		devices[1].renderModel = "oculus_quest_plus_controller_left";
		devices[2].renderModel = "oculus_quest_plus_controller_right";
	}

	void RuntimeUp(bool up)
	{
		runtimeUp = up;
		++initToken;
	}

}

namespace vr {

	uint32_t VR_CALLTYPE VR_GetInitToken()
	{
		return fakevr::initToken;
	}

	void* VR_CALLTYPE VR_GetGenericInterface(const char* version, EVRInitError* error)
	{
		if (!fakevr::runtimeUp) {
			if (error) *error = VRInitError_Init_NotInitialized;
			return nullptr;
		}
		if (error) *error = VRInitError_None;
		if (std::strcmp(version, IVRSystem_Version) == 0) return static_cast<IVRSystem*>(&fakevr::system);
		if (std::strcmp(version, IVRSettings_Version) == 0) return static_cast<IVRSettings*>(&fakevr::settings);
		if (error) *error = VRInitError_Init_InterfaceNotFound;
		return nullptr;
	}

	bool VR_GetRuntimePath(char* buffer, uint32_t size, uint32_t* required)
	{
		if (buffer && size) buffer[0] = 0;
		if (required) *required = 0;
		return false;
	}

}

CalibrationContext CalCtx;
VRSessionState VRSess;

namespace stubs {
	int saveProfile = 0;
	int applySmoothing = 0;
	int smoothingQueries = 0;
	int redraws = 0;
	std::vector<KnownDevice> knownDevices;
}

void RequestImmediateRedraw()
{
	++stubs::redraws;
}

const std::vector<KnownDevice>& GetKnownDevices()
{
	return stubs::knownDevices;
}

void StartCalibration() {}
void StartContinuousCalibration() {}
void EndContinuousCalibration() {}
void LoadChaperoneBounds() {}
void ApplyChaperoneBounds() {}
void DebugApplyRandomOffset() {}

void SaveProfile(CalibrationContext&)
{
	++stubs::saveProfile;
}

void ApplySmoothingSettings()
{
	++stubs::applySmoothing;
	SaveProfile(CalCtx);
}

bool QuerySmoothingStats(uint32_t id, protocol::SmoothingStats& stats)
{
	++stubs::smoothingQueries;
	stats = {};
	stats.openVRID = id;
	const fakevr::Device* d = fakevr::Find(id);
	stats.active = d && d->deviceClass == vr::TrackedDeviceClass_GenericTracker;
	stats.rawJitterMm = 0.8;
	stats.smoothJitterMm = 0.3;
	return true;
}

void SaveUpdateSettings(const UpdateSettings&) {}

void LoadBaseStationsSettings(spacecal::basestations::BaseStationsSettings&) {}
void SaveBaseStationsSettings(const spacecal::basestations::BaseStationsSettings&) {}

namespace fakeble {

	using namespace spacecal::basestations;

	std::vector<DiscoveredAdvert> queued;

	class Client final : public IBleClient
	{
	public:
		bool StartScan() override { return true; }
		void StopScan() override {}
		bool ScanFailed() const override { return false; }
		void EnqueueWrite(BleWriteRequest) override {}
		std::vector<DiscoveredAdvert> DrainAdverts() override
		{
			std::vector<DiscoveredAdvert> out;
			out.swap(queued);
			return out;
		}
		std::vector<BleWriteResult> DrainWriteResults() override { return {}; }
	};

	DiscoveredAdvert V2Advert(const char* name, uint64_t address, uint8_t channel, PowerState power)
	{
		DiscoveredAdvert advert;
		advert.address = address;
		advert.name = name;
		advert.manufacturerData = {0, 0, channel, 0, (uint8_t)power, 0, 0};
		return advert;
	}

}

std::unique_ptr<spacecal::basestations::IBleClient> spacecal::basestations::CreateWinRtBleClient()
{
	return std::make_unique<fakeble::Client>();
}

#include "../src/overlay/basestations/BaseStationsController.cpp"

namespace items {
	struct Item
	{
		ImGuiID id;
		std::string label;
		ImRect rect;
	};

	std::unordered_map<ImGuiID, ImRect> rects;
	std::vector<Item> list;

	const Item* Find(const char* label, int occurrence = 0)
	{
		for (const Item& item : list) {
			if (item.label == label && occurrence-- == 0) return &item;
		}
		return nullptr;
	}
}

void ImGuiTestEngineHook_ItemAdd(ImGuiContext*, ImGuiID id, const ImRect& bb, const ImGuiLastItemData*)
{
	items::rects[id] = bb;
}

void ImGuiTestEngineHook_ItemInfo(ImGuiContext*, ImGuiID id, const char* label, ImGuiItemStatusFlags)
{
	auto it = items::rects.find(id);
	items::list.push_back({id, label ? label : "", it != items::rects.end() ? it->second : ImRect()});
}

void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...) {}

const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID)
{
	return nullptr;
}

namespace current {

	void CalibrationContent()
	{
		DrawAutoDetectCard();
		InvalidateVRStateCache();
		const VRState& state = LoadVRState();
		ImGui::BeginDisabled(CalCtx.state == CalibrationState::Continuous);
		BuildSystemSelection(state);
		BuildDeviceSelections(state);
		ImGui::EndDisabled();
		BuildMenu(true);
	}

	struct SmoothingStatsRow
	{
		std::string label;
		protocol::SmoothingStats stats;
	};

	void RefreshSmoothingStats(std::vector<SmoothingStatsRow>& rows)
	{
		rows.clear();
		if (!vr::VRSystem()) {
			return;
		}
		char serial[vr::k_unMaxPropertyStringSize];
		for (uint32_t id = 0; id < vr::k_unMaxTrackedDeviceCount; ++id) {
			auto deviceClass = vr::VRSystem()->GetTrackedDeviceClass(id);
			if (deviceClass != vr::TrackedDeviceClass_GenericTracker && deviceClass != vr::TrackedDeviceClass_Controller) {
				continue;
			}
			protocol::SmoothingStats stats;
			if (!QuerySmoothingStats(id, stats) || !stats.active) {
				continue;
			}
			serial[0] = 0;
			vr::VRSystem()->GetStringTrackedDeviceProperty(id, vr::Prop_SerialNumber_String, serial, sizeof serial);
			rows.push_back({serial, stats});
		}
	}

	struct DriverLink
	{
		spacecal::DriverRequestCache cache;
		std::vector<protocol::Request> sent;

		void Send(const protocol::Request& request)
		{
			if (cache.ShouldSend(request)) sent.push_back(request);
		}
	};

	void ResetAndDisableOffsets(DriverLink& link, uint32_t id)
	{
		vr::HmdVector3d_t zeroV;
		zeroV.v[0] = zeroV.v[1] = zeroV.v[2] = 0;
		vr::HmdQuaternion_t zeroQ;
		zeroQ.x = 0;
		zeroQ.y = 0;
		zeroQ.z = 0;
		zeroQ.w = 1;
		protocol::Request req(protocol::RequestSetDeviceTransform);
		req.setDeviceTransform = {id, false, zeroV, zeroQ, 1.0};
		link.Send(req);
	}

	void SendProfileParams(CalibrationContext& ctx, DriverLink& link)
	{
		protocol::Request setParamsReq(protocol::RequestSetAlignmentSpeedParams);
		setParamsReq.setAlignmentSpeedParams = ctx.alignmentSpeedParams;
		link.Send(setParamsReq);
		protocol::Request setSmoothingReq(protocol::RequestSetSmoothingParams);
		setSmoothingReq.setSmoothingParams = ctx.smoothingParams;
		link.Send(setSmoothingReq);
		protocol::Request setFingerReq(protocol::RequestSetFingerSmoothing);
		setFingerReq.setFingerSmoothing = ctx.fingerSmoothing;
		link.Send(setFingerReq);
		protocol::Request setStickReq(protocol::RequestSetStickSmoothing);
		setStickReq.setStickSmoothing = spacecal::stick_settings::ForDriver(ctx.stickSmoothing, ctx.stickOn);
		link.Send(setStickReq);
	}

	void SendDeviceTransform(CalibrationContext& ctx, DriverLink& link, uint32_t id, vr::ETrackedDeviceClass deviceClass)
	{
		protocol::Request req(protocol::RequestSetDeviceTransform);
		req.setDeviceTransform = {id, true, VRTranslationVec(ctx.calibratedTranslation), VRRotationQuat(ctx.calibratedRotation),
		                          ctx.calibratedScale};
		req.setDeviceTransform.lerp = CalCtx.state == CalibrationState::Continuous;
		req.setDeviceTransform.quash =
		    CalCtx.state == CalibrationState::Continuous && id == CalCtx.targetID && CalCtx.quashTargetInContinuous;
		req.setDeviceTransform.smooth = deviceClass == vr::TrackedDeviceClass_GenericTracker ||
		                                (ctx.smoothControllers && deviceClass == vr::TrackedDeviceClass_Controller);
		link.Send(req);
	}

	void ScanAndApplyProfileImpl(CalibrationContext& ctx, DriverLink& link)
	{
		std::unique_ptr<char[]> buffer_array(new char[vr::k_unMaxPropertyStringSize]);
		char* buffer = buffer_array.get();
		ctx.enabled = ctx.validProfile;
		SendProfileParams(ctx, link);

		for (uint32_t id = 0; id < vr::k_unMaxTrackedDeviceCount; ++id) {
			auto deviceClass = vr::VRSystem()->GetTrackedDeviceClass(id);
			if (deviceClass == vr::TrackedDeviceClass_Invalid) continue;

			if (!ctx.enabled) {
				ResetAndDisableOffsets(link, id);
				continue;
			}

			vr::ETrackedPropertyError err = vr::TrackedProp_Success;
			vr::VRSystem()->GetStringTrackedDeviceProperty(id, vr::Prop_TrackingSystemName_String, buffer, vr::k_unMaxPropertyStringSize,
			                                               &err);

			if (err != vr::TrackedProp_Success) {
				ResetAndDisableOffsets(link, id);
				continue;
			}

			std::string trackingSystem(buffer);

			if (id == vr::k_unTrackedDeviceIndex_Hmd) {
				ApplyTrackingSystemFixups(id, deviceClass, trackingSystem);
				if (trackingSystem != ctx.referenceTrackingSystem) {
					ctx.enabled = false;
				}
				ResetAndDisableOffsets(link, id);
				continue;
			}

			ApplyTrackingSystemFixups(id, deviceClass, trackingSystem);

			if (trackingSystem != ctx.targetTrackingSystem) {
				ResetAndDisableOffsets(link, id);
				continue;
			}

			SendDeviceTransform(ctx, link, id, deviceClass);
		}
	}

	void LoadFonts(bool noPowerOfTwoHeight, int oversampleH)
	{
		ImGuiIO& io = ImGui::GetIO();
		if (noPowerOfTwoHeight) io.Fonts->Flags |= ImFontAtlasFlags_NoPowerOfTwoHeight;
		ImFontConfig bodyConfig;
		bodyConfig.OversampleH = oversampleH;
		io.Fonts->AddFontFromMemoryCompressedTTF(InterFont_compressed_data, InterFont_compressed_size, 22.0f, &bodyConfig);
		ImFontConfig iconConfig;
		iconConfig.MergeMode = true;
		iconConfig.GlyphMinAdvanceX = 22.0f;
		iconConfig.GlyphOffset = ImVec2(0.0f, 3.0f);
		iconConfig.OversampleH = oversampleH;
		io.Fonts->AddFontFromMemoryCompressedTTF(IconFont_compressed_data, IconFont_compressed_size, 22.0f, &iconConfig, kIconGlyphRanges);
		ImFontConfig headingConfig;
		headingConfig.OversampleH = oversampleH;
		io.Fonts->AddFontFromMemoryCompressedTTF(InterFont_compressed_data, InterFont_compressed_size, 30.0f, &headingConfig);
	}

}

namespace candidate {

	void CalibrationContent()
	{
		DrawAutoDetectCard();
		const VRState& state = LoadVRState();
		ImGui::BeginDisabled(CalCtx.state == CalibrationState::Continuous);
		BuildSystemSelection(state);
		BuildDeviceSelections(state);
		ImGui::EndDisabled();
		BuildMenu(true);
	}

	void RefreshSmoothingStats(const VRState& state, std::vector<current::SmoothingStatsRow>& rows)
	{
		rows.clear();
		for (const VRDevice& device : state.devices) {
			const bool tracker = device.deviceClass == vr::TrackedDeviceClass_GenericTracker;
			const bool controller = device.deviceClass == vr::TrackedDeviceClass_Controller;
			if (!tracker && !controller) continue;
			if (device.trackingSystem != CalCtx.targetTrackingSystem) continue;
			if (controller && !CalCtx.smoothControllers) continue;
			protocol::SmoothingStats stats;
			if (!QuerySmoothingStats(device.id, stats) || !stats.active) continue;
			rows.push_back({device.serial, stats});
		}
	}

	struct ScannedDevice
	{
		uint32_t id = 0;
		vr::ETrackedDeviceClass deviceClass = vr::TrackedDeviceClass_Invalid;
		bool hasSystem = false;
		std::string trackingSystem;
	};

	std::vector<ScannedDevice> ScanDevices()
	{
		std::vector<ScannedDevice> devices;
		std::unique_ptr<char[]> buffer(new char[vr::k_unMaxPropertyStringSize]);
		for (uint32_t id = 0; id < vr::k_unMaxTrackedDeviceCount; ++id) {
			const auto deviceClass = vr::VRSystem()->GetTrackedDeviceClass(id);
			if (deviceClass == vr::TrackedDeviceClass_Invalid) continue;
			ScannedDevice device;
			device.id = id;
			device.deviceClass = deviceClass;
			vr::ETrackedPropertyError err = vr::TrackedProp_Success;
			vr::VRSystem()->GetStringTrackedDeviceProperty(id, vr::Prop_TrackingSystemName_String, buffer.get(),
			                                               vr::k_unMaxPropertyStringSize, &err);
			device.hasSystem = err == vr::TrackedProp_Success;
			if (device.hasSystem) {
				device.trackingSystem = buffer.get();
				ApplyTrackingSystemFixups(id, deviceClass, device.trackingSystem);
			}
			devices.push_back(std::move(device));
		}
		return devices;
	}

	void ScanAndApplyProfileImpl(CalibrationContext& ctx, const std::vector<ScannedDevice>& devices, current::DriverLink& link)
	{
		ctx.enabled = ctx.validProfile;
		current::SendProfileParams(ctx, link);
		for (const ScannedDevice& device : devices) {
			if (!ctx.enabled || !device.hasSystem) {
				current::ResetAndDisableOffsets(link, device.id);
				continue;
			}
			if (device.id == vr::k_unTrackedDeviceIndex_Hmd) {
				if (device.trackingSystem != ctx.referenceTrackingSystem) ctx.enabled = false;
				current::ResetAndDisableOffsets(link, device.id);
				continue;
			}
			if (device.trackingSystem != ctx.targetTrackingSystem) {
				current::ResetAndDisableOffsets(link, device.id);
				continue;
			}
			current::SendDeviceTransform(ctx, link, device.id, device.deviceClass);
		}
	}

}

namespace {

	constexpr int kFramesPerSecond = 90;
	float displayHeight = 800.0f;

	void BeginFrame()
	{
		ImGuiIO& io = ImGui::GetIO();
		io.DisplaySize = ImVec2(1200.0f, displayHeight);
		io.DeltaTime = 1.0f / kFramesPerSecond;
		if (GImGui->TestEngineHookItems) {
			items::rects.clear();
			items::list.clear();
		}
		ImGui::NewFrame();
	}

	void MainFrame()
	{
		BeginFrame();
		BuildMainWindow(true);
		ImGui::Render();
	}

	template <class F> void ContentFrame(F&& content)
	{
		BeginFrame();
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);
		ImGui::Begin("SpaceCalibrator", nullptr, bareWindowFlags);
		ImGui::BeginChild("content", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
		content();
		ImGui::EndChild();
		ImGui::End();
		ImGui::Render();
	}

	struct Cost
	{
		double usPerFrame = 0.0;
		double allocsPerFrame = 0.0;
		double kibPerFrame = 0.0;
		double callsPerFrame = 0.0;
		fakevr::Calls calls;
		int vertices = 0;
		int frames = 0;
		int pipeRoundTrips = 0;
	};

	template <class Draw> Cost Measure(int frames, Draw&& draw)
	{
		for (int i = 0; i < 3; ++i)
			draw();
		Cost cost;
		cost.frames = frames;
		fakevr::calls = {};
		stubs::smoothingQueries = 0;
		const auto heap = perf::MeasureHeap([&] {
			for (int i = 0; i < frames; ++i)
				draw();
		});
		cost.pipeRoundTrips = stubs::smoothingQueries;
		cost.calls = fakevr::calls;
		cost.callsPerFrame = (double)fakevr::calls.Total() / frames;
		cost.allocsPerFrame = (double)heap.allocations / frames;
		cost.kibPerFrame = (double)heap.bytesRequested / 1024.0 / frames;
		cost.usPerFrame = perf::BestSecondsPerOp(5, frames, [&](int) { draw(); }) * 1e6;
		cost.vertices = ImGui::GetDrawData() ? ImGui::GetDrawData()->TotalVtxCount : 0;
		return cost;
	}

	void Print(const char* name, const Cost& c)
	{
		const double f = (double)c.frames;
		std::printf("INFO ui %-44s %8.1f us/frame %7.1f allocs/frame %7.2f KiB/frame %6.1f openvr/frame (class %.1f connected %.1f "
		            "string %.1f prop %.1f settings %.1f) %6d vtx\n",
		            name, c.usPerFrame, c.allocsPerFrame, c.kibPerFrame, c.callsPerFrame, c.calls.deviceClass / f, c.calls.connected / f,
		            c.calls.stringProp / f, c.calls.otherProp / f, c.calls.settings / f, c.vertices);
	}

	void ResetCalibration()
	{
		CalCtx = CalibrationContext();
		CalCtx.validProfile = true;
		CalCtx.enabled = true;
		CalCtx.referenceTrackingSystem = "oculus";
		CalCtx.targetTrackingSystem = "lighthouse";
		CalCtx.calibratedRotation = Eigen::Vector3d(0.0, 37.0, 0.0);
		CalCtx.calibratedTranslation = Eigen::Vector3d(42.0, -8.0, 130.0);
		CalCtx.state = CalibrationState::None;
	}

	void Connect()
	{
		fakevr::MixedSetup();
		fakevr::RuntimeUp(true);
		VRSess.state = VRConnectionState::Connected;
		VRSess.lastInitError = vr::VRInitError_None;
	}

	void FillMetrics()
	{
		const double end = Metrics::timestamp();
		for (int i = 0; i <= 60; ++i) {
			Metrics::CurrentTime = end - 30.0 + i * 0.5;
			Metrics::posOffset_rawComputed.Push(Eigen::Vector3d(1.0, 2.0, 3.0));
			Metrics::posOffset_currentCal.Push(Eigen::Vector3d(1.5, 2.5, 3.5));
			Metrics::posOffset_lastSample.Push(Eigen::Vector3d(0.5, 1.0, 1.5));
			Metrics::posOffset_byRelPose.Push(Eigen::Vector3d(0.2, 0.4, 0.6));
			Metrics::error_rawComputed.Push(2.0 + (i % 7) * 0.1);
			Metrics::error_currentCal.Push(1.8);
			Metrics::error_byRelPose.Push(1.2);
			Metrics::error_currentCalRelPose.Push(1.4);
			Metrics::axisIndependence.Push(0.0015);
			Metrics::computationTime.Push(3.0);
			Metrics::jitterRef.Push(0.4);
			Metrics::jitterTarget.Push(0.6);
			if (i % 10 == 0) Metrics::calibrationApplied.Push(i % 20 == 0);
		}
	}

	std::vector<spacecal::basestations::DiscoveredAdvert> StationAdverts(spacecal::basestations::PowerState firstPower)
	{
		using namespace spacecal::basestations;
		const char* serials[] = {"LHB-1A2B3C4D", "LHB-4DE2A1F0", "LHB-9C31B7E2", "LHB-E5F60718"};
		const uint8_t channels[] = {1, 2, 3, 3};
		std::vector<DiscoveredAdvert> adverts;
		adverts.reserve(4);
		for (int i = 0; i < 4; ++i) {
			adverts.push_back(
			    fakeble::V2Advert(serials[i], 0xC0FFEE000000ull + i, channels[i], i == 0 ? firstPower : PowerState::AwakeFromSleep));
		}
		return adverts;
	}

	void UseStations()
	{
		using namespace spacecal::basestations;
		auto& ctl = BaseStationsController::Get();
		ctl.EnsureStarted();
		fakeble::queued = StationAdverts(PowerState::AwakeFromSleep);
		ctl.SetNickname("LHB-1A2B3C4D", "Left corner");
		ctl.Settings().automation.powerManagement = true;
		ctl.Tick(true);
	}

	template <class Draw> bool DragItem(const char* label, float fromX, float toX, Draw&& draw)
	{
		GImGui->TestEngineHookItems = true;
		draw();
		const items::Item* found = items::Find(label);
		if (!found) {
			std::printf("FAIL item not found: %s\n", label);
			++perf::failures;
			GImGui->TestEngineHookItems = false;
			return false;
		}
		const ImRect r = found->rect;
		ImGuiIO& io = ImGui::GetIO();
		auto at = [&](float fx) {
			return ImVec2(r.Min.x + (r.Max.x - r.Min.x) * fx, (r.Min.y + r.Max.y) * 0.5f);
		};
		io.AddMousePosEvent(at(fromX).x, at(fromX).y);
		draw();
		draw();
		io.AddMouseButtonEvent(0, true);
		draw();
		const int steps = 30;
		for (int i = 1; i <= steps; ++i) {
			const ImVec2 p = at(fromX + (toX - fromX) * i / steps);
			io.AddMousePosEvent(p.x, p.y);
			draw();
		}
		io.AddMouseButtonEvent(0, false);
		draw();
		draw();
		io.AddMousePosEvent(-1000.0f, -1000.0f);
		draw();
		GImGui->TestEngineHookItems = false;
		return true;
	}

	template <class Draw> void ClickAt(ImVec2 p, Draw&& draw)
	{
		ImGuiIO& io = ImGui::GetIO();
		io.AddMousePosEvent(p.x, p.y);
		draw();
		io.AddMouseButtonEvent(0, true);
		draw();
		io.AddMouseButtonEvent(0, false);
		draw();
		io.AddMousePosEvent(-1000.0f, -1000.0f);
		draw();
		GImGui->TestEngineHookItems = false;
	}

	template <class Draw> bool ClickItem(const char* label, Draw&& draw, int occurrence = 0)
	{
		GImGui->TestEngineHookItems = true;
		draw();
		const items::Item* found = items::Find(label, occurrence);
		if (!found) {
			std::printf("FAIL item not found: %s\n", label);
			++perf::failures;
			GImGui->TestEngineHookItems = false;
			return false;
		}
		ClickAt(found->rect.GetCenter(), draw);
		return true;
	}

	template <class Draw> bool ClickContentItemById(const char* label, Draw&& draw)
	{
		GImGui->TestEngineHookItems = true;
		draw();
		ImGuiWindow* content = nullptr;
		for (ImGuiWindow* window : GImGui->Windows) {
			if (std::strstr(window->Name, "/content_") && window->ParentWindow &&
			    std::strcmp(window->ParentWindow->Name, "SpaceCalibrator") == 0) {
				content = window;
			}
		}
		const auto found = content ? items::rects.find(ImHashStr(label, 0, content->IDStack[0])) : items::rects.end();
		if (found == items::rects.end()) {
			std::printf("FAIL item not found: %s\n", label);
			++perf::failures;
			GImGui->TestEngineHookItems = false;
			return false;
		}
		ClickAt(found->second.GetCenter(), draw);
		return true;
	}

	template <class Draw> bool TypeInto(const char* label, int occurrence, const char* text, Draw&& draw)
	{
		if (!ClickItem(label, draw, occurrence)) return false;
		ImGuiIO& io = ImGui::GetIO();
		io.AddKeyEvent(ImGuiMod_Ctrl, true);
		io.AddKeyEvent(ImGuiKey_A, true);
		draw();
		io.AddKeyEvent(ImGuiKey_A, false);
		io.AddKeyEvent(ImGuiMod_Ctrl, false);
		draw();
		io.AddInputCharactersUTF8(text);
		draw();
		io.AddKeyEvent(ImGuiKey_Enter, true);
		draw();
		io.AddKeyEvent(ImGuiKey_Enter, false);
		draw();
		return true;
	}

	bool PopupOpen(const char* name)
	{
		for (const ImGuiPopupData& popup : GImGui->OpenPopupStack) {
			if (popup.Window && std::strcmp(popup.Window->Name, name) == 0) return true;
		}
		return false;
	}

	struct AtlasResult
	{
		double ms = 0.0;
		int width = 0;
		int height = 0;
		int glyphs = 0;
	};

	AtlasResult BuildAtlas(bool appFonts, bool noPowerOfTwoHeight, int oversampleH)
	{
		AtlasResult best;
		best.ms = 1e9;
		for (int round = 0; round < 3; ++round) {
			ImGuiContext* ctx = ImGui::CreateContext();
			ImGui::SetCurrentContext(ctx);
			const double start = perf::QpcSeconds();
			if (appFonts)
				ui::LoadFonts();
			else
				current::LoadFonts(noPowerOfTwoHeight, oversampleH);
			unsigned char* pixels = nullptr;
			int w = 0, h = 0;
			ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
			const double ms = (perf::QpcSeconds() - start) * 1000.0;
			int glyphs = 0;
			for (ImFont* font : ImGui::GetIO().Fonts->Fonts)
				glyphs += font->Glyphs.Size;
			ImGui::DestroyContext(ctx);
			ImGui::SetCurrentContext(nullptr);
			if (ms < best.ms) best.ms = ms;
			best.width = w;
			best.height = h;
			best.glyphs = glyphs;
		}
		return best;
	}

	void TestFontAtlas()
	{
		const AtlasResult app = BuildAtlas(true, false, 2);
		const AtlasResult same = BuildAtlas(false, false, 2);
		const AtlasResult npot = BuildAtlas(false, true, 2);
		const AtlasResult over1 = BuildAtlas(false, true, 1);
		auto mib = [](const AtlasResult& r) {
			return r.width * (double)r.height * 4.0 / (1024.0 * 1024.0);
		};
		std::printf("INFO font atlas: app LoadFonts %dx%d (%.2f MiB RGBA32, %d glyphs) built in %.2f ms\n", app.width, app.height, mib(app),
		            app.glyphs, app.ms);
		std::printf("INFO font atlas: current copy %dx%d %.2f MiB %.2f ms\n", same.width, same.height, mib(same), same.ms);
		std::printf("INFO font atlas: candidate NoPowerOfTwoHeight %dx%d %.2f MiB %.2f ms (no visual change)\n", npot.width, npot.height,
		            mib(npot), npot.ms);
		std::printf("INFO font atlas: candidate NoPowerOfTwoHeight + OversampleH 1 %dx%d %.2f MiB %.2f ms (visual change)\n", over1.width,
		            over1.height, mib(over1), over1.ms);
		PERF_CHECK(same.width == app.width && same.height == app.height && same.glyphs == app.glyphs);
		PERF_CHECK(npot.width * npot.height <= app.width * app.height);
		perf::Budget("font atlas: build at startup (ms)", app.ms, 500.0, "ms");
	}

	void SetupContext()
	{
		ImGui::CreateContext();
		ImPlot::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.IniFilename = nullptr;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
		ui::LoadFonts();
		io.Fonts->AddFontDefault();
		unsigned char* pixels = nullptr;
		int w = 0, h = 0;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
		ui::ApplyStyle();
	}

	void TestVRStateLoad()
	{
		Connect();
		VRState::Load();
		fakevr::calls = {};
		const VRState state = VRState::Load();
		const fakevr::Calls one = fakevr::calls;
		const auto heap = perf::MeasureHeap([] { VRState::Load(); });
		const double seconds = perf::BestSecondsPerOp(5, 200, [](int) { VRState::Load(); });
		std::printf(
		    "INFO VRState::Load, 8-device mixed setup: %llu openvr calls (class %llu, string %llu, prop %llu), %llu allocs, %.2f us "
		    "with a zero-cost fake runtime\n",
		    (unsigned long long)one.Total(), (unsigned long long)one.deviceClass, (unsigned long long)one.stringProp,
		    (unsigned long long)one.otherProp, (unsigned long long)heap.allocations, seconds * 1e6);
		PERF_CHECK(state.devices.size() == 6);
		PERF_CHECK(one.deviceClass == vr::k_unMaxTrackedDeviceCount);
	}

	void TestPages()
	{
		Connect();
		ResetCalibration();
		struct PageCase
		{
			const char* name;
			ui::Page page;
			std::function<void()> setup;
		};
		const PageCase cases[] = {
		    {"calibration (idle, connected)", ui::Page::Calibration, [] {}},
		    {"calibration (idle, smoothing 50%)", ui::Page::Calibration, [] { CalCtx.smoothingParams.strength = 50; }},
		    {"calibration (continuous, status tab)", ui::Page::Calibration,
		     [] {
			     CalCtx.state = CalibrationState::Continuous;
			     CalCtx.referenceID = 0;
			     CalCtx.targetID = 5;
			     CalCtx.referenceStandby = {"oculus", "Oculus Quest 3", "2G0YC1ZF8J0123"};
			     CalCtx.targetStandby = {"lighthouse", "VIVE Tracker 3.0 MV", "LHR-1A2B3C4D"};
			     CalCtx.devicePoses[0].poseIsValid = true;
			     CalCtx.devicePoses[0].result = vr::TrackingResult_Running_OK;
			     CalCtx.devicePoses[5].poseIsValid = true;
			     CalCtx.devicePoses[5].result = vr::TrackingResult_Running_OK;
			     FillMetrics();
		     }},
		    {"smoothing (all off)", ui::Page::Smoothing, [] {}},
		    {"smoothing (50%)", ui::Page::Smoothing, [] { CalCtx.smoothingParams.strength = 50; }},
		    {"settings", ui::Page::Settings, [] {}},
		    {"base stations (4 stations, automation on)", ui::Page::BaseStations, [] { UseStations(); }},
		    {"learn", ui::Page::Learn, [] {}},
		    {"about (shell only)", ui::Page::About, [] {}},
		};

		std::map<std::string, Cost> costs;
		for (const PageCase& c : cases) {
			ResetCalibration();
			c.setup();
			ui::SetPage(c.page);
			double firstMs = 0.0;
			{
				const double start = perf::QpcSeconds();
				MainFrame();
				firstMs = (perf::QpcSeconds() - start) * 1000.0;
			}
			const Cost cost = Measure(kFramesPerSecond * 2, MainFrame);
			Print(c.name, cost);
			std::printf("INFO ui %-44s first frame %.2f ms, %.0f openvr calls/s at %d fps, %.1f blocking driver round trips/s\n", c.name,
			            firstMs, cost.callsPerFrame * kFramesPerSecond, kFramesPerSecond, cost.pipeRoundTrips / 2.0);
			perf::Budget((std::string("ui: ") + c.name + " (us/frame)").c_str(), cost.usPerFrame, 20000.0, "us");
			costs[c.name] = cost;
		}
		const Cost& shell = costs["about (shell only)"];
		PERF_CHECK(costs["calibration (idle, connected)"].callsPerFrame <= 10.0);
		PERF_CHECK(costs["calibration (idle, smoothing 50%)"].callsPerFrame <= 10.0);
		PERF_CHECK(costs["smoothing (50%)"].pipeRoundTrips / 2.0 < 10.0);
		PERF_CHECK(costs["settings"].allocsPerFrame <= shell.allocsPerFrame);
		PERF_CHECK(costs["base stations (4 stations, automation on)"].calls.settings / (double)shell.frames <= 0.1);

		ResetCalibration();
		CalCtx.state = CalibrationState::Continuous;
		FillMetrics();
		ui::SetPage(ui::Page::Calibration);
		ClickItem("More graphs", MainFrame);
		const Cost graphs = Measure(kFramesPerSecond * 2, MainFrame);
		Print("calibration (continuous, More Graphs tab)", graphs);
		ClickItem("Status", MainFrame);

		ResetCalibration();
		fakevr::RuntimeUp(false);
		VRSess.state = VRConnectionState::Disconnected;
		VRSess.lastInitError = vr::VRInitError_Init_NoServerForBackgroundApp;
		stubs::knownDevices.clear();
		for (uint32_t id = 0; id < 8; ++id) {
			const fakevr::Device& d = fakevr::devices[id];
			stubs::knownDevices.push_back({d.system, d.model, d.serial, d.deviceClass});
		}
		ui::SetPage(ui::Page::Calibration);
		const Cost desktop = Measure(kFramesPerSecond * 2, MainFrame);
		Print("calibration (desktop mode, no SteamVR)", desktop);
		PERF_CHECK(desktop.callsPerFrame == 0.0);
		stubs::knownDevices.clear();
		Connect();
	}

	void TestCalibrationPageCache()
	{
		Connect();
		ResetCalibration();
		const int frames = kFramesPerSecond * 10;
		ContentFrame(current::CalibrationContent);
		const Cost before = Measure(frames, [] { ContentFrame(current::CalibrationContent); });
		ResetCalibration();
		InvalidateVRStateCache();
		const Cost after = Measure(frames, [] { ContentFrame(candidate::CalibrationContent); });
		Print("calibration content, VRState reloaded every frame", before);
		Print("calibration content, LoadVRState cache (1 s)", after);
		std::printf("INFO ui calibration content openvr calls/s at %d fps: reload every frame %.0f, cached %.0f (%.0f of them "
		            "IsTrackedDeviceConnected)\n",
		            kFramesPerSecond, before.callsPerFrame * kFramesPerSecond, after.callsPerFrame * kFramesPerSecond,
		            after.calls.connected / (double)frames * kFramesPerSecond);
		PERF_CHECK(after.callsPerFrame <= 10.0);
		PERF_CHECK(after.callsPerFrame * 5.0 < before.callsPerFrame);
		PERF_CHECK(after.allocsPerFrame < before.allocsPerFrame);
	}

	bool SameRequests(const std::vector<protocol::Request>& a, const std::vector<protocol::Request>& b)
	{
		if (a.size() != b.size()) return false;
		for (size_t i = 0; i < a.size(); ++i) {
			if (!spacecal::SameDriverState(a[i], b[i])) return false;
		}
		return true;
	}

	void TestScanAndApply()
	{
		Connect();
		struct Case
		{
			const char* name;
			std::function<void()> setup;
		};
		const Case cases[] = {
		    {"idle profile", [] {}},
		    {"continuous, hide tracker",
		     [] {
			     CalCtx.state = CalibrationState::Continuous;
			     CalCtx.targetID = 5;
			     CalCtx.quashTargetInContinuous = true;
		     }},
		    {"smoothing controllers too",
		     [] {
			     CalCtx.smoothControllers = true;
			     CalCtx.smoothingParams.strength = 40;
		     }},
		    {"headset in another space",
		     [] {
			     CalCtx.referenceTrackingSystem = "lighthouse";
			     CalCtx.targetTrackingSystem = "oculus";
		     }},
		    {"no profile", [] { CalCtx.validProfile = false; }},
		};
		for (const Case& c : cases) {
			ResetCalibration();
			c.setup();
			current::DriverLink before;
			fakevr::calls = {};
			current::ScanAndApplyProfileImpl(CalCtx, before);
			const uint64_t currentCalls = fakevr::calls.Total();
			const bool enabledBefore = CalCtx.enabled;
			fakevr::calls = {};
			const auto repeatHeap = perf::MeasureHeap([&] { current::ScanAndApplyProfileImpl(CalCtx, before); });
			const uint64_t repeatCalls = fakevr::calls.Total();
			const size_t sentFirst = before.sent.size();

			ResetCalibration();
			c.setup();
			fakevr::calls = {};
			const auto devices = candidate::ScanDevices();
			const uint64_t scanCalls = fakevr::calls.Total();
			current::DriverLink after;
			fakevr::calls = {};
			candidate::ScanAndApplyProfileImpl(CalCtx, devices, after);
			const uint64_t candidateCalls = fakevr::calls.Total();
			const auto candidateHeap = perf::MeasureHeap([&] { candidate::ScanAndApplyProfileImpl(CalCtx, devices, after); });

			PERF_CHECK(CalCtx.enabled == enabledBefore);
			PERF_CHECK(SameRequests(before.sent, after.sent));
			PERF_CHECK(candidateCalls == 0);
			std::printf("INFO ScanAndApplyProfile (%s): current %llu openvr calls per apply (repeat %llu, %llu allocs), candidate %llu per "
			            "apply after a %llu-call scan (%llu allocs); %zu driver requests, identical: %s\n",
			            c.name, (unsigned long long)currentCalls, (unsigned long long)repeatCalls,
			            (unsigned long long)repeatHeap.allocations, (unsigned long long)candidateCalls, (unsigned long long)scanCalls,
			            (unsigned long long)candidateHeap.allocations, sentFirst, SameRequests(before.sent, after.sent) ? "yes" : "NO");
		}
	}

	void TestSmoothingStatsRefresh()
	{
		Connect();
		ResetCalibration();
		CalCtx.smoothingParams.strength = 50;
		std::vector<current::SmoothingStatsRow> rowsBefore, rowsAfter;
		fakevr::calls = {};
		stubs::smoothingQueries = 0;
		current::RefreshSmoothingStats(rowsBefore);
		const fakevr::Calls before = fakevr::calls;
		const int queriesBefore = stubs::smoothingQueries;
		const VRState& state = LoadVRState();
		fakevr::calls = {};
		stubs::smoothingQueries = 0;
		candidate::RefreshSmoothingStats(state, rowsAfter);
		const fakevr::Calls after = fakevr::calls;
		const int queriesAfter = stubs::smoothingQueries;
		std::printf("INFO smoothing stats refresh (every 0.5 s while the panel shows and strength > 0): current %llu openvr calls + %d "
		            "blocking pipe round trips, candidate %llu openvr calls + %d round trips\n",
		            (unsigned long long)before.Total(), queriesBefore, (unsigned long long)after.Total(), queriesAfter);
		PERF_CHECK(rowsBefore.size() == rowsAfter.size());
		for (size_t i = 0; i < rowsBefore.size() && i < rowsAfter.size(); ++i) {
			PERF_CHECK(rowsBefore[i].label == rowsAfter[i].label);
			PERF_CHECK(rowsBefore[i].stats.openVRID == rowsAfter[i].stats.openVRID);
		}
		PERF_CHECK(after.Total() < before.Total());
		PERF_CHECK(queriesAfter <= queriesBefore);
	}

	void TestSettingsEdits()
	{
		Connect();
		ResetCalibration();
		displayHeight = 3000.0f;
		ui::SetPage(ui::Page::Settings);
		for (int i = 0; i < 3; ++i)
			MainFrame();
		stubs::saveProfile = 0;
		const double scaleBefore = CalCtx.calibratedScale;
		const char* rejected[] = {"0", "-2", "nan", "inf", "1e999"};
		bool scaleKept = true;
		for (const char* text : rejected) {
			TypeInto("##label", 3, text, MainFrame);
			scaleKept &= CalCtx.calibratedScale == scaleBefore;
		}
		TypeInto("##label", 3, "1.25", MainFrame);
		const double scaleAccepted = CalCtx.calibratedScale;
		TypeInto("##label", 0, "nan", MainFrame);
		const double offsetAfterNan = CalCtx.continuousCalibrationOffset.x();
		TypeInto("##label", 0, "0.5", MainFrame);
		const double offsetAccepted = CalCtx.continuousCalibrationOffset.x();
		const int typingSaves = stubs::saveProfile;
		std::printf("INFO settings editors: playspace scale kept %.2f for 0, -2, nan, inf, 1e999, took %.2f; offset X kept %.2f for nan, "
		            "took %.2f; %d profile saves\n",
		            scaleBefore, scaleAccepted, offsetAfterNan, offsetAccepted, typingSaves);
		PERF_CHECK(scaleKept);
		PERF_CHECK(scaleAccepted == 1.25);
		PERF_CHECK(offsetAfterNan == 0.0);
		PERF_CHECK(offsetAccepted == 0.5);
		PERF_CHECK(typingSaves >= 2);

		stubs::saveProfile = 0;
		CalCtx.continuousCalibrationThreshold = 7.0f;
		ClickItem("Reset settings", MainFrame);
		PERF_CHECK(CalCtx.continuousCalibrationThreshold == 7.0f);
		PERF_CHECK(stubs::saveProfile == 0);
		ClickItem("Reset settings", MainFrame, 1);
		std::printf("INFO settings Reset settings: threshold %.2f, %d profile saves\n", CalCtx.continuousCalibrationThreshold,
		            stubs::saveProfile);
		PERF_CHECK(CalCtx.continuousCalibrationThreshold == 1.5f);
		PERF_CHECK(stubs::saveProfile == 1);

		ResetCalibration();
		CalCtx.state = CalibrationState::Editing;
		ui::SetPage(ui::Page::Calibration);
		for (int i = 0; i < 3; ++i)
			MainFrame();
		bool profileScaleKept = true;
		for (const char* text : rejected) {
			TypeInto("##Scale", 0, text, MainFrame);
			profileScaleKept &= CalCtx.calibratedScale == 1.0;
		}
		TypeInto("##Yaw", 0, "nan", MainFrame);
		const double yawAfterNan = CalCtx.calibratedRotation(1);
		TypeInto("##Scale", 0, "1.5", MainFrame);
		const double profileScaleAccepted = CalCtx.calibratedScale;
		std::printf("INFO profile editor: scale kept for 0, -2, nan, inf, 1e999 %s, took %.2f; yaw after nan %.2f\n",
		            profileScaleKept ? "yes" : "NO", profileScaleAccepted, yawAfterNan);
		PERF_CHECK(profileScaleKept);
		PERF_CHECK(profileScaleAccepted == 1.5);
		PERF_CHECK(yawAfterNan == 37.0);
		ResetCalibration();
		displayHeight = 800.0f;
	}

	void TestStationTick()
	{
		using namespace spacecal::basestations;
		Connect();
		UseStations();
		auto& ctl = BaseStationsController::Get();
		ctl.Tick(false);
		ctl.Tick(false);
		const int ticks = 1000;
		const auto idle = perf::MeasureHeap([&] {
			for (int i = 0; i < ticks; ++i)
				ctl.Tick(false);
		});
		const std::vector<DiscoveredAdvert> batch = StationAdverts(PowerState::AwakeFromSleep);
		size_t copied = 0;
		const auto copies = perf::MeasureHeap([&] {
			for (int i = 0; i < ticks; ++i)
				copied += std::vector<DiscoveredAdvert>(batch).size();
		});
		PERF_CHECK(copied == ticks * batch.size());
		const auto repeated = perf::MeasureHeap([&] {
			for (int i = 0; i < ticks; ++i) {
				fakeble::queued = batch;
				ctl.Tick(false);
			}
		});
		std::printf("INFO base stations Tick (4 stations): %.1f allocs per idle tick; %.1f allocs per tick with repeated adverts, of "
		            "which %.1f are the fake client's copies\n",
		            idle.allocations / (double)ticks, repeated.allocations / (double)ticks, copies.allocations / (double)ticks);
		PERF_CHECK(idle.allocations == 0);
		PERF_CHECK(repeated.allocations == copies.allocations);

		auto find = [&ctl](const char* serial) -> const Station* {
			for (const Station& station : ctl.Stations()) {
				if (station.serial == serial) return &station;
			}
			return nullptr;
		};
		fakeble::queued = StationAdverts(PowerState::Sleeping);
		ctl.Tick(false);
		const Station* first = find("LHB-1A2B3C4D");
		PERF_CHECK(first && first->powerState == PowerState::Sleeping && first->nickname == "Left corner");
		ctl.SetNickname("LHB-E5F60718", "Back wall");
		ctl.Tick(false);
		const Station* last = find("LHB-E5F60718");
		PERF_CHECK(last && last->nickname == "Back wall");
		PERF_CHECK(ctl.Stations().size() == 4);
		ctl.SetNickname("LHB-E5F60718", "");
	}

	void TestDriverConflictStamp()
	{
		wchar_t temp[MAX_PATH] = {};
		GetTempPathW(MAX_PATH, temp);
		const std::filesystem::path root = std::filesystem::path(temp) / L"wk" / L"polish-perf-overlay" / L"fake_driver";
		const std::filesystem::path dll = root / L"bin" / L"win64" / L"driver_01spacecalibrator.dll";
		std::error_code ec;
		std::filesystem::create_directories(dll.parent_path(), ec);
		{
			std::ofstream file(dll, std::ios::binary);
			file << "x";
		}
		DriverConflictCtx.ownDriverDir = root.string();
		DriverConflictCtx.RefreshOwnDriverDll();
		const std::string stamp = DriverConflictCtx.OwnDriverDllStamp();
		PERF_CHECK(!stamp.empty());
		PERF_CHECK(DriverConflictCtx.OwnDriverDllPath() == dll.string());
		const double refreshUs = perf::BestSecondsPerOp(5, 200, [](int) { DriverConflictCtx.RefreshOwnDriverDll(); }) * 1e6;
		std::filesystem::last_write_time(dll, std::filesystem::last_write_time(dll, ec) - std::chrono::hours(49), ec);

		Connect();
		ResetCalibration();
		DriverConflictCtx.report = spacecal::ConflictReport{};
		DriverConflictCtx.report.tier = spacecal::ConflictTier::Blocking;
		DriverConflictCtx.report.ownDriverStale = true;
		ui::SetPage(ui::Page::Calibration);
		const Cost blocked = Measure(kFramesPerSecond, MainFrame);
		Print("calibration (stale driver panel)", blocked);
		const bool cachedWhileDrawing = DriverConflictCtx.OwnDriverDllStamp() == stamp;
		DriverConflictCtx.RefreshOwnDriverDll();
		const bool refreshSeesChange = DriverConflictCtx.OwnDriverDllStamp() != stamp;
		std::printf("INFO stale driver panel: stamp computed in Refresh (%.2f us per refresh), unchanged while drawing after the DLL "
		            "time moved: %s, picked up on the next refresh: %s\n",
		            refreshUs, cachedWhileDrawing ? "yes" : "NO", refreshSeesChange ? "yes" : "NO");
		PERF_CHECK(cachedWhileDrawing);
		PERF_CHECK(refreshSeesChange);
		DriverConflictCtx.report = spacecal::ConflictReport{};
		DriverConflictCtx.ownDriverDir.clear();
		DriverConflictCtx.RefreshOwnDriverDll();
		std::filesystem::remove(dll, ec);
	}

	void TestSaveOnDrag()
	{
		Connect();
		ResetCalibration();
		ui::SetPage(ui::Page::Smoothing);
		stubs::saveProfile = 0;
		stubs::applySmoothing = 0;
		DragItem("Smoothing", 0.1f, 0.6f, MainFrame);
		std::printf("INFO drag Smoothing slider over 30 frames: strength now %d, %d profile saves, %d driver applies\n",
		            (int)CalCtx.smoothingParams.strength, stubs::saveProfile, stubs::applySmoothing);
		PERF_CHECK(CalCtx.smoothingParams.strength > 0);
		PERF_CHECK(stubs::saveProfile <= 1);

		ResetCalibration();
		ui::SetPage(ui::Page::Settings);
		stubs::saveProfile = 0;
		const float before = CalCtx.continuousCalibrationThreshold;
		DragItem("##recalibration_threshold_slider", 0.1f, 0.6f, MainFrame);
		std::printf("INFO drag Settings recalibration threshold over 30 frames: %.2f -> %.2f, %d profile saves\n", before,
		            CalCtx.continuousCalibrationThreshold, stubs::saveProfile);
		PERF_CHECK(CalCtx.continuousCalibrationThreshold != before);
		PERF_CHECK(stubs::saveProfile == 1);
	}

	bool SameStandbyDevice(const StandbyDevice& standby, const fakevr::Device& device)
	{
		return standby.trackingSystem == device.system && standby.model == device.model && standby.serial == device.serial;
	}

	void TestPickersKeepLiveProfile()
	{
		Connect();
		ResetCalibration();
		ui::SetPage(ui::Page::Calibration);
		stubs::saveProfile = 0;
		for (int i = 0; i < 5; ++i)
			MainFrame();
		PERF_CHECK(CalCtx.referenceID == -1 && CalCtx.targetID == -1);
		PERF_CHECK(CalCtx.referenceStandby.serial.empty() && CalCtx.targetStandby.serial.empty());

		ClickContentItemById("##ReferenceTrackingSystem", MainFrame);
		ClickItem("SteamVR Tracking", MainFrame);
		for (int i = 0; i < 3; ++i)
			MainFrame();
		const bool liveKept = CalCtx.referenceTrackingSystem == "oculus" && CalCtx.targetTrackingSystem == "lighthouse" &&
		                      CalCtx.referenceID == -1 && CalCtx.targetID == -1 && CalCtx.referenceStandby.serial.empty();
		const int savesWhilePicking = stubs::saveProfile;

		ClickItem("Start calibration", MainFrame);
		const bool committed = CalCtx.referenceTrackingSystem == "lighthouse" && CalCtx.targetTrackingSystem == "oculus" &&
		                       CalCtx.referenceID == 5 && CalCtx.targetID == 1 &&
		                       SameStandbyDevice(CalCtx.referenceStandby, fakevr::devices[5]) &&
		                       SameStandbyDevice(CalCtx.targetStandby, fakevr::devices[1]);
		ClickItem("Close", MainFrame);

		ResetCalibration();
		for (int i = 0; i < 3; ++i)
			MainFrame();
		ClickItem("Start calibration", MainFrame);
		const bool defaultStart = CalCtx.referenceTrackingSystem == "oculus" && CalCtx.targetTrackingSystem == "lighthouse" &&
		                          CalCtx.referenceID == 1 && CalCtx.targetID == 5 &&
		                          SameStandbyDevice(CalCtx.referenceStandby, fakevr::devices[1]) &&
		                          SameStandbyDevice(CalCtx.targetStandby, fakevr::devices[5]);
		ClickItem("Close", MainFrame);

		CalCtx.targetID = 6;
		CalCtx.targetStandby = {"lighthouse", fakevr::devices[6].model, fakevr::devices[6].serial};
		MainFrame();
		ClickItem("Continuous calibration", MainFrame);
		const bool externalChangeFollowed =
		    CalCtx.targetID == 6 && SameStandbyDevice(CalCtx.targetStandby, fakevr::devices[6]) && CalCtx.referenceID == 1;
		std::printf("INFO device pickers: live profile kept while picking %s (%d saves), picked spaces and devices copied on Start %s, "
		            "untouched pickers start like before %s, outside changes followed %s\n",
		            liveKept ? "yes" : "NO", savesWhilePicking, committed ? "yes" : "NO", defaultStart ? "yes" : "NO",
		            externalChangeFollowed ? "yes" : "NO");
		PERF_CHECK(liveKept);
		PERF_CHECK(savesWhilePicking == 0);
		PERF_CHECK(committed);
		PERF_CHECK(defaultStart);
		PERF_CHECK(externalChangeFollowed);
		ResetCalibration();
	}

	void TestIdentifyPulses()
	{
		Connect();
		ResetCalibration();
		ui::SetPage(ui::Page::Calibration);
		for (int i = 0; i < 3; ++i)
			MainFrame();
		fakevr::haptics.clear();
		fakevr::hapticMicroseconds.clear();
		stubs::redraws = 0;
		std::vector<size_t> perFrame;
		auto counted = [&perFrame] {
			const size_t before = fakevr::haptics.size();
			MainFrame();
			perFrame.push_back(fakevr::haptics.size() - before);
		};
		ClickItem("Identify selected devices (vibrates)", counted);
		for (int i = 0; i < kFramesPerSecond; ++i)
			counted();
		size_t total = 0, busiest = 0, lastPulseFrame = 0, firstPulseFrame = perFrame.size();
		for (size_t i = 0; i < perFrame.size(); ++i) {
			total += perFrame[i];
			busiest = std::max(busiest, perFrame[i]);
			if (perFrame[i] != 0) {
				lastPulseFrame = i;
				firstPulseFrame = std::min(firstPulseFrame, i);
			}
		}
		bool pairsMatchSelection = fakevr::haptics.size() % 2 == 0;
		for (size_t i = 0; i + 1 < fakevr::haptics.size(); i += 2) {
			pairsMatchSelection &= fakevr::haptics[i] == 5 && fakevr::haptics[i + 1] == 1;
		}
		const size_t pairs = total / 2;
		const double seconds = (lastPulseFrame - firstPulseFrame) / (double)kFramesPerSecond;
		const double frameGapUs = 1.0e6 / kFramesPerSecond;
		const unsigned short expectedFill = (unsigned short)std::clamp(frameGapUs - 1000.0, 2000.0, 3999.0);
		bool durationsMatch = fakevr::hapticMicroseconds.size() == fakevr::haptics.size() && fakevr::hapticMicroseconds.size() >= 2 &&
		                      fakevr::hapticMicroseconds[0] == 2000 && fakevr::hapticMicroseconds[1] == 2000;
		double targetBuzzUs = 0.0;
		for (size_t i = 0; i < fakevr::hapticMicroseconds.size(); ++i) {
			if (i >= 2) durationsMatch &= fakevr::hapticMicroseconds[i] == expectedFill;
			if (i % 2 == 0) targetBuzzUs += fakevr::hapticMicroseconds[i];
		}
		const double duty = targetBuzzUs / 0.5e6;
		std::printf("INFO identify: %zu pulse pairs over %.2f s, at most %zu pulses in one frame, %d redraw requests; pulses after the "
		            "first last %u us at %d fps, %.0f%% of the 0.5 s buzzing\n",
		            pairs, seconds, busiest, stubs::redraws, (unsigned)expectedFill, kFramesPerSecond, duty * 100.0);
		PERF_CHECK(busiest == 2);
		PERF_CHECK(pairs >= 44 && pairs <= 46);
		PERF_CHECK(pairsMatchSelection);
		PERF_CHECK(lastPulseFrame + 10 < perFrame.size());
		PERF_CHECK(stubs::redraws > 0);
		PERF_CHECK(durationsMatch);
		PERF_CHECK(duty >= 0.3);
		ResetCalibration();
	}

	DeviceSelection SelectionOf(const CalibrationContext& ctx)
	{
		DeviceSelection selection;
		selection.referenceTrackingSystem = ctx.referenceTrackingSystem;
		selection.targetTrackingSystem = ctx.targetTrackingSystem;
		selection.referenceID = ctx.referenceID;
		selection.targetID = ctx.targetID;
		selection.referenceStandby = ctx.referenceStandby;
		selection.targetStandby = ctx.targetStandby;
		return selection;
	}

	bool SameSelectionAs(const DeviceSelection& a, const DeviceSelection& b)
	{
		auto sameStandby = [](const StandbyDevice& x, const StandbyDevice& y) {
			return x.trackingSystem == y.trackingSystem && x.model == y.model && x.serial == y.serial;
		};
		return a.referenceTrackingSystem == b.referenceTrackingSystem && a.targetTrackingSystem == b.targetTrackingSystem &&
		       a.referenceID == b.referenceID && a.targetID == b.targetID && sameStandby(a.referenceStandby, b.referenceStandby) &&
		       sameStandby(a.targetStandby, b.targetStandby);
	}

	DeviceSelection StartWithSlimeTarget()
	{
		Connect();
		fakevr::Set(8, vr::TrackedDeviceClass_GenericTracker, "SlimeVR", "SlimeVR Tracker (Left Upper Leg)", "human://LEFT_UPPER_LEG");
		ResetCalibration();
		CalCtx.referenceID = 1;
		CalCtx.targetID = 5;
		CalCtx.referenceStandby = {"oculus", fakevr::devices[1].model, fakevr::devices[1].serial};
		CalCtx.targetStandby = {"lighthouse", fakevr::devices[5].model, fakevr::devices[5].serial};
		const DeviceSelection before = SelectionOf(CalCtx);
		ui::SetPage(ui::Page::Calibration);
		InvalidateVRStateCache();
		for (int i = 0; i < 3; ++i)
			MainFrame();
		stubs::saveProfile = 0;
		ClickContentItemById("##TargetTrackingSystem", MainFrame);
		ClickItem("SlimeVR", MainFrame);
		for (int i = 0; i < 3; ++i)
			MainFrame();
		ClickItem("Start calibration", MainFrame);
		return before;
	}

	bool CommittedSlimeTarget()
	{
		return CalCtx.targetTrackingSystem == "SlimeVR" && CalCtx.targetID == 8 &&
		       SameStandbyDevice(CalCtx.targetStandby, fakevr::devices[8]) && CalCtx.referenceTrackingSystem == "oculus" &&
		       CalCtx.referenceID == 1;
	}

	void TestCancelRestoresSelection()
	{
		const DeviceSelection before = StartWithSlimeTarget();
		const bool committed = CommittedSlimeTarget();
		CalCtx.state = CalibrationState::Rotation;
		for (int i = 0; i < 3; ++i)
			MainFrame();
		ClickItem("Cancel", MainFrame);
		for (int i = 0; i < 3; ++i)
			MainFrame();
		const bool restored = SameSelectionAs(SelectionOf(CalCtx), before);
		const bool closed = !PopupOpen("Calibration progress") && CalCtx.state == CalibrationState::None;
		const bool rescanSoon = CalCtx.timeLastScan < -1.0e8;
		const int saves = stubs::saveProfile;
		ClickItem("Start calibration", MainFrame);
		const bool pickersRestored = SameSelectionAs(SelectionOf(CalCtx), before);
		ClickItem("Close", MainFrame);
		std::printf("INFO cancel: new target committed on Start %s, selection restored %s, popup closed %s, rescan on next tick %s, "
		            "pickers show the restored devices %s, %d profile saves\n",
		            committed ? "yes" : "NO", restored ? "yes" : "NO", closed ? "yes" : "NO", rescanSoon ? "yes" : "NO",
		            pickersRestored ? "yes" : "NO", saves);
		PERF_CHECK(committed);
		PERF_CHECK(restored);
		PERF_CHECK(closed);
		PERF_CHECK(rescanSoon);
		PERF_CHECK(pickersRestored);
		PERF_CHECK(saves == 0);
		fakevr::devices[8] = fakevr::Device{};
		InvalidateVRStateCache();
		ResetCalibration();
	}

	void TestCloseAfterFailureRestoresSelection()
	{
		const DeviceSelection before = StartWithSlimeTarget();
		const bool committed = CommittedSlimeTarget();
		CalCtx.state = CalibrationState::None;
		CalCtx.failureReason = kFailureTargetNotTracking;
		for (int i = 0; i < 3; ++i)
			MainFrame();
		ClickItem("Try again", MainFrame);
		for (int i = 0; i < 3; ++i)
			MainFrame();
		const bool keptForRetry = CommittedSlimeTarget();
		ClickItem("Close", MainFrame);
		for (int i = 0; i < 3; ++i)
			MainFrame();
		const bool restored = SameSelectionAs(SelectionOf(CalCtx), before);
		const bool closed = !PopupOpen("Calibration progress");
		const int saves = stubs::saveProfile;

		ClickItem("Start calibration", MainFrame);
		CalCtx.failureReason = kFailureMissingDevice;
		GImGui->TestEngineHookItems = true;
		for (int i = 0; i < 3; ++i)
			MainFrame();
		const bool missingHidesRetry = items::Find("Try again") == nullptr && items::Find("Close") != nullptr;
		GImGui->TestEngineHookItems = false;
		ClickItem("Close", MainFrame);
		std::printf("INFO close after failure: new target committed %s, kept for Try again %s, selection restored on Close %s, popup "
		            "closed %s, %d profile saves; missing-device reason hides Try again %s\n",
		            committed ? "yes" : "NO", keptForRetry ? "yes" : "NO", restored ? "yes" : "NO", closed ? "yes" : "NO", saves,
		            missingHidesRetry ? "yes" : "NO");
		PERF_CHECK(committed);
		PERF_CHECK(keptForRetry);
		PERF_CHECK(restored);
		PERF_CHECK(closed);
		PERF_CHECK(saves == 0);
		PERF_CHECK(missingHidesRetry);
		fakevr::devices[8] = fakevr::Device{};
		InvalidateVRStateCache();
		ResetCalibration();
	}

	void TestSuccessKeepsSelection()
	{
		StartWithSlimeTarget();
		CalCtx.state = CalibrationState::None;
		CalCtx.failureReason.clear();
		for (int i = 0; i < 3; ++i)
			MainFrame();
		ClickItem("Close", MainFrame);
		for (int i = 0; i < 3; ++i)
			MainFrame();
		const bool kept = CommittedSlimeTarget() && !CalCtx.hasSelectionBeforeCommit;
		std::printf("INFO success: new selection kept after Close %s\n", kept ? "yes" : "NO");
		PERF_CHECK(kept);
		fakevr::devices[8] = fakevr::Device{};
		InvalidateVRStateCache();
		ResetCalibration();
	}

	struct FakeChaperone final : public vr::IVRChaperoneSetup
	{
		std::vector<vr::HmdQuad_t> live;

		bool CommitWorkingCopy(vr::EChaperoneConfigFile) override { return true; }
		void RevertWorkingCopy() override {}
		bool GetWorkingPlayAreaSize(float* x, float* z) override
		{
			*x = 4.0f;
			*z = 3.0f;
			return true;
		}
		bool GetWorkingPlayAreaRect(vr::HmdQuad_t*) override { return false; }
		bool GetWorkingCollisionBoundsInfo(vr::HmdQuad_t*, uint32_t* count) override
		{
			*count = 0;
			return false;
		}
		bool GetLiveCollisionBoundsInfo(vr::HmdQuad_t* buffer, uint32_t* count) override
		{
			if (!buffer || *count < live.size()) {
				*count = (uint32_t)live.size();
				return false;
			}
			std::copy(live.begin(), live.end(), buffer);
			*count = (uint32_t)live.size();
			return true;
		}
		bool GetWorkingSeatedZeroPoseToRawTrackingPose(vr::HmdMatrix34_t*) override { return false; }
		bool GetWorkingStandingZeroPoseToRawTrackingPose(vr::HmdMatrix34_t* m) override
		{
			*m = {};
			m->m[0][0] = m->m[1][1] = m->m[2][2] = 1.0f;
			m->m[0][3] = 0.25f;
			return true;
		}
		void SetWorkingPlayAreaSize(float, float) override {}
		void SetWorkingCollisionBoundsInfo(vr::HmdQuad_t*, uint32_t) override {}
		void SetWorkingPerimeter(vr::HmdVector2_t*, uint32_t) override {}
		void SetWorkingSeatedZeroPoseToRawTrackingPose(const vr::HmdMatrix34_t*) override {}
		void SetWorkingStandingZeroPoseToRawTrackingPose(const vr::HmdMatrix34_t*) override {}
		void ReloadFromDisk(vr::EChaperoneConfigFile) override {}
		bool GetLiveSeatedZeroPoseToRawTrackingPose(vr::HmdMatrix34_t*) override { return false; }
		bool ExportLiveToBuffer(char*, uint32_t*) override { return false; }
		bool ImportFromBufferToWorking(const char*, uint32_t) override { return false; }
		void ShowWorkingSetPreview() override {}
		void HideWorkingSetPreview() override {}
		void RoomSetupStarting() override {}
	};

	void TestChaperoneCopyKeepsSavedBounds()
	{
		CalibrationContext::Chaperone saved;
		saved.valid = true;
		saved.autoApply = false;
		saved.geometry.resize(3);
		for (size_t i = 0; i < saved.geometry.size(); ++i) {
			for (int c = 0; c < 4; ++c)
				saved.geometry[i].vCorners[c] = {(float)i, (float)c, 2.5f};
		}
		saved.playSpaceSize = {2.0f, 1.5f};
		saved.standingCenter.m[0][3] = 7.0f;

		FakeChaperone setup;
		CalibrationContext::Chaperone chaperone = saved;
		const bool copiedEmpty = CopyLiveChaperoneBounds(setup, chaperone);
		bool kept = chaperone.valid && !chaperone.autoApply && chaperone.geometry.size() == saved.geometry.size() &&
		            chaperone.playSpaceSize.v[0] == 2.0f && chaperone.playSpaceSize.v[1] == 1.5f &&
		            chaperone.standingCenter.m[0][3] == 7.0f;
		kept = kept && std::memcmp(chaperone.geometry.data(), saved.geometry.data(), sizeof(vr::HmdQuad_t) * saved.geometry.size()) == 0;

		setup.live.resize(5);
		for (size_t i = 0; i < setup.live.size(); ++i) {
			for (int c = 0; c < 4; ++c)
				setup.live[i].vCorners[c] = {(float)i * 0.5f, c < 2 ? 0.0f : 2.4f, -1.0f};
		}
		const bool copiedLive = CopyLiveChaperoneBounds(setup, chaperone);
		const bool tookLive = chaperone.valid && chaperone.geometry.size() == 5 && chaperone.geometry[4].vCorners[0].v[0] == 2.0f &&
		                      chaperone.playSpaceSize.v[0] == 4.0f && chaperone.standingCenter.m[0][3] == 0.25f;
		std::printf("INFO chaperone copy: zero live quads skipped %s and saved bounds kept %s; 5 live quads copied %s\n",
		            !copiedEmpty ? "yes" : "NO", kept ? "yes" : "NO", copiedLive && tookLive ? "yes" : "NO");
		PERF_CHECK(!copiedEmpty);
		PERF_CHECK(kept);
		PERF_CHECK(copiedLive);
		PERF_CHECK(tookLive);
	}

	void TestUpdatePromptWaitsForOtherPopups()
	{
		Connect();
		ResetCalibration();
		ui::SetPage(ui::Page::Calibration);
		for (int i = 0; i < 3; ++i)
			MainFrame();
		ClickItem("Start calibration", MainFrame);
		const bool progressOpen = PopupOpen("Calibration progress");
		UpdaterCtx.available.tag = "v2099.1.1.0";
		UpdaterCtx.state = UpdateState::Available;
		for (int i = 0; i < 5; ++i)
			MainFrame();
		const bool waited = !PopupOpen("Update available");
		ClickItem("Close", MainFrame);
		for (int i = 0; i < 2; ++i)
			MainFrame();
		const bool openedAfter = PopupOpen("Update available") && !PopupOpen("Calibration progress");
		std::printf("INFO update prompt: waited while another popup was open %s, opened once it closed %s\n", waited ? "yes" : "NO",
		            openedAfter ? "yes" : "NO");
		PERF_CHECK(progressOpen);
		PERF_CHECK(waited);
		PERF_CHECK(openedAfter);
		ClickItem("Later", MainFrame);
		PERF_CHECK(!PopupOpen("Update available"));
		PERF_CHECK(UpdaterCtx.state == UpdateState::Idle);
		ResetCalibration();
	}

	void TestDebugGraphEarlyReturn()
	{
		ResetCalibration();
		FillMetrics();
		bool skippedChild = false;
		bool balanced = true;
		for (int frame = 0; frame < 6 && balanced; ++frame) {
			BeginFrame();
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
			ImGui::SetNextWindowSize(ImVec2(400.0f, 300.0f), ImGuiCond_Always);
			ImGui::Begin("scrolled-away debug graphs", nullptr, bareWindowFlags);
			ImGui::Dummy(ImVec2(10.0f, 2000.0f));
			ImGuiWindow* window = GImGui->CurrentWindow;
			const int colors = GImGui->ColorStack.Size;
			const int plotColors = ImPlot::GetCurrentContext()->ColorModifiers.Size;
			ShowCalibrationDebug(1, 3);
			balanced = GImGui->CurrentWindow == window && GImGui->ColorStack.Size == colors &&
			           ImPlot::GetCurrentContext()->ColorModifiers.Size == plotColors;
			for (ImGuiWindow* child : GImGui->Windows) {
				if (child->ParentWindow == window && (child->Flags & ImGuiWindowFlags_ChildWindow) && child->SkipItems) skippedChild = true;
			}
			if (!balanced) break;
			ImGui::End();
			ImGui::Render();
		}
		std::printf("INFO debug graphs scrolled out of view: child skipped %s, style stacks balanced %s\n", skippedChild ? "yes" : "NO",
		            balanced ? "yes" : "NO");
		PERF_CHECK(skippedChild);
		PERF_CHECK(balanced);
	}

	void TestAutoDetectTick()
	{
		Connect();
		ResetCalibration();
		CalCtx.validProfile = false;
		auto& detect = AutoDetectController::Get();
		double t = 100.0;
		detect.Tick(t);
		fakevr::calls = {};
		const int ticks = kFramesPerSecond * 4;
		const auto heap = perf::MeasureHeap([&] {
			for (int i = 0; i < ticks; ++i) {
				t += 1.0 / kFramesPerSecond;
				detect.Tick(t);
			}
		});
		std::printf("INFO auto-detect tick (no profile, dashboard open %d Hz loop): %.2f allocs/tick, %.1f openvr calls/s (VRState::Load "
		            "every 2 s)\n",
		            kFramesPerSecond, heap.allocations / (double)ticks, fakevr::calls.Total() / 4.0);
		PERF_CHECK(detect.Active());
		detect.Cancel();
	}

	void TestStartupAndFirstUse()
	{
		const double legacyMs = perf::BestSecondsPerOp(1, 1, [](int) { DetectLegacyInstalls(); }) * 1000.0;
		const double conflictFirst =
		    perf::BestSecondsPerOp(1, 1, [](int) { DriverConflictCtx.Refresh(spacecal::HandshakeOutcome::Unavailable); });
		const double conflictBest =
		    perf::BestSecondsPerOp(3, 1, [](int) { DriverConflictCtx.Refresh(spacecal::HandshakeOutcome::Unavailable); });
		const double noteMs = perf::BestSecondsPerOp(3, 1, [](int) { UpdaterCtx.LoadLastRunNote(); }) * 1000.0;
		std::printf("INFO startup: DriverConflictCtx.Refresh %.2f ms first, %.2f ms warm (also runs on every driver connect attempt, every "
		            "2 s while the handshake fails)\n",
		            conflictFirst * 1000.0, conflictBest * 1000.0);
		std::printf("INFO DetectLegacyInstalls warm %.2f ms (runs once, on the first Settings frame; fake runtime path, so SteamVR driver "
		            "folders are not probed)\n",
		            legacyMs);
		std::printf("INFO startup: Updater LoadLastRunNote %.2f ms\n", noteMs);
		perf::Budget("startup: driver conflict refresh (ms)", conflictBest * 1000.0, 1000.0, "ms");
		DriverConflictCtx.report = spacecal::ConflictReport{};
	}

}

int main()
{
	PERF_CHECK(perf::InstallHeapMeter());

	TestFontAtlas();
	SetupContext();
	TestVRStateLoad();
	TestPages();
	TestCalibrationPageCache();
	TestScanAndApply();
	TestSmoothingStatsRefresh();
	TestSettingsEdits();
	TestStationTick();
	TestDriverConflictStamp();
	TestSaveOnDrag();
	TestPickersKeepLiveProfile();
	TestIdentifyPulses();
	TestCancelRestoresSelection();
	TestCloseAfterFailureRestoresSelection();
	TestSuccessKeepsSelection();
	TestChaperoneCopyKeepsSavedBounds();
	TestUpdatePromptWaitsForOtherPopups();
	TestDebugGraphEarlyReturn();
	TestAutoDetectTick();
	TestStartupAndFirstUse();

	ImPlot::DestroyContext();
	ImGui::DestroyContext();
	perf::RemoveHeapMeter();
	MH_Uninitialize();
	return perf::Finish("perf_overlay_ui_tests");
}
