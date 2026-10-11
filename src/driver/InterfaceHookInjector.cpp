#include "Logging.h"
#include "Hooking.h"
#include "HookGate.h"
#include "InterfaceHookInjector.h"
#include "PoseHookGuard.h"
#include "ServerTrackedDeviceProvider.h"
#include "SkeletalHook.h"
#include "StickHook.h"

#include <atomic>
#include <cstring>
#include <mutex>

static ServerTrackedDeviceProvider* Driver = nullptr;

static Hook<void* (*)(vr::IVRDriverContext*, const char*, vr::EVRInitError*)>
    GetGenericInterfaceHook("IVRDriverContext::GetGenericInterface");

using TrackedDevicePoseUpdatedFn = void (*)(vr::IVRServerDriverHost*, uint32_t, const vr::DriverPose_t*, uint32_t);

static Hook<TrackedDevicePoseUpdatedFn> TrackedDevicePoseUpdatedHook005("IVRServerDriverHost005::TrackedDevicePoseUpdated");

static Hook<TrackedDevicePoseUpdatedFn> TrackedDevicePoseUpdatedHook006("IVRServerDriverHost006::TrackedDevicePoseUpdated");

static void HandleTrackedDevicePoseUpdated(Hook<TrackedDevicePoseUpdatedFn>& hook, vr::IVRServerDriverHost* _this, uint32_t unWhichDevice,
                                           const vr::DriverPose_t* newPose, uint32_t unPoseStructSize)
{
	spacecal::hook_gate::Scope gate;
	const spacecal::PoseHookAction action = spacecal::ClassifyPoseUpdate(newPose, unPoseStructSize);
	if (!gate.Open() && action == spacecal::PoseHookAction::Process) {
		hook.Original()(_this, unWhichDevice, newPose, unPoseStructSize);
		return;
	}
	switch (action) {
		case spacecal::PoseHookAction::Drop: return;
		case spacecal::PoseHookAction::Forward: hook.Original()(_this, unWhichDevice, newPose, unPoseStructSize); return;
		case spacecal::PoseHookAction::Process: {
			vr::DriverPose_t pose = *newPose;
			if (Driver->HandleDevicePoseUpdated(unWhichDevice, pose)) {
				hook.Original()(_this, unWhichDevice, &pose, unPoseStructSize);
			}
			return;
		}
	}
}

static void DetourTrackedDevicePoseUpdated005(vr::IVRServerDriverHost* _this, uint32_t unWhichDevice, const vr::DriverPose_t* newPose,
                                              uint32_t unPoseStructSize)
{
	HandleTrackedDevicePoseUpdated(TrackedDevicePoseUpdatedHook005, _this, unWhichDevice, newPose, unPoseStructSize);
}

static void DetourTrackedDevicePoseUpdated006(vr::IVRServerDriverHost* _this, uint32_t unWhichDevice, const vr::DriverPose_t* newPose,
                                              uint32_t unPoseStructSize)
{
	HandleTrackedDevicePoseUpdated(TrackedDevicePoseUpdatedHook006, _this, unWhichDevice, newPose, unPoseStructSize);
}

static void InstallPoseHook(Hook<TrackedDevicePoseUpdatedFn>& hook, void* iface, void* detour)
{
	std::lock_guard<std::mutex> lock(spacecal::hook_gate::InstallMutex());
	if (!spacecal::hook_gate::IsOpen() || IHook::Exists(hook.name)) return;
	hook.CreateHookInObjectVTable(iface, 1, detour);
	IHook::Register(&hook);
}

static void* DetourGetGenericInterface(vr::IVRDriverContext* _this, const char* pchInterfaceVersion, vr::EVRInitError* peError)
{
	spacecal::hook_gate::Scope gate;
	TRACE("ServerTrackedDeviceProvider::DetourGetGenericInterface(%s)", pchInterfaceVersion);
	auto originalInterface = GetGenericInterfaceHook.Original()(_this, pchInterfaceVersion, peError);

	if (!gate.Open() || !originalInterface || !pchInterfaceVersion) {
		return originalInterface;
	}

	try {
		if (std::strstr(pchInterfaceVersion, "IVRDriverInput_") != nullptr && std::strstr(pchInterfaceVersion, "Internal") == nullptr) {
			spacecal::skeletal_hook::TryInstallPublicHooks(originalInterface);
			spacecal::stick_hook::TryInstallPublicHooks(originalInterface);
		}

		if (std::strcmp(pchInterfaceVersion, "IVRServerDriverHost_005") == 0) {
			InstallPoseHook(TrackedDevicePoseUpdatedHook005, originalInterface, &DetourTrackedDevicePoseUpdated005);
		}
		else if (std::strcmp(pchInterfaceVersion, "IVRServerDriverHost_006") == 0) {
			InstallPoseHook(TrackedDevicePoseUpdatedHook006, originalInterface, &DetourTrackedDevicePoseUpdated006);
		}
	}
	catch (...) {
		static std::atomic<bool> logged{false};
		if (!logged.exchange(true)) {
			LOG("%s", "GetGenericInterface detour threw while installing hooks; continuing without them");
		}
	}

	return originalInterface;
}

void InjectHooks(ServerTrackedDeviceProvider* driver, vr::IVRDriverContext* pDriverContext)
{
	Driver = driver;
	try {
		std::lock_guard<std::mutex> lock(spacecal::hook_gate::InstallMutex());
		spacecal::hook_gate::Reopen();

		auto err = MH_Initialize();
		if (err == MH_OK) {
			GetGenericInterfaceHook.CreateHookInObjectVTable(pDriverContext, 0, &DetourGetGenericInterface);
			IHook::Register(&GetGenericInterfaceHook);
		}
		else {
			LOG("MH_Initialize error: %s", MH_StatusToString(err));
		}
	}
	catch (...) {
		LOG("%s", "InjectHooks threw; running without hooks");
	}
}

void DisableHooks()
{
	std::lock_guard<std::mutex> lock(spacecal::hook_gate::InstallMutex());
	spacecal::hook_gate::Close();
	if (!IHook::DisableAll()) {
		LOG("%s", "hook teardown: MinHook could not disable every hook");
	}
}

bool RemoveHooks()
{
	if (!spacecal::hook_gate::Drain(1000)) {
		static std::atomic<bool> logged{false};
		if (!logged.exchange(true)) {
			LOG("%s", "hook teardown: a detour was still running after 1 s; leaving hooks, finger state and pose memory in place");
		}
		return false;
	}
	std::lock_guard<std::mutex> lock(spacecal::hook_gate::InstallMutex());
	IHook::DestroyAll();
	MH_Uninitialize();
	return true;
}