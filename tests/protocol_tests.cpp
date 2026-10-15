#include <openvr.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "Protocol.h"
#include "protocol_layout_fill.h"

WireLayout OverlayWireLayout()
{
	return FillWireLayout();
}

namespace {

	int failures = 0;

#define CHECK(cond)                                                                                                                        \
	do {                                                                                                                                   \
		if (!(cond)) {                                                                                                                     \
			std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);                                                                     \
			++failures;                                                                                                                    \
		}                                                                                                                                  \
	} while (0)

	static_assert(protocol::Version != 10 || sizeof(protocol::Request) == 88, "Request changed size without a protocol::Version bump");
	static_assert(protocol::Version != 10 || sizeof(protocol::Response) == 40, "Response changed size without a protocol::Version bump");
	static_assert(protocol::Version != 10 || sizeof(protocol::SetDeviceTransform) == 80, "SetDeviceTransform changed size");
	static_assert(protocol::Version != 10 || sizeof(protocol::AlignmentSpeedParams) == 72, "AlignmentSpeedParams changed size");
	static_assert(protocol::Version != 10 || sizeof(protocol::FingerSmoothingConfig) == 14, "FingerSmoothingConfig changed size");
	static_assert(protocol::Version != 10 || sizeof(protocol::StickSmoothingConfig) == 16, "StickSmoothingConfig changed size");
	static_assert(protocol::Version != 10 || offsetof(protocol::StickRampConfig, lockedPct) == 5, "StickRampConfig.lockedPct moved");
	static_assert(protocol::Version != 10 || sizeof(protocol::SmoothingStats) == 32, "SmoothingStats changed size");
	static_assert(protocol::Version != 10 || sizeof(protocol::DriverPoseShmem::AugmentedPose) == 296, "AugmentedPose changed size");
	static_assert(protocol::Version != 10 || offsetof(protocol::Request, setDeviceTransform) == 8, "Request payload moved");
	static_assert(protocol::Version != 10 || protocol::RequestSetStickSmoothing == 8, "RequestType values are wire values");
	static_assert(protocol::Version != 10 || protocol::ResponseSmoothingStats == 3, "ResponseType values are wire values");
	static_assert(sizeof(protocol::RequestType) == 4 && sizeof(protocol::ResponseType) == 4, "wire enums are 32-bit");
	static_assert(protocol::kAllFingersMask == (1u << 10) - 1, "ten fingers");

	void TestOverlayAndDriverAgreeOnTheWire()
	{
		const WireLayout o = OverlayWireLayout();
		const WireLayout d = DriverWireLayout();
		const size_t numeric = offsetof(WireLayout, pipeName);
		const unsigned char* a = reinterpret_cast<const unsigned char*>(&o);
		const unsigned char* b = reinterpret_cast<const unsigned char*>(&d);
		size_t mismatches = 0;
		for (size_t i = 0; i < numeric; i += sizeof(size_t)) {
			if (std::memcmp(a + i, b + i, sizeof(size_t)) != 0) {
				++mismatches;
				std::printf("  layout word %zu differs between the overlay and driver headers\n", i / sizeof(size_t));
			}
		}
		CHECK(mismatches == 0);
		CHECK(std::strcmp(o.pipeName, d.pipeName) == 0);
		CHECK(std::strcmp(o.shmemName, d.shmemName) == 0);
		CHECK(o.poseSize == sizeof(vr::DriverPose_t));
		std::printf("  Request %zu B, Response %zu B, DriverPose_t %zu B, AugmentedPose %zu B, version %u\n", o.requestSize, o.responseSize,
		            o.poseSize, o.augmentedSize, o.version);
	}

	vr::DriverPose_t PoseFor(int id)
	{
		vr::DriverPose_t pose{};
		const double v = id;
		pose.poseTimeOffset = v;
		pose.qWorldFromDriverRotation = {v, v, v, v};
		pose.qDriverFromHeadRotation = {v, v, v, v};
		pose.qRotation = {v, v, v, v};
		for (int i = 0; i < 3; ++i) {
			pose.vecWorldFromDriverTranslation[i] = v;
			pose.vecDriverFromHeadTranslation[i] = v;
			pose.vecPosition[i] = v;
			pose.vecVelocity[i] = v;
			pose.vecAcceleration[i] = v;
			pose.vecAngularVelocity[i] = v;
			pose.vecAngularAcceleration[i] = v;
		}
		pose.result = vr::TrackingResult_Running_OK;
		pose.poseIsValid = true;
		pose.deviceIsConnected = true;
		return pose;
	}

	bool PoseIsWholly(const vr::DriverPose_t& pose, int id)
	{
		const vr::DriverPose_t expected = PoseFor(id);
		return std::memcmp(&pose, &expected, offsetof(vr::DriverPose_t, result)) == 0;
	}

	std::string UniqueName(const char* tag)
	{
		return std::string("SpaceCalibratorProtocolTest-") + tag + "-" + std::to_string(GetCurrentProcessId());
	}

	void TestReaderSeesTheNewestPose()
	{
		const std::string name = UniqueName("newest");
		protocol::DriverPoseShmem writer;
		CHECK(writer.Create(name.c_str()));
		auto reader = std::make_unique<protocol::DriverPoseShmem>();
		reader->Open(name.c_str());
		writer.SetPose(3, PoseFor(3));
		vr::DriverPose_t seen{};
		reader->GetPose(3, seen);
		const bool fresh = seen.poseIsValid && PoseIsWholly(seen, 3);
		CHECK(fresh);
		if (!fresh) std::printf("  the pose written last is not visible to the reader until another pose follows it\n");
	}

	void TestReaderSeesEveryPoseOnceInOrder()
	{
		const std::string name = UniqueName("order");
		protocol::DriverPoseShmem writer;
		CHECK(writer.Create(name.c_str()));
		auto reader = std::make_unique<protocol::DriverPoseShmem>();
		reader->Open(name.c_str());
		for (int id = 1; id <= 10; ++id) {
			writer.SetPose(id, PoseFor(id));
		}
		std::vector<int> ids;
		reader->ReadNewPoses([&](const protocol::DriverPoseShmem::AugmentedPose& p) {
			if (p.pose.poseIsValid) ids.push_back(p.deviceId);
		});
		std::vector<int> expected;
		for (int id = 1; id <= 10; ++id) {
			expected.push_back(id);
		}
		CHECK(ids == expected);
		std::printf("  reader saw %zu of 10 poses written\n", ids.size());
	}

	struct RawShmem
	{
		uint64_t index;
		protocol::DriverPoseShmem::AugmentedPose poses[64 * 1024];
	};

	void TestConcurrentWritersLoseNothing()
	{
		const std::string name = UniqueName("race");
		protocol::DriverPoseShmem writer;
		CHECK(writer.Create(name.c_str()));
		HANDLE mapping = OpenFileMappingA(FILE_MAP_READ, FALSE, name.c_str());
		CHECK(mapping != nullptr);
		if (!mapping) return;
		const RawShmem* raw = static_cast<const RawShmem*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(RawShmem)));
		CHECK(raw != nullptr);
		if (!raw) {
			CloseHandle(mapping);
			return;
		}
		const int perThread = 200000;
		auto run = [&](int id) {
			const vr::DriverPose_t pose = PoseFor(id);
			for (int i = 0; i < perThread; ++i) {
				writer.SetPose(id, pose);
			}
		};
		std::thread a(run, 1);
		std::thread b(run, 2);
		a.join();
		b.join();
		const uint64_t written = raw->index;
		size_t torn = 0;
		const size_t slots = written < 64 * 1024 ? (size_t)written : 64 * 1024;
		for (size_t i = 1; i <= slots; ++i) {
			const auto& slot = raw->poses[i % (64 * 1024)];
			if ((slot.deviceId != 1 && slot.deviceId != 2) || !PoseIsWholly(slot.pose, slot.deviceId)) ++torn;
		}
		std::printf("  2 threads x %d SetPose calls: index advanced %llu, %llu updates lost, %zu torn slots in the ring\n", perThread,
		            (unsigned long long)written, (unsigned long long)(2ull * perThread - written), torn);
		CHECK(written == 2ull * perThread);
		CHECK(torn == 0);
		UnmapViewOfFile(raw);
		CloseHandle(mapping);
	}

	void TestGetPoseRejectsOutOfRangeIndex()
	{
		const std::string name = UniqueName("range");
		protocol::DriverPoseShmem writer;
		CHECK(writer.Create(name.c_str()));
		auto reader = std::make_unique<protocol::DriverPoseShmem>();
		reader->Open(name.c_str());
		vr::DriverPose_t pose{};
		CHECK(reader->GetPose(0, pose));
		CHECK(reader->GetPose((int)vr::k_unMaxTrackedDeviceCount - 1, pose));
		volatile int below = -1;
		volatile int above = (int)vr::k_unMaxTrackedDeviceCount;
		const bool lowAccepted = reader->GetPose(below, pose);
		const bool highAccepted = reader->GetPose(above, pose);
		CHECK(!lowAccepted);
		CHECK(!highAccepted);
	}

	void TestSetPoseIgnoresOutOfRangeIndex()
	{
		const std::string name = UniqueName("setrange");
		protocol::DriverPoseShmem writer;
		CHECK(writer.Create(name.c_str()));
		HANDLE mapping = OpenFileMappingA(FILE_MAP_READ, FALSE, name.c_str());
		const RawShmem* raw =
		    mapping ? static_cast<const RawShmem*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(RawShmem))) : nullptr;
		CHECK(raw != nullptr);
		if (!raw) return;
		writer.SetPose((int)vr::k_unMaxTrackedDeviceCount, PoseFor(1));
		CHECK(raw->index == 0);
		writer.SetPose(-1, PoseFor(1));
		CHECK(raw->index == 0);
		UnmapViewOfFile(raw);
		CloseHandle(mapping);
	}

}

int main()
{
	TestOverlayAndDriverAgreeOnTheWire();
	TestReaderSeesTheNewestPose();
	TestReaderSeesEveryPoseOnceInOrder();
	TestConcurrentWritersLoseNothing();
	TestGetPoseRejectsOutOfRangeIndex();
	TestSetPoseIgnoresOutOfRangeIndex();

	if (failures == 0) {
		std::printf("protocol_tests: all tests passed\n");
		return 0;
	}
	std::printf("protocol_tests: %d failure(s)\n", failures);
	return 1;
}
