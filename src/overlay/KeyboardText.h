#pragma once

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

namespace ImStb {
#include <imgui/imstb_textedit.h>
}

#include <cstddef>
#include <cstring>

static_assert(IMGUI_VERSION_NUM == 19143, "KeyboardText.h edits ImGuiInputTextState internals; recheck them against this ImGui version");

namespace spacecal::keyboard {

	inline size_t Utf8Prefix(const char* text, size_t len, size_t maxBytes)
	{
		if (len <= maxBytes) return len;
		size_t n = maxBytes;
		while (n > 0 && (static_cast<unsigned char>(text[n]) & 0xC0) == 0x80)
			--n;
		return n;
	}

	inline size_t CopyPrefill(const char* text, int len, char* out, size_t outSize)
	{
		if (out == nullptr || outSize == 0) return 0;
		size_t n = 0;
		if (text != nullptr && len > 0) {
			n = Utf8Prefix(text, static_cast<size_t>(len), outSize - 1);
			std::memcpy(out, text, n);
		}
		out[n] = '\0';
		return n;
	}

	inline void ApplyDone(ImGuiInputTextState& state, const char* utf8)
	{
		if (utf8 == nullptr || state.Stb == nullptr || state.BufCapacityA <= 0) return;
		const size_t n = Utf8Prefix(utf8, std::strlen(utf8), static_cast<size_t>(state.BufCapacityA - 1));
		const int len = static_cast<int>(n);
		if (state.TextA.Size < len + 1) state.TextA.resize(len + 1);
		std::memcpy(state.TextA.Data, utf8, n);
		state.TextA.Data[len] = '\0';
		state.CurLenA = len;
		state.CursorClamp();
		state.Stb->undostate.undo_point = 0;
		state.Stb->undostate.undo_char_point = 0;
		state.Stb->undostate.redo_point = IMSTB_TEXTEDIT_UNDOSTATECOUNT;
		state.Stb->undostate.redo_char_point = IMSTB_TEXTEDIT_UNDOCHARCOUNT;
		state.CursorAnimReset();
	}

	inline bool ApplyDone(ImGuiInputTextState* state, const char* utf8)
	{
		if (state == nullptr) return false;
		ApplyDone(*state, utf8);
		return true;
	}

}
