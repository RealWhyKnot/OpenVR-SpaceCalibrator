#include "SkeletalHook.h"
#include "Hooking.h"
#include "Logging.h"
#include "ServerTrackedDeviceProvider.h"
#include "SkeletalSmoothingMath.h"

#include <atomic>
#include <cstring>
#include <exception>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace spacecal::skeletal_hook {

	namespace {

		namespace math = spacecal::skeletal;

		ServerTrackedDeviceProvider* g_driver = nullptr;
		std::atomic<bool> g_skeletalFaulted{false};

		struct HandState
		{
			math::FingerFrameState frame[math::kMotionRangeCount];

			float windowMaxPosDelta = 0.0f;
			int windowMaxPosDeltaBone = -1;
			float windowMinQuatDot = 1.0f;
			int windowMinQuatDotBone = -1;
		};
		HandState g_handState[2];

		std::unordered_map<vr::VRInputComponentHandle_t, int> g_handleToHandedness;

		std::shared_mutex g_handednessMutex;
		std::mutex g_handStateMutex;

		struct PerHandStats
		{
			std::atomic<uint64_t> totalCalls{0};
			std::atomic<uint64_t> smoothedCalls{0};
			std::atomic<uint64_t> passthroughCalls{0};
			std::atomic<bool> firstCallLogged{false};
		};
		PerHandStats g_stats[2];
		std::atomic<uint64_t> g_unknownHandleCalls{0};
		std::atomic<uint64_t> g_invalidTransformCalls{0};
		std::atomic<int64_t> g_lastStatsLogQpc{0};
		std::atomic<int64_t> g_lastDeepStateLogQpc{0};
		std::atomic<int64_t> g_subsystemInitQpc{0};
		LARGE_INTEGER g_qpcFreq{};
		constexpr double kStatsLogIntervalSec = 30.0;
		constexpr double kDeepStateLogIntervalSec = 60.0;

		constexpr int kVerboseFirstCalls = 3;
		std::atomic<int> g_verboseCallsRemaining[2] = {{kVerboseFirstCalls}, {kVerboseFirstCalls}};

		std::atomic<bool> g_firstUnknownHandleLogged{false};
		std::atomic<bool> g_firstCreateSkeletonLogged{false};
		std::atomic<bool> g_lastAnySmoothing[2] = {{false}, {false}};

		bool IsReadableMemoryRange(const void* address, size_t bytes)
		{
			if (!address) return false;
			MEMORY_BASIC_INFORMATION info = {};
			if (VirtualQuery(address, &info, sizeof(info)) == 0) return false;
			if (info.State != MEM_COMMIT) return false;
			if (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
			const uintptr_t start = reinterpret_cast<uintptr_t>(address);
			const uintptr_t regionEnd = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
			return start + bytes <= regionEnd;
		}

		void SkeletalContainmentFault(const char* what)
		{
			if (g_skeletalFaulted.exchange(true, std::memory_order_relaxed)) return;
			LOG("[skeletal] finger-smoothing detour threw: %s -- passing fingers through unsmoothed",
			    (what && what[0]) ? what : "(unknown exception)");
		}

		void DumpHandleMap(const char* callerTag)
		{
			std::shared_lock<std::shared_mutex> lk(g_handednessMutex);
			if (g_handleToHandedness.empty()) {
				LOG("[skeletal]   handle_map(%s): EMPTY -- CreateSkeleton never matched /left or /right", callerTag);
				return;
			}
			LOG("[skeletal]   handle_map(%s): %zu entries:", callerTag, g_handleToHandedness.size());
			for (const auto& kv : g_handleToHandedness) {
				LOG("[skeletal]     handle=%llu -> %s", (unsigned long long)kv.first,
				    kv.second == 0 ? "left" : (kv.second == 1 ? "right" : "?"));
			}
		}

		void MaybeLogStats(const char* callerTag)
		{
			if (g_qpcFreq.QuadPart == 0) return;
			LARGE_INTEGER now;
			QueryPerformanceCounter(&now);
			int64_t last = g_lastStatsLogQpc.load(std::memory_order_relaxed);
			if (last == 0) {
				g_lastStatsLogQpc.compare_exchange_strong(last, now.QuadPart);
				return;
			}
			double elapsedSec = (double)(now.QuadPart - last) / (double)g_qpcFreq.QuadPart;
			if (elapsedSec < kStatsLogIntervalSec) return;
			if (!g_lastStatsLogQpc.compare_exchange_strong(last, now.QuadPart)) return;

			uint64_t l_total = g_stats[0].totalCalls.load();
			uint64_t l_smooth = g_stats[0].smoothedCalls.load();
			uint64_t l_pass = g_stats[0].passthroughCalls.load();
			uint64_t r_total = g_stats[1].totalCalls.load();
			uint64_t r_smooth = g_stats[1].smoothedCalls.load();
			uint64_t r_pass = g_stats[1].passthroughCalls.load();

			LOG("[skeletal] stats(%s, %.1fs window) L:%llu(s%llu/p%llu) R:%llu(s%llu/p%llu) unknown_handle=%llu "
			    "invalid_transforms=%llu",
			    callerTag, elapsedSec, (unsigned long long)l_total, (unsigned long long)l_smooth, (unsigned long long)l_pass,
			    (unsigned long long)r_total, (unsigned long long)r_smooth, (unsigned long long)r_pass,
			    (unsigned long long)g_unknownHandleCalls.load(), (unsigned long long)g_invalidTransformCalls.load());

			float l_posDelta, r_posDelta, l_quatDot, r_quatDot;
			int l_posBone, r_posBone, l_quatBone, r_quatBone;
			{
				std::lock_guard<std::mutex> lk(g_handStateMutex);
				l_posDelta = g_handState[0].windowMaxPosDelta;
				l_posBone = g_handState[0].windowMaxPosDeltaBone;
				l_quatDot = g_handState[0].windowMinQuatDot;
				l_quatBone = g_handState[0].windowMinQuatDotBone;
				r_posDelta = g_handState[1].windowMaxPosDelta;
				r_posBone = g_handState[1].windowMaxPosDeltaBone;
				r_quatDot = g_handState[1].windowMinQuatDot;
				r_quatBone = g_handState[1].windowMinQuatDotBone;
				for (int h = 0; h < 2; ++h) {
					g_handState[h].windowMaxPosDelta = 0.0f;
					g_handState[h].windowMaxPosDeltaBone = -1;
					g_handState[h].windowMinQuatDot = 1.0f;
					g_handState[h].windowMinQuatDotBone = -1;
				}
			}
			LOG("[skeletal] motion(%s, %.1fs window) L:maxPosDelta=%.4fm(bone=%d) minQuatDot=%.4f(bone=%d)  "
			    "R:maxPosDelta=%.4fm(bone=%d) minQuatDot=%.4f(bone=%d)",
			    callerTag, elapsedSec, l_posDelta, l_posBone, l_quatDot, l_quatBone, r_posDelta, r_posBone, r_quatDot, r_quatBone);
		}

		void MaybeLogDeepState(const char* callerTag)
		{
			if (g_qpcFreq.QuadPart == 0) return;
			LARGE_INTEGER now;
			QueryPerformanceCounter(&now);
			int64_t last = g_lastDeepStateLogQpc.load(std::memory_order_relaxed);
			if (last == 0) {
				g_lastDeepStateLogQpc.compare_exchange_strong(last, now.QuadPart);
				return;
			}
			double elapsedSec = (double)(now.QuadPart - last) / (double)g_qpcFreq.QuadPart;
			if (elapsedSec < kDeepStateLogIntervalSec) return;
			if (!g_lastDeepStateLogQpc.compare_exchange_strong(last, now.QuadPart)) return;

			uint64_t l_total = g_stats[0].totalCalls.load();
			uint64_t r_total = g_stats[1].totalCalls.load();
			const int64_t initQpc = g_subsystemInitQpc.load(std::memory_order_relaxed);
			const double sinceInitSec = initQpc != 0 ? (double)(now.QuadPart - initQpc) / (double)g_qpcFreq.QuadPart : 0.0;

			LOG("[skeletal] deep_state(%s, %.1fs window): hooks create=%d update=%d L=%llu(%.1fHz) R=%llu(%.1fHz)", callerTag, elapsedSec,
			    (int)IHook::Exists("IVRDriverInput::CreateSkeletonComponent"),
			    (int)IHook::Exists("IVRDriverInput::UpdateSkeletonComponent"), (unsigned long long)l_total,
			    math::ComputeRateHz(l_total, sinceInitSec), (unsigned long long)r_total, math::ComputeRateHz(r_total, sinceInitSec));

			if (g_driver) {
				auto cfg = g_driver->GetFingerSmoothingConfig();
				LOG("[skeletal]   cfg: strength=%u mask=0x%04x", (unsigned)cfg.strength, (unsigned)cfg.fingerMask);
			}

			DumpHandleMap("deep_state");
		}

		Hook<vr::EVRInputError (*)(vr::IVRDriverInput*, vr::VRInputComponentHandle_t, vr::EVRSkeletalMotionRange,
		                           const vr::VRBoneTransform_t*, uint32_t)>
		    PublicUpdateSkeletonHook("IVRDriverInput::UpdateSkeletonComponent");
		Hook<vr::EVRInputError (*)(vr::IVRDriverInput*, vr::PropertyContainerHandle_t, const char*, const char*, const char*,
		                           vr::EVRSkeletalTrackingLevel, const vr::VRBoneTransform_t*, uint32_t, vr::VRInputComponentHandle_t*)>
		    PublicCreateSkeletonHook("IVRDriverInput::CreateSkeletonComponent");

		vr::EVRInputError DetourPublicCreateSkeletonComponent(vr::IVRDriverInput* _this, vr::PropertyContainerHandle_t ulContainer,
		                                                      const char* pchName, const char* pchSkeletonPath, const char* pchBasePosePath,
		                                                      vr::EVRSkeletalTrackingLevel eSkeletalTrackingLevel,
		                                                      const vr::VRBoneTransform_t* pGripLimitTransforms,
		                                                      uint32_t unGripLimitTransformCount, vr::VRInputComponentHandle_t* pHandle)
		{
			auto result =
			    PublicCreateSkeletonHook.originalFunc(_this, ulContainer, pchName, pchSkeletonPath, pchBasePosePath, eSkeletalTrackingLevel,
			                                          pGripLimitTransforms, unGripLimitTransformCount, pHandle);

			bool firstCreateExpected = false;
			if (g_firstCreateSkeletonLogged.compare_exchange_strong(firstCreateExpected, true)) {
				LOG("[skeletal] FIRST CreateSkeleton call: result=%d path='%s' outHandle=%llu", (int)result,
				    pchSkeletonPath ? pchSkeletonPath : "(null)", pHandle ? (unsigned long long)*pHandle : 0ULL);
			}

			if (result == vr::VRInputError_None && pHandle && *pHandle != vr::k_ulInvalidInputComponentHandle && pchSkeletonPath) {
				int handedness = -1;
				if (std::strstr(pchSkeletonPath, "/left"))
					handedness = 0;
				else if (std::strstr(pchSkeletonPath, "/right"))
					handedness = 1;
				if (handedness >= 0) {
					std::unique_lock<std::shared_mutex> hlk(g_handednessMutex);
					std::lock_guard<std::mutex> slk(g_handStateMutex);

					size_t evicted = 0;
					for (auto it = g_handleToHandedness.begin(); it != g_handleToHandedness.end();) {
						if (it->second == handedness && it->first != *pHandle) {
							it = g_handleToHandedness.erase(it);
							++evicted;
						}
						else {
							++it;
						}
					}

					g_handleToHandedness[*pHandle] = handedness;
					for (int range = 0; range < math::kMotionRangeCount; ++range) {
						g_handState[handedness].frame[range].initialized = false;
					}
					g_verboseCallsRemaining[handedness].store(kVerboseFirstCalls);
					LOG("[skeletal] CreateSkeleton MAPPED handle=%llu -> %s (path=%s evicted=%zu map_size_now=%zu)",
					    (unsigned long long)*pHandle, handedness == 0 ? "left" : "right", pchSkeletonPath, evicted,
					    g_handleToHandedness.size());
				}
			}
			return result;
		}

		vr::EVRInputError DetourPublicUpdateSkeletonComponentImpl(vr::IVRDriverInput* _this, vr::VRInputComponentHandle_t ulComponent,
		                                                          vr::EVRSkeletalMotionRange eMotionRange,
		                                                          const vr::VRBoneTransform_t* pTransforms, uint32_t unTransformCount)
		{
			if (!g_driver || !pTransforms || unTransformCount != math::kFingerBoneCount) {
				if (!pTransforms || unTransformCount != math::kFingerBoneCount) {
					g_invalidTransformCalls.fetch_add(1, std::memory_order_relaxed);
				}
				return PublicUpdateSkeletonHook.originalFunc(_this, ulComponent, eMotionRange, pTransforms, unTransformCount);
			}
			auto cfg = g_driver->GetFingerSmoothingConfig();

			int handedness = -1;
			{
				std::shared_lock<std::shared_mutex> lk(g_handednessMutex);
				auto it = g_handleToHandedness.find(ulComponent);
				if (it != g_handleToHandedness.end()) handedness = it->second;
			}
			if (handedness < 0) {
				g_unknownHandleCalls.fetch_add(1, std::memory_order_relaxed);

				bool expectedFirstUnknown = false;
				if (g_firstUnknownHandleLogged.compare_exchange_strong(expectedFirstUnknown, true)) {
					LOG("[skeletal] FIRST unknown-handle UpdateSkeleton: handle=%llu count=%u motionRange=%d",
					    (unsigned long long)ulComponent, unTransformCount, (int)eMotionRange);
					DumpHandleMap("first_unknown");
				}

				MaybeLogStats("UpdateSkeleton/unknown");
				return PublicUpdateSkeletonHook.originalFunc(_this, ulComponent, eMotionRange, pTransforms, unTransformCount);
			}

			g_stats[handedness].totalCalls.fetch_add(1, std::memory_order_relaxed);

			bool expected = false;
			if (g_stats[handedness].firstCallLogged.compare_exchange_strong(expected, true)) {
				LOG("[skeletal] first UpdateSkeleton on %s hand: handle=%llu count=%u motionRange=%d cfg{strength=%u mask=0x%04x}",
				    handedness == 0 ? "left" : "right", (unsigned long long)ulComponent, unTransformCount, (int)eMotionRange,
				    (unsigned)cfg.strength, (unsigned)cfg.fingerMask);
			}

			int verboseRem = g_verboseCallsRemaining[handedness].fetch_sub(1, std::memory_order_relaxed);
			if (verboseRem > 0) {
				const auto& bone1 = pTransforms[1];
				LOG("[skeletal] verbose %s call %d/%d: handle=%llu motion=%d bone1.pos=(%.4f,%.4f,%.4f)", handedness == 0 ? "L" : "R",
				    kVerboseFirstCalls - verboseRem + 1, kVerboseFirstCalls, (unsigned long long)ulComponent, (int)eMotionRange,
				    bone1.position.v[0], bone1.position.v[1], bone1.position.v[2]);
			}

			MaybeLogDeepState("UpdateSkeleton");

			const int handBase = handedness * math::kFingersPerHand;
			float alphaPerFinger[math::kFingersPerHand];
			bool anySmoothing = false;
			for (int f = 0; f < math::kFingersPerHand; ++f) {
				uint8_t s = cfg.perFinger[handBase + f];
				if (s == 0) s = cfg.strength;
				alphaPerFinger[f] = math::SmoothnessToAlpha(s);
				const bool fingerEnabled = ((cfg.fingerMask >> (handBase + f)) & 1u) != 0;
				if (s != 0 && fingerEnabled) anySmoothing = true;
			}

			const bool prevAnySmoothing = g_lastAnySmoothing[handedness].exchange(anySmoothing, std::memory_order_relaxed);

			if (!anySmoothing) {
				g_stats[handedness].passthroughCalls.fetch_add(1, std::memory_order_relaxed);
				MaybeLogStats("UpdateSkeleton");
				return PublicUpdateSkeletonHook.originalFunc(_this, ulComponent, eMotionRange, pTransforms, unTransformCount);
			}

			vr::VRBoneTransform_t smoothed[math::kFingerBoneCount];

			{
				std::lock_guard<std::mutex> lk(g_handStateMutex);
				HandState& state = g_handState[handedness];
				const int rangeIdx = math::MotionRangeIndex((int)eMotionRange);

				if (!prevAnySmoothing) {
					LOG("[skeletal] enable transition on %s hand: alpha=[%.3f %.3f %.3f %.3f %.3f] init=%d",
					    handedness == 0 ? "left" : "right", alphaPerFinger[0], alphaPerFinger[1], alphaPerFinger[2], alphaPerFinger[3],
					    alphaPerFinger[4], (int)state.frame[rangeIdx].initialized);
				}

				const auto frameResult = math::SmoothFingerFrame(state.frame[rangeIdx], pTransforms, unTransformCount, handBase,
				                                                 cfg.fingerMask, alphaPerFinger, smoothed);
				if (frameResult.maxPosDelta > state.windowMaxPosDelta) {
					state.windowMaxPosDelta = frameResult.maxPosDelta;
					state.windowMaxPosDeltaBone = frameResult.maxPosDeltaBone;
				}
				if (frameResult.minQuatDot < state.windowMinQuatDot) {
					state.windowMinQuatDot = frameResult.minQuatDot;
					state.windowMinQuatDotBone = frameResult.minQuatDotBone;
				}
			}

			g_stats[handedness].smoothedCalls.fetch_add(1, std::memory_order_relaxed);
			MaybeLogStats("UpdateSkeleton");
			return PublicUpdateSkeletonHook.originalFunc(_this, ulComponent, eMotionRange, smoothed, unTransformCount);
		}

		vr::EVRInputError DetourPublicUpdateSkeletonComponent(vr::IVRDriverInput* _this, vr::VRInputComponentHandle_t ulComponent,
		                                                      vr::EVRSkeletalMotionRange eMotionRange,
		                                                      const vr::VRBoneTransform_t* pTransforms, uint32_t unTransformCount)
		{
			if (g_skeletalFaulted.load(std::memory_order_relaxed)) {
				return PublicUpdateSkeletonHook.originalFunc(_this, ulComponent, eMotionRange, pTransforms, unTransformCount);
			}
			try {
				return DetourPublicUpdateSkeletonComponentImpl(_this, ulComponent, eMotionRange, pTransforms, unTransformCount);
			}
			catch (const std::exception& ex) {
				SkeletalContainmentFault(ex.what());
			}
			catch (...) {
				SkeletalContainmentFault(nullptr);
			}
			return PublicUpdateSkeletonHook.originalFunc(_this, ulComponent, eMotionRange, pTransforms, unTransformCount);
		}

	}

	void Init(ServerTrackedDeviceProvider* driver)
	{
		g_driver = driver;
		QueryPerformanceFrequency(&g_qpcFreq);
		{
			LARGE_INTEGER initNow;
			QueryPerformanceCounter(&initNow);
			g_subsystemInitQpc.store(initNow.QuadPart);
		}
		LOG("[skeletal] Init: subsystem armed, awaiting IVRDriverInput interface queries");
	}

	void Shutdown()
	{
		MaybeLogStats("Shutdown");
		g_driver = nullptr;
		{
			std::unique_lock<std::shared_mutex> hlk(g_handednessMutex);
			std::lock_guard<std::mutex> slk(g_handStateMutex);
			g_handleToHandedness.clear();
			for (int h = 0; h < 2; ++h) {
				g_handState[h] = HandState{};
				g_lastAnySmoothing[h].store(false, std::memory_order_relaxed);
			}
		}
		LOG("[skeletal] Shutdown: subsystem disarmed");
	}

	void MarkFingersNeedReseed(uint16_t fingerBits)
	{
		if (fingerBits == 0) return;
		std::lock_guard<std::mutex> lk(g_handStateMutex);
		for (int h = 0; h < 2; ++h) {
			for (int f = 0; f < math::kFingersPerHand; ++f) {
				if ((fingerBits >> (h * math::kFingersPerHand + f)) & 1u) {
					for (int range = 0; range < math::kMotionRangeCount; ++range) {
						g_handState[h].frame[range].reseed_pending[f] = true;
					}
				}
			}
		}
	}

	void TryInstallPublicHooks(void* iface)
	{
		if (!iface) return;

		bool createAlready = IHook::Exists(PublicCreateSkeletonHook.name);
		bool updateAlready = IHook::Exists(PublicUpdateSkeletonHook.name);
		if (createAlready && updateAlready) return;

		if (!IsReadableMemoryRange(iface, sizeof(void*))) {
			LOG("[skeletal] iface %p not readable; aborting install", iface);
			return;
		}
		void** vtable = *((void***)iface);
		if (!IsReadableMemoryRange(vtable, sizeof(void*) * 7)) {
			LOG("[skeletal] vtable %p not readable for 7 slots; aborting install (iface=%p)", (void*)vtable, iface);
			return;
		}
		intptr_t spread = (intptr_t)vtable[6] - (intptr_t)vtable[0];
		if (spread < 0) spread = -spread;
		if (spread > 0x10000) {
			LOG("[skeletal] vtable spread |slot6 - slot0| = 0x%llx bytes (>64KB); refusing to install (iface=%p)",
			    (unsigned long long)spread, iface);
			return;
		}

		if (!createAlready) {
			PublicCreateSkeletonHook.CreateHookInObjectVTable(iface, 5, &DetourPublicCreateSkeletonComponent);
			IHook::Register(&PublicCreateSkeletonHook);
		}
		if (!updateAlready) {
			PublicUpdateSkeletonHook.CreateHookInObjectVTable(iface, 6, &DetourPublicUpdateSkeletonComponent);
			IHook::Register(&PublicUpdateSkeletonHook);
		}

		LOG("[skeletal] installed PUBLIC IVRDriverInput hooks: vtable[5]=Create, vtable[6]=Update");
	}

}
