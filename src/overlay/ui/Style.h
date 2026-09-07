#pragma once

struct ImFont;

namespace ui {

	extern ImFont* fontBody;
	extern ImFont* fontHeading;

	void LoadFonts();
	void ApplyStyle();

}
