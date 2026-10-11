#include "perf_harness.h"
#include "driver_test_support.h"

#include "Hooking.h"
#include "Logging.h"
#include "Protocol.h"
#include "ServerTrackedDeviceProvider.h"
#include "SkeletalHook.h"
#include "SkeletalSmoothingMath.h"
#include "StickHook.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace driver_access {

	struct ProviderServer
	{
		using type = IPCServer ServerTrackedDeviceProvider::*;
		friend type Get(ProviderServer);
	};
	template struct Grant<ProviderServer, &ServerTrackedDeviceProvider::server>;

	struct ServerHandleRequest
	{
		using type = void (IPCServer::*)(const protocol::Request&, protocol::Response&);
		friend type Get(ServerHandleRequest);
	};
	template struct Grant<ServerHandleRequest, &IPCServer::HandleRequest>;

	void ClearDebugOffset(ServerTrackedDeviceProvider& provider);

	template <auto SettingsMember, auto LockMember> struct DebugOffsetGrant
	{
		friend void ClearDebugOffset(ServerTrackedDeviceProvider& provider)
		{
			auto& settings = provider.*SettingsMember;
			for (double& v : settings.debugTranslation)
				v = 0.0;
			settings.debugRotation[0] = 1.0;
			settings.debugRotation[1] = 0.0;
			settings.debugRotation[2] = 0.0;
			settings.debugRotation[3] = 0.0;
			(provider.*LockMember).Write(settings);
		}
	};
	template struct DebugOffsetGrant<&ServerTrackedDeviceProvider::ipcSettings, &ServerTrackedDeviceProvider::settingsLock>;

}

namespace {

	constexpr vr::PropertyContainerHandle_t kLeftController = 11;
	constexpr vr::PropertyContainerHandle_t kRightController = 12;
	constexpr uint32_t kDevices = 8;

	class FakeDriverInput : public vr::IVRDriverInput
	{
	public:
		vr::VRInputComponentHandle_t nextHandle = 100;
		volatile float lastScalar = 0.0f;
		volatile uint32_t skeletonUpdates = 0;
		volatile uint32_t booleanUpdates = 0;

		__declspec(noinline) vr::EVRInputError CreateBooleanComponent(vr::PropertyContainerHandle_t, const char*,
		                                                              vr::VRInputComponentHandle_t* pHandle) override
		{
			*pHandle = nextHandle++;
			return vr::VRInputError_None;
		}
		__declspec(noinline) vr::EVRInputError UpdateBooleanComponent(vr::VRInputComponentHandle_t, bool, double) override
		{
			booleanUpdates = booleanUpdates + 1;
			return vr::VRInputError_None;
		}
		__declspec(noinline) vr::EVRInputError CreateScalarComponent(vr::PropertyContainerHandle_t, const char*,
		                                                             vr::VRInputComponentHandle_t* pHandle, vr::EVRScalarType,
		                                                             vr::EVRScalarUnits) override
		{
			*pHandle = nextHandle;
			nextHandle = nextHandle + 1;
			return vr::VRInputError_None;
		}
		__declspec(noinline) vr::EVRInputError UpdateScalarComponent(vr::VRInputComponentHandle_t, float value, double) override
		{
			lastScalar = value;
			return vr::VRInputError_None;
		}
		__declspec(noinline) vr::EVRInputError CreateHapticComponent(vr::PropertyContainerHandle_t, const char*,
		                                                             vr::VRInputComponentHandle_t* pHandle) override
		{
			*pHandle = nextHandle + 1000;
			return vr::VRInputError_None;
		}
		__declspec(noinline) vr::EVRInputError CreateSkeletonComponent(vr::PropertyContainerHandle_t, const char*, const char*, const char*,
		                                                               vr::EVRSkeletalTrackingLevel, const vr::VRBoneTransform_t*, uint32_t,
		                                                               vr::VRInputComponentHandle_t* pHandle) override
		{
			*pHandle = nextHandle + 2000;
			nextHandle = nextHandle + 1;
			return vr::VRInputError_None;
		}
		__declspec(noinline) vr::EVRInputError UpdateSkeletonComponent(vr::VRInputComponentHandle_t, vr::EVRSkeletalMotionRange,
		                                                               const vr::VRBoneTransform_t* bones, uint32_t) override
		{
			skeletonUpdates = skeletonUpdates + (bones ? 1 : 2);
			return vr::VRInputError_None;
		}
	};

	class FakeProperties : public vr::IVRProperties
	{
	public:
		vr::ETrackedPropertyError ReadPropertyBatch(vr::PropertyContainerHandle_t container, vr::PropertyRead_t* batch,
		                                            uint32_t count) override
		{
			for (uint32_t i = 0; i < count; ++i) {
				vr::PropertyRead_t& read = batch[i];
				int32_t role = container == kLeftController    ? vr::TrackedControllerRole_LeftHand
				               : container == kRightController ? vr::TrackedControllerRole_RightHand
				                                               : -1;
				if (read.prop != vr::Prop_ControllerRoleHint_Int32 || role < 0 || read.unBufferSize < sizeof(int32_t)) {
					read.eError = vr::TrackedProp_UnknownProperty;
					read.unTag = vr::k_unInvalidPropertyTag;
					read.unRequiredBufferSize = 0;
					continue;
				}
				std::memcpy(read.pvBuffer, &role, sizeof(int32_t));
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
	ServerTrackedDeviceProvider g_provider;
	FILE* g_log = nullptr;

	vr::EVRInitError InitContext()
	{
		VR_INIT_SERVER_DRIVER_CONTEXT(&g_context);
		return vr::VRInitError_None;
	}

	long LogBytes()
	{
		std::fflush(g_log);
		return std::ftell(g_log);
	}

	vr::HmdQuaternion_t YawQuat(double radians)
	{
		return {std::cos(radians / 2), 0.0, std::sin(radians / 2), 0.0};
	}

	std::vector<vr::DriverPose_t> MakePoses(size_t count)
	{
		std::vector<vr::DriverPose_t> poses(count);
		for (size_t i = 0; i < count; ++i) {
			vr::DriverPose_t& p = poses[i];
			std::memset(&p, 0, sizeof p);
			const double t = (double)i / 1000.0;
			p.poseIsValid = true;
			p.deviceIsConnected = true;
			p.result = vr::TrackingResult_Running_OK;
			p.qWorldFromDriverRotation = {1, 0, 0, 0};
			p.qDriverFromHeadRotation = {1, 0, 0, 0};
			p.qRotation = YawQuat(0.3 * std::sin(t));
			p.vecPosition[0] = 0.2 * std::sin(t);
			p.vecPosition[1] = 1.1 + 0.01 * std::cos(3 * t);
			p.vecPosition[2] = 0.1 * std::cos(t);
			p.vecVelocity[0] = 0.2 * std::cos(t);
			p.poseTimeOffset = -0.016;
		}
		return poses;
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

	protocol::SetDeviceTransform CalibratedTransform(uint32_t id, bool smooth)
	{
		protocol::SetDeviceTransform t(id, true, vr::HmdVector3d_t{0.12, -0.03, 0.4}, YawQuat(0.7), 1.0);
		t.smooth = smooth;
		return t;
	}

	protocol::FingerSmoothingConfig FingerConfig(uint8_t strength)
	{
		protocol::FingerSmoothingConfig c{};
		c.strength = strength;
		c.fingerMask = protocol::kAllFingersMask;
		return c;
	}

	double PosePathNs(const std::vector<vr::DriverPose_t>& source, int ops)
	{
		return 1e9 * perf::BestSecondsPerOp(7, ops, [&](int i) {
			       vr::DriverPose_t pose = source[i % source.size()];
			       g_provider.HandleDevicePoseUpdated(1 + (uint32_t)i % kDevices, pose);
		       });
	}

	void TestPosePath()
	{
		const auto poses = MakePoses(4096);
		g_provider.HandleSetAlignmentSpeedParams(OverlayDefaultAlignment());

		for (uint32_t id = 1; id <= kDevices; ++id)
			g_provider.SetDeviceTransform(protocol::SetDeviceTransform(id, false));
		protocol::SmoothingParams off{0};
		g_provider.HandleSetSmoothingParams(off);
		PosePathNs(poses, 2000);
		perf::Budget("driver pose: passthrough (ns/pose)", PosePathNs(poses, 20000), 300.0, "ns");

		for (uint32_t id = 1; id <= kDevices; ++id)
			g_provider.SetDeviceTransform(CalibratedTransform(id, false));
		PosePathNs(poses, 2000);
		perf::Budget("driver pose: calibrated (ns/pose)", PosePathNs(poses, 20000), 1500.0, "ns");

		for (uint32_t id = 1; id <= kDevices; ++id)
			g_provider.SetDeviceTransform(CalibratedTransform(id, true));
		protocol::SmoothingParams half{50};
		g_provider.HandleSetSmoothingParams(half);
		PosePathNs(poses, 2000);
		const double smoothedNs = PosePathNs(poses, 20000);
		perf::Budget("driver pose: calibrated + smoothed (ns/pose)", smoothedNs, 2000.0, "ns");
		perf::Report("driver pose: core share at 8 devices x 1000 Hz (%)", smoothedNs * 8000.0 / 1e9 * 100.0, "%");

		const auto heap = perf::MeasureHeap([&] {
			for (int i = 0; i < 100000; ++i) {
				vr::DriverPose_t pose = poses[i % poses.size()];
				g_provider.HandleDevicePoseUpdated(1 + (uint32_t)i % kDevices, pose);
			}
		});
		perf::Budget("driver pose: heap allocations per 100k poses", (double)heap.allocations, 0.0, "allocs");
	}

	void TestPoseShmem()
	{
		const std::string name = "SpaceCalibratorPerfTest" + std::to_string(GetCurrentProcessId());
		protocol::DriverPoseShmem writer;
		protocol::DriverPoseShmem reader;
		PERF_CHECK(writer.Create(name.c_str()));
		reader.Open(name.c_str());

		const auto poses = MakePoses(1024);
		const double writeNs = 1e9 * perf::BestSecondsPerOp(7, 20000, [&](int i) { writer.SetPose(1 + i % kDevices, poses[i % 1024]); });
		perf::Budget("pose shmem: write (ns/pose)", writeNs, 1000.0, "ns");

		uint64_t seen = 0;
		reader.ReadNewPoses([&](const protocol::DriverPoseShmem::AugmentedPose&) { ++seen; });
		const double readNs =
		    1e9 * perf::BestSecondsPerOp(7, 50, [&](int i) {
			    for (int k = 0; k < 500; ++k)
				    writer.SetPose(1 + k % kDevices, poses[k]);
			    reader.ReadNewPoses([&](const protocol::DriverPoseShmem::AugmentedPose& p) { seen += (uint64_t)p.deviceId; });
		    });
		perf::Report("pose shmem: write 500 + read 500 (us per overlay tick)", readNs / 1000.0, "us");

		const auto heap = perf::MeasureHeap([&] {
			for (int i = 0; i < 10000; ++i)
				writer.SetPose(1 + i % kDevices, poses[i % 1024]);
			for (int i = 0; i < 20; ++i)
				reader.ReadNewPoses([&](const protocol::DriverPoseShmem::AugmentedPose& p) { seen += (uint64_t)p.deviceId; });
		});
		perf::Budget("pose shmem: heap allocations per 10k writes + 20 reads", (double)heap.allocations, 0.0, "allocs");

		const double sharedMb = (double)(64 * 1024) * sizeof(protocol::DriverPoseShmem::AugmentedPose) / (1024.0 * 1024.0);
		perf::Budget("pose shmem: mapped ring size (MiB)", sharedMb, 21.0, "MiB");
		PERF_CHECK(seen > 0);
	}

	std::vector<vr::VRBoneTransform_t> MakeHandFrame(int frame)
	{
		std::vector<vr::VRBoneTransform_t> bones(spacecal::skeletal::kFingerBoneCount);
		for (uint32_t b = 0; b < bones.size(); ++b) {
			const double a = 0.01 * b + 0.002 * frame;
			bones[b].position = {{(float)(0.01 * b), (float)(0.02 * std::sin(a)), (float)(0.01 * std::cos(a)), 1.0f}};
			bones[b].orientation = {(float)std::cos(a / 2), (float)std::sin(a / 2), 0.0f, 0.0f};
		}
		return bones;
	}

	void TestSkeletalHook()
	{
		spacecal::skeletal_hook::Init(&g_provider);
		spacecal::skeletal_hook::TryInstallPublicHooks(g_input);
		vr::VRInputComponentHandle_t left = 0;
		g_input->CreateSkeletonComponent(kLeftController, "/input/skeleton/left", "/skeleton/hand/left", "/pose/raw",
		                                 vr::VRSkeletalTracking_Full, nullptr, 0, &left);

		std::vector<std::vector<vr::VRBoneTransform_t>> frames;
		frames.reserve(64);
		for (int f = 0; f < 64; ++f)
			frames.push_back(MakeHandFrame(f));

		auto update = [&](int i) {
			g_input->UpdateSkeletonComponent(left, vr::VRSkeletalMotionRange_WithController, frames[i % frames.size()].data(),
			                                 spacecal::skeletal::kFingerBoneCount);
		};

		g_provider.SetFingerSmoothingConfig(FingerConfig(0));
		for (int i = 0; i < 100; ++i)
			update(i);
		perf::Budget("finger hook: off (ns/hand frame)", 1e9 * perf::BestSecondsPerOp(7, 20000, update), 1000.0, "ns");

		g_provider.SetFingerSmoothingConfig(FingerConfig(60));
		for (int i = 0; i < 100; ++i)
			update(i);
		perf::Budget("finger hook: smoothing (ns/hand frame)", 1e9 * perf::BestSecondsPerOp(7, 20000, update), 5000.0, "ns");

		const auto heap = perf::MeasureHeap([&] {
			for (int i = 0; i < 20000; ++i)
				update(i);
		});
		perf::Budget("finger hook: heap allocations per 20k frames", (double)heap.allocations, 0.0, "allocs");
		PERF_CHECK(g_fakeInput.skeletonUpdates > 0);
	}

	void TestStickHook()
	{
		spacecal::stick_hook::Init();
		spacecal::stick_hook::TryInstallPublicHooks(g_input);
		vr::VRInputComponentHandle_t axes[4] = {};
		g_input->CreateScalarComponent(kLeftController, "/input/joystick/x", &axes[0], vr::VRScalarType_Absolute,
		                               vr::VRScalarUnits_NormalizedTwoSided);
		g_input->CreateScalarComponent(kLeftController, "/input/joystick/y", &axes[1], vr::VRScalarType_Absolute,
		                               vr::VRScalarUnits_NormalizedTwoSided);
		g_input->CreateScalarComponent(kRightController, "/input/thumbstick/x", &axes[2], vr::VRScalarType_Absolute,
		                               vr::VRScalarUnits_NormalizedTwoSided);
		g_input->CreateScalarComponent(kRightController, "/input/thumbstick/y", &axes[3], vr::VRScalarType_Absolute,
		                               vr::VRScalarUnits_NormalizedTwoSided);

		auto update = [&](int i) {
			g_input->UpdateScalarComponent(axes[i & 3], (float)((i >> 2) % 200 - 100) / 100.0f, 0.0);
		};

		protocol::StickSmoothingConfig off{};
		spacecal::stick_hook::SetConfig(off);
		for (int i = 0; i < 100; ++i)
			update(i);
		perf::Budget("stick hook: off (ns/axis update)", 1e9 * perf::BestSecondsPerOp(7, 50000, update), 1000.0, "ns");

		protocol::StickSmoothingConfig on{{{2400, 900, 100, 0}, {2400, 900, 100, 0}}};
		spacecal::stick_hook::SetConfig(on);
		for (int i = 0; i < 100; ++i)
			update(i);
		perf::Budget("stick hook: easing (ns/axis update)", 1e9 * perf::BestSecondsPerOp(7, 50000, update), 1000.0, "ns");
		perf::Budget("stick hook: RunFrame pump, 4 axes (ns/frame)",
		             1e9 * perf::BestSecondsPerOp(7, 50000, [](int) { spacecal::stick_hook::Pump(); }), 1000.0, "ns");

		const auto heap = perf::MeasureHeap([&] {
			for (int i = 0; i < 50000; ++i) {
				update(i);
				if ((i & 7) == 0) spacecal::stick_hook::Pump();
			}
		});
		perf::Budget("stick hook: heap allocations per 50k updates", (double)heap.allocations, 0.0, "allocs");
	}

	void ApplyOverlayProfileOnce()
	{
		g_provider.HandleSetAlignmentSpeedParams(OverlayDefaultAlignment());
		protocol::SmoothingParams smoothing{40};
		g_provider.HandleSetSmoothingParams(smoothing);
		g_provider.SetFingerSmoothingConfig(FingerConfig(30));
		protocol::StickSmoothingConfig sticks{{{800, 300, 100, 0}, {}}};
		spacecal::stick_hook::SetConfig(sticks);
		for (uint32_t id = 1; id <= kDevices; ++id)
			g_provider.SetDeviceTransform(CalibratedTransform(id, true));
	}

	void TestSteadyStateProfileReapply()
	{
		ApplyOverlayProfileOnce();
		const long before = LogBytes();
		const auto heap = perf::MeasureHeap([&] {
			for (int second = 0; second < 600; ++second)
				ApplyOverlayProfileOnce();
		});
		const long grown = LogBytes() - before;
		perf::Budget("profile re-apply x600: driver log growth (bytes)", (double)grown, 0.0, "bytes");
		perf::Budget("profile re-apply x600: heap allocations", (double)heap.allocations, 0.0, "allocs");
	}

	void Ipc(const protocol::Request& request, protocol::Response& response)
	{
		IPCServer& server = g_provider.*Get(driver_access::ProviderServer{});
		(server.*Get(driver_access::ServerHandleRequest{}))(request, response);
	}

	enum class HammerMode
	{
		Identical,
		Changing
	};

	class IpcHammer
	{
	public:
		explicit IpcHammer(HammerMode mode) : mode(mode), started(perf::QpcSeconds())
		{
			worker = std::thread([this] { Run(); });
		}

		~IpcHammer() { Stop(); }

		void Stop()
		{
			if (!worker.joinable()) return;
			stop.store(true, std::memory_order_relaxed);
			worker.join();
			elapsed = perf::QpcSeconds() - started;
		}

		double RequestsPerSecond() const { return elapsed > 0 ? (double)count.load() / elapsed : 0.0; }

	private:
		void Run()
		{
			protocol::Response response;
			uint64_t n = 0;
			while (!stop.load(std::memory_order_relaxed)) {
				const bool flip = mode == HammerMode::Changing && ((n >> 12) & 1) != 0;
				switch (n % 6) {
					case 0:
					case 1: {
						protocol::Request r(protocol::RequestSetDeviceTransform);
						r.setDeviceTransform = CalibratedTransform(1 + (uint32_t)((n / 6) % kDevices), true);
						if (mode == HammerMode::Changing) {
							r.setDeviceTransform.translation.v[0] += (n & 2) ? 0.001 : 0.0;
							r.setDeviceTransform.lerp = true;
						}
						Ipc(r, response);
						break;
					}
					case 2: {
						protocol::Request r(protocol::RequestGetSmoothingStats);
						r.getSmoothingStats.openVRID = 1 + (uint32_t)(n % kDevices);
						Ipc(r, response);
						break;
					}
					case 3: {
						protocol::Request r(protocol::RequestSetSmoothingParams);
						r.setSmoothingParams.strength = flip ? 51 : 50;
						Ipc(r, response);
						break;
					}
					case 4: {
						protocol::Request r(protocol::RequestSetFingerSmoothing);
						r.setFingerSmoothing = FingerConfig(flip ? 61 : 60);
						Ipc(r, response);
						break;
					}
					default: {
						protocol::Request r(protocol::RequestSetStickSmoothing);
						r.setStickSmoothing = {{{2400, 900, 100, 0}, {2400, (uint16_t)(flip ? 901 : 900), 100, 0}}};
						Ipc(r, response);
						break;
					}
				}
				++n;
				count.store(n, std::memory_order_relaxed);
			}
		}

		HammerMode mode;
		double started;
		double elapsed = 0;
		std::atomic<bool> stop{false};
		std::atomic<uint64_t> count{0};
		std::thread worker;
	};

	void ApplyTailBaseline()
	{
		g_provider.HandleSetAlignmentSpeedParams(OverlayDefaultAlignment());
		protocol::SmoothingParams half{50};
		g_provider.HandleSetSmoothingParams(half);
		g_provider.SetFingerSmoothingConfig(FingerConfig(60));
		protocol::StickSmoothingConfig sticks{{{2400, 900, 100, 0}, {2400, 900, 100, 0}}};
		spacecal::stick_hook::SetConfig(sticks);
		for (uint32_t id = 1; id <= kDevices; ++id)
			g_provider.SetDeviceTransform(CalibratedTransform(id, true));
	}

	uint32_t Rng(uint32_t& s)
	{
		s ^= s << 13;
		s ^= s >> 17;
		s ^= s << 5;
		return s;
	}

	void TestIpcFuzz()
	{
		ApplyTailBaseline();
		protocol::Response response;

		protocol::Request oob(protocol::RequestSetDeviceTransform);
		oob.setDeviceTransform = CalibratedTransform(0xFFFFFFFFu, true);
		Ipc(oob, response);
		oob.setDeviceTransform.openVRID = vr::k_unMaxTrackedDeviceCount;
		Ipc(oob, response);

		protocol::Request nan(protocol::RequestSetDeviceTransform);
		nan.setDeviceTransform = CalibratedTransform(3, true);
		const double inf = std::numeric_limits<double>::infinity();
		const double qnan = std::numeric_limits<double>::quiet_NaN();
		nan.setDeviceTransform.translation = {qnan, inf, -inf};
		nan.setDeviceTransform.rotation = {qnan, qnan, qnan, qnan};
		nan.setDeviceTransform.scale = qnan;
		Ipc(nan, response);
		{
			vr::DriverPose_t pose = MakePoses(1)[0];
			g_provider.HandleDevicePoseUpdated(3, pose);
			PERF_CHECK(std::isfinite(pose.vecWorldFromDriverTranslation[0]) && std::isfinite(pose.vecWorldFromDriverTranslation[1]) &&
			           std::isfinite(pose.vecWorldFromDriverTranslation[2]) && std::isfinite(pose.qWorldFromDriverRotation.w));
		}
		g_provider.SetDeviceTransform(CalibratedTransform(3, true));

		protocol::Request stats(protocol::RequestGetSmoothingStats);
		stats.getSmoothingStats.openVRID = 0xFFFFFFFFu;
		response = protocol::Response(protocol::ResponseInvalid);
		Ipc(stats, response);
		PERF_CHECK(response.type == protocol::ResponseSmoothingStats);
		PERF_CHECK(response.smoothingStats.active == false);

		protocol::Request badFinger(protocol::RequestSetFingerSmoothing);
		badFinger.setFingerSmoothing.strength = 255;
		for (auto& v : badFinger.setFingerSmoothing.perFinger)
			v = 255;
		badFinger.setFingerSmoothing.fingerMask = 0xFFFF;
		Ipc(badFinger, response);

		protocol::Request badStick(protocol::RequestSetStickSmoothing);
		badStick.setStickSmoothing = {{{0xFFFF, 0xFFFF, 255, 0, 0xFFFF}, {0, 0, 0, 0, 0}}};
		Ipc(badStick, response);

		uint32_t seed = 0x9E3779B9u;
		const int iterations = 300000;
		for (int i = 0; i < iterations; ++i) {
			protocol::Request r;
			uint8_t* bytes = reinterpret_cast<uint8_t*>(&r);
			for (size_t b = 0; b < sizeof r; ++b)
				bytes[b] = (uint8_t)Rng(seed);
			r.type = (protocol::RequestType)(Rng(seed) % 12);
			Ipc(r, response);
		}
		g_provider.SetFingerSmoothingConfig(FingerConfig(60));
		ApplyTailBaseline();
		driver_access::ClearDebugOffset(g_provider);
		perf::Report("IPC fuzz: random/oob/NaN requests survived", (double)(iterations + 7), "requests");

		const uint32_t probeId = kDevices + 1;
		const protocol::SetDeviceTransform probe = CalibratedTransform(probeId, false);
		g_provider.SetDeviceTransform(probe);
		const vr::DriverPose_t input = MakePoses(1)[0];
		vr::DriverPose_t pose = input;
		g_provider.HandleDevicePoseUpdated(probeId, pose);
		bool unshifted = true;
		bool calibrated = true;
		for (int i = 0; i < 3; ++i) {
			unshifted = unshifted && pose.vecPosition[i] == input.vecPosition[i];
			calibrated = calibrated && std::fabs(pose.vecWorldFromDriverTranslation[i] - probe.translation.v[i]) < 1e-9;
		}
		calibrated = calibrated && std::fabs(pose.qWorldFromDriverRotation.w - probe.rotation.w) < 1e-9 &&
		             std::fabs(pose.qWorldFromDriverRotation.y - probe.rotation.y) < 1e-9;
		PERF_CHECK(unshifted);
		PERF_CHECK(calibrated);
	}

	void TestPoseTail()
	{
		ApplyTailBaseline();
		const auto poses = MakePoses(4096);
		std::vector<uint64_t> samples(200000);
		auto op = [&](int i) {
			vr::DriverPose_t pose = poses[i % poses.size()];
			g_provider.HandleDevicePoseUpdated(1 + (uint32_t)i % kDevices, pose);
		};
		driverlat::Measure(samples, op);
		driverlat::Print("pose path: timer overhead only", driverlat::Measure(samples, [](int) {}));
		const auto alone = driverlat::Measure(samples, op);
		driverlat::Print("pose path: calibrated+smoothed, alone", alone);
		perf::Budget("pose path tail: p50 alone (ns)", alone.p50Ns, 10000.0, "ns");

		for (HammerMode mode : {HammerMode::Identical, HammerMode::Changing}) {
			IpcHammer hammer(mode);
			const auto contended = driverlat::Measure(samples, op);
			const auto heap = perf::MeasureHeap([&] {
				for (int i = 0; i < 20000; ++i)
					op(i);
			});
			hammer.Stop();
			const bool same = mode == HammerMode::Identical;
			driverlat::Print(same ? "pose path: + IPC hammer (identical requests)" : "pose path: + IPC hammer (changing requests)",
			                 contended);
			perf::Report(same ? "pose path: IPC hammer rate, identical (req/s)" : "pose path: IPC hammer rate, changing (req/s)",
			             hammer.RequestsPerSecond(), "req/s");
			perf::Budget(same ? "pose path tail: p50 + identical hammer (ns)" : "pose path tail: p50 + changing hammer (ns)",
			             contended.p50Ns, 10000.0, "ns");
			perf::Budget("pose path: heap allocations per 20k poses under IPC load", (double)heap.allocations, 0.0, "allocs");
		}
		ApplyTailBaseline();
	}

	void TestSkeletalTail()
	{
		ApplyTailBaseline();
		vr::VRInputComponentHandle_t left = 0;
		vr::VRInputComponentHandle_t right = 0;
		g_input->CreateSkeletonComponent(kLeftController, "/input/skeleton/left", "/skeleton/hand/left", "/pose/raw",
		                                 vr::VRSkeletalTracking_Full, nullptr, 0, &left);
		g_input->CreateSkeletonComponent(kRightController, "/input/skeleton/right", "/skeleton/hand/right", "/pose/raw",
		                                 vr::VRSkeletalTracking_Full, nullptr, 0, &right);
		std::vector<std::vector<vr::VRBoneTransform_t>> frames;
		for (int f = 0; f < 64; ++f)
			frames.push_back(MakeHandFrame(f));
		auto update = [&](vr::VRInputComponentHandle_t handle, int i) {
			g_input->UpdateSkeletonComponent(handle, vr::VRSkeletalMotionRange_WithController, frames[i % frames.size()].data(),
			                                 spacecal::skeletal::kFingerBoneCount);
		};

		std::vector<uint64_t> samples(100000);
		driverlat::Measure(samples, [&](int i) { update(left, i); });
		driverlat::Print("finger hook: smoothing, one hand alone", driverlat::Measure(samples, [&](int i) { update(left, i); }));

		{
			std::atomic<bool> stop{false};
			std::thread other([&] {
				int i = 0;
				while (!stop.load(std::memory_order_relaxed))
					update(right, i++);
			});
			const auto both = driverlat::Measure(samples, [&](int i) { update(left, i); });
			stop.store(true);
			other.join();
			driverlat::Print("finger hook: left while right hand runs on 2nd thread", both);
			perf::Budget("finger hook tail: p50 with both hands (ns)", both.p50Ns, 20000.0, "ns");
		}

		for (HammerMode mode : {HammerMode::Identical, HammerMode::Changing}) {
			IpcHammer hammer(mode);
			const auto contended = driverlat::Measure(samples, [&](int i) { update(left, i); });
			hammer.Stop();
			driverlat::Print(mode == HammerMode::Identical ? "finger hook: + IPC hammer (identical requests)"
			                                               : "finger hook: + IPC hammer (changing requests)",
			                 contended);
		}
		ApplyTailBaseline();
	}

	void TestStickTail()
	{
		ApplyTailBaseline();
		vr::VRInputComponentHandle_t axis = 0;
		g_input->CreateScalarComponent(kLeftController, "/input/joystick/x", &axis, vr::VRScalarType_Absolute,
		                               vr::VRScalarUnits_NormalizedTwoSided);
		auto update = [&](int i) {
			g_input->UpdateScalarComponent(axis, (float)((i >> 2) % 200 - 100) / 100.0f, 0.0);
		};

		std::vector<uint64_t> samples(200000);
		driverlat::Measure(samples, update);
		driverlat::Print("stick hook: easing, alone", driverlat::Measure(samples, update));

		std::atomic<bool> stop{false};
		std::thread frameLoop([&] {
			double next = perf::QpcSeconds();
			while (!stop.load(std::memory_order_relaxed)) {
				spacecal::stick_hook::Pump();
				next += 0.001;
				while (perf::QpcSeconds() < next && !stop.load(std::memory_order_relaxed))
					std::this_thread::yield();
			}
		});
		driverlat::Print("stick hook: + RunFrame pump at 1 kHz", driverlat::Measure(samples, update));
		for (HammerMode mode : {HammerMode::Identical, HammerMode::Changing}) {
			IpcHammer hammer(mode);
			const auto contended = driverlat::Measure(samples, update);
			hammer.Stop();
			driverlat::Print(mode == HammerMode::Identical ? "stick hook: + pump + IPC hammer (identical)"
			                                               : "stick hook: + pump + IPC hammer (changing)",
			                 contended);
			perf::Budget("stick hook tail: p50 under pump + IPC load (ns)", contended.p50Ns, 10000.0, "ns");
		}
		stop.store(true);
		frameLoop.join();
		ApplyTailBaseline();
	}

	void TestLogCost()
	{
		std::vector<uint64_t> samples(2000);
		const auto single = driverlat::Measure(samples, [](int i) { LOG("[perf] log cost probe %d value=%.4f", i, i * 0.5); });
		driverlat::Print("LOG(): one line + fflush to a file", single);

		std::vector<uint64_t> bursts(300);
		const auto burst = driverlat::Measure(bursts, [](int i) {
			for (int k = 0; k < 8; ++k)
				LOG("[perf] stats burst %d line %d value=%.4f", i, k, k * 0.25);
		});
		driverlat::Print("LOG(): 8-line skeletal stats burst", burst);

		const std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "\\spacecal_perf_driver_buffered_" +
		                         std::to_string(GetCurrentProcessId()) + ".log";
		FILE* buffered = std::fopen(path.c_str(), "w");
		if (buffered) {
			std::setvbuf(buffered, nullptr, _IOFBF, 64 * 1024);
			const auto unflushed = driverlat::Measure(bursts, [&](int i) {
				for (int k = 0; k < 8; ++k) {
					tm logNow = TimeForLog();
					std::fprintf(buffered, "[%02d:%02d:%02d] [perf] stats burst %d line %d value=%.4f\n", logNow.tm_hour, logNow.tm_min,
					             logNow.tm_sec, i, k, k * 0.25);
				}
			});
			std::fclose(buffered);
			DeleteFileA(path.c_str());
			driverlat::Print("prototype: 8-line burst, buffered, no fflush", unflushed);
		}
	}

	void TestFootprint()
	{
		perf::Report("sizeof(ServerTrackedDeviceProvider) (KiB)", sizeof(ServerTrackedDeviceProvider) / 1024.0, "KiB");
		perf::Report("sizeof(DriverPoseShmem) (KiB)", sizeof(protocol::DriverPoseShmem) / 1024.0, "KiB");
		perf::Report("pose shmem ring mapping (MiB)",
		             (8.0 + 64.0 * 1024 * sizeof(protocol::DriverPoseShmem::AugmentedPose)) / (1024.0 * 1024.0), "MiB");
		perf::Report("sizeof(protocol::Request) (bytes)", (double)sizeof(protocol::Request), "bytes");
	}

}

int main()
{
	const std::string logPath = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "\\spacecal_perf_driver_tests.log";
	g_log = std::fopen(logPath.c_str(), "w+");
	LogFile = g_log ? g_log : stdout;
	if (!g_log) g_log = stdout;

	PERF_CHECK(InitContext() == vr::VRInitError_None);
	PERF_CHECK(perf::InstallHeapMeter());

	TestPosePath();
	TestPoseShmem();
	TestSkeletalHook();
	TestStickHook();
	TestSteadyStateProfileReapply();
	TestIpcFuzz();
	TestPoseTail();
	TestSkeletalTail();
	TestStickTail();
	TestLogCost();
	TestFootprint();

	spacecal::stick_hook::Shutdown();
	spacecal::skeletal_hook::Shutdown();
	perf::RemoveHeapMeter();
	IHook::DestroyAll();
	MH_Uninitialize();
	if (g_log != stdout) std::fclose(g_log);

	return perf::Finish("perf_driver_tests");
}
