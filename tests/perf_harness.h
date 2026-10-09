#pragma once

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <MinHook.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace perf {

	inline int failures = 0;

#define PERF_CHECK(cond)                                                                                                                   \
	do {                                                                                                                                   \
		if (!(cond)) {                                                                                                                     \
			std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);                                                                     \
			++perf::failures;                                                                                                              \
		}                                                                                                                                  \
	} while (0)

	struct HeapUsage
	{
		uint64_t allocations = 0;
		uint64_t bytesRequested = 0;
		int64_t liveBytes = 0;
		int64_t peakBytes = 0;
	};

	namespace detail {
		using AllocFn = void*(NTAPI*)(void*, ULONG, SIZE_T);
		using FreeFn = BOOLEAN(NTAPI*)(void*, ULONG, void*);
		using ReAllocFn = void*(NTAPI*)(void*, ULONG, void*, SIZE_T);
		using SizeFn = SIZE_T(NTAPI*)(void*, ULONG, const void*);

		inline AllocFn origAlloc = nullptr;
		inline FreeFn origFree = nullptr;
		inline ReAllocFn origReAlloc = nullptr;
		inline SizeFn sizeOf = nullptr;
		inline std::atomic<DWORD> trackedThread{0};
		inline HeapUsage usage;

		inline bool Tracked()
		{
			const DWORD id = trackedThread.load(std::memory_order_relaxed);
			return id != 0 && id == GetCurrentThreadId();
		}

		inline void AddLive(int64_t delta)
		{
			usage.liveBytes += delta;
			usage.peakBytes = std::max(usage.peakBytes, usage.liveBytes);
		}

		inline void* NTAPI Alloc(void* heap, ULONG flags, SIZE_T size)
		{
			void* p = origAlloc(heap, flags, size);
			if (p && Tracked()) {
				++usage.allocations;
				usage.bytesRequested += size;
				AddLive((int64_t)size);
			}
			return p;
		}

		inline BOOLEAN NTAPI Free(void* heap, ULONG flags, void* p)
		{
			if (p && Tracked()) {
				const SIZE_T size = sizeOf(heap, 0, p);
				if (size != (SIZE_T)-1) AddLive(-(int64_t)size);
			}
			return origFree(heap, flags, p);
		}

		inline void* NTAPI ReAlloc(void* heap, ULONG flags, void* p, SIZE_T size)
		{
			const bool tracked = Tracked();
			SIZE_T before = 0;
			if (tracked && p) {
				before = sizeOf(heap, 0, p);
				if (before == (SIZE_T)-1) before = 0;
			}
			void* q = origReAlloc(heap, flags, p, size);
			if (q && tracked) {
				++usage.allocations;
				usage.bytesRequested += size;
				AddLive((int64_t)size - (int64_t)before);
			}
			return q;
		}
	}

	inline bool InstallHeapMeter()
	{
		const MH_STATUS init = MH_Initialize();
		if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) return false;
		detail::sizeOf = (detail::SizeFn)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlSizeHeap");
		if (!detail::sizeOf) return false;
		if (MH_CreateHookApi(L"ntdll", "RtlAllocateHeap", (LPVOID)&detail::Alloc, (LPVOID*)&detail::origAlloc) != MH_OK) return false;
		if (MH_CreateHookApi(L"ntdll", "RtlFreeHeap", (LPVOID)&detail::Free, (LPVOID*)&detail::origFree) != MH_OK) return false;
		if (MH_CreateHookApi(L"ntdll", "RtlReAllocateHeap", (LPVOID)&detail::ReAlloc, (LPVOID*)&detail::origReAlloc) != MH_OK) return false;
		return MH_EnableHook(MH_ALL_HOOKS) == MH_OK;
	}

	inline void RemoveHeapMeter()
	{
		MH_DisableHook(MH_ALL_HOOKS);
	}

	template <class F> HeapUsage MeasureHeap(F&& body)
	{
		detail::usage = HeapUsage{};
		detail::trackedThread.store(GetCurrentThreadId(), std::memory_order_relaxed);
		body();
		detail::trackedThread.store(0, std::memory_order_relaxed);
		return detail::usage;
	}

	inline double QpcSeconds()
	{
		static const double freq = [] {
			LARGE_INTEGER f;
			QueryPerformanceFrequency(&f);
			return (double)f.QuadPart;
		}();
		LARGE_INTEGER now;
		QueryPerformanceCounter(&now);
		return (double)now.QuadPart / freq;
	}

	template <class F> double BestSecondsPerOp(int rounds, int opsPerRound, F&& op)
	{
		double best = std::numeric_limits<double>::infinity();
		for (int r = 0; r < rounds; ++r) {
			const double start = QpcSeconds();
			for (int i = 0; i < opsPerRound; ++i)
				op(i);
			best = std::min(best, (QpcSeconds() - start) / opsPerRound);
		}
		return best;
	}

	inline void Budget(const char* name, double measured, double budget, const char* unit)
	{
		const bool ok = measured <= budget;
		std::printf("%s %-52s %12.3f %-6s (budget %.3f)\n", ok ? "PERF" : "FAIL", name, measured, unit, budget);
		if (!ok) ++failures;
	}

	inline void Report(const char* name, double measured, const char* unit)
	{
		std::printf("INFO %-52s %12.3f %s\n", name, measured, unit);
	}

	inline int Finish(const char* suite)
	{
		if (failures == 0) {
			std::printf("%s: all tests passed\n", suite);
			return 0;
		}
		std::printf("%s: %d failure(s)\n", suite, failures);
		return 1;
	}

}
