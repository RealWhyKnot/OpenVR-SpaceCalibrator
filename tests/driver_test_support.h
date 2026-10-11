#pragma once

#include "perf_harness.h"

#include <intrin.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace driver_access {

	template <class Tag, typename Tag::type Member> struct Grant
	{
		friend typename Tag::type Get(Tag) { return Member; }
	};

}

namespace driverlat {

	inline uint64_t Ticks()
	{
		_mm_lfence();
		const uint64_t t = __rdtsc();
		_mm_lfence();
		return t;
	}

	inline double TicksPerNs()
	{
		static const double ratio = [] {
			const double q0 = perf::QpcSeconds();
			const uint64_t t0 = Ticks();
			while (perf::QpcSeconds() - q0 < 0.05) {}
			const double q1 = perf::QpcSeconds();
			const uint64_t t1 = Ticks();
			return (double)(t1 - t0) / ((q1 - q0) * 1e9);
		}();
		return ratio;
	}

	struct Stats
	{
		size_t n = 0;
		double meanNs = 0;
		double p50Ns = 0;
		double p99Ns = 0;
		double p999Ns = 0;
		double maxNs = 0;
		size_t over10us = 0;
		size_t over100us = 0;
	};

	inline Stats Summarize(std::vector<uint64_t>& ticks)
	{
		Stats s;
		s.n = ticks.size();
		if (ticks.empty()) return s;
		const double k = 1.0 / TicksPerNs();
		double sum = 0;
		for (uint64_t t : ticks) {
			const double ns = (double)t * k;
			sum += ns;
			if (ns > 10000.0) ++s.over10us;
			if (ns > 100000.0) ++s.over100us;
		}
		std::sort(ticks.begin(), ticks.end());
		auto at = [&](double q) {
			return (double)ticks[std::min(ticks.size() - 1, (size_t)(q * (double)(ticks.size() - 1)))] * k;
		};
		s.meanNs = sum / (double)ticks.size();
		s.p50Ns = at(0.50);
		s.p99Ns = at(0.99);
		s.p999Ns = at(0.999);
		s.maxNs = (double)ticks.back() * k;
		return s;
	}

	inline void Print(const char* name, const Stats& s)
	{
		std::printf("TAIL %-50s mean %7.0f p50 %7.0f p99 %7.0f p99.9 %8.0f max %9.0f ns  >10us %zu >100us %zu (n=%zu)\n", name, s.meanNs,
		            s.p50Ns, s.p99Ns, s.p999Ns, s.maxNs, s.over10us, s.over100us, s.n);
	}

	template <class F> Stats Measure(std::vector<uint64_t>& buffer, F&& op)
	{
		for (size_t i = 0; i < buffer.size(); ++i) {
			const uint64_t t0 = Ticks();
			op((int)i);
			buffer[i] = Ticks() - t0;
		}
		return Summarize(buffer);
	}

}
