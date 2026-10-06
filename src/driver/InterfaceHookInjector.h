#pragma once

#include <openvr_driver.h>

class ServerTrackedDeviceProvider;

void InjectHooks(ServerTrackedDeviceProvider* driver, vr::IVRDriverContext* pDriverContext);
void DisableHooks();