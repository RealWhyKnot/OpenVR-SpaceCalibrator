#pragma once

#include "Protocol.h"

namespace spacecal::stick_hook {

	void Init();
	void Shutdown();
	void SetConfig(const protocol::StickSmoothingConfig& config);
	void TryInstallPublicHooks(void* iface);
	void Pump();

}
