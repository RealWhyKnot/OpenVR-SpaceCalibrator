#pragma once

#include "Calibration.h"
#include "Updater.h"

void LoadProfile(CalibrationContext& ctx);
void SaveProfile(CalibrationContext& ctx);
void LoadUpdateSettings(UpdateSettings& settings);
void SaveUpdateSettings(const UpdateSettings& settings);
