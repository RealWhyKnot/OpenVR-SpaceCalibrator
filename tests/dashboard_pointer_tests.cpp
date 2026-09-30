#include "DashboardPointer.h"

#include <imgui/imgui.h>

#include <cstdio>
#include <cstdlib>

namespace {

	int failures = 0;

#define CHECK(cond)                                                                                                                        \
	do {                                                                                                                                   \
		if (!(cond)) {                                                                                                                     \
			std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);                                                                     \
			++failures;                                                                                                                    \
		}                                                                                                                                  \
	} while (0)

	struct SliderHarness
	{
		ImGuiContext* context = nullptr;
		DashboardPointer pointer;
		int value = 0;
		ImVec2 min{};
		ImVec2 max{};

		SliderHarness()
		{
			context = ImGui::CreateContext();
			ImGuiIO& io = ImGui::GetIO();
			io.DisplaySize = ImVec2(1200.0f, 800.0f);
			io.DeltaTime = 1.0f / 90.0f;
			io.IniFilename = nullptr;
			unsigned char* pixels = nullptr;
			int width = 0, height = 0;
			io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
		}

		~SliderHarness() { ImGui::DestroyContext(context); }

		void Frame(bool reassert)
		{
			ImGuiIO& io = ImGui::GetIO();
			io.AddMousePosEvent(1500.0f, 400.0f);
			if (reassert) {
				pointer.Reassert(io);
			}
			ImGui::NewFrame();
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(io.DisplaySize);
			ImGui::Begin("harness", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
			ImGui::SetNextItemWidth(600.0f);
			ImGui::SliderInt("##slider", &value, 0, 100, "%d", ImGuiSliderFlags_AlwaysClamp);
			min = ImGui::GetItemRectMin();
			max = ImGui::GetItemRectMax();
			ImGui::End();
			ImGui::EndFrame();
		}

		ImVec2 At(float fraction) const { return ImVec2(min.x + (max.x - min.x) * fraction, (min.y + max.y) * 0.5f); }

		int Drag(bool reassert)
		{
			Frame(reassert);
			ImGuiIO& io = ImGui::GetIO();

			ImVec2 start = At(0.25f);
			pointer.Move(io, start.x, start.y);
			Frame(reassert);

			pointer.Move(io, start.x, start.y);
			io.AddMouseButtonEvent(0, true);
			Frame(reassert);

			for (int step = 1; step <= 5; ++step) {
				ImVec2 p = At(0.25f + 0.1f * step);
				pointer.Move(io, p.x, p.y);
				Frame(reassert);
			}

			io.AddMouseButtonEvent(0, false);
			Frame(reassert);
			Frame(reassert);
			return value;
		}
	};

	void DragFollowsLaserWhenDesktopCursorIsQueuedAfterIt()
	{
		SliderHarness harness;
		int result = harness.Drag(true);
		if (!(result >= 70 && result <= 80)) {
			std::printf("drag ended at %d\n", result);
		}
		CHECK(result >= 70 && result <= 80);
	}

	void StaleDesktopCursorWinsWithoutReassert()
	{
		SliderHarness harness;
		int result = harness.Drag(false);
		CHECK(result == 100);
	}

	void ReassertBeforeAnyMoveQueuesNothing()
	{
		SliderHarness harness;
		ImGuiIO& io = ImGui::GetIO();
		harness.pointer.Reassert(io);
		harness.Frame(false);
		CHECK(io.MousePos.x == 1500.0f);
	}

}

int main()
{
	DragFollowsLaserWhenDesktopCursorIsQueuedAfterIt();
	StaleDesktopCursorWinsWithoutReassert();
	ReassertBeforeAnyMoveQueuesNothing();
	if (failures != 0) {
		std::printf("%d failure(s)\n", failures);
		return EXIT_FAILURE;
	}
	std::printf("dashboard pointer tests passed\n");
	return EXIT_SUCCESS;
}
