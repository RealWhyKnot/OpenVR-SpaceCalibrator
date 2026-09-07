#include "stdafx.h"
#include "OverlayApp.h"
#include "AppWindow.h"
#include "Calibration.h"
#include "LegacyInstall.h"
#include "UserInterface.h"
#include "VRSession.h"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/backends/imgui_impl_glfw.h>
#include <imgui/backends/imgui_impl_opengl3.h>
#include <GL/gl3w.h>
#include <GLFW/glfw3.h>
#include <openvr.h>

#include <algorithm>
#include <chrono>
#include <thread>

static char textBuf[0x400] = {};
static const float MINIMIZED_MAX_FPS = 60.0f;

static void DrawGithubConflictPopup()
{
	static bool githubPopupDismissed = false;

	if (!IsGithubVersionInstalled() || githubPopupDismissed) return;

	ImGui::OpenPopup("Conflicting Space Calibrator install");
	if (ImGui::BeginPopupModal("Conflicting Space Calibrator install", 0, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) {
		ImGui::Text("You have multiple versions of Space Calibrator installed!\n\nPlease uninstall the GitHub version to use "
		            "the Steam version of Space Calibrator.\n\nDo you wish to open the settings app to uninstall the GitHub "
		            "version of Space Calibrator? (SteamVR will have to be closed)");

		ImGui::NewLine();

		float windowWidth = ImGui::GetWindowWidth();
		ImGui::SetCursorPosX(windowWidth / 11.0f);
		if (ImGui::Button("Yes", ImVec2(windowWidth * 3.0f / 11.0f, 0))) {
			UninstallGithubSpaceCalibrator();
			githubPopupDismissed = true;
		}
		ImGui::SameLine();
		ImGui::SetCursorPosX(windowWidth / 11.0f * 6.0f);
		if (ImGui::Button("No", ImVec2(windowWidth * 3.0f / 11.0f, 0))) {
			githubPopupDismissed = true;
		}

		ImGui::EndPopup();
	}
}

void RunLoop()
{
	double lastFrameStartTime = glfwGetTime();

	while (!glfwWindowShouldClose(AppWindow.window)) {
		TryCreateVROverlay();
		double time = glfwGetTime();
		CalibrationTick(time);

		bool dashboardVisible = false;
		int width, height;
		glfwGetFramebufferSize(AppWindow.window, &width, &height);
		const bool windowVisible = (width > 0 && height > 0);

		if (VRSess.overlayMainHandle && vr::VROverlay()) {
			auto& io = ImGui::GetIO();
			dashboardVisible = vr::VROverlay()->IsActiveDashboardOverlay(VRSess.overlayMainHandle);

			static bool keyboardOpen = false, keyboardJustClosed = false;

			// After closing the keyboard, this code waits one frame for ImGui to pick up the new text from SetActiveText
			// before clearing the active widget. Then it waits another frame before allowing the keyboard to open again,
			// otherwise it will do so instantly since WantTextInput is still true on the second frame.
			if (keyboardJustClosed && keyboardOpen) {
				ImGui::ClearActiveID();
				keyboardOpen = false;
			}
			else if (keyboardJustClosed) {
				keyboardJustClosed = false;
			}
			else if (!io.WantTextInput) {
				// User might close the keyboard without hitting Done, so we unset the flag to allow it to open again.
				keyboardOpen = false;
			}
			else if (io.WantTextInput && !keyboardOpen && !keyboardJustClosed) {
				int id = ImGui::GetActiveID();
				auto textInfo = ImGui::GetInputTextState(id);

				if (textInfo != nullptr) {
					textBuf[0] = 0;
					int len = WideCharToMultiByte(CP_UTF8, 0, (LPCWCH)textInfo->TextA.Data, textInfo->TextA.Size, textBuf, sizeof(textBuf),
					                              nullptr, nullptr);
					textBuf[std::min(static_cast<size_t>(len), sizeof(textBuf) - 1)] = 0;

					uint32_t unFlags = 0;

					vr::VROverlay()->ShowKeyboardForOverlay(VRSess.overlayMainHandle, vr::k_EGamepadTextInputModeNormal,
					                                        vr::k_EGamepadTextInputLineModeSingleLine, unFlags, "Space Calibrator Overlay",
					                                        sizeof textBuf, textBuf, 0);
					keyboardOpen = true;
				}
			}

			vr::VREvent_t vrEvent;
			while (vr::VROverlay()->PollNextOverlayEvent(VRSess.overlayMainHandle, &vrEvent, sizeof(vrEvent))) {
				switch (vrEvent.eventType) {
					case vr::VREvent_MouseMove: io.AddMousePosEvent(vrEvent.data.mouse.x, vrEvent.data.mouse.y); break;
					case vr::VREvent_MouseButtonDown:
						io.AddMouseButtonEvent((vrEvent.data.mouse.button & vr::VRMouseButton_Left) == vr::VRMouseButton_Left ? 0 : 1,
						                       true);
						break;
					case vr::VREvent_MouseButtonUp:
						io.AddMouseButtonEvent((vrEvent.data.mouse.button & vr::VRMouseButton_Left) == vr::VRMouseButton_Left ? 0 : 1,
						                       false);
						break;
					case vr::VREvent_ScrollDiscrete: {
						float x = vrEvent.data.scroll.xdelta * 360.0f * 8.0f;
						float y = vrEvent.data.scroll.ydelta * 360.0f * 8.0f;
						io.AddMouseWheelEvent(x, y);
						break;
					}
					case vr::VREvent_KeyboardDone: {
						vr::VROverlay()->GetKeyboardText(textBuf, sizeof textBuf);

						int id = ImGui::GetActiveID();
						auto textInfo = ImGui::GetInputTextState(id);
						int bufSize = MultiByteToWideChar(CP_UTF8, 0, textBuf, -1, nullptr, 0);
						textInfo->TextA.resize(bufSize);
						MultiByteToWideChar(CP_UTF8, 0, textBuf, -1, (LPWSTR)textInfo->TextA.Data, bufSize);
						textInfo->CurLenA = bufSize;
						textInfo->CurLenA = WideCharToMultiByte(CP_UTF8, 0, (LPCWCH)textInfo->TextA.Data, textInfo->TextA.Size, nullptr, 0,
						                                        nullptr, nullptr);

						keyboardJustClosed = true;
						break;
					}
					case vr::VREvent_Quit: return;
				}
			}
		}

		if (windowVisible || dashboardVisible) {
			auto& io = ImGui::GetIO();

			// These change state now, so we must execute these before doing our own modifications to the io state for VR
			ImGui_ImplOpenGL3_NewFrame();
			ImGui_ImplGlfw_NewFrame();

			io.DisplaySize = ImVec2((float)AppWindow.fboWidth, (float)AppWindow.fboHeight);
			io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

			io.ConfigFlags = io.ConfigFlags & ~ImGuiConfigFlags_NoMouseCursorChange;
			if (dashboardVisible) {
				io.ConfigFlags = io.ConfigFlags | ImGuiConfigFlags_NoMouseCursorChange;
			}

			ImGui::NewFrame();

			BuildMainWindow(dashboardVisible);
			DrawGithubConflictPopup();

			ImGui::Render();

			glBindFramebuffer(GL_FRAMEBUFFER, AppWindow.fbo);
			glViewport(0, 0, AppWindow.fboWidth, AppWindow.fboHeight);
			glClearColor(0, 0, 0, 1);
			glClear(GL_COLOR_BUFFER_BIT);

			ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

			glBindFramebuffer(GL_FRAMEBUFFER, 0);

			if (width && height) {
				glBindFramebuffer(GL_READ_FRAMEBUFFER, AppWindow.fbo);
				glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
				glfwSwapBuffers(AppWindow.window);
			}

			if (dashboardVisible) {
				vr::Texture_t vrTex = {
				    .handle = (void*)
#if defined _WIN64 || defined _LP64
				                  (uint64_t)
#endif
				                      AppWindow.fboTexture,
				    .eType = vr::TextureType_OpenGL,
				    .eColorSpace = vr::ColorSpace_Auto,
				};

				vr::HmdVector2_t mouseScale = {(float)AppWindow.fboWidth, (float)AppWindow.fboHeight};

				vr::VROverlay()->SetOverlayTexture(VRSess.overlayMainHandle, &vrTex);
				vr::VROverlay()->SetOverlayMouseScale(VRSess.overlayMainHandle, &mouseScale);
			}
		}

		const double dashboardInterval = 1.0 / 90.0;
		double waitEventsTimeout = std::max(CalCtx.wantedUpdateInterval, dashboardInterval);

		if (dashboardVisible && waitEventsTimeout > dashboardInterval) waitEventsTimeout = dashboardInterval;

		if (ConsumeImmediateRedraw()) {
			waitEventsTimeout = 0;
		}

		glfwWaitEventsTimeout(waitEventsTimeout);

		// If we're minimized rendering won't limit our frame rate so we need to do it ourselves.
		if (glfwGetWindowAttrib(AppWindow.window, GLFW_ICONIFIED)) {
			double targetFrameTime = 1 / MINIMIZED_MAX_FPS;
			double waitTime = targetFrameTime - (glfwGetTime() - lastFrameStartTime);
			if (waitTime > 0) {
				std::this_thread::sleep_for(std::chrono::duration<double>(waitTime));
			}

			lastFrameStartTime += targetFrameTime;
		}
	}
}
