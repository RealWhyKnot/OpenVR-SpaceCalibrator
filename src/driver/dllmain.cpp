#include "BuildId.h"
#include "Logging.h"
#include "Version.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>

extern "C" __declspec(dllexport) const char SpaceCalibratorBuildMarker[] = "SPACECAL_BUILD_ID=" SPACECAL_BUILD_ID;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
	switch (ul_reason_for_call) {
		case DLL_PROCESS_ATTACH:
			OpenLogFile();
			LOG("OpenVR-SpaceCalibratorDriver " SPACECAL_VERSION_STRING " build " SPACECAL_BUILD_ID " loaded");
			break;
		case DLL_PROCESS_DETACH: LOG("OpenVR-SpaceCalibratorDriver unloaded"); break;
		case DLL_THREAD_ATTACH:
		case DLL_THREAD_DETACH: break;
	}
	return TRUE;
}
