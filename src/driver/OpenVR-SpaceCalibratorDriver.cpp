#include "OpenVR-SpaceCalibratorDriver.h"
#include "ServerTrackedDeviceProvider.h"
#include "Logging.h"

#include <cstdio>
#include <cstring>
#include <openvr_driver.h>

OPENVRSPACECALIBRATORDRIVER_API void* HmdDriverFactory(const char* pInterfaceName, int* pReturnCode)
{
	TRACE("HmdDriverFactory(%s)", pInterfaceName);

	if (std::strcmp(vr::IServerTrackedDeviceProvider_Version, pInterfaceName) == 0) {
		static ServerTrackedDeviceProvider server;
		return &server;
	}

	if (pReturnCode) {
		*pReturnCode = vr::VRInitError_Init_InterfaceNotFound;
	}
	return nullptr;
}
