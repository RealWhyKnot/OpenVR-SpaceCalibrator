#pragma once

#include "Calibration.h"
#include "Updater.h"
#include "VRState.h"

#include <vector>

void LoadProfile(CalibrationContext& ctx);
void SaveProfile(CalibrationContext& ctx);
void LoadUpdateSettings(UpdateSettings& settings);
void SaveUpdateSettings(const UpdateSettings& settings);
void LoadKnownDevices(std::vector<KnownDevice>& devices);
void SaveKnownDevices(const std::vector<KnownDevice>& devices);

namespace spacecal::basestations {
	struct BaseStationsSettings;
}
void LoadBaseStationsSettings(spacecal::basestations::BaseStationsSettings& settings);
void SaveBaseStationsSettings(const spacecal::basestations::BaseStationsSettings& settings);
