#include "perf_harness.h"
#include "driver_test_support.h"

#include "Hooking.h"
#include "InterfaceHookInjector.h"
#include "Logging.h"
#include "Protocol.h"
#include "ServerTrackedDeviceProvider.h"
#include "SkeletalHook.h"
#include "StickHook.h"

#include <psapi.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace driver_access {

	struct ProviderShmem
	{
		using type = protocol::DriverPoseShmem ServerTrackedDeviceProvider::*;
		friend type Get(ProviderShmem);
	};
	template struct Grant<ProviderShmem, &ServerTrackedDeviceProvider::shmem>;

}

namespace {

	using AugmentedPose = protocol::DriverPoseShmem::AugmentedPose;
	constexpr uint32_t kRingSlots = 64 * 1024;
	constexpr double kWriterStride = 1e9;

	ServerTrackedDeviceProvider g_provider;

	std::string UniqueName(const char* tag)
	{
		return std::string("SpaceCalDriverRace_") + tag + "_" + std::to_string(GetCurrentProcessId());
	}

	vr::DriverPose_t TaggedPose(int writer, uint64_t seq)
	{
		const double v = writer * kWriterStride + (double)seq;
		vr::DriverPose_t p;
		std::memset(&p, 0, sizeof p);
		p.poseIsValid = true;
		p.deviceIsConnected = true;
		p.result = vr::TrackingResult_Running_OK;
		p.poseTimeOffset = v;
		for (int i = 0; i < 3; ++i) {
			p.vecWorldFromDriverTranslation[i] = v;
			p.vecDriverFromHeadTranslation[i] = v;
			p.vecPosition[i] = v;
			p.vecVelocity[i] = v;
			p.vecAcceleration[i] = v;
			p.vecAngularVelocity[i] = v;
			p.vecAngularAcceleration[i] = v;
		}
		p.qWorldFromDriverRotation = {v, v, v, v};
		p.qDriverFromHeadRotation = {v, v, v, v};
		p.qRotation = {v, v, v, v};
		return p;
	}

	bool TagOf(const vr::DriverPose_t& p, double& v)
	{
		v = p.poseTimeOffset;
		return p.qWorldFromDriverRotation.w == v && p.vecWorldFromDriverTranslation[2] == v && p.vecPosition[0] == v &&
		       p.vecPosition[2] == v && p.vecVelocity[1] == v && p.qRotation.z == v && p.vecAngularVelocity[0] == v &&
		       p.vecAngularAcceleration[2] == v;
	}

	struct RingAudit
	{
		explicit RingAudit(uint64_t perWriter)
		{
			for (auto& s : seen)
				s.assign(perWriter, 0);
		}

		void Check(const AugmentedPose& a)
		{
			++delivered;
			double v = 0;
			if (!TagOf(a.pose, v)) {
				++torn;
				return;
			}
			const int writer = (int)(v / kWriterStride);
			const uint64_t seq = (uint64_t)(v - writer * kWriterStride);
			if (writer < 1 || writer > 2 || writer != a.deviceId || seq >= seen[writer].size()) {
				++wrongDevice;
				return;
			}
			if (any[writer] && seq <= last[writer]) ++outOfOrder;
			if (!any[writer] || seq > last[writer]) last[writer] = seq;
			any[writer] = true;
			seen[writer][seq] = 1;
		}

		uint64_t Distinct() const
		{
			uint64_t n = 0;
			for (const auto& s : seen)
				for (uint8_t b : s)
					n += b;
			return n;
		}

		uint64_t delivered = 0;
		uint64_t torn = 0;
		uint64_t wrongDevice = 0;
		uint64_t outOfOrder = 0;
		std::vector<uint8_t> seen[3];
		uint64_t last[3] = {};
		bool any[3] = {};
	};

	struct RingResult
	{
		uint64_t written = 0;
		uint64_t delivered = 0;
		uint64_t torn = 0;
		uint64_t wrongDevice = 0;
		uint64_t outOfOrder = 0;
		uint64_t lost = 0;
		double seconds = 0;
		driverlat::Stats writerTail;
	};

	template <class Write> RingResult RunRing(const std::string& name, int writers, uint64_t perWriter, double periodNs, Write&& write)
	{
		RingResult result;
		protocol::DriverPoseShmem reader;
		reader.Open(name.c_str());
		reader.ReadNewPoses([](const AugmentedPose&) {});
		RingAudit audit(perWriter);
		std::vector<uint64_t> tail(perWriter);
		std::atomic<int> running{writers};
		std::atomic<bool> go{false};
		const uint64_t period = (uint64_t)(periodNs * driverlat::TicksPerNs());
		std::vector<std::thread> threads;
		for (int w = 1; w <= writers; ++w) {
			threads.emplace_back([&, w] {
				while (!go.load(std::memory_order_acquire)) {}
				uint64_t next = driverlat::Ticks();
				for (uint64_t s = 0; s < perWriter; ++s) {
					if (period) {
						while (__rdtsc() < next)
							_mm_pause();
						next += period;
					}
					const vr::DriverPose_t pose = TaggedPose(w, s);
					const uint64_t t0 = driverlat::Ticks();
					write(w, pose);
					if (w == 1) tail[s] = driverlat::Ticks() - t0;
				}
				running.fetch_sub(1, std::memory_order_release);
			});
		}
		const double start = perf::QpcSeconds();
		go.store(true, std::memory_order_release);
		auto cb = [&](const AugmentedPose& a) {
			audit.Check(a);
		};
		while (running.load(std::memory_order_acquire) > 0)
			reader.ReadNewPoses(cb);
		for (auto& t : threads)
			t.join();
		reader.ReadNewPoses(cb);
		result.seconds = perf::QpcSeconds() - start;
		result.written = perWriter * writers;
		result.delivered = audit.delivered;
		result.torn = audit.torn;
		result.wrongDevice = audit.wrongDevice;
		result.outOfOrder = audit.outOfOrder;
		result.lost = result.written - audit.Distinct();
		result.writerTail = driverlat::Summarize(tail);
		return result;
	}

	void PrintRing(const char* label, const RingResult& r)
	{
		std::printf("RING %-44s written %9llu delivered %9llu torn %6llu wrong_dev %5llu out_of_order %6llu lost %7llu (%.2fs)\n", label,
		            (unsigned long long)r.written, (unsigned long long)r.delivered, (unsigned long long)r.torn,
		            (unsigned long long)r.wrongDevice, (unsigned long long)r.outOfOrder, (unsigned long long)r.lost, r.seconds);
		driverlat::Print(label, r.writerTail);
	}

	bool RingIntegrity(const RingResult& r)
	{
		return r.torn == 0 && r.wrongDevice == 0;
	}

	void TestRingMultiWriter()
	{
		const std::string name = UniqueName("ring");
		protocol::DriverPoseShmem writer;
		PERF_CHECK(writer.Create(name.c_str()));
		const uint64_t perWriter = 2000000;

		auto write = [&](int w, const vr::DriverPose_t& p) {
			writer.SetPose(w, p);
		};

		const auto dual = RunRing(name, 2, perWriter, 250.0, write);
		PrintRing("SetPose, 2 writers @4 MHz each", dual);
		perf::Budget("ring @4 MHz: torn poses, 2 writers", (double)dual.torn, 0.0, "poses");
		perf::Budget("ring @4 MHz: wrong-device poses, 2 writers", (double)dual.wrongDevice, 0.0, "poses");

		const auto dualSlow = RunRing(name, 2, 200000, 20000.0, write);
		PrintRing("SetPose, 2 writers @50 kHz each", dualSlow);
		perf::Budget("ring @50 kHz: torn+wrong-device poses, 2 writers", (double)(dualSlow.torn + dualSlow.wrongDevice), 0.0, "poses");
		perf::Budget("ring @50 kHz: out-of-order poses, 2 writers", (double)dualSlow.outOfOrder, 0.0, "poses");
		perf::Budget("ring @50 kHz: poses never delivered, 2 writers", (double)dualSlow.lost, 0.0, "poses");

		const auto pose = TaggedPose(1, 7);
		const double ns = 1e9 * perf::BestSecondsPerOp(7, 200000, [&](int i) { writer.SetPose(1 + i % 8, pose); });
		perf::Report("ring write, 1 thread (ns/pose)", ns, "ns");
	}

	void TestRingFirstLap()
	{
		const std::string name = UniqueName("lap");
		protocol::DriverPoseShmem writer;
		PERF_CHECK(writer.Create(name.c_str()));
		const auto pose = TaggedPose(1, 3);
		std::vector<uint64_t> samples(kRingSlots);

		PROCESS_MEMORY_COUNTERS_EX m0{}, m1{}, m2{};
		GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&m0, sizeof m0);
		const auto lap1 = driverlat::Measure(samples, [&](int i) { writer.SetPose(1 + i % 8, pose); });
		GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&m1, sizeof m1);
		const auto lap2 = driverlat::Measure(samples, [&](int i) { writer.SetPose(1 + i % 8, pose); });
		GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&m2, sizeof m2);

		driverlat::Print("ring write: first lap of a fresh mapping", lap1);
		driverlat::Print("ring write: second lap", lap2);
		perf::Report("ring first lap: page faults", (double)(m1.PageFaultCount - m0.PageFaultCount), "faults");
		perf::Report("ring second lap: page faults", (double)(m2.PageFaultCount - m1.PageFaultCount), "faults");
		perf::Report("ring first lap: working set growth (MiB)",
		             ((double)m1.WorkingSetSize - (double)m0.WorkingSetSize) / (1024.0 * 1024.0), "MiB");
	}

	vr::HmdQuaternion_t YawQuat(double radians)
	{
		return {std::cos(radians / 2), 0.0, std::sin(radians / 2), 0.0};
	}

	protocol::AlignmentSpeedParams OverlayDefaultAlignment()
	{
		const double deg = 3.14159265358979323846 / 180.0;
		protocol::AlignmentSpeedParams p{};
		p.thr_rot_tiny = 0.49 * deg;
		p.thr_rot_small = 0.5 * deg;
		p.thr_rot_large = 5.0 * deg;
		p.thr_trans_tiny = 0.98 / 1000.0;
		p.thr_trans_small = 1.0 / 1000.0;
		p.thr_trans_large = 20.0 / 1000.0;
		p.align_speed_tiny = 1.0;
		p.align_speed_small = 1.0;
		p.align_speed_large = 2.0;
		return p;
	}

	vr::DriverPose_t InputPose()
	{
		vr::DriverPose_t p;
		std::memset(&p, 0, sizeof p);
		p.poseIsValid = true;
		p.deviceIsConnected = true;
		p.result = vr::TrackingResult_Running_OK;
		p.qWorldFromDriverRotation = {1, 0, 0, 0};
		p.qDriverFromHeadRotation = {1, 0, 0, 0};
		p.qRotation = YawQuat(0.3);
		p.vecPosition[0] = 0.2;
		p.vecPosition[1] = 1.1;
		p.vecPosition[2] = 0.1;
		return p;
	}

	struct PoseOut
	{
		double q[4];
		double t[3];
	};

	constexpr uint32_t kRaceDevice = 5;

	PoseOut RunPose()
	{
		vr::DriverPose_t pose = InputPose();
		g_provider.HandleDevicePoseUpdated(kRaceDevice, pose);
		const auto& q = pose.qWorldFromDriverRotation;
		return {{q.w, q.x, q.y, q.z},
		        {pose.vecWorldFromDriverTranslation[0], pose.vecWorldFromDriverTranslation[1], pose.vecWorldFromDriverTranslation[2]}};
	}

	bool Near(const PoseOut& a, const PoseOut& b, double tol)
	{
		for (int i = 0; i < 4; ++i)
			if (!(std::fabs(a.q[i] - b.q[i]) <= tol)) return false;
		for (int i = 0; i < 3; ++i)
			if (!(std::fabs(a.t[i] - b.t[i]) <= tol)) return false;
		return true;
	}

	double NormError(const PoseOut& o)
	{
		return std::fabs(std::sqrt(o.q[0] * o.q[0] + o.q[1] * o.q[1] + o.q[2] * o.q[2] + o.q[3] * o.q[3]) - 1.0);
	}

	protocol::SetDeviceTransform RaceTransform(bool a)
	{
		protocol::SetDeviceTransform t(kRaceDevice, true, a ? vr::HmdVector3d_t{0.12, -0.03, 0.4} : vr::HmdVector3d_t{-0.5, 0.2, -0.3},
		                               YawQuat(a ? 0.7 : -1.2), 1.0);
		return t;
	}

	void TestTransformRace()
	{
		g_provider.HandleSetAlignmentSpeedParams(OverlayDefaultAlignment());
		const auto A = RaceTransform(true);
		const auto B = RaceTransform(false);
		g_provider.SetDeviceTransform(A);
		RunPose();
		const PoseOut outA = RunPose();
		g_provider.SetDeviceTransform(B);
		RunPose();
		const PoseOut outB = RunPose();
		g_provider.SetDeviceTransform(A);
		PERF_CHECK(Near(RunPose(), outA, 1e-9));

		std::atomic<bool> stop{false};
		std::atomic<uint64_t> gen{0};
		std::atomic<uint64_t> ack{0};
		uint64_t calls = 0, badNorm = 0, neither = 0, lostSnaps = 0;
		std::thread poseThread([&] {
			uint64_t seenGen = 0;
			int callsInGen = 0;
			while (!stop.load(std::memory_order_relaxed)) {
				const uint64_t g = gen.load(std::memory_order_acquire);
				if (g != seenGen) {
					seenGen = g;
					callsInGen = 0;
				}
				const PoseOut o = RunPose();
				++calls;
				++callsInGen;
				if (NormError(o) > 1e-9) ++badNorm;
				if (!Near(o, outA, 1e-6) && !Near(o, outB, 1e-6)) ++neither;
				if (g != 0 && callsInGen == 4) {
					if (!Near(o, (g & 1) ? outB : outA, 1e-6)) ++lostSnaps;
					ack.store(g, std::memory_order_release);
				}
			}
		});
		const int trials = 20000;
		for (int t = 1; t <= trials; ++t) {
			g_provider.SetDeviceTransform((t & 1) ? B : A);
			gen.store((uint64_t)t, std::memory_order_release);
			while (ack.load(std::memory_order_acquire) < (uint64_t)t)
				_mm_pause();
		}
		stop.store(true);
		poseThread.join();

		perf::Report("transform race: pose calls during 20k IPC snaps", (double)calls, "calls");
		perf::Report("transform race: snaps lost (4th pose after write != target)", (double)lostSnaps, "of 20000");
		perf::Report("transform race: torn blends of two calibrations", (double)neither, "poses");
		perf::Report("transform race: non-unit rotations (|q|-1 > 1e-9)", (double)badNorm, "poses");
		PERF_CHECK(neither == 0);
		PERF_CHECK(badNorm == 0);
	}

	struct TransformMailbox
	{
		std::atomic<uint32_t> seq{0};
		protocol::SetDeviceTransform cmd{0, false};

		void Publish(const protocol::SetDeviceTransform& next)
		{
			const uint32_t s = seq.load(std::memory_order_relaxed);
			seq.store(s + 1, std::memory_order_relaxed);
			std::atomic_thread_fence(std::memory_order_release);
			std::memcpy((void*)&cmd, &next, sizeof cmd);
			seq.store(s + 2, std::memory_order_release);
		}

		bool Poll(uint32_t& lastSeen, protocol::SetDeviceTransform& out) const
		{
			uint32_t s1 = seq.load(std::memory_order_acquire);
			if (s1 == lastSeen) return false;
			for (;;) {
				if ((s1 & 1) == 0) {
					std::memcpy((void*)&out, (const void*)&cmd, sizeof out);
					std::atomic_thread_fence(std::memory_order_acquire);
					const uint32_t s2 = seq.load(std::memory_order_relaxed);
					if (s1 == s2) {
						lastSeen = s1;
						return true;
					}
					s1 = s2;
				}
				else {
					_mm_pause();
					s1 = seq.load(std::memory_order_acquire);
				}
			}
		}
	};

	bool SamePayload(const protocol::SetDeviceTransform& a, const protocol::SetDeviceTransform& b)
	{
		return a.translation.v[0] == b.translation.v[0] && a.translation.v[1] == b.translation.v[1] &&
		       a.translation.v[2] == b.translation.v[2] && a.rotation.w == b.rotation.w && a.rotation.x == b.rotation.x &&
		       a.rotation.y == b.rotation.y && a.rotation.z == b.rotation.z;
	}

	void TestMailboxPrototype()
	{
		const auto A = RaceTransform(true);
		const auto B = RaceTransform(false);
		TransformMailbox box;
		box.Publish(A);
		volatile protocol::SetDeviceTransform shared = A;

		std::atomic<bool> stop{false};
		std::thread writer([&] {
			uint64_t n = 0;
			while (!stop.load(std::memory_order_relaxed)) {
				const auto& next = (n & 1) ? A : B;
				box.Publish(next);
				std::memcpy((void*)&shared, &next, sizeof next);
				++n;
			}
		});
		uint64_t tornPlain = 0, tornBox = 0, polled = 0;
		uint32_t lastSeen = 0;
		protocol::SetDeviceTransform got{0, false};
		for (int i = 0; i < 2000000; ++i) {
			protocol::SetDeviceTransform plain{0, false};
			std::memcpy(&plain, (const void*)&shared, sizeof plain);
			if (!SamePayload(plain, A) && !SamePayload(plain, B)) ++tornPlain;
			if (box.Poll(lastSeen, got)) {
				++polled;
				if (!SamePayload(got, A) && !SamePayload(got, B)) ++tornBox;
			}
		}
		stop.store(true);
		writer.join();
		perf::Report("mailbox: torn plain copies of SetDeviceTransform (of 2M)", (double)tornPlain, "copies");
		perf::Report("mailbox: torn seqlock copies (of polled)", (double)tornBox, "copies");
		perf::Report("mailbox: seqlock copies taken", (double)polled, "copies");
		PERF_CHECK(tornBox == 0);

		uint32_t idle = lastSeen;
		protocol::SetDeviceTransform sink{0, false};
		const double pollNs = 1e9 * perf::BestSecondsPerOp(7, 1000000, [&](int) { box.Poll(idle, sink); });
		perf::Report("mailbox: Poll() with no new command (ns/pose)", pollNs, "ns");
	}

	class FakeProperties : public vr::IVRProperties
	{
	public:
		__declspec(noinline) vr::ETrackedPropertyError ReadPropertyBatch(vr::PropertyContainerHandle_t, vr::PropertyRead_t* batch,
		                                                                 uint32_t count) override
		{
			for (uint32_t i = 0; i < count; ++i)
				batch[i].eError = vr::TrackedProp_UnknownProperty;
			return vr::TrackedProp_Success;
		}
		__declspec(noinline) vr::ETrackedPropertyError WritePropertyBatch(vr::PropertyContainerHandle_t, vr::PropertyWrite_t*,
		                                                                  uint32_t) override
		{
			return vr::TrackedProp_InvalidOperation;
		}
		const char* GetPropErrorNameFromEnum(vr::ETrackedPropertyError) override { return "error"; }
		vr::PropertyContainerHandle_t TrackedDeviceToPropertyContainer(vr::TrackedDeviceIndex_t) override
		{
			return vr::k_ulInvalidPropertyContainer;
		}
	};

	class FakeHost : public vr::IVRServerDriverHost
	{
	public:
		volatile uint64_t poses = 0;
		volatile double lastX = 0;

		__declspec(noinline) bool TrackedDeviceAdded(const char*, vr::ETrackedDeviceClass, vr::ITrackedDeviceServerDriver*) override
		{
			return lastX > 1e300;
		}
		__declspec(noinline) void TrackedDevicePoseUpdated(uint32_t, const vr::DriverPose_t& newPose, uint32_t) override
		{
			lastX = newPose.vecPosition[0];
			poses = poses + 1;
		}
		void VsyncEvent(double) override {}
		void VendorSpecificEvent(uint32_t, vr::EVREventType, const vr::VREvent_Data_t&, double) override {}
		bool IsExiting() override { return false; }
		bool PollNextEvent(vr::VREvent_t*, uint32_t) override { return false; }
		void GetRawTrackedDevicePoses(float, vr::TrackedDevicePose_t*, uint32_t) override {}
		void RequestRestart(const char*, const char*, const char*, const char*) override {}
		uint32_t GetFrameTimings(vr::Compositor_FrameTiming*, uint32_t) override { return 0; }
		void SetDisplayEyeToHead(uint32_t, const vr::HmdMatrix34_t&, const vr::HmdMatrix34_t&) override {}
		void SetDisplayProjectionRaw(uint32_t, const vr::HmdRect2_t&, const vr::HmdRect2_t&) override {}
		void SetRecommendedRenderTargetSize(uint32_t, uint32_t, uint32_t) override {}
	};

	class FakeContext : public vr::IVRDriverContext
	{
	public:
		FakeProperties properties;
		FakeHost host;
		std::atomic<bool> blockNextQuery{false};
		std::atomic<bool> queryBlocked{false};
		HANDLE releaseQuery = nullptr;

		__declspec(noinline) void* GetGenericInterface(const char* version, vr::EVRInitError* error) override
		{
			if (blockNextQuery.load() && blockNextQuery.exchange(false)) {
				queryBlocked.store(true);
				WaitForSingleObject(releaseQuery, 10000);
			}
			if (error) *error = vr::VRInitError_None;
			if (std::strcmp(version, vr::IVRServerDriverHost_Version) == 0) return &host;
			if (std::strcmp(version, vr::IVRProperties_Version) == 0) return &properties;
			return this;
		}
		__declspec(noinline) vr::DriverHandle_t GetDriverHandle() override { return 77; }
	};

	FakeContext g_context;
	vr::IVRDriverContext* volatile g_contextBase = &g_context;

	vr::EVRInitError InitContext()
	{
		VR_INIT_SERVER_DRIVER_CONTEXT(g_contextBase);
		return vr::VRInitError_None;
	}

	LONG WINAPI ExitOnCrash(EXCEPTION_POINTERS* info)
	{
		TerminateProcess(GetCurrentProcess(), info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0xDEAD);
		return EXCEPTION_EXECUTE_HANDLER;
	}

	void QuietCrashes()
	{
		SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
		SetUnhandledExceptionFilter(ExitOnCrash);
		_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
	}

	int CleanupRaceChild()
	{
		QuietCrashes();
		const std::string name = UniqueName("cleanup");
		auto& shmem = g_provider.*Get(driver_access::ProviderShmem{});
		const int iterations = 100;
		uint64_t posesSeen = 0;
		double slowestCleanup = 0.0;
		for (int it = 0; it < iterations; ++it) {
			if (InitContext() != vr::VRInitError_None) return 2;
			spacecal::skeletal_hook::Init(&g_provider);
			spacecal::stick_hook::Init();
			InjectHooks(&g_provider, g_contextBase);
			if (!shmem.Create(name.c_str())) return 3;
			auto* host =
			    static_cast<vr::IVRServerDriverHost*>(g_contextBase->GetGenericInterface(vr::IVRServerDriverHost_Version, nullptr));
			if (!host) return 4;
			std::atomic<bool> stop{false};
			std::atomic<bool> started{false};
			std::thread poseThread([&] {
				const vr::DriverPose_t pose = InputPose();
				vr::IVRServerDriverHost* volatile target = host;
				while (!stop.load(std::memory_order_relaxed)) {
					target->TrackedDevicePoseUpdated(1, pose, sizeof pose);
					started.store(true, std::memory_order_relaxed);
				}
			});
			while (!started.load())
				_mm_pause();
			Sleep(1);
			const double cleanupStart = perf::QpcSeconds();
			g_provider.Cleanup();
			slowestCleanup = std::max(slowestCleanup, perf::QpcSeconds() - cleanupStart);
			stop.store(true);
			poseThread.join();
			posesSeen = g_context.host.poses;
		}
		std::printf("cleanup race child: %d Init/Cleanup cycles with a live pose thread survived (%llu poses, slowest Cleanup %.1f ms)\n",
		            iterations, (unsigned long long)posesSeen, slowestCleanup * 1000.0);
		return slowestCleanup < 5.0 ? 0 : 5;
	}

	int LateHookInstallChild()
	{
		QuietCrashes();
		if (InitContext() != vr::VRInitError_None) return 2;
		InjectHooks(&g_provider, g_contextBase);
		g_context.releaseQuery = CreateEventA(nullptr, TRUE, FALSE, nullptr);
		if (!g_context.releaseQuery) return 3;
		g_context.blockNextQuery.store(true);
		const uint64_t posesBefore = g_context.host.poses;
		std::atomic<vr::IVRServerDriverHost*> host{nullptr};
		std::thread late([&] {
			auto* found =
			    static_cast<vr::IVRServerDriverHost*>(g_contextBase->GetGenericInterface(vr::IVRServerDriverHost_Version, nullptr));
			host.store(found);
			if (!found) return;
			const vr::DriverPose_t pose = InputPose();
			found->TrackedDevicePoseUpdated(1, pose, sizeof pose);
		});
		const ULONGLONG deadline = GetTickCount64() + 10000;
		while (!g_context.queryBlocked.load() && GetTickCount64() < deadline)
			SwitchToThread();
		const bool blocked = g_context.queryBlocked.load();
		DisableHooks();
		SetEvent(g_context.releaseQuery);
		late.join();
		const bool removed = RemoveHooks();
		const uint64_t delivered = g_context.host.poses - posesBefore;
		CloseHandle(g_context.releaseQuery);
		std::printf("late hook child: blocked=%d host=%p delivered=%llu removed=%d\n", (int)blocked, (void*)host.load(),
		            (unsigned long long)delivered, (int)removed);
		return blocked && host.load() && delivered == 1 && removed ? 0 : 7;
	}

	DWORD RunChild(const char* flag, DWORD timeoutMs)
	{
		char exe[MAX_PATH];
		GetModuleFileNameA(nullptr, exe, MAX_PATH);
		std::string cmd = std::string("\"") + exe + "\" " + flag;
		STARTUPINFOA si{};
		si.cb = sizeof si;
		PROCESS_INFORMATION pi{};
		if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return 0xFFFFFFFF;
		DWORD code = 0xFFFFFFFE;
		if (WaitForSingleObject(pi.hProcess, timeoutMs) == WAIT_OBJECT_0)
			GetExitCodeProcess(pi.hProcess, &code);
		else
			TerminateProcess(pi.hProcess, 0xFFFFFFFD);
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
		return code;
	}

	void TestCleanupRace()
	{
		const DWORD code = RunChild("--cleanup-race-child", 180000);
		const bool crashed = (code & 0xF0000000) == 0xC0000000;
		std::printf("INFO cleanup race child exit code 0x%08lX%s\n", (unsigned long)code,
		            crashed ? "  <-- Cleanup() faulted with a pose update in flight" : "");
		PERF_CHECK(code == 0);
	}

	void TestLateHookInstallDuringTeardown()
	{
		const DWORD code = RunChild("--late-hook-child", 60000);
		std::printf("INFO late hook install child exit code 0x%08lX%s\n", (unsigned long)code,
		            code == 0xC00000FD ? "  <-- a hook created during teardown recursed into itself" : "");
		PERF_CHECK(code == 0);
	}

	bool WriteFileOfSize(const std::string& path, size_t bytes)
	{
		FILE* f = std::fopen(path.c_str(), "wb");
		if (!f) return false;
		const std::vector<char> block(64 * 1024, 'x');
		for (size_t written = 0; written < bytes; written += block.size())
			std::fwrite(block.data(), 1, std::min(block.size(), bytes - written), f);
		std::fclose(f);
		return true;
	}

	int64_t FileSize(const std::string& path)
	{
		WIN32_FILE_ATTRIBUTE_DATA info;
		if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &info)) return -1;
		return ((int64_t)info.nFileSizeHigh << 32) | info.nFileSizeLow;
	}

	void OpenAndCloseDriverLog()
	{
		OpenLogFile();
		if (LogFile && LogFile != stderr) std::fclose(LogFile);
	}

	void TestLogRotation()
	{
		char tempDir[MAX_PATH];
		char previousDir[MAX_PATH];
		GetTempPathA(MAX_PATH, tempDir);
		GetCurrentDirectoryA(MAX_PATH, previousDir);
		const std::string dir = std::string(tempDir) + UniqueName("logrotation");
		PERF_CHECK(CreateDirectoryA(dir.c_str(), nullptr) != 0);
		PERF_CHECK(SetCurrentDirectoryA(dir.c_str()) != 0);
		const std::string logPath = dir + "\\space_calibrator_driver.log";
		const std::string oldPath = dir + "\\space_calibrator_driver.old.log";
		const int64_t cap = 8ll * 1024 * 1024;
		FILE* const savedLog = LogFile;

		PERF_CHECK(WriteFileOfSize(logPath, (size_t)cap));
		OpenAndCloseDriverLog();
		PERF_CHECK(FileSize(logPath) == cap);
		PERF_CHECK(FileSize(oldPath) == -1);

		PERF_CHECK(WriteFileOfSize(oldPath, 100));
		PERF_CHECK(WriteFileOfSize(logPath, (size_t)cap + 1));
		OpenAndCloseDriverLog();
		PERF_CHECK(FileSize(oldPath) == cap + 1);
		PERF_CHECK(FileSize(logPath) == 0);

		LogFile = savedLog;
		SetCurrentDirectoryA(previousDir);
		DeleteFileA(logPath.c_str());
		DeleteFileA(oldPath.c_str());
		RemoveDirectoryA(dir.c_str());
	}

	void TestGetGenericInterfaceAllocs()
	{
		PERF_CHECK(InitContext() == vr::VRInitError_None);
		InjectHooks(&g_provider, g_contextBase);
		PERF_CHECK(perf::InstallHeapMeter());
		g_contextBase->GetGenericInterface(vr::IVRServerDriverHost_Version, nullptr);
		const auto heap = perf::MeasureHeap([&] {
			for (int i = 0; i < 1000; ++i)
				g_contextBase->GetGenericInterface(vr::IVRServerDriverHost_Version, nullptr);
		});
		perf::Report("GetGenericInterface detour: heap allocations per 1000 calls", (double)heap.allocations, "allocs");
		const double ns = 1e9 * perf::BestSecondsPerOp(
		                            5, 20000, [&](int) { g_contextBase->GetGenericInterface(vr::IVRServerDriverHost_Version, nullptr); });
		perf::Report("GetGenericInterface detour (ns/call)", ns, "ns");
		perf::RemoveHeapMeter();
		g_provider.Cleanup();
	}

}

int main(int argc, char** argv)
{
	const std::string logPath = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "\\spacecal_driver_race_tests_" +
	                            std::to_string(GetCurrentProcessId()) + ".log";
	FILE* log = std::fopen(logPath.c_str(), "w");
	LogFile = log ? log : stdout;

	int rc = 0;
	if (argc > 1 && std::strcmp(argv[1], "--cleanup-race-child") == 0) {
		rc = CleanupRaceChild();
	}
	else if (argc > 1 && std::strcmp(argv[1], "--late-hook-child") == 0) {
		rc = LateHookInstallChild();
	}
	else {
		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
		TestRingFirstLap();
		TestRingMultiWriter();
		TestTransformRace();
		TestMailboxPrototype();
		TestGetGenericInterfaceAllocs();
		TestLogRotation();
		TestLateHookInstallDuringTeardown();
		TestCleanupRace();
		rc = perf::Finish("driver_race_tests");
	}

	LogFile = stdout;
	if (log) std::fclose(log);
	DeleteFileA(logPath.c_str());
	return rc;
}
