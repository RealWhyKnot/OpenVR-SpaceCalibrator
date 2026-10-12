#pragma once

#include <imgui/imgui.h>

namespace ui {

	bool BeginCard(const char* id);
	void EndCard();
	void TextHeading(const char* text);
	void PillText(const char* text, const ImVec4& color);
	bool IconButton(const char* icon, const char* label, const ImVec2& size = ImVec2(0, 0));
	bool CheckboxWithDescription(const char* label, const char* description, bool* value);
	bool ConfirmPopup(const char* title, const char* text, const char* action);
	void Tooltip(const char* text, ImGuiHoveredFlags flags = 0);
	extern const char* const kCalibrationSpeedTips[3];

	enum class NoteTone
	{
		Ok,
		Pending,
		Error,
	};

	ImVec4 NoteTint(NoteTone tone);
	bool BeginNote(const char* id, NoteTone tone);
	void EndNote();
	void Note(const char* id, NoteTone tone, const char* text);
	void BeginDisabledLook(bool disabled);
	void EndDisabledLook();

}
