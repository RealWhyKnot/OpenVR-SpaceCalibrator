#include "KeyboardText.h"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

	int failures = 0;

#define CHECK(cond)                                                                                                                        \
	do {                                                                                                                                   \
		if (!(cond)) {                                                                                                                     \
			std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);                                                                     \
			++failures;                                                                                                                    \
		}                                                                                                                                  \
	} while (0)

	void PrefillCopiesUtf8Text()
	{
		char out[0x400];
		std::memset(out, 'x', sizeof out);
		CHECK(spacecal::keyboard::CopyPrefill("2.5", 3, out, sizeof out) == 3);
		CHECK(std::strcmp(out, "2.5") == 0);

		const char* accented = "Pi\xC3\xB1o";
		CHECK(spacecal::keyboard::CopyPrefill(accented, 5, out, sizeof out) == 5);
		CHECK(std::strcmp(out, accented) == 0);
	}

	void PrefillTruncatesAtCodepointBoundary()
	{
		char out[4];
		CHECK(spacecal::keyboard::CopyPrefill("ab\xC3\xA9", 4, out, sizeof out) == 2);
		CHECK(std::strcmp(out, "ab") == 0);

		CHECK(spacecal::keyboard::CopyPrefill("abcdef", 6, out, sizeof out) == 3);
		CHECK(std::strcmp(out, "abc") == 0);
	}

	void PrefillHandlesEmptyInput()
	{
		char out[8] = {'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x'};
		CHECK(spacecal::keyboard::CopyPrefill(nullptr, 3, out, sizeof out) == 0);
		CHECK(out[0] == '\0');
		out[0] = 'x';
		CHECK(spacecal::keyboard::CopyPrefill("abc", 0, out, sizeof out) == 0);
		CHECK(out[0] == '\0');
		out[0] = 'x';
		CHECK(spacecal::keyboard::CopyPrefill("abc", -1, out, sizeof out) == 0);
		CHECK(out[0] == '\0');
		out[0] = 'x';
		CHECK(spacecal::keyboard::CopyPrefill("abc", 3, out, 0) == 0);
		CHECK(out[0] == 'x');
	}

	void PrepareState(ImGuiInputTextState& state, int capacity, const char* initial)
	{
		state.BufCapacityA = capacity;
		state.TextA.resize(capacity + 1);
		state.CurLenA = (int)std::strlen(initial);
		std::memcpy(state.TextA.Data, initial, state.CurLenA + 1);
	}

	void DoneStoresUtf8TextAndLength()
	{
		ImGuiInputTextState state;
		PrepareState(state, 64, "2.5");
		spacecal::keyboard::ApplyDone(state, "0.99876");
		CHECK(state.CurLenA == 7);
		CHECK(std::strcmp(state.TextA.Data, "0.99876") == 0);
	}

	void DoneTruncatesToWidgetCapacity()
	{
		ImGuiInputTextState state;
		PrepareState(state, 33, "");
		const std::string entry = "0123456789012345678901234567890123456";
		CHECK(entry.size() == 37);
		spacecal::keyboard::ApplyDone(state, entry.c_str());
		CHECK(state.CurLenA == 32);
		CHECK(std::string(state.TextA.Data) == entry.substr(0, 32));

		const std::string split = std::string(31, 'a') + "\xC3\xA9" + "bcdef";
		spacecal::keyboard::ApplyDone(state, split.c_str());
		CHECK(state.CurLenA == 31);
		CHECK(std::string(state.TextA.Data) == std::string(31, 'a'));
	}

	void DoneClampsCursorAndClearsUndo()
	{
		ImGuiInputTextState state;
		PrepareState(state, 64, "123456789");
		state.Stb->cursor = 9;
		state.Stb->select_start = 2;
		state.Stb->select_end = 9;
		state.Stb->undostate.undo_point = 3;
		state.Stb->undostate.undo_char_point = 5;
		spacecal::keyboard::ApplyDone(state, "12");
		CHECK(state.Stb->cursor <= 2);
		CHECK(state.Stb->select_start <= 2);
		CHECK(state.Stb->select_end <= 2);
		CHECK(state.Stb->undostate.undo_point == 0);
		CHECK(state.Stb->undostate.undo_char_point == 0);
		CHECK(state.Stb->undostate.redo_point == IMSTB_TEXTEDIT_UNDOSTATECOUNT);
		CHECK(state.Stb->undostate.redo_char_point == IMSTB_TEXTEDIT_UNDOCHARCOUNT);
	}

	void NullStateIsNoOp()
	{
		CHECK(!spacecal::keyboard::ApplyDone(static_cast<ImGuiInputTextState*>(nullptr), "1.0"));

		ImGuiInputTextState state;
		PrepareState(state, 64, "2.5");
		CHECK(spacecal::keyboard::ApplyDone(&state, nullptr));
		CHECK(state.CurLenA == 3);
		CHECK(std::strcmp(state.TextA.Data, "2.5") == 0);
	}

	struct WidgetHarness
	{
		struct Guarded
		{
			char text[33] = {};
			char canary[16] = {};
		};

		ImGuiContext* context = nullptr;
		double value = 2.5;
		Guarded guarded;
		ImVec2 valueMin{}, valueMax{}, textMin{}, textMax{};

		WidgetHarness()
		{
			std::memset(guarded.canary, 0x5A, sizeof guarded.canary);
			context = ImGui::CreateContext();
			ImGuiIO& io = ImGui::GetIO();
			io.DisplaySize = ImVec2(1200.0f, 800.0f);
			io.DeltaTime = 1.0f / 90.0f;
			io.IniFilename = nullptr;
			io.Fonts->AddFontDefault();
			unsigned char* pixels = nullptr;
			int width = 0, height = 0;
			io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
			Frame();
		}

		~WidgetHarness() { ImGui::DestroyContext(context); }

		void Frame()
		{
			ImGuiIO& io = ImGui::GetIO();
			ImGui::NewFrame();
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(io.DisplaySize);
			ImGui::Begin("harness", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
			ImGui::InputDouble("##value", &value, 0.0, 0.0, "%.1f");
			valueMin = ImGui::GetItemRectMin();
			valueMax = ImGui::GetItemRectMax();
			ImGui::InputText("##text", guarded.text, sizeof guarded.text);
			textMin = ImGui::GetItemRectMin();
			textMax = ImGui::GetItemRectMax();
			ImGui::End();
			ImGui::EndFrame();
		}

		void Click(ImVec2 min, ImVec2 max)
		{
			ImGuiIO& io = ImGui::GetIO();
			io.AddMousePosEvent((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
			Frame();
			io.AddMouseButtonEvent(0, true);
			Frame();
			io.AddMouseButtonEvent(0, false);
			Frame();
		}

		bool CanaryIntact() const
		{
			for (char c : guarded.canary) {
				if (c != 0x5A) return false;
			}
			return true;
		}
	};

	void KeyboardRoundTripStoresDecimalValue()
	{
		WidgetHarness harness;
		harness.Click(harness.valueMin, harness.valueMax);
		ImGuiInputTextState* state = ImGui::GetInputTextState(ImGui::GetActiveID());
		CHECK(state != nullptr);
		if (state == nullptr) return;

		char prefill[0x400];
		CHECK(spacecal::keyboard::CopyPrefill(state->TextA.Data, state->CurLenA, prefill, sizeof prefill) == 3);
		CHECK(std::strcmp(prefill, "2.5") == 0);

		CHECK(spacecal::keyboard::ApplyDone(state, "0.99876"));
		harness.Frame();
		ImGui::ClearActiveID();
		harness.Frame();
		harness.Frame();
		CHECK(harness.value == 0.99876);

		harness.Click(harness.valueMin, harness.valueMax);
		CHECK(spacecal::keyboard::ApplyDone(ImGui::GetInputTextState(ImGui::GetActiveID()), "2.5"));
		harness.Frame();
		ImGui::ClearActiveID();
		harness.Frame();
		CHECK(harness.value == 2.5);
	}

	void KeyboardRoundTripTruncatesLongEntry()
	{
		WidgetHarness harness;
		harness.Click(harness.textMin, harness.textMax);
		CHECK(spacecal::keyboard::ApplyDone(ImGui::GetInputTextState(ImGui::GetActiveID()), "0123456789012345678901234567890123456"));
		harness.Frame();
		ImGui::ClearActiveID();
		harness.Frame();
		CHECK(std::strlen(harness.guarded.text) == 32);
		CHECK(std::string(harness.guarded.text) == "01234567890123456789012345678901");
		CHECK(harness.CanaryIntact());
	}

	void UndoAfterKeyboardDoneKeepsText()
	{
		WidgetHarness harness;
		harness.Click(harness.textMin, harness.textMax);
		ImGuiIO& io = ImGui::GetIO();
		io.AddInputCharactersUTF8("123456789");
		harness.Frame();
		CHECK(std::strcmp(harness.guarded.text, "123456789") == 0);

		CHECK(spacecal::keyboard::ApplyDone(ImGui::GetInputTextState(ImGui::GetActiveID()), "7"));
		harness.Frame();
		CHECK(std::strcmp(harness.guarded.text, "7") == 0);

		io.AddKeyEvent(ImGuiMod_Ctrl, true);
		io.AddKeyEvent(ImGuiKey_Z, true);
		harness.Frame();
		io.AddKeyEvent(ImGuiKey_Z, false);
		io.AddKeyEvent(ImGuiMod_Ctrl, false);
		harness.Frame();
		CHECK(std::strcmp(harness.guarded.text, "7") == 0);
		ImGuiInputTextState* state = ImGui::GetInputTextState(ImGui::GetActiveID());
		CHECK(state != nullptr);
		if (state != nullptr) CHECK(state->CurLenA == 1);
		CHECK(harness.CanaryIntact());
	}

}

int main()
{
	PrefillCopiesUtf8Text();
	PrefillTruncatesAtCodepointBoundary();
	PrefillHandlesEmptyInput();
	DoneStoresUtf8TextAndLength();
	DoneTruncatesToWidgetCapacity();
	DoneClampsCursorAndClearsUndo();
	NullStateIsNoOp();
	KeyboardRoundTripStoresDecimalValue();
	KeyboardRoundTripTruncatesLongEntry();
	UndoAfterKeyboardDoneKeepsText();
	if (failures != 0) {
		std::printf("%d failure(s)\n", failures);
		return EXIT_FAILURE;
	}
	std::printf("keyboard text tests passed\n");
	return EXIT_SUCCESS;
}
