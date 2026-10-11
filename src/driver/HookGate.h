#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <mutex>

namespace spacecal::hook_gate {

	namespace detail {
		inline std::atomic<bool> open{true};
		inline std::atomic<int64_t> inFlight{0};
		inline std::mutex installMutex;
	}

	struct Scope
	{
		Scope() : open(false)
		{
			detail::inFlight.fetch_add(1, std::memory_order_seq_cst);
			open = detail::open.load(std::memory_order_seq_cst);
		}

		~Scope() { detail::inFlight.fetch_sub(1, std::memory_order_seq_cst); }

		Scope(const Scope&) = delete;
		Scope& operator=(const Scope&) = delete;

		bool Open() const { return open; }

	private:
		bool open;
	};

	inline std::mutex& InstallMutex()
	{
		return detail::installMutex;
	}

	inline bool IsOpen()
	{
		return detail::open.load(std::memory_order_seq_cst);
	}

	inline void Close()
	{
		detail::open.store(false, std::memory_order_seq_cst);
	}

	inline void Reopen()
	{
		detail::open.store(true, std::memory_order_seq_cst);
	}

	inline bool Drain(unsigned timeoutMs)
	{
		const ULONGLONG deadline = GetTickCount64() + timeoutMs;
		int quietPolls = 0;
		for (;;) {
			if (detail::inFlight.load(std::memory_order_seq_cst) == 0) {
				if (++quietPolls >= 2) return true;
			}
			else {
				quietPolls = 0;
			}
			if (GetTickCount64() >= deadline) return false;
			Sleep(1);
		}
	}

}
