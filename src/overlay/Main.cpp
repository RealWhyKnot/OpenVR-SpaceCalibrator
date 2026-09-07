#include "stdafx.h"
#include "AppWindow.h"
#include "Calibration.h"
#include "CommandLine.h"
#include "Configuration.h"
#include "Constants.h"
#include "LegacyInstall.h"
#include "OverlayApp.h"
#include "Updater.h"
#include "VRSession.h"

#include <GLFW/glfw3.h>
#include <openvr.h>
#include <direct.h>

#include <cstdio>
#include <iostream>
#include <stdexcept>

#pragma comment(linker, "\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

extern "C" __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
extern "C" __declspec(dllexport) DWORD AmdPowerXpressRequestHighPerformance = 0x00000001;

static char cwd[MAX_PATH];

const char* AppCwd()
{
	return cwd;
}

static HANDLE hSteamMutex = INVALID_HANDLE_VALUE;
static HANDLE hAppMutex = INVALID_HANDLE_VALUE;

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
	if (_getcwd(cwd, MAX_PATH) == nullptr) {}
	HandleCommandLine(lpCmdLine);

#ifdef DEBUG_LOGS
	CreateConsole();
#endif

	if (!InitGlfw()) {
		MessageBox(nullptr, L"Failed to initialize GLFW", L"", 0);
		return 0;
	}

	hAppMutex = CreateMutexA(NULL, FALSE, APP_MUTEX_KEY);
	if (hAppMutex != nullptr && GetLastError() == ERROR_ALREADY_EXISTS) {
		CloseHandle(hAppMutex);
		MessageBox(nullptr, L"Space Calibrator is already running.", L"Space Calibrator", 0);
		return 0;
	}

	bool isRunningViaSteam = false;
	char steamAppId[256] = {};
	DWORD result = GetEnvironmentVariableA("SteamAppId", steamAppId, sizeof(steamAppId));
	if (result > 0 && steamAppId != nullptr) {
		if (strcmp(SPACE_CALIBRATOR_STEAM_APP_ID, steamAppId) == 0 || strcmp(STEAMVR_STEAM_APP_ID, steamAppId) == 0) {
			hSteamMutex = CreateMutexA(NULL, FALSE, STEAM_MUTEX_KEY);
			isRunningViaSteam = true;
			if (hSteamMutex == nullptr) {
				hSteamMutex = INVALID_HANDLE_VALUE;
			}
			else {
				if (GetLastError() == ERROR_ALREADY_EXISTS) {
					CloseHandle(hSteamMutex);
					hSteamMutex = INVALID_HANDLE_VALUE;
					return 0;
				}
			}
		}
	}

	try {
		if (isRunningViaSteam) {
			CheckGithubVersionInstalledOnSteam();
		}
		CreateGLFWWindow(IsVRServerRunning());
		LoadProfile(CalCtx);
		LoadUpdateSettings(UpdaterCtx.settings);
		if (UpdaterCtx.settings.checkOnStartup) {
			UpdaterCtx.StartCheck(false);
		}
		RunLoop();
		std::cerr << "run loop exited\n";

		if (vr::VRSystem()) vr::VR_Shutdown();

		DestroyGLFWResources();
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

	if (hAppMutex != INVALID_HANDLE_VALUE && hAppMutex != nullptr) {
		CloseHandle(hAppMutex);
		hAppMutex = nullptr;
	}

	if (AppWindow.window) glfwDestroyWindow(AppWindow.window);

	glfwTerminate();
	return 0;
}
