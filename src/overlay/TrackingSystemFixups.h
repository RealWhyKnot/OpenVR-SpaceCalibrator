#pragma once

#include <openvr.h>
#include <string>

inline constexpr const char* kPimaxCrystalHmdSystem = "Pimax Crystal HMD";
inline constexpr const char* kPimaxCrystalControllerSystem = "Pimax Crystal Controllers";

// Crystal's projection matrix is constant 0s or 1s except for [0][3], which stores the IPD offset from the nose
inline bool IsPimaxCrystalEyeToHead(const vr::HmdMatrix34_t& m)
{
	return m.m[0][0] == 1 && m.m[0][1] == 0 && m.m[0][2] == 0 && m.m[1][0] == 0 && m.m[1][1] == 1 && m.m[1][2] == 0 && m.m[1][3] == 0 &&
	       m.m[2][0] == 0 && m.m[2][1] == 0 && m.m[2][2] == 1 && m.m[2][3] == 0;
}

inline bool IsPimaxCrystalController(const std::string& renderModel, const std::string& connectedWirelessDongle)
{
	return renderModel.find("{aapvr}") != std::string::npos && renderModel.find("crystal") != std::string::npos &&
	       connectedWirelessDongle.find("lighthouse") != std::string::npos;
}

inline void ApplyTrackingSystemFixups(uint32_t id, vr::ETrackedDeviceClass deviceClass, std::string& trackingSystem)
{
	if (deviceClass == vr::TrackedDeviceClass_HMD && trackingSystem == "aapvr") {
		vr::HmdMatrix34_t eyeToHeadLeft = vr::VRSystem()->GetEyeToHeadTransform(vr::Eye_Left);
		if (IsPimaxCrystalEyeToHead(eyeToHeadLeft)) {
			trackingSystem = kPimaxCrystalHmdSystem;
		}
	}
	else if (deviceClass == vr::TrackedDeviceClass_Controller && trackingSystem == "oculus") {
		char buffer[vr::k_unMaxPropertyStringSize];
		vr::ETrackedPropertyError err = vr::TrackedProp_Success;
		buffer[0] = 0;
		vr::VRSystem()->GetStringTrackedDeviceProperty(id, vr::Prop_RenderModelName_String, buffer, vr::k_unMaxPropertyStringSize, &err);
		std::string renderModel(buffer);
		buffer[0] = 0;
		vr::VRSystem()->GetStringTrackedDeviceProperty(id, vr::Prop_ConnectedWirelessDongle_String, buffer, vr::k_unMaxPropertyStringSize,
		                                               &err);
		std::string connectedWirelessDongle(buffer);
		if (IsPimaxCrystalController(renderModel, connectedWirelessDongle)) {
			trackingSystem = kPimaxCrystalControllerSystem;
		}
	}
}
