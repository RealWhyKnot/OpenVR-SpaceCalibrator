#pragma once

#include <string>
#include <vector>
#include <openvr.h>

struct KnownDevice
{
	std::string trackingSystem;
	std::string model;
	std::string serial;
	vr::TrackedDeviceClass deviceClass = vr::TrackedDeviceClass_Invalid;
};

struct VRDevice
{
	int id = -1;
	vr::TrackedDeviceClass deviceClass = vr::TrackedDeviceClass::TrackedDeviceClass_Invalid;
	std::string model = "";
	std::string serial = "";
	std::string trackingSystem = "";
	vr::ETrackedControllerRole controllerRole = vr::ETrackedControllerRole::TrackedControllerRole_Invalid;
};

struct VRState
{
	std::vector<std::string> trackingSystems;
	std::vector<VRDevice> devices;

	[[nodiscard]] int FindDevice(const std::string& trackingSystem, const std::string& model, const std::string& serial) const;

	static VRState Load();
};