#pragma once

#include <openvr.h>

struct VRSessionState
{
	vr::VROverlayHandle_t overlayMainHandle = 0;
	vr::VROverlayHandle_t overlayThumbnailHandle = 0;
};

extern VRSessionState VRSess;

void InitVR();
void ActivateMultipleDrivers();
void VerifySetupCorrect();
void TryCreateVROverlay();
