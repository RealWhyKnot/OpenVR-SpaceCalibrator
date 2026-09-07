#include "stdafx.h"
#include "Style.h"
#include "IconGlyphs.h"
#include "EmbeddedFonts.h"

#include <imgui/imgui.h>

namespace ui {

	ImFont* fontBody = nullptr;
	ImFont* fontHeading = nullptr;

	void LoadFonts()
	{
		ImGuiIO& io = ImGui::GetIO();

		fontBody = io.Fonts->AddFontFromMemoryCompressedTTF(InterFont_compressed_data, InterFont_compressed_size, 22.0f);

		ImFontConfig iconConfig;
		iconConfig.MergeMode = true;
		iconConfig.GlyphMinAdvanceX = 22.0f;
		iconConfig.GlyphOffset = ImVec2(0.0f, 3.0f);
		io.Fonts->AddFontFromMemoryCompressedTTF(IconFont_compressed_data, IconFont_compressed_size, 22.0f, &iconConfig, kIconGlyphRanges);

		fontHeading = io.Fonts->AddFontFromMemoryCompressedTTF(InterFont_compressed_data, InterFont_compressed_size, 30.0f);
	}

	void ApplyStyle()
	{
		ImGui::StyleColorsDark();
		ImGuiStyle& style = ImGui::GetStyle();

		style.WindowBorderSize = 0.0f;
		style.ChildRounding = 6.0f;
		style.FrameRounding = 5.0f;
		style.PopupRounding = 6.0f;
		style.TabRounding = 5.0f;
		style.GrabRounding = 5.0f;
		style.ScrollbarRounding = 12.0f;
		style.ScrollbarSize = 18.0f;
		style.FramePadding = ImVec2(10.0f, 6.0f);
		style.ItemSpacing = ImVec2(10.0f, 8.0f);
		style.WindowPadding = ImVec2(14.0f, 14.0f);

		ImVec4* colors = style.Colors;
		colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.09f, 0.10f, 1.00f);
		colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		colors[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.18f, 0.21f, 1.00f);
		colors[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.25f, 0.29f, 1.00f);
		colors[ImGuiCol_FrameBgActive] = ImVec4(0.26f, 0.30f, 0.35f, 1.00f);
		colors[ImGuiCol_Button] = ImVec4(0.18f, 0.32f, 0.51f, 1.00f);
		colors[ImGuiCol_ButtonHovered] = ImVec4(0.24f, 0.40f, 0.62f, 1.00f);
		colors[ImGuiCol_ButtonActive] = ImVec4(0.29f, 0.47f, 0.72f, 1.00f);
		colors[ImGuiCol_Header] = ImVec4(0.18f, 0.32f, 0.51f, 0.70f);
		colors[ImGuiCol_HeaderHovered] = ImVec4(0.24f, 0.40f, 0.62f, 0.85f);
		colors[ImGuiCol_HeaderActive] = ImVec4(0.29f, 0.47f, 0.72f, 1.00f);
		colors[ImGuiCol_Tab] = ImVec4(0.13f, 0.15f, 0.17f, 1.00f);
		colors[ImGuiCol_TabHovered] = ImVec4(0.24f, 0.40f, 0.62f, 1.00f);
		colors[ImGuiCol_TabActive] = ImVec4(0.18f, 0.32f, 0.51f, 1.00f);
		colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.14f, 0.16f, 1.00f);
		colors[ImGuiCol_CheckMark] = ImVec4(0.55f, 0.75f, 1.00f, 1.00f);
		colors[ImGuiCol_SliderGrab] = ImVec4(0.35f, 0.55f, 0.85f, 1.00f);
		colors[ImGuiCol_SliderGrabActive] = ImVec4(0.45f, 0.65f, 0.95f, 1.00f);
	}

}
