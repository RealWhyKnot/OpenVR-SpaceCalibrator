#include "stdafx.h"
#include "Widgets.h"
#include "Style.h"

#include <string>

namespace ui {

	bool BeginCard(const char* id)
	{
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.14f, 0.16f, 1.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
		bool open = ImGui::BeginChild(id, ImVec2(0, 0),
		                              ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
		ImGui::PopStyleVar();
		return open;
	}

	void EndCard()
	{
		ImGui::EndChild();
		ImGui::PopStyleVar();
		ImGui::PopStyleColor();
	}

	void TextHeading(const char* text)
	{
		ImGui::PushFont(fontHeading);
		ImGui::TextUnformatted(text);
		ImGui::PopFont();
		ImGui::Spacing();
	}

	void PillText(const char* text, const ImVec4& color)
	{
		ImVec2 textSize = ImGui::CalcTextSize(text);
		ImVec2 pos = ImGui::GetCursorScreenPos();
		const float padX = 10.0f, padY = 3.0f;
		ImVec2 size(textSize.x + padX * 2, textSize.y + padY * 2);
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, 0.25f)),
		                        size.y * 0.5f);
		drawList->AddText(ImVec2(pos.x + padX, pos.y + padY), ImGui::GetColorU32(color), text);
		ImGui::Dummy(size);
	}

	bool IconButton(const char* icon, const char* label, const ImVec2& size)
	{
		std::string text = std::string(icon) + "  " + label;
		return ImGui::Button(text.c_str(), size);
	}

	bool CheckboxWithDescription(const char* label, const char* description, bool* value)
	{
		bool changed = ImGui::Checkbox(label, value);
		if (description && description[0]) {
			ImGui::Indent(28.0f);
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			ImGui::TextWrapped("%s", description);
			ImGui::PopStyleColor();
			ImGui::Unindent(28.0f);
		}
		return changed;
	}

}
