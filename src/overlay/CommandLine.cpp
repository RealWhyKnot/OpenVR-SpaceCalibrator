#include "stdafx.h"
#include "CommandLine.h"
#include "Constants.h"
#include "VRSession.h"

#include <openvr.h>
#include <cstdio>
#include <iostream>
#include <string>

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

void HandleCommandLine(LPWSTR lpCmdLine)
{
	size_t length = wcslen(lpCmdLine);
	while (length > 0 && iswspace(lpCmdLine[length - 1])) {
		lpCmdLine[--length] = 0;
	}
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
			if (vr::VRApplications()->IsApplicationInstalled(STEAM_OPENVR_APPLICATION_KEY)) {
				vr::VRApplications()->SetApplicationAutoLaunch(STEAM_OPENVR_APPLICATION_KEY, false);
			}
			std::string manifestPath = AppCwd();
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
				std::string manifestPath = AppCwd();
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
