#pragma once

#include <imgui/imgui.h>

namespace ui {

	bool BeginCard(const char* id);
	void EndCard();
	void TextHeading(const char* text);
	void PillText(const char* text, const ImVec4& color);
	bool IconButton(const char* icon, const char* label, const ImVec2& size = ImVec2(0, 0));
	bool CheckboxWithDescription(const char* label, const char* description, bool* value);

}
