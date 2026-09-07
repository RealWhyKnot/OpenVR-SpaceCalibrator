#include "stdafx.h"
#include "VRSession.h"
#include "Constants.h"

#include <cstdio>
#include <format>
#include <iostream>
#include <stdexcept>
#include <string>

VRSessionState VRSess;

void ActivateMultipleDrivers()
{
	vr::EVRSettingsError vrSettingsError;
	bool enabled = vr::VRSettings()->GetBool(vr::k_pch_SteamVR_Section, vr::k_pch_SteamVR_ActivateMultipleDrivers_Bool, &vrSettingsError);

	if (vrSettingsError != vr::VRSettingsError_None) {
		std::string err = "Could not read \"" + std::string(vr::k_pch_SteamVR_ActivateMultipleDrivers_Bool) +
		                  "\" setting: " + vr::VRSettings()->GetSettingsErrorNameFromEnum(vrSettingsError);

		throw std::runtime_error(err);
	}

	if (!enabled) {
		vr::VRSettings()->SetBool(vr::k_pch_SteamVR_Section, vr::k_pch_SteamVR_ActivateMultipleDrivers_Bool, true, &vrSettingsError);
		if (vrSettingsError != vr::VRSettingsError_None) {
			std::string err = "Could not set \"" + std::string(vr::k_pch_SteamVR_ActivateMultipleDrivers_Bool) +
			                  "\" setting: " + vr::VRSettings()->GetSettingsErrorNameFromEnum(vrSettingsError);

			throw std::runtime_error(err);
		}

		std::cerr << "Enabled \"" << vr::k_pch_SteamVR_ActivateMultipleDrivers_Bool << "\" setting" << '\n';
	}
	else {
		std::cerr << "\"" << vr::k_pch_SteamVR_ActivateMultipleDrivers_Bool << "\" setting previously enabled" << '\n';
	}
}

void InitVR()
{
	auto initError = vr::VRInitError_None;
	vr::VR_Init(&initError, vr::VRApplication_Overlay);
	if (initError != vr::VRInitError_None) {
		auto error = vr::VR_GetVRInitErrorAsEnglishDescription(initError);
		throw std::runtime_error("OpenVR error:" + std::string(error));
	}

	if (!vr::VR_IsInterfaceVersionValid(vr::IVRSystem_Version)) {
		throw std::runtime_error("OpenVR error: Outdated IVRSystem_Version");
	}
	else if (!vr::VR_IsInterfaceVersionValid(vr::IVRSettings_Version)) {
		throw std::runtime_error("OpenVR error: Outdated IVRSettings_Version");
	}
	else if (!vr::VR_IsInterfaceVersionValid(vr::IVROverlay_Version)) {
		throw std::runtime_error("OpenVR error: Outdated IVROverlay_Version");
	}

	ActivateMultipleDrivers();
}

void TryCreateVROverlay()
{
	if (VRSess.overlayMainHandle || !vr::VROverlay()) return;

	vr::VROverlayError error = vr::VROverlay()->CreateDashboardOverlay(OPENVR_APPLICATION_KEY, "Space Calibrator",
	                                                                   &VRSess.overlayMainHandle, &VRSess.overlayThumbnailHandle);

	if (error == vr::VROverlayError_KeyInUse) {
		throw std::runtime_error("Another instance of Space Calibrator is already running");
	}
	else if (error != vr::VROverlayError_None) {
		throw std::runtime_error("Error creating VR overlay: " + std::string(vr::VROverlay()->GetOverlayErrorNameFromEnum(error)));
	}

	vr::VROverlay()->SetOverlayWidthInMeters(VRSess.overlayMainHandle, 3.0f);
	vr::VROverlay()->SetOverlayInputMethod(VRSess.overlayMainHandle, vr::VROverlayInputMethod_Mouse);
	vr::VROverlay()->SetOverlayFlag(VRSess.overlayMainHandle, vr::VROverlayFlags_SendVRDiscreteScrollEvents, true);

	std::string iconPath = AppCwd();
	iconPath += "\\icon.png";
	vr::VROverlay()->SetOverlayFromFile(VRSess.overlayThumbnailHandle, iconPath.c_str());
}

void VerifySetupCorrect()
{
	if (vr::VRApplications()->IsApplicationInstalled(STEAM_OPENVR_APPLICATION_KEY) &&
	    vr::VRApplications()->GetApplicationAutoLaunch(STEAM_OPENVR_APPLICATION_KEY)) {
		std::cout << "Disabling autolaunch of the Steam copy of Space Calibrator\n";
		vr::VRApplications()->SetApplicationAutoLaunch(STEAM_OPENVR_APPLICATION_KEY, false);
	}

	if (!vr::VRApplications()->IsApplicationInstalled(OPENVR_APPLICATION_KEY)) {
		std::string manifestPath = std::format("{}\\{}", AppCwd(), "manifest.vrmanifest");
		std::cout << "Adding manifest path: " << manifestPath << '\n';
		auto vrAppErr = vr::VRApplications()->AddApplicationManifest(manifestPath.c_str());
		if (vrAppErr != vr::VRApplicationError_None) {
			fprintf(stderr, "Failed to add manifest: %s\n", vr::VRApplications()->GetApplicationsErrorNameFromEnum(vrAppErr));
		}
		else {
			vr::VRApplications()->SetApplicationAutoLaunch(OPENVR_APPLICATION_KEY, true);
		}
	}
	else {
		std::cout << "Space Calibrator already registered with SteamVR. Skipping..." << '\n';
	}

	if (vr::VRApplications()->IsApplicationInstalled(LEGACY_OPENVR_APPLICATION_KEY)) {
		std::cout << "Found a legacy version of Space Calibrator..." << '\n';
		vr::EVRApplicationError appErr = vr::EVRApplicationError::VRApplicationError_None;
		char manifestPathBuffer[MAX_PATH + 32] = {};
		uint32_t szBufferSize =
		    vr::VRApplications()->GetApplicationPropertyString(LEGACY_OPENVR_APPLICATION_KEY, vr::VRApplicationProperty_BinaryPath_String,
		                                                       manifestPathBuffer, sizeof(manifestPathBuffer), &appErr);
		if (appErr != vr::VRApplicationError_None) {
			std::cout << "Failed to get binary path of " << LEGACY_OPENVR_APPLICATION_KEY << '\n';
			return;
		}

		const char* newFileName = "manifest.vrmanifest";
		char* lastSlash = strrchr(manifestPathBuffer, '\\');
		if (lastSlash) {
			*(lastSlash + 1) = '\0';
			strcat_s(manifestPathBuffer, sizeof(manifestPathBuffer), newFileName);
		}

		appErr = vr::VRApplications()->RemoveApplicationManifest(manifestPathBuffer);
		if (appErr != vr::VRApplicationError_None) {
			std::cout << "Failed to remove legacy application manifest. You may have duplicate entries in the overlays list." << '\n';
		}
	}
}
