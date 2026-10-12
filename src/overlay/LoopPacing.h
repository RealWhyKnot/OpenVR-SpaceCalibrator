#pragma once

#include <algorithm>

namespace spacecal::pacing {

	inline constexpr double kDashboardFrameSeconds = 1.0 / 90.0;
	inline constexpr double kCalibrationTickSeconds = 0.05;
	inline constexpr double kWakeSlackSeconds = 0.001;
	inline constexpr double kIdleWakeCapSeconds = 0.25;

	inline double WaitSeconds(double now, double frameStart, double lastCalibrationTick, double wantedInterval, bool dashboardVisible,
	                          bool immediateRedraw)
	{
		if (dashboardVisible) return std::clamp(frameStart + kDashboardFrameSeconds - now, 0.0, kDashboardFrameSeconds);
		if (immediateRedraw) return 0.0;
		double untilTick = lastCalibrationTick + kCalibrationTickSeconds - now;
		if (untilTick <= 0.0) untilTick = kCalibrationTickSeconds;
		return std::min(kIdleWakeCapSeconds, std::max(wantedInterval, untilTick + kWakeSlackSeconds));
	}

}
