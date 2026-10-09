#include "StickHook.h"
#include "DriverInputVTable.h"
#include "Hooking.h"
#include "Logging.h"
#include "StickSmoothingMath.h"

#include <atomic>
#include <cmath>
#include <exception>
#include <mutex>
#include <unordered_map>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace spacecal::stick_hook {

	namespace {

		namespace math = spacecal::stick;

		struct AxisState
		{
			vr::PropertyContainerHandle_t container = vr::k_ulInvalidPropertyContainer;
			int hand = -1;
			int64_t nextRoleQueryQpc = 0;
			vr::IVRDriverInput* iface = nullptr;
			math::AxisFilter filter;
			int64_t lastStepQpc = 0;
			int64_t lastDriverUpdateQpc = 0;
			bool loggedFirstUpdate = false;
			bool loggedFirstPump = false;
		};

		struct PendingUpdate
		{
			vr::IVRDriverInput* iface;
			vr::VRInputComponentHandle_t handle;
			float value;
		};

		struct PendingRoleQuery
		{
			vr::VRInputComponentHandle_t handle;
			vr::PropertyContainerHandle_t container;
		};

		using CreateScalarFn = vr::EVRInputError (*)(vr::IVRDriverInput*, vr::PropertyContainerHandle_t, const char*,
		                                             vr::VRInputComponentHandle_t*, vr::EVRScalarType, vr::EVRScalarUnits);
		using UpdateScalarFn = vr::EVRInputError (*)(vr::IVRDriverInput*, vr::VRInputComponentHandle_t, float, double);

		Hook<CreateScalarFn> CreateScalarHook("IVRDriverInput::CreateScalarComponent");
		Hook<UpdateScalarFn> UpdateScalarHook("IVRDriverInput::UpdateScalarComponent");

		std::atomic<bool> g_armed{false};
		protocol::StickSmoothingConfig g_config{};
		math::Ramp g_ramps[math::kHandCount]{math::Ramp{}, math::Ramp{}};
		std::atomic<bool> g_faulted{false};
		std::unordered_map<vr::VRInputComponentHandle_t, AxisState> g_axes;
		std::mutex g_axesMutex;
		LARGE_INTEGER g_qpcFreq{};
		std::vector<PendingUpdate> g_pumpUpdates;
		std::vector<PendingRoleQuery> g_pumpRoleQueries;

		const char* HandName(int hand)
		{
			return hand == 0 ? "left" : (hand == 1 ? "right" : "unknown");
		}

		math::Ramp RampFor(int hand)
		{
			return hand < 0 ? math::Ramp{} : g_ramps[hand];
		}

		int64_t NowQpc()
		{
			LARGE_INTEGER now;
			QueryPerformanceCounter(&now);
			return now.QuadPart;
		}

		double SecondsBetween(int64_t from, int64_t to)
		{
			if (from == 0 || g_qpcFreq.QuadPart == 0) return 0.0;
			return (double)(to - from) / (double)g_qpcFreq.QuadPart;
		}

		int ResolveHand(vr::PropertyContainerHandle_t container)
		{
			auto* props = vr::VRProperties();
			if (!props || container == vr::k_ulInvalidPropertyContainer) return -1;
			vr::ETrackedPropertyError err = vr::TrackedProp_Success;
			const int32_t role = props->GetInt32Property(container, vr::Prop_ControllerRoleHint_Int32, &err);
			if (err != vr::TrackedProp_Success) return -1;
			if (role == vr::TrackedControllerRole_LeftHand) return 0;
			if (role == vr::TrackedControllerRole_RightHand) return 1;
			return -1;
		}

		void ContainmentFault(const char* where, const char* what)
		{
			if (g_faulted.exchange(true, std::memory_order_relaxed)) return;
			LOG("[stick] %s threw: %s. Joystick acceleration is off and sticks pass through unchanged.", where,
			    (what && what[0]) ? what : "(unknown exception)");
		}

		void RegisterAxis(vr::PropertyContainerHandle_t container, const char* path, vr::VRInputComponentHandle_t handle)
		{
			const int hand = ResolveHand(container);
			std::lock_guard<std::mutex> lk(g_axesMutex);
			AxisState& state = g_axes[handle];
			state = AxisState{};
			state.container = container;
			state.hand = hand;
			LOG("[stick] tracking %s handle=%llu container=%llu hand=%s", path, (unsigned long long)handle, (unsigned long long)container,
			    HandName(hand));
		}

		vr::EVRInputError DetourCreateScalarComponent(vr::IVRDriverInput* _this, vr::PropertyContainerHandle_t ulContainer,
		                                              const char* pchName, vr::VRInputComponentHandle_t* pHandle, vr::EVRScalarType eType,
		                                              vr::EVRScalarUnits eUnits)
		{
			auto result = CreateScalarHook.originalFunc(_this, ulContainer, pchName, pHandle, eType, eUnits);
			if (result != vr::VRInputError_None || !pHandle || *pHandle == vr::k_ulInvalidInputComponentHandle ||
			    !math::IsStickAxisPath(pchName) || g_faulted.load(std::memory_order_relaxed)) {
				return result;
			}
			try {
				RegisterAxis(ulContainer, pchName, *pHandle);
			}
			catch (const std::exception& ex) {
				ContainmentFault("CreateScalarComponent", ex.what());
			}
			catch (...) {
				ContainmentFault("CreateScalarComponent", nullptr);
			}
			return result;
		}

		float EasedValue(vr::IVRDriverInput* iface, vr::VRInputComponentHandle_t handle, float value)
		{
			if (!g_armed.load(std::memory_order_acquire)) return value;
			const int64_t now = NowQpc();

			std::lock_guard<std::mutex> lk(g_axesMutex);
			auto it = g_axes.find(handle);
			if (it == g_axes.end()) return value;
			AxisState& state = it->second;
			state.iface = iface;
			state.lastDriverUpdateQpc = now;
			const math::Ramp ramp = RampFor(state.hand);
			const double dt = SecondsBetween(state.lastStepQpc, now);
			state.lastStepQpc = now;
			if (!state.loggedFirstUpdate) {
				state.loggedFirstUpdate = true;
				LOG("[stick] first update handle=%llu hand=%s value=%.3f push=%.2fs held_back=%.2f delay=%.2fs release=%.2fs",
				    (unsigned long long)handle, HandName(state.hand), value, ramp.pushSeconds, ramp.heldBack, ramp.delaySeconds,
				    ramp.releaseSeconds);
			}
			return (float)math::OnDriverSample(state.filter, value, dt, ramp);
		}

		vr::EVRInputError DetourUpdateScalarComponent(vr::IVRDriverInput* _this, vr::VRInputComponentHandle_t ulComponent, float fNewValue,
		                                              double fTimeOffset)
		{
			float value = fNewValue;
			if (!g_faulted.load(std::memory_order_relaxed) && std::isfinite(fNewValue)) {
				try {
					value = EasedValue(_this, ulComponent, fNewValue);
				}
				catch (const std::exception& ex) {
					ContainmentFault("UpdateScalarComponent", ex.what());
					value = fNewValue;
				}
				catch (...) {
					ContainmentFault("UpdateScalarComponent", nullptr);
					value = fNewValue;
				}
			}
			return UpdateScalarHook.originalFunc(_this, ulComponent, value, fTimeOffset);
		}

		void PumpImpl()
		{
			const int64_t now = NowQpc();
			g_pumpUpdates.clear();
			g_pumpRoleQueries.clear();
			{
				std::lock_guard<std::mutex> lk(g_axesMutex);
				for (auto& [handle, state] : g_axes) {
					if (state.hand < 0) {
						if (now >= state.nextRoleQueryQpc) {
							state.nextRoleQueryQpc = now + g_qpcFreq.QuadPart;
							g_pumpRoleQueries.push_back({handle, state.container});
						}
						continue;
					}
					if (!state.iface || !math::NeedsPump(state.filter, SecondsBetween(state.lastDriverUpdateQpc, now))) {
						continue;
					}
					const math::Ramp ramp = RampFor(state.hand);
					const double dt = SecondsBetween(state.lastStepQpc, now);
					state.lastStepQpc = now;
					if (!state.loggedFirstPump) {
						state.loggedFirstPump = true;
						LOG("[stick] driver went quiet mid-ramp on handle=%llu hand=%s; easing from the frame loop",
						    (unsigned long long)handle, HandName(state.hand));
					}
					g_pumpUpdates.push_back({state.iface, handle, (float)math::Advance(state.filter, dt, ramp)});
				}
			}

			for (const auto& query : g_pumpRoleQueries) {
				const int hand = ResolveHand(query.container);
				if (hand < 0) continue;
				std::lock_guard<std::mutex> lk(g_axesMutex);
				auto it = g_axes.find(query.handle);
				if (it == g_axes.end()) continue;
				it->second.hand = hand;
				LOG("[stick] handle=%llu resolved to the %s hand", (unsigned long long)query.handle, HandName(hand));
			}

			for (const auto& update : g_pumpUpdates) {
				UpdateScalarHook.originalFunc(update.iface, update.handle, update.value, 0.0);
			}
		}

	}

	void Init()
	{
		QueryPerformanceFrequency(&g_qpcFreq);
		g_faulted.store(false, std::memory_order_relaxed);
		g_armed.store(true, std::memory_order_release);
		LOG("%s", "[stick] Init: joystick acceleration armed, awaiting IVRDriverInput interface queries");
	}

	void Shutdown()
	{
		g_armed.store(false, std::memory_order_release);
		std::lock_guard<std::mutex> lk(g_axesMutex);
		g_axes.clear();
		LOG("%s", "[stick] Shutdown: joystick acceleration disarmed");
	}

	void SetConfig(const protocol::StickSmoothingConfig& config)
	{
		math::Ramp ramps[math::kHandCount];
		bool changed = false;
		{
			std::lock_guard<std::mutex> lk(g_axesMutex);
			for (int hand = 0; hand < math::kHandCount; ++hand) {
				const protocol::StickRampConfig& stick = config.sticks[hand];
				const protocol::StickRampConfig& previous = g_config.sticks[hand];
				changed |= stick.pushMs != previous.pushMs || stick.releaseMs != previous.releaseMs || stick.delayMs != previous.delayMs ||
				           stick.strength != previous.strength;
				g_ramps[hand] = math::RampFromSettings(stick.pushMs, stick.releaseMs, stick.strength, stick.delayMs);
				ramps[hand] = g_ramps[hand];
			}
			g_config = config;
		}
		if (changed) {
			LOG("[stick] joystick acceleration left: timer=%.2fs strength=%.0f%% delay=%.2fs stop=%.2fs, right: timer=%.2fs "
			    "strength=%.0f%% "
			    "delay=%.2fs stop=%.2fs",
			    ramps[0].pushSeconds, ramps[0].heldBack * 100.0, ramps[0].delaySeconds, ramps[0].releaseSeconds, ramps[1].pushSeconds,
			    ramps[1].heldBack * 100.0, ramps[1].delaySeconds, ramps[1].releaseSeconds);
		}
	}

	void Pump()
	{
		if (!g_armed.load(std::memory_order_acquire) || !UpdateScalarHook.originalFunc || g_faulted.load(std::memory_order_relaxed)) {
			return;
		}
		try {
			PumpImpl();
		}
		catch (const std::exception& ex) {
			ContainmentFault("RunFrame pump", ex.what());
		}
		catch (...) {
			ContainmentFault("RunFrame pump", nullptr);
		}
	}

	void TryInstallPublicHooks(void* iface)
	{
		if (!iface) return;

		const bool createAlready = IHook::Exists(CreateScalarHook.name);
		const bool updateAlready = IHook::Exists(UpdateScalarHook.name);
		if (createAlready && updateAlready) return;

		if (!IsDriverInputVTableSane(iface, "stick")) return;

		if (!createAlready) {
			CreateScalarHook.CreateHookInObjectVTable(iface, 2, &DetourCreateScalarComponent);
			IHook::Register(&CreateScalarHook);
		}
		if (!updateAlready) {
			UpdateScalarHook.CreateHookInObjectVTable(iface, 3, &DetourUpdateScalarComponent);
			IHook::Register(&UpdateScalarHook);
		}

		LOG("%s", "[stick] installed PUBLIC IVRDriverInput hooks: vtable[2]=CreateScalar, vtable[3]=UpdateScalar");
	}

}
