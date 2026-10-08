#pragma once

#include "Logging.h"

#include <cstddef>
#include <cstdint>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace spacecal {

	inline bool IsReadableMemoryRange(const void* address, size_t bytes)
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

	inline bool IsDriverInputVTableSane(void* iface, const char* logTag)
	{
		if (!IsReadableMemoryRange(iface, sizeof(void*))) {
			LOG("[%s] iface %p not readable; aborting install", logTag, iface);
			return false;
		}
		void** vtable = *((void***)iface);
		if (!IsReadableMemoryRange(vtable, sizeof(void*) * 7)) {
			LOG("[%s] vtable %p not readable for 7 slots; aborting install (iface=%p)", logTag, (void*)vtable, iface);
			return false;
		}
		intptr_t spread = (intptr_t)vtable[6] - (intptr_t)vtable[0];
		if (spread < 0) spread = -spread;
		if (spread > 0x10000) {
			LOG("[%s] vtable spread |slot6 - slot0| = 0x%llx bytes (>64KB); refusing to install (iface=%p)", logTag,
			    (unsigned long long)spread, iface);
			return false;
		}
		return true;
	}

}
