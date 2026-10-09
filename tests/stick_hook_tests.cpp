#include "Hooking.h"
#include "Logging.h"
#include "StickHook.h"
#include "StickSmoothingMath.h"

#include <MinHook.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <thread>

namespace {

	int failures = 0;

#define CHECK(cond)                                                                                                                        \
	do {                                                                                                                                   \
		if (!(cond)) {                                                                                                                     \
			std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);                                                                     \
			++failures;                                                                                                                    \
		}                                                                                                                                  \
	} while (0)

	constexpr vr::PropertyContainerHandle_t kLeftController = 11;
	constexpr vr::PropertyContainerHandle_t kRightController = 12;
	constexpr vr::PropertyContainerHandle_t kTracker = 13;

	class FakeDriverInput : public vr::IVRDriverInput
	{
	public:
		std::map<vr::VRInputComponentHandle_t, float> lastValue;
		std::map<vr::VRInputComponentHandle_t, int> updates;
		std::map<vr::VRInputComponentHandle_t, double> lastTimeOffset;
		vr::VRInputComponentHandle_t nextHandle = 100;

		__declspec(noinline) vr::EVRInputError CreateBooleanComponent(vr::PropertyContainerHandle_t, const char*,
		                                                              vr::VRInputComponentHandle_t* pHandle) override
		{
			*pHandle = nextHandle++;
			return vr::VRInputError_None;
		}
		__declspec(noinline) vr::EVRInputError UpdateBooleanComponent(vr::VRInputComponentHandle_t, bool, double) override
		{
			return vr::VRInputError_None;
		}
		__declspec(noinline) vr::EVRInputError CreateScalarComponent(vr::PropertyContainerHandle_t, const char*,
		                                                             vr::VRInputComponentHandle_t* pHandle, vr::EVRScalarType,
		                                                             vr::EVRScalarUnits) override
		{
			*pHandle = nextHandle++;
			return vr::VRInputError_None;
		}
		__declspec(noinline) vr::EVRInputError UpdateScalarComponent(vr::VRInputComponentHandle_t handle, float value,
		                                                             double timeOffset) override
		{
			lastValue[handle] = value;
			updates[handle]++;
			lastTimeOffset[handle] = timeOffset;
			return vr::VRInputError_None;
		}
		__declspec(noinline) vr::EVRInputError CreateHapticComponent(vr::PropertyContainerHandle_t, const char*,
		                                                             vr::VRInputComponentHandle_t* pHandle) override
		{
			*pHandle = nextHandle++;
			return vr::VRInputError_None;
		}
		__declspec(noinline) vr::EVRInputError CreateSkeletonComponent(vr::PropertyContainerHandle_t, const char*, const char*, const char*,
		                                                               vr::EVRSkeletalTrackingLevel, const vr::VRBoneTransform_t*, uint32_t,
		                                                               vr::VRInputComponentHandle_t* pHandle) override
		{
			*pHandle = nextHandle++;
			return vr::VRInputError_None;
		}
		__declspec(noinline) vr::EVRInputError UpdateSkeletonComponent(vr::VRInputComponentHandle_t, vr::EVRSkeletalMotionRange,
		                                                               const vr::VRBoneTransform_t*, uint32_t) override
		{
			return vr::VRInputError_None;
		}
	};

	class FakeProperties : public vr::IVRProperties
	{
	public:
		std::map<vr::PropertyContainerHandle_t, int32_t> roles;

		vr::ETrackedPropertyError ReadPropertyBatch(vr::PropertyContainerHandle_t container, vr::PropertyRead_t* batch,
		                                            uint32_t count) override
		{
			for (uint32_t i = 0; i < count; ++i) {
				vr::PropertyRead_t& read = batch[i];
				auto it = roles.find(container);
				if (read.prop != vr::Prop_ControllerRoleHint_Int32 || it == roles.end() || read.unBufferSize < sizeof(int32_t)) {
					read.eError = vr::TrackedProp_UnknownProperty;
					read.unTag = vr::k_unInvalidPropertyTag;
					read.unRequiredBufferSize = 0;
					continue;
				}
				std::memcpy(read.pvBuffer, &it->second, sizeof(int32_t));
				read.eError = vr::TrackedProp_Success;
				read.unTag = vr::k_unInt32PropertyTag;
				read.unRequiredBufferSize = sizeof(int32_t);
			}
			return vr::TrackedProp_Success;
		}
		vr::ETrackedPropertyError WritePropertyBatch(vr::PropertyContainerHandle_t, vr::PropertyWrite_t*, uint32_t) override
		{
			return vr::TrackedProp_Success;
		}
		const char* GetPropErrorNameFromEnum(vr::ETrackedPropertyError) override { return "error"; }
		vr::PropertyContainerHandle_t TrackedDeviceToPropertyContainer(vr::TrackedDeviceIndex_t) override
		{
			return vr::k_ulInvalidPropertyContainer;
		}
	};

	class FakeContext : public vr::IVRDriverContext
	{
	public:
		FakeProperties properties;

		void* GetGenericInterface(const char* version, vr::EVRInitError* error) override
		{
			if (error) *error = vr::VRInitError_None;
			if (std::strcmp(version, vr::IVRProperties_Version) == 0) return &properties;
			return this;
		}
		vr::DriverHandle_t GetDriverHandle() override { return 1; }
	};

	FakeContext g_context;
	FakeDriverInput g_fakeInput;
	vr::IVRDriverInput* g_input = &g_fakeInput;

	vr::EVRInitError InitContext()
	{
		VR_INIT_SERVER_DRIVER_CONTEXT(&g_context);
		return vr::VRInitError_None;
	}

	vr::VRInputComponentHandle_t Create(vr::PropertyContainerHandle_t container, const char* path)
	{
		vr::VRInputComponentHandle_t handle = vr::k_ulInvalidInputComponentHandle;
		g_input->CreateScalarComponent(container, path, &handle, vr::VRScalarType_Absolute, vr::VRScalarUnits_NormalizedTwoSided);
		return handle;
	}

	void SetStrengths(uint8_t left, uint8_t right)
	{
		protocol::StickSmoothingConfig config{};
		config.sticks[0] = {spacecal::stick::LegacyPushMs(left), spacecal::stick::LegacyReleaseMs(left), 100, 0};
		config.sticks[1] = {spacecal::stick::LegacyPushMs(right), spacecal::stick::LegacyReleaseMs(right), 100, 0};
		spacecal::stick_hook::SetConfig(config);
	}

	void SleepMs(int ms)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(ms));
	}

	void TestOffForwardsRawValues(vr::VRInputComponentHandle_t leftX)
	{
		SetStrengths(0, 0);
		g_input->UpdateScalarComponent(leftX, 0.42f, -0.003);
		CHECK(g_fakeInput.lastValue[leftX] == 0.42f);
		CHECK(g_fakeInput.lastTimeOffset[leftX] == -0.003);
		g_input->UpdateScalarComponent(leftX, 0.0f, 0.0);
		CHECK(g_fakeInput.lastValue[leftX] == 0.0f);
	}

	void TestEachHandUsesItsOwnSlider(vr::VRInputComponentHandle_t leftX, vr::VRInputComponentHandle_t rightX)
	{
		SetStrengths(0, 100);
		g_input->UpdateScalarComponent(leftX, 1.0f, 0.0);
		g_input->UpdateScalarComponent(rightX, 1.0f, 0.0);
		CHECK(g_fakeInput.lastValue[leftX] == 1.0f);
		CHECK(g_fakeInput.lastValue[rightX] < 0.01f);

		SleepMs(60);
		g_input->UpdateScalarComponent(rightX, 1.0f, 0.0);
		const float eased = g_fakeInput.lastValue[rightX];
		CHECK(eased > 0.0f);
		CHECK(eased < 0.5f);
	}

	void TestStrengthSendsTheStartSpeedOnTheFirstSample(vr::VRInputComponentHandle_t leftY)
	{
		protocol::StickSmoothingConfig config{};
		config.sticks[0] = {3000, 0, 60, 0};
		spacecal::stick_hook::SetConfig(config);
		g_input->UpdateScalarComponent(leftY, -1.0f, 0.0);
		CHECK(std::fabs(g_fakeInput.lastValue[leftY] + 0.4f) < 1e-6f);
		SleepMs(60);
		g_input->UpdateScalarComponent(leftY, -1.0f, 0.0);
		CHECK(g_fakeInput.lastValue[leftY] < -0.4f);
		CHECK(g_fakeInput.lastValue[leftY] > -0.5f);
		g_input->UpdateScalarComponent(leftY, 0.0f, 0.0);
		CHECK(g_fakeInput.lastValue[leftY] == 0.0f);
	}

	void TestDelayHoldsTheStartSpeedThroughTheHook(vr::VRInputComponentHandle_t leftThumbX)
	{
		protocol::StickSmoothingConfig config{};
		config.sticks[0] = {3000, 0, 60, 0, 400};
		spacecal::stick_hook::SetConfig(config);
		g_input->UpdateScalarComponent(leftThumbX, 1.0f, 0.0);
		CHECK(std::fabs(g_fakeInput.lastValue[leftThumbX] - 0.4f) < 1e-6f);
		SleepMs(100);
		g_input->UpdateScalarComponent(leftThumbX, 1.0f, 0.0);
		CHECK(std::fabs(g_fakeInput.lastValue[leftThumbX] - 0.4f) < 1e-6f);
		SleepMs(450);
		g_input->UpdateScalarComponent(leftThumbX, 1.0f, 0.0);
		CHECK(g_fakeInput.lastValue[leftThumbX] > 0.4f);
		CHECK(g_fakeInput.lastValue[leftThumbX] < 1.0f);
	}

	void TestPumpKeepsRampingWhenDriverGoesQuiet(vr::VRInputComponentHandle_t rightY)
	{
		SetStrengths(0, 50);
		g_input->UpdateScalarComponent(rightY, -1.0f, 0.0);
		const int driverUpdates = g_fakeInput.updates[rightY];

		spacecal::stick_hook::Pump();
		CHECK(g_fakeInput.updates[rightY] == driverUpdates);

		float previous = g_fakeInput.lastValue[rightY];
		int pumped = 0;
		for (int i = 0; i < 140; ++i) {
			SleepMs(25);
			spacecal::stick_hook::Pump();
			const float now = g_fakeInput.lastValue[rightY];
			CHECK(now <= previous);
			if (g_fakeInput.updates[rightY] > driverUpdates + pumped) ++pumped;
			previous = now;
		}
		CHECK(pumped > 5);
		CHECK(g_fakeInput.lastValue[rightY] == -1.0f);
		CHECK(g_fakeInput.lastTimeOffset[rightY] == 0.0);

		const int settledUpdates = g_fakeInput.updates[rightY];
		SleepMs(25);
		spacecal::stick_hook::Pump();
		CHECK(g_fakeInput.updates[rightY] == settledUpdates);
	}

	void TestTurningOffMidRampSnapsToTheStick(vr::VRInputComponentHandle_t rightY)
	{
		SetStrengths(0, 100);
		g_input->UpdateScalarComponent(rightY, 0.0f, 0.0);
		CHECK(g_fakeInput.lastValue[rightY] == -1.0f);
		SleepMs(30);
		spacecal::stick_hook::Pump();
		CHECK(g_fakeInput.lastValue[rightY] < -0.5f);
		CHECK(g_fakeInput.lastValue[rightY] > -1.0f);

		SetStrengths(0, 0);
		SleepMs(25);
		spacecal::stick_hook::Pump();
		CHECK(g_fakeInput.lastValue[rightY] == 0.0f);
	}

	void TestOtherAxesAndUnknownDevicesPassThrough(vr::VRInputComponentHandle_t trigger, vr::VRInputComponentHandle_t trackerX)
	{
		SetStrengths(100, 100);
		g_input->UpdateScalarComponent(trigger, 0.8f, 0.0);
		CHECK(g_fakeInput.lastValue[trigger] == 0.8f);
		g_input->UpdateScalarComponent(trackerX, 0.6f, 0.0);
		CHECK(g_fakeInput.lastValue[trackerX] == 0.6f);
	}

	void TestLateRoleIsPickedUpByThePump(vr::VRInputComponentHandle_t trackerX)
	{
		SetStrengths(100, 100);
		g_context.properties.roles[kTracker] = vr::TrackedControllerRole_LeftHand;
		SleepMs(1100);
		spacecal::stick_hook::Pump();
		g_input->UpdateScalarComponent(trackerX, -0.6f, 0.0);
		CHECK(g_fakeInput.lastValue[trackerX] == 0.6f);
		SleepMs(25);
		spacecal::stick_hook::Pump();
		CHECK(g_fakeInput.lastValue[trackerX] < 0.6f);
		CHECK(g_fakeInput.lastValue[trackerX] > 0.0f);
	}

}

int main()
{
	LogFile = stdout;
	CHECK(InitContext() == vr::VRInitError_None);
	CHECK(MH_Initialize() == MH_OK);

	g_context.properties.roles[kLeftController] = vr::TrackedControllerRole_LeftHand;
	g_context.properties.roles[kRightController] = vr::TrackedControllerRole_RightHand;

	spacecal::stick_hook::Init();
	spacecal::stick_hook::TryInstallPublicHooks(g_input);

	const auto leftX = Create(kLeftController, "/input/joystick/x");
	const auto leftY = Create(kLeftController, "/input/joystick/y");
	const auto leftThumbX = Create(kLeftController, "/input/thumbstick/x");
	const auto rightX = Create(kRightController, "/input/thumbstick/x");
	const auto rightY = Create(kRightController, "/input/thumbstick/y");
	const auto trigger = Create(kRightController, "/input/trigger/value");
	const auto trackerX = Create(kTracker, "/input/joystick/x");

	TestOffForwardsRawValues(leftX);
	TestEachHandUsesItsOwnSlider(leftX, rightX);
	TestStrengthSendsTheStartSpeedOnTheFirstSample(leftY);
	TestDelayHoldsTheStartSpeedThroughTheHook(leftThumbX);
	TestPumpKeepsRampingWhenDriverGoesQuiet(rightY);
	TestTurningOffMidRampSnapsToTheStick(rightY);
	TestOtherAxesAndUnknownDevicesPassThrough(trigger, trackerX);
	TestLateRoleIsPickedUpByThePump(trackerX);

	spacecal::stick_hook::Shutdown();
	IHook::DestroyAll();
	MH_Uninitialize();

	if (failures == 0) {
		std::printf("stick_hook_tests: all tests passed\n");
		return 0;
	}
	std::printf("stick_hook_tests: %d failure(s)\n", failures);
	return 1;
}
