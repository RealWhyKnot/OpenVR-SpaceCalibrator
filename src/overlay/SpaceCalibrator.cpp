#include "stdafx.h"
#include "Calibration.h"
#include "Configuration.h"
#include "EmbeddedFiles.h"
#include "UserInterface.h"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/backends/imgui_impl_glfw.h>
#include <imgui/backends/imgui_impl_opengl3.h>
#include <implot/implot.h>
#include <GL/gl3w.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <openvr.h>
#include <direct.h>
#include <chrono>
#include <thread>
#include <dwmapi.h>
#include <algorithm>
#include <filesystem>

#include <shellapi.h>

#include <stb_image.h>

#pragma comment(linker, "\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#define LEGACY_OPENVR_APPLICATION_KEY "pushrax.SpaceCalibrator"
#define OPENVR_APPLICATION_KEY "steam.overlay.3368750"
std::string c_SPACE_CALIBRATOR_STEAM_APP_ID = "3368750";
std::string c_STEAMVR_STEAM_APP_ID = "250820";
constexpr const char* STEAM_MUTEX_KEY = "Global\\MUTEX__SpaceCalibrator_Steam";
HANDLE hSteamMutex = INVALID_HANDLE_VALUE;
bool s_isGitHubVersionInstalled = false;

extern "C" __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
extern "C" __declspec(dllexport) DWORD AmdPowerXpressRequestHighPerformance = 0x00000001;

void CreateConsole()
{
	static bool created = false;
	if (!created) {
		AllocConsole();
		FILE* file = nullptr;
		freopen_s(&file, "CONIN$", "r", stdin);
		freopen_s(&file, "CONOUT$", "w", stdout);
		freopen_s(&file, "CONOUT$", "w", stderr);
		created = true;
	}
}

std::string GetRegistryString(const HKEY hKeyGroup, const char* szRegistryKey, const char* szRegistryPropKey) noexcept
{
	DWORD dwType = REG_SZ;
	HKEY hKey = 0;
	// 4 KiB string buffer
	char buffer[4 * 1024] = {};
	DWORD bufferSize = sizeof(buffer);
	LSTATUS lResult = RegOpenKeyExA(hKeyGroup, szRegistryKey, 0, KEY_QUERY_VALUE, &hKey);
	if (lResult == ERROR_SUCCESS) {
		lResult = RegQueryValueExA(hKey, szRegistryPropKey, NULL, &dwType, (LPBYTE)&buffer, &bufferSize);
		if (lResult == ERROR_SUCCESS) {
			return buffer;
		}
	}
	return "";
}

// #define DEBUG_LOGS

void GLFWErrorCallback(int error, const char* description)
{
	fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

void openGLDebugCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message,
                         const void* userParam)
{
	fprintf(stderr, "OpenGL Debug %u: %.*s\n", id, length, message);
}

static void HandleCommandLine(LPWSTR lpCmdLine);

static GLFWwindow* glfwWindow = nullptr;
static vr::VROverlayHandle_t overlayMainHandle = 0, overlayThumbnailHandle = 0;
static GLuint fboHandle = 0, fboTextureHandle = 0;
static int fboTextureWidth = 0, fboTextureHeight = 0;

static char cwd[MAX_PATH];
const float MINIMIZED_MAX_FPS = 60.0f;

enum DWMA_USE_IMMSERSIVE_DARK_MODE_ENUM
{
	DWMA_USE_IMMERSIVE_DARK_MODE = 20,
	DWMA_USE_IMMERSIVE_DARK_MODE_PRE_20H1 = 19,
};

const bool EnableDarkModeTopBar(const HWND windowHwmd)
{
	const BOOL darkBorder = TRUE;
	const bool ok = SUCCEEDED(DwmSetWindowAttribute(windowHwmd, DWMA_USE_IMMERSIVE_DARK_MODE, &darkBorder, sizeof(darkBorder))) ||
	                SUCCEEDED(DwmSetWindowAttribute(windowHwmd, DWMA_USE_IMMERSIVE_DARK_MODE_PRE_20H1, &darkBorder, sizeof(darkBorder)));
	return ok;
}

void CreateGLFWWindow()
{
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_RESIZABLE, false);

#ifdef DEBUG_LOGS
	glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);
#endif

	fboTextureWidth = 1200;
	fboTextureHeight = 800;

	glfwWindow = glfwCreateWindow(fboTextureWidth, fboTextureHeight, "Space Calibrator", nullptr, nullptr);
	if (!glfwWindow) throw std::runtime_error("Failed to create window");

	glfwMakeContextCurrent(glfwWindow);
	glfwSwapInterval(1);
	gl3wInit();

	// Minimise the window
	glfwIconifyWindow(glfwWindow);
	HWND windowHwmd = glfwGetWin32Window(glfwWindow);
	EnableDarkModeTopBar(windowHwmd);

	// Load icon and set it in the window
	GLFWimage images[1] = {};
	std::string iconPath = cwd;
	iconPath += "\\taskbar_icon.png";
	images[0].pixels = stbi_load(iconPath.c_str(), &images[0].width, &images[0].height, 0, 4);
	glfwSetWindowIcon(glfwWindow, 1, images);
	stbi_image_free(images[0].pixels);

#ifdef DEBUG_LOGS
	glDebugMessageCallback(openGLDebugCallback, nullptr);
	glEnable(GL_DEBUG_OUTPUT);
#endif

	ImGui::CreateContext();
	ImPlot::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
	io.IniFilename = nullptr;
	io.Fonts->AddFontFromMemoryCompressedTTF(DroidSans_compressed_data, DroidSans_compressed_size, 24.0f);

	ImGui_ImplGlfw_InitForOpenGL(glfwWindow, true);
	ImGui_ImplOpenGL3_Init("#version 330");

	ImGui::StyleColorsDark();

	glGenTextures(1, &fboTextureHandle);
	glBindTexture(GL_TEXTURE_2D, fboTextureHandle);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, fboTextureWidth, fboTextureHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

	glGenFramebuffers(1, &fboHandle);
	glBindFramebuffer(GL_FRAMEBUFFER, fboHandle);
	glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, fboTextureHandle, 0);

	GLenum drawBuffers[1] = {GL_COLOR_ATTACHMENT0};
	glDrawBuffers(1, drawBuffers);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		throw std::runtime_error("OpenGL framebuffer incomplete");
	}
}

void TryCreateVROverlay()
{
	if (overlayMainHandle || !vr::VROverlay()) return;

	vr::VROverlayError error =
	    vr::VROverlay()->CreateDashboardOverlay(OPENVR_APPLICATION_KEY, "Space Calibrator", &overlayMainHandle, &overlayThumbnailHandle);

	if (error == vr::VROverlayError_KeyInUse) {
		throw std::runtime_error("Another instance of Space Calibrator is already running");
	}
	else if (error != vr::VROverlayError_None) {
		throw std::runtime_error("Error creating VR overlay: " + std::string(vr::VROverlay()->GetOverlayErrorNameFromEnum(error)));
	}

	vr::VROverlay()->SetOverlayWidthInMeters(overlayMainHandle, 3.0f);
	vr::VROverlay()->SetOverlayInputMethod(overlayMainHandle, vr::VROverlayInputMethod_Mouse);
	vr::VROverlay()->SetOverlayFlag(overlayMainHandle, vr::VROverlayFlags_SendVRDiscreteScrollEvents, true);

	std::string iconPath = cwd;
	iconPath += "\\icon.png";
	vr::VROverlay()->SetOverlayFromFile(overlayThumbnailHandle, iconPath.c_str());
}

void ActivateMultipleDrivers()
{
	vr::EVRSettingsError vrSettingsError;
	bool enabled = vr::VRSettings()->GetBool(vr::k_pch_SteamVR_Section, vr::k_pch_SteamVR_ActivateMultipleDrivers_Bool, &vrSettingsError);

	if (vrSettingsError != vr::VRSettingsError_None) {
		std::string err = "Could not read \"" + std::string(vr::k_pch_SteamVR_ActivateMultipleDrivers_Bool) +
		                  "\" setting: " + vr::VRSettings()->GetSettingsErrorNameFromEnum(vrSettingsError);

		throw std::runtime_error(err);
	}

	if (!enabled) {
		vr::VRSettings()->SetBool(vr::k_pch_SteamVR_Section, vr::k_pch_SteamVR_ActivateMultipleDrivers_Bool, true, &vrSettingsError);
		if (vrSettingsError != vr::VRSettingsError_None) {
			std::string err = "Could not set \"" + std::string(vr::k_pch_SteamVR_ActivateMultipleDrivers_Bool) +
			                  "\" setting: " + vr::VRSettings()->GetSettingsErrorNameFromEnum(vrSettingsError);

			throw std::runtime_error(err);
		}

		std::cerr << "Enabled \"" << vr::k_pch_SteamVR_ActivateMultipleDrivers_Bool << "\" setting" << '\n';
	}
	else {
		std::cerr << "\"" << vr::k_pch_SteamVR_ActivateMultipleDrivers_Bool << "\" setting previously enabled" << '\n';
	}
}

void InitVR()
{
	auto initError = vr::VRInitError_None;
	vr::VR_Init(&initError, vr::VRApplication_Overlay);
	if (initError != vr::VRInitError_None) {
		auto error = vr::VR_GetVRInitErrorAsEnglishDescription(initError);
		throw std::runtime_error("OpenVR error:" + std::string(error));
	}

	if (!vr::VR_IsInterfaceVersionValid(vr::IVRSystem_Version)) {
		throw std::runtime_error("OpenVR error: Outdated IVRSystem_Version");
	}
	else if (!vr::VR_IsInterfaceVersionValid(vr::IVRSettings_Version)) {
		throw std::runtime_error("OpenVR error: Outdated IVRSettings_Version");
	}
	else if (!vr::VR_IsInterfaceVersionValid(vr::IVROverlay_Version)) {
		throw std::runtime_error("OpenVR error: Outdated IVROverlay_Version");
	}

	ActivateMultipleDrivers();
}

static char textBuf[0x400] = {};

static bool immediateRedraw;
void RequestImmediateRedraw()
{
	immediateRedraw = true;
}

bool UninstallGithubSpaceCalibrator()
{
	// find the uninstall key

	// HKLM\Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenVRSpaceCalibrator
	std::string uninstallKeyValue = GetRegistryString(
	    HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\OpenVRSpaceCalibrator", "UninstallString");
	if (uninstallKeyValue.empty()) {
		// HKLM\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\OpenVRSpaceCalibrator
		uninstallKeyValue = GetRegistryString(HKEY_LOCAL_MACHINE,
		                                      "SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\OpenVRSpaceCalibrator",
		                                      "UninstallString");
		if (uninstallKeyValue.empty()) {
			// HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall
			uninstallKeyValue = GetRegistryString(
			    HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\OpenVRSpaceCalibrator", "UninstallString");
		}
	}

	printf("uninst: %s\n", uninstallKeyValue.c_str());

	if (!uninstallKeyValue.empty()) {
		int size_needed = MultiByteToWideChar(CP_UTF8, 0, &uninstallKeyValue[0], (int)uninstallKeyValue.size(), 0, 0);
		std::wstring uninstallPathUnicode(size_needed, 0);
		MultiByteToWideChar(CP_UTF8, 0, &uninstallKeyValue[0], (int)uninstallKeyValue.size(), &uninstallPathUnicode[0], size_needed);

		// split uninstall key into file and cli args
		std::wstring executablePath = uninstallPathUnicode;
		std::wstring commandLineArgs = uninstallPathUnicode;

		// some form of parsing incase a fork changes the executable path and stuff
		if (executablePath[0] == L'"') {
			// executable name is wrapped in double quotes, find closing quote
			auto it = std::find(executablePath.begin(), executablePath.end(), '"');
			it = std::find(it + 1, executablePath.end(), '"');
			if (it != executablePath.end()) {
				size_t idx = it - executablePath.begin();
				// now that we know where the quotes are, substring
				executablePath = uninstallPathUnicode.substr(1, idx - 1);
				commandLineArgs = uninstallPathUnicode.substr(idx + 1);
			}
		}
		else {
			// executable name is until the first space, find it and split accordingly
			auto it = std::find(executablePath.begin(), executablePath.end(), ' ');
			if (it != executablePath.end()) {
				size_t idx = it - executablePath.begin();
				// now that we know where the quotes are, substring
				executablePath = uninstallPathUnicode.substr(1, idx - 1);
				commandLineArgs = uninstallPathUnicode.substr(idx);
			}
		}

		SHELLEXECUTEINFO shExInfo = {0};
		shExInfo.cbSize = sizeof(shExInfo);
		shExInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
		shExInfo.hwnd = 0;
		shExInfo.lpVerb = L"open";
		shExInfo.lpFile = L"ms-settings:appsfeatures";
		shExInfo.lpParameters = L"";
		shExInfo.lpDirectory = 0;
		shExInfo.nShow = SW_SHOW;
		shExInfo.hInstApp = 0;

		if (ShellExecuteExW(&shExInfo)) {
			// valid
			return true;
		}
	}

	return false;
}

double lastFrameStartTime = glfwGetTime();
void RunLoop()
{
	while (!glfwWindowShouldClose(glfwWindow)) {
		TryCreateVROverlay();
		double time = glfwGetTime();
		CalibrationTick(time);

		bool dashboardVisible = false;
		int width, height;
		glfwGetFramebufferSize(glfwWindow, &width, &height);
		const bool windowVisible = (width > 0 && height > 0);

		if (overlayMainHandle && vr::VROverlay()) {
			auto& io = ImGui::GetIO();
			dashboardVisible = vr::VROverlay()->IsActiveDashboardOverlay(overlayMainHandle);

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

					uint32_t unFlags = 0; // EKeyboardFlags

					vr::VROverlay()->ShowKeyboardForOverlay(overlayMainHandle, vr::k_EGamepadTextInputModeNormal,
					                                        vr::k_EGamepadTextInputLineModeSingleLine, unFlags, "Space Calibrator Overlay",
					                                        sizeof textBuf, textBuf, 0);
					keyboardOpen = true;
				}
			}

			vr::VREvent_t vrEvent;
			while (vr::VROverlay()->PollNextOverlayEvent(overlayMainHandle, &vrEvent, sizeof(vrEvent))) {
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

			io.DisplaySize = ImVec2((float)fboTextureWidth, (float)fboTextureHeight);
			io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

			io.ConfigFlags = io.ConfigFlags & ~ImGuiConfigFlags_NoMouseCursorChange;
			if (dashboardVisible) {
				io.ConfigFlags = io.ConfigFlags | ImGuiConfigFlags_NoMouseCursorChange;
			}

			ImGui::NewFrame();

			BuildMainWindow(dashboardVisible);

			// @TODO: Move to a separate function, for now it works
			static bool githubPopupDismissed = false;

			if (s_isGitHubVersionInstalled && !githubPopupDismissed) {
				ImGui::OpenPopup("Conflicting Space Calibrator install");
				if (ImGui::BeginPopupModal("Conflicting Space Calibrator install", 0,
				                           ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) {
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

			ImGui::Render();

			glBindFramebuffer(GL_FRAMEBUFFER, fboHandle);
			glViewport(0, 0, fboTextureWidth, fboTextureHeight);
			glClearColor(0, 0, 0, 1);
			glClear(GL_COLOR_BUFFER_BIT);

			ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

			glBindFramebuffer(GL_FRAMEBUFFER, 0);

			if (width && height) {
				glBindFramebuffer(GL_READ_FRAMEBUFFER, fboHandle);
				glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
				glfwSwapBuffers(glfwWindow);
			}

			if (dashboardVisible) {
				vr::Texture_t vrTex = {
				    .handle = (void*)
#if defined _WIN64 || defined _LP64
				        (uint64_t)
#endif
				            fboTextureHandle,
				    .eType = vr::TextureType_OpenGL,
				    .eColorSpace = vr::ColorSpace_Auto,
				};

				vr::HmdVector2_t mouseScale = {(float)fboTextureWidth, (float)fboTextureHeight};

				vr::VROverlay()->SetOverlayTexture(overlayMainHandle, &vrTex);
				vr::VROverlay()->SetOverlayMouseScale(overlayMainHandle, &mouseScale);
			}
		}

		const double dashboardInterval = 1.0 / 90.0; // fps
		double waitEventsTimeout = std::max(CalCtx.wantedUpdateInterval, dashboardInterval);

		if (dashboardVisible && waitEventsTimeout > dashboardInterval) waitEventsTimeout = dashboardInterval;

		if (immediateRedraw) {
			waitEventsTimeout = 0;
			immediateRedraw = false;
		}

		glfwWaitEventsTimeout(waitEventsTimeout);

		// If we're minimized rendering won't limit our frame rate so we need to do it ourselves.
		if (glfwGetWindowAttrib(glfwWindow, GLFW_ICONIFIED)) {
			double targetFrameTime = 1 / MINIMIZED_MAX_FPS;
			double waitTime = targetFrameTime - (glfwGetTime() - lastFrameStartTime);
			if (waitTime > 0) {
				std::this_thread::sleep_for(std::chrono::duration<double>(waitTime));
			}

			lastFrameStartTime += targetFrameTime;
		}
	}
}

void VerifySetupCorrect()
{
	// register the manifest so that it shows up in the overlays menu
	if (!vr::VRApplications()->IsApplicationInstalled(OPENVR_APPLICATION_KEY)) {
		std::string manifestPath = std::format("{}\\{}", cwd, "manifest.vrmanifest");
		std::cout << "Adding manifest path: " << manifestPath << '\n';
		// If manifest is not installed, try installing it, and set it to auto-start with SteamVR
		auto vrAppErr = vr::VRApplications()->AddApplicationManifest(manifestPath.c_str());
		if (vrAppErr != vr::VRApplicationError_None) {
			fprintf(stderr, "Failed to add manifest: %s\n", vr::VRApplications()->GetApplicationsErrorNameFromEnum(vrAppErr));
		}
		else {
			vr::VRApplications()->SetApplicationAutoLaunch(OPENVR_APPLICATION_KEY, true);
		}
	}
	else {
		// Application is already registered, do not alter settings
		std::cout << "Space Calibrator already registered with SteamVR. Skipping..." << '\n';
	}

	// try removing the legacy app manifest from Steam, otherwise people will have multiple entries in the overlays menu
	if (vr::VRApplications()->IsApplicationInstalled(LEGACY_OPENVR_APPLICATION_KEY)) {
		std::cout << "Found a legacy version of Space Calibrator..." << '\n';
		// GitHub key is installed, uninstall it
		vr::EVRApplicationError appErr = vr::EVRApplicationError::VRApplicationError_None;
		char manifestPathBuffer[MAX_PATH + 32 /* for good measure */] = {};
		uint32_t szBufferSize =
		    vr::VRApplications()->GetApplicationPropertyString(LEGACY_OPENVR_APPLICATION_KEY, vr::VRApplicationProperty_BinaryPath_String,
		                                                       manifestPathBuffer, sizeof(manifestPathBuffer), &appErr);
		if (appErr != vr::VRApplicationError_None) {
			std::cout << "Failed to get binary path of " << LEGACY_OPENVR_APPLICATION_KEY << '\n';
			return;
		}

		// replace XXX.exe with manifest.vrmanifest
		const char* newFileName = "manifest.vrmanifest";
		char* lastSlash = strrchr(manifestPathBuffer, '\\');
		if (lastSlash) {
			*(lastSlash + 1) = '\0';
			strcat_s(manifestPathBuffer, sizeof(manifestPathBuffer), newFileName);
		}

		appErr = vr::VRApplications()->RemoveApplicationManifest(manifestPathBuffer);
		if (appErr != vr::VRApplicationError_None) {
			std::cout << "Failed to remove legacy application manifest. You may have duplicate entries in the overlays list." << '\n';
		}
	}
}

// Checks if a GitHub install of Space Calibrator is available when running via Steam.
// If the GitHub version is found, we yell at the user telling them to uninstall it first as it conflicts with the Steam version
void CheckGithubVersionInstalledOnSteam()
{
	// there are 3 locations for the uninstall string, test each one of them

	// HKLM\Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenVRSpaceCalibrator
	std::string uninstallKeyValue = GetRegistryString(
	    HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\OpenVRSpaceCalibrator", "UninstallString");
	if (!uninstallKeyValue.empty()) {
		// Space Calibrator was installed via GitHub, but we're running via Steam! Uh oh!
		s_isGitHubVersionInstalled = true;
		return;
	}

	// HKLM\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\OpenVRSpaceCalibrator
	uninstallKeyValue =
	    GetRegistryString(HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\OpenVRSpaceCalibrator",
	                      "UninstallString");
	if (!uninstallKeyValue.empty()) {
		// Space Calibrator was installed via GitHub, but we're running via Steam! Uh oh!
		s_isGitHubVersionInstalled = true;
		return;
	}

	// HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall
	uninstallKeyValue = GetRegistryString(
	    HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\OpenVRSpaceCalibrator", "UninstallString");
	if (!uninstallKeyValue.empty()) {
		// Space Calibrator was installed via GitHub, but we're running via Steam! Uh oh!
		s_isGitHubVersionInstalled = true;
		return;
	}

	// Also check for the presence of a driver in the SteamVR runtime's drivers directory (I should really make the installer not install
	// the driver there...)
	char cVrRuntimePath[MAX_PATH] = {0};
	unsigned int szPathLen = 0;
	vr::VR_GetRuntimePath(cVrRuntimePath, MAX_PATH, &szPathLen);
	if (szPathLen > 0 && std::filesystem::is_directory(cVrRuntimePath)) {
		// vr runtime path is valid, check if the drivers dir exists and likewise 01spacecalibrator is present
		// can be found at: {runtimedir}\\drivers\\01spacecalibrator
		std::filesystem::path spaceCalibratorGitHubDriverPath = cVrRuntimePath;
		spaceCalibratorGitHubDriverPath = spaceCalibratorGitHubDriverPath / "drivers" / "01spacecalibrator";
		if (std::filesystem::is_directory(spaceCalibratorGitHubDriverPath)) {
			// drivers dir exists, so set state
			s_isGitHubVersionInstalled = true;
		}
	}
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
	if (_getcwd(cwd, MAX_PATH) == nullptr) {
		// @TODO: Handle Invalid working dir case. Should never happen but you never know
	}
	HandleCommandLine(lpCmdLine);

#ifdef DEBUG_LOGS
	CreateConsole();
#endif

	if (!glfwInit()) {
		MessageBox(nullptr, L"Failed to initialize GLFW", L"", 0);
		return 0;
	}

	glfwSetErrorCallback(GLFWErrorCallback);

	bool isRunningViaSteam = false;
	char steamAppId[256] = {};
	DWORD result = GetEnvironmentVariableA("SteamAppId", steamAppId, sizeof(steamAppId));
	if (result > 0 && steamAppId != nullptr) {
		if (c_SPACE_CALIBRATOR_STEAM_APP_ID == steamAppId || c_STEAMVR_STEAM_APP_ID == steamAppId) {
			// We got launched via the Steam client UI.
			hSteamMutex = CreateMutexA(NULL, FALSE, STEAM_MUTEX_KEY);
			isRunningViaSteam = true;
			if (hSteamMutex == nullptr) {
				hSteamMutex = INVALID_HANDLE_VALUE;
			}
			else {
				// mutex opened, check if we opened one, if so exit
				if (GetLastError() == ERROR_ALREADY_EXISTS) {
					CloseHandle(hSteamMutex);
					hSteamMutex = INVALID_HANDLE_VALUE;
					return 0;
				}
			}
		}
	}

	try {
		InitVR();
		printf("isSteam: %d\n", isRunningViaSteam);
		if (isRunningViaSteam) {
			CheckGithubVersionInstalledOnSteam();
			printf("foundGithub: %d\n", s_isGitHubVersionInstalled);
			if (s_isGitHubVersionInstalled) {}
		}
		VerifySetupCorrect();
		CreateGLFWWindow();
		InitCalibrator();
		LoadProfile(CalCtx);
		RunLoop();

		vr::VR_Shutdown();

		if (fboHandle) glDeleteFramebuffers(1, &fboHandle);

		if (fboTextureHandle) glDeleteTextures(1, &fboTextureHandle);

		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImPlot::DestroyContext();
		ImGui::DestroyContext();
	}
	catch (std::runtime_error& e) {
		std::cerr << "Runtime error: " << e.what() << '\n';
		wchar_t message[1024];
		swprintf(message, 1024, L"%hs", e.what());
		MessageBox(nullptr, message, L"Runtime Error", 0);
	}

	if (hSteamMutex != INVALID_HANDLE_VALUE && hSteamMutex != nullptr) {
		CloseHandle(hSteamMutex);
		hSteamMutex = nullptr;
	}

	if (glfwWindow) glfwDestroyWindow(glfwWindow);

	glfwTerminate();
	return 0;
}

static void HandleCommandLine(LPWSTR lpCmdLine)
{
	if (lstrcmp(lpCmdLine, L"-openvrpath") == 0) {
		auto vrErr = vr::VRInitError_None;
		vr::VR_Init(&vrErr, vr::VRApplication_Utility);
		if (vrErr == vr::VRInitError_None) {
			char cruntimePath[MAX_PATH] = {0};
			unsigned int pathLen;
			vr::VR_GetRuntimePath(cruntimePath, MAX_PATH, &pathLen);

			printf("%s", cruntimePath);
			vr::VR_Shutdown();
			exit(0);
		}
		fprintf(stderr, "Failed to initialize OpenVR: %s\n", vr::VR_GetVRInitErrorAsEnglishDescription(vrErr));
		vr::VR_Shutdown();
		exit(-2);
	}
	else if (lstrcmp(lpCmdLine, L"-installmanifest") == 0) {
		auto vrErr = vr::VRInitError_None;
		vr::VR_Init(&vrErr, vr::VRApplication_Utility);
		if (vrErr == vr::VRInitError_None) {
			if (vr::VRApplications()->IsApplicationInstalled(OPENVR_APPLICATION_KEY)) {
				char oldWd[MAX_PATH] = {0};
				auto vrAppErr = vr::VRApplicationError_None;
				vr::VRApplications()->GetApplicationPropertyString(
				    OPENVR_APPLICATION_KEY, vr::VRApplicationProperty_WorkingDirectory_String, oldWd, MAX_PATH, &vrAppErr);
				if (vrAppErr != vr::VRApplicationError_None) {
					fprintf(stderr, "Failed to get old working dir, skipping removal: %s\n",
					        vr::VRApplications()->GetApplicationsErrorNameFromEnum(vrAppErr));
				}
				else {
					std::string manifestPath = oldWd;
					manifestPath += "\\manifest.vrmanifest";
					std::cout << "Removing old manifest path: " << manifestPath << '\n';
					vr::VRApplications()->RemoveApplicationManifest(manifestPath.c_str());
				}
			}
			std::string manifestPath = cwd;
			manifestPath += "\\manifest.vrmanifest";
			std::cout << "Adding manifest path: " << manifestPath << '\n';
			auto vrAppErr = vr::VRApplications()->AddApplicationManifest(manifestPath.c_str());
			if (vrAppErr != vr::VRApplicationError_None) {
				fprintf(stderr, "Failed to add manifest: %s\n", vr::VRApplications()->GetApplicationsErrorNameFromEnum(vrAppErr));
			}
			else {
				vr::VRApplications()->SetApplicationAutoLaunch(OPENVR_APPLICATION_KEY, true);
			}
			vr::VR_Shutdown();
			exit(-2);
		}
		fprintf(stderr, "Failed to initialize OpenVR: %s\n", vr::VR_GetVRInitErrorAsEnglishDescription(vrErr));
		vr::VR_Shutdown();
		exit(-2);
	}
	else if (lstrcmp(lpCmdLine, L"-removemanifest") == 0) {
		auto vrErr = vr::VRInitError_None;
		vr::VR_Init(&vrErr, vr::VRApplication_Utility);
		if (vrErr == vr::VRInitError_None) {
			if (vr::VRApplications()->IsApplicationInstalled(OPENVR_APPLICATION_KEY)) {
				std::string manifestPath = cwd;
				manifestPath += "\\manifest.vrmanifest";
				std::cout << "Removing manifest path: " << manifestPath << '\n';
				vr::VRApplications()->RemoveApplicationManifest(manifestPath.c_str());
			}
			vr::VR_Shutdown();
			exit(0);
		}
		fprintf(stderr, "Failed to initialize OpenVR: %s\n", vr::VR_GetVRInitErrorAsEnglishDescription(vrErr));
		vr::VR_Shutdown();
		exit(-2);
	}
	else if (lstrcmp(lpCmdLine, L"-activatemultipledrivers") == 0) {
		int ret = -2;
		auto vrErr = vr::VRInitError_None;
		vr::VR_Init(&vrErr, vr::VRApplication_Utility);
		if (vrErr == vr::VRInitError_None) {
			try {
				ActivateMultipleDrivers();
				ret = 0;
			}
			catch (std::runtime_error& e) {
				std::cerr << e.what() << '\n';
			}
		}
		else {
			fprintf(stderr, "Failed to initialize OpenVR: %s\n", vr::VR_GetVRInitErrorAsEnglishDescription(vrErr));
		}
		vr::VR_Shutdown();
		exit(ret);
	}
#ifndef DEBUG_LOGS
	else if (lstrcmp(lpCmdLine, L"-console") == 0) {
		CreateConsole();
	}
#endif
}