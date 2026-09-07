#pragma once

#include <cstdint>

class ServerTrackedDeviceProvider;

namespace spacecal::skeletal_hook {

	void Init(ServerTrackedDeviceProvider* driver);
	void Shutdown();
	void MarkFingersNeedReseed(uint16_t fingerBits);
	void TryInstallPublicHooks(void* iface);

}
