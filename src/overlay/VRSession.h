#pragma once

#include <openvr.h>
#include <string>

enum class VRConnectionState
{
	Disconnected,
	Connecting,
	Connected,
};

struct VRSessionState
{
	VRConnectionState state = VRConnectionState::Disconnected;
	vr::VROverlayHandle_t overlayMainHandle = 0;
	vr::VROverlayHandle_t overlayThumbnailHandle = 0;
	vr::EVRInitError lastInitError = vr::VRInitError_None;
	std::string statusText;
	double nextAttemptTime = 0.0;
	double nextServerCheckTime = 0.0;
	bool quitRequested = false;
};

extern VRSessionState VRSess;

bool IsVRServerRunning();
void VRSessionTick(double time);
void VRSessionHandleLost(double time);
void VRSessionDriverLost(double time, const std::string& why);
void ActivateMultipleDrivers();
void VerifySetupCorrect();
void TryCreateVROverlay();
