#pragma once

namespace ui {

	enum class Page
	{
		Calibration,
		BaseStations,
		Smoothing,
		Settings,
		Learn,
		About,
	};

	Page CurrentPage();
	void SetPage(Page page);
	void DrawSidebar();

}
