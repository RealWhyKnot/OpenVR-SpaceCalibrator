#include "SkeletalSmoothingMath.h"
#include "FingerConfigPacking.h"

#include <cmath>
#include <cstdio>

namespace {

	using namespace spacecal::skeletal;

	int failures = 0;

#define CHECK(cond)                                                                                                                        \
	do {                                                                                                                                   \
		if (!(cond)) {                                                                                                                     \
			std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);                                                                     \
			++failures;                                                                                                                    \
		}                                                                                                                                  \
	} while (0)

	constexpr float kEpsilon = 1e-5f;

	vr::VRBoneTransform_t Bone(float x, float y, float z)
	{
		vr::VRBoneTransform_t bone{};
		bone.position.v[0] = x;
		bone.position.v[1] = y;
		bone.position.v[2] = z;
		bone.position.v[3] = 1.0f;
		bone.orientation = {1.0f, 0.0f, 0.0f, 0.0f};
		return bone;
	}

	void FillFrame(vr::VRBoneTransform_t (&bones)[kFingerBoneCount], float x)
	{
		for (uint32_t i = 0; i < kFingerBoneCount; ++i) {
			bones[i] = Bone(x, 0.0f, 0.0f);
		}
	}

	void TestAlphaMapping()
	{
		CHECK(std::fabs(SmoothnessToAlpha(0) - 1.0f) < kEpsilon);
		CHECK(std::fabs(SmoothnessToAlpha(50) - 0.525f) < kEpsilon);
		CHECK(std::fabs(SmoothnessToAlpha(100) - 0.05f) < kEpsilon);
		CHECK(SmoothnessToAlpha(10) > SmoothnessToAlpha(20));
	}

	void TestBoneFingerMap()
	{
		CHECK(FingerIndexForBone(0) == -1);
		CHECK(FingerIndexForBone(1) == -1);
		CHECK(FingerIndexForBone(2) == 0);
		CHECK(FingerIndexForBone(5) == 0);
		CHECK(FingerIndexForBone(6) == 1);
		CHECK(FingerIndexForBone(10) == 1);
		CHECK(FingerIndexForBone(11) == 2);
		CHECK(FingerIndexForBone(15) == 2);
		CHECK(FingerIndexForBone(16) == 3);
		CHECK(FingerIndexForBone(20) == 3);
		CHECK(FingerIndexForBone(21) == 4);
		CHECK(FingerIndexForBone(25) == 4);
		CHECK(FingerIndexForBone(26) == -1);
		CHECK(FingerIndexForBone(30) == -1);
	}

	void TestSeedThenHalfLerp()
	{
		FingerFrameState state;
		vr::VRBoneTransform_t input[kFingerBoneCount];
		vr::VRBoneTransform_t output[kFingerBoneCount];
		const float alphas[kFingersPerHand] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f};

		FillFrame(input, 0.0f);
		auto seedResult = SmoothFingerFrame(state, input, kFingerBoneCount, 0, protocol::kAllFingersMask, alphas, output);
		CHECK(seedResult.seeded);
		CHECK(state.initialized);

		FillFrame(input, 1.0f);
		auto result = SmoothFingerFrame(state, input, kFingerBoneCount, 0, protocol::kAllFingersMask, alphas, output);
		CHECK(result.appliedSmoothing);
		CHECK(std::fabs(output[6].position.v[0] - 0.5f) < kEpsilon);
		CHECK(std::fabs(output[0].position.v[0] - 1.0f) < kEpsilon);
		CHECK(std::fabs(output[1].position.v[0] - 1.0f) < kEpsilon);
		CHECK(std::fabs(output[26].position.v[0] - 1.0f) < kEpsilon);
	}

	void TestPassthroughAtAlphaOne()
	{
		FingerFrameState state;
		vr::VRBoneTransform_t input[kFingerBoneCount];
		vr::VRBoneTransform_t output[kFingerBoneCount];
		const float alphas[kFingersPerHand] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f};

		FillFrame(input, 0.0f);
		SmoothFingerFrame(state, input, kFingerBoneCount, 0, protocol::kAllFingersMask, alphas, output);
		FillFrame(input, 2.0f);
		auto result = SmoothFingerFrame(state, input, kFingerBoneCount, 0, protocol::kAllFingersMask, alphas, output);
		CHECK(!result.appliedSmoothing);
		CHECK(std::fabs(output[6].position.v[0] - 2.0f) < kEpsilon);
	}

	void TestMaskedFingerPassesThrough()
	{
		FingerFrameState state;
		vr::VRBoneTransform_t input[kFingerBoneCount];
		vr::VRBoneTransform_t output[kFingerBoneCount];
		const float alphas[kFingersPerHand] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f};

		FillFrame(input, 0.0f);
		const uint16_t maskNoIndex = protocol::kAllFingersMask & ~(uint16_t)(1u << protocol::FingerBit(0, 1));
		SmoothFingerFrame(state, input, kFingerBoneCount, 0, maskNoIndex, alphas, output);
		FillFrame(input, 1.0f);
		SmoothFingerFrame(state, input, kFingerBoneCount, 0, maskNoIndex, alphas, output);
		CHECK(std::fabs(output[6].position.v[0] - 1.0f) < kEpsilon);
		CHECK(std::fabs(output[11].position.v[0] - 0.5f) < kEpsilon);
	}

	void TestReseedTakesOneRawFrame()
	{
		FingerFrameState state;
		vr::VRBoneTransform_t input[kFingerBoneCount];
		vr::VRBoneTransform_t output[kFingerBoneCount];
		const float alphas[kFingersPerHand] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f};

		FillFrame(input, 0.0f);
		SmoothFingerFrame(state, input, kFingerBoneCount, 0, protocol::kAllFingersMask, alphas, output);

		state.reseed_pending[1] = true;
		FillFrame(input, 1.0f);
		auto reseedResult = SmoothFingerFrame(state, input, kFingerBoneCount, 0, protocol::kAllFingersMask, alphas, output);
		CHECK(reseedResult.reseeded);
		CHECK(std::fabs(output[6].position.v[0] - 1.0f) < kEpsilon);
		CHECK(std::fabs(output[11].position.v[0] - 0.5f) < kEpsilon);
		CHECK(!state.reseed_pending[1]);

		FillFrame(input, 2.0f);
		SmoothFingerFrame(state, input, kFingerBoneCount, 0, protocol::kAllFingersMask, alphas, output);
		CHECK(std::fabs(output[6].position.v[0] - 1.5f) < kEpsilon);
	}

	void TestReseedPersistsWhileFingerMaskedOff()
	{
		FingerFrameState state;
		vr::VRBoneTransform_t input[kFingerBoneCount];
		vr::VRBoneTransform_t output[kFingerBoneCount];
		const float alphas[kFingersPerHand] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f};

		FillFrame(input, 0.0f);
		SmoothFingerFrame(state, input, kFingerBoneCount, 0, protocol::kAllFingersMask, alphas, output);

		state.reseed_pending[1] = true;
		const uint16_t maskNoIndex = protocol::kAllFingersMask & ~(uint16_t)(1u << protocol::FingerBit(0, 1));
		FillFrame(input, 1.0f);
		SmoothFingerFrame(state, input, kFingerBoneCount, 0, maskNoIndex, alphas, output);
		CHECK(state.reseed_pending[1]);

		FillFrame(input, 2.0f);
		SmoothFingerFrame(state, input, kFingerBoneCount, 0, protocol::kAllFingersMask, alphas, output);
		CHECK(!state.reseed_pending[1]);
		CHECK(std::fabs(output[6].position.v[0] - 2.0f) < kEpsilon);
	}

	void TestMotionRangeIndex()
	{
		CHECK(MotionRangeIndex((int)vr::VRSkeletalMotionRange_WithController) == 0);
		CHECK(MotionRangeIndex((int)vr::VRSkeletalMotionRange_WithoutController) == 1);
	}

	void TestComputeRateHz()
	{
		CHECK(std::fabs(ComputeRateHz(340, 1.0) - 340.0) < 1e-9);
		CHECK(ComputeRateHz(100, 0.0) == 0.0);
	}

	void TestPackRoundTrip()
	{
		protocol::FingerSmoothingConfig cfg{};
		cfg.strength = 42;
		cfg.fingerMask = 0x02A5;
		for (int i = 0; i < 10; ++i) {
			cfg.perFinger[i] = (uint8_t)(i * 9);
		}

		const auto roundTripped = UnpackFingerSmoothing(PackFingerHeader(cfg), PackFingerLow(cfg));
		CHECK(roundTripped.strength == cfg.strength);
		CHECK(roundTripped.fingerMask == cfg.fingerMask);
		for (int i = 0; i < 10; ++i) {
			CHECK(roundTripped.perFinger[i] == cfg.perFinger[i]);
		}
	}

	void TestReseedBits()
	{
		protocol::FingerSmoothingConfig prev{};
		prev.fingerMask = protocol::kAllFingersMask;
		protocol::FingerSmoothingConfig next = prev;
		next.strength = 50;
		CHECK(ComputeFingerSmoothingReseedBits(prev, next) == protocol::kAllFingersMask);

		prev = next;
		next.strength = 80;
		CHECK(ComputeFingerSmoothingReseedBits(prev, next) == 0);

		next.strength = 0;
		next.perFinger[3] = 25;
		CHECK(ComputeFingerSmoothingReseedBits(prev, next) == 0);
		CHECK(ComputeFingerSmoothingReseedBits(next, prev) == (protocol::kAllFingersMask & ~(uint16_t)(1u << 3)));
	}

	void TestFingerBit()
	{
		CHECK(protocol::FingerBit(0, 0) == 0);
		CHECK(protocol::FingerBit(0, 4) == 4);
		CHECK(protocol::FingerBit(1, 0) == 5);
		CHECK(protocol::FingerBit(1, 4) == 9);
	}

}

int main()
{
	TestAlphaMapping();
	TestBoneFingerMap();
	TestSeedThenHalfLerp();
	TestPassthroughAtAlphaOne();
	TestMaskedFingerPassesThrough();
	TestReseedTakesOneRawFrame();
	TestReseedPersistsWhileFingerMaskedOff();
	TestMotionRangeIndex();
	TestComputeRateHz();
	TestPackRoundTrip();
	TestFingerBit();
	TestReseedBits();

	if (failures == 0) {
		std::printf("skeletal_tests: all tests passed\n");
		return 0;
	}
	std::printf("skeletal_tests: %d failure(s)\n", failures);
	return 1;
}
