#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace spacecal {

	template <class T> class Seqlock
	{
		static_assert(std::is_trivially_copyable_v<T>, "Seqlock payloads are copied with memcpy");

	public:
		void Write(const T& value)
		{
			const uint32_t s = seq.load(std::memory_order_relaxed);
			seq.store(s + 1, std::memory_order_relaxed);
			std::atomic_thread_fence(std::memory_order_release);
			std::memcpy((void*)&data, &value, sizeof(T));
			seq.store(s + 2, std::memory_order_release);
		}

		bool ReadIfChanged(T& out, uint32_t& lastSeen) const
		{
			const uint32_t s1 = seq.load(std::memory_order_acquire);
			if (s1 == lastSeen || (s1 & 1u) != 0) return false;
			alignas(T) unsigned char copy[sizeof(T)];
			std::memcpy(copy, (const void*)&data, sizeof(T));
			std::atomic_thread_fence(std::memory_order_acquire);
			if (seq.load(std::memory_order_relaxed) != s1) return false;
			std::memcpy((void*)&out, copy, sizeof(T));
			lastSeen = s1;
			return true;
		}

	private:
		std::atomic<uint32_t> seq{0};
		T data{};
	};

}
