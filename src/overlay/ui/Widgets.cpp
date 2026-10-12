#include "stdafx.h"
#include "Widgets.h"
#include "Style.h"

#include <imgui/imgui_internal.h>

#include <string>

namespace ui {

	const char* const kCalibrationSpeedTips[3] = {
	    "Collects 100 samples, which takes at least 5 seconds. Continuous calibration works from the latest 100.",
	    "Collects 250 samples, which takes at least 12.5 seconds. Continuous calibration works from the latest 250.",
	    "Collects 500 samples, which takes at least 25 seconds. Continuous calibration works from the latest 500.",
	};

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
			const float indent = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x;
			ImGui::Indent(indent);
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			ImGui::TextWrapped("%s", description);
			ImGui::PopStyleColor();
			ImGui::Unindent(indent);
		}
		return changed;
	}

	bool ConfirmPopup(const char* title, const char* text, const char* action)
	{
		const ImGuiIO& io = ImGui::GetIO();
		const float width = io.DisplaySize.x - 80.0f < 520.0f ? io.DisplaySize.x - 80.0f : 520.0f;
		ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(width, 0.0f), ImGuiCond_Always);
		const ImGuiWindowFlags flags =
		    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
		bool confirmed = false;
		if (ImGui::BeginPopupModal(title, nullptr, flags)) {
			ImGui::Spacing();
			ImGui::TextWrapped("%s", text);
			ImGui::Spacing();
			const float button = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
			if (ImGui::Button(action, ImVec2(button, 0.0f))) {
				confirmed = true;
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(button, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
		return confirmed;
	}

	void Tooltip(const char* text, ImGuiHoveredFlags flags)
	{
		if (!ImGui::IsItemHovered(flags)) return;
		if (ImGui::BeginTooltip()) {
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 20.0f);
			ImGui::TextUnformatted(text);
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}
	}

	ImVec4 NoteTint(NoteTone tone)
	{
		switch (tone) {
			case NoteTone::Ok: return ImVec4(0.35f, 0.85f, 0.45f, 0.16f);
			case NoteTone::Pending: return ImVec4(1.0f, 0.75f, 0.3f, 0.22f);
			default: return ImVec4(0.9f, 0.4f, 0.4f, 0.24f);
		}
	}

	bool BeginNote(const char* id, NoteTone tone)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(10.0f, 6.0f));
		if (!ImGui::BeginTable(id, 1, ImGuiTableFlags_None)) {
			ImGui::PopStyleVar();
			return false;
		}
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(NoteTint(tone)));
		return true;
	}

	void EndNote()
	{
		ImGui::EndTable();
		ImGui::PopStyleVar();
	}

	void Note(const char* id, NoteTone tone, const char* text)
	{
		if (BeginNote(id, tone)) {
			ImGui::TextWrapped("%s", text);
			EndNote();
		}
	}

	static ImVector<bool> disabledLookStack;

	void BeginDisabledLook(bool disabled)
	{
		const bool push = disabled && (GImGui->CurrentItemFlags & ImGuiItemFlags_Disabled) == 0;
		disabledLookStack.push_back(push);
		if (push) {
			const ImVec4 fill(0.16f, 0.18f, 0.21f, 1.0f);
			const ImVec4 label(0.62f, 0.64f, 0.67f, 1.0f);
			const ImVec4 border(0.45f, 0.47f, 0.50f, 1.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, 1.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
			ImGui::PushStyleColor(ImGuiCol_Button, fill);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, fill);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, fill);
			ImGui::PushStyleColor(ImGuiCol_FrameBg, fill);
			ImGui::PushStyleColor(ImGuiCol_Text, label);
			ImGui::PushStyleColor(ImGuiCol_Border, border);
			ImGui::PushStyleColor(ImGuiCol_CheckMark, label);
			ImGui::PushStyleColor(ImGuiCol_SliderGrab, label);
		}
		ImGui::BeginDisabled(disabled);
	}

	void EndDisabledLook()
	{
		ImGui::EndDisabled();
		if (disabledLookStack.back()) {
			ImGui::PopStyleColor(8);
			ImGui::PopStyleVar(2);
		}
		disabledLookStack.pop_back();
	}
}
