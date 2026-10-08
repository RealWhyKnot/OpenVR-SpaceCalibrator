#pragma once

#include <algorithm>

namespace spacecal::pacing {

	inline constexpr double kDashboardFrameSeconds = 1.0 / 90.0;
	inline constexpr double kCalibrationTickSeconds = 0.05;
	inline constexpr double kWakeSlackSeconds = 0.001;

	inline double WaitSeconds(double now, double lastCalibrationTick, double wantedInterval, bool dashboardVisible, bool immediateRedraw)
	{
		if (immediateRedraw) return 0.0;
		if (dashboardVisible) return kDashboardFrameSeconds;
		double untilTick = lastCalibrationTick + kCalibrationTickSeconds - now;
		if (untilTick <= 0.0) untilTick = kCalibrationTickSeconds;
		return std::max(wantedInterval, untilTick + kWakeSlackSeconds);
	}

}
