#include <iostream>

#include "Calibration.h"
#include "CalibrationCalc.h"
#include "CalibrationMath.h"
#include "IsometryTransform.h"
#include "PoseHealth.h"
#include "UpdateVersion.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <random>
#include <vector>

CalibrationContext CalCtx;

namespace {

	int failures = 0;

#define CHECK(cond)                                                                                                                        \
	do {                                                                                                                                   \
		if (!(cond)) {                                                                                                                     \
			std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);                                                                     \
			++failures;                                                                                                                    \
		}                                                                                                                                  \
	} while (0)

	constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

	double MaxAbs(const Eigen::MatrixXd& m)
	{
		return m.cwiseAbs().maxCoeff();
	}

	bool AllFinite(const Eigen::AffineCompact3d& t)
	{
		return t.matrix().allFinite();
	}

	Eigen::Quaterniond RandomRotation(std::mt19937& rng)
	{
		std::uniform_real_distribution<double> unit(-1.0, 1.0);
		Eigen::Vector3d axis(unit(rng), unit(rng), unit(rng));
		if (axis.norm() < 1e-3) axis = Eigen::Vector3d::UnitX();
		return Eigen::Quaterniond(Eigen::AngleAxisd(unit(rng) * EIGEN_PI, axis.normalized()));
	}

	void TestIsoTranslationConstructorKeepsTranslation()
	{
		const Eigen::Vector3d t(1.0, -2.0, 3.5);
		const IsoTransform iso(t);
		CHECK(iso.rotation.isApprox(Eigen::Quaterniond::Identity()));
		const bool kept = iso.translation == t;
		CHECK(kept);
		if (!kept)
			std::printf("  IsoTransform(Vector3d) left translation as (%g, %g, %g)\n", iso.translation(0), iso.translation(1),
			            iso.translation(2));
	}

	void TestIsoComposeMatchesAffine()
	{
		std::mt19937 rng(7);
		std::uniform_real_distribution<double> unit(-2.0, 2.0);
		double worst = 0.0;
		for (int i = 0; i < 200; ++i) {
			const IsoTransform a(RandomRotation(rng), Eigen::Vector3d(unit(rng), unit(rng), unit(rng)));
			const IsoTransform b(RandomRotation(rng), Eigen::Vector3d(unit(rng), unit(rng), unit(rng)));
			const Eigen::Vector3d p(unit(rng), unit(rng), unit(rng));
			worst = std::max(worst, (((a * b) * p) - (a * (b * p))).norm());
			const Eigen::Isometry3d ea = Eigen::Translation3d(a.translation) * a.rotation;
			worst = std::max(worst, ((a * p) - (ea * p)).norm());
		}
		CHECK(worst < 1e-12);
	}

	void TestIsoInterpolateAroundEndpointsAndPivot()
	{
		std::mt19937 rng(11);
		std::uniform_real_distribution<double> unit(-1.0, 1.0);
		double worstEnds = 0.0, worstPivot = 0.0;
		for (int i = 0; i < 200; ++i) {
			const IsoTransform from(RandomRotation(rng), Eigen::Vector3d(unit(rng), unit(rng), unit(rng)));
			const IsoTransform to(RandomRotation(rng), Eigen::Vector3d(unit(rng), unit(rng), unit(rng)));
			const Eigen::Vector3d pivot(unit(rng), unit(rng), unit(rng));
			const IsoTransform start = from.interpolateAround(0.0, to, pivot);
			const IsoTransform end = from.interpolateAround(1.0, to, pivot);
			worstEnds = std::max(worstEnds, (start.translation - from.translation).norm());
			worstEnds = std::max(worstEnds, std::abs(std::abs(start.rotation.dot(from.rotation)) - 1.0));
			worstEnds = std::max(worstEnds, (end.translation - to.translation).norm());
			worstEnds = std::max(worstEnds, std::abs(std::abs(end.rotation.dot(to.rotation)) - 1.0));
			const double lerp = 0.3;
			const IsoTransform mid = from.interpolateAround(lerp, to, pivot);
			const Eigen::Vector3d expected = (from * pivot) * (1 - lerp) + (to * pivot) * lerp;
			worstPivot = std::max(worstPivot, ((mid * pivot) - expected).norm());
		}
		CHECK(worstEnds < 1e-9);
		CHECK(worstPivot < 1e-12);
	}

	void TestQuaternionProductMatchesEigen()
	{
		std::mt19937 rng(3);
		double worst = 0.0;
		for (int i = 0; i < 200; ++i) {
			const Eigen::Quaterniond a = RandomRotation(rng), b = RandomRotation(rng);
			const vr::HmdQuaternion_t va{a.w(), a.x(), a.y(), a.z()}, vb{b.w(), b.x(), b.y(), b.z()};
			const vr::HmdQuaternion_t vp = va * vb;
			const Eigen::Quaterniond p = a * b;
			worst = std::max({worst, std::abs(vp.w - p.w()), std::abs(vp.x - p.x()), std::abs(vp.y - p.y()), std::abs(vp.z - p.z())});
		}
		CHECK(worst < 1e-15);
	}

	void TestEulerQuatIsZyxDegrees()
	{
		const Eigen::Vector3d euler(10.0, -35.0, 80.0);
		const vr::HmdQuaternion_t q = VRRotationQuat(euler);
		const Eigen::Matrix3d expected = (Eigen::AngleAxisd(euler(0) * EIGEN_PI / 180.0, Eigen::Vector3d::UnitZ()) *
		                                  Eigen::AngleAxisd(euler(1) * EIGEN_PI / 180.0, Eigen::Vector3d::UnitY()) *
		                                  Eigen::AngleAxisd(euler(2) * EIGEN_PI / 180.0, Eigen::Vector3d::UnitX()))
		                                     .toRotationMatrix();
		CHECK(MaxAbs(quaternionRotateMatrix(q) - expected) < 1e-15);
		const vr::HmdVector3d_t t = VRTranslationVec(Eigen::Vector3d(100.0, -50.0, 2.5));
		CHECK(t.v[0] == 1.0 && t.v[1] == -0.5 && t.v[2] == 0.025);
	}

	struct Scenario
	{
		Eigen::AffineCompact3d calibration;
		std::vector<Sample> samples;
	};

	Scenario MakeScenario(int count, unsigned seed)
	{
		Scenario s;
		s.calibration = Eigen::Translation3d(0.5, 0.2, -1.0) * Eigen::AngleAxisd(30.0 * EIGEN_PI / 180.0, Eigen::Vector3d::UnitY());
		const Eigen::AffineCompact3d mount =
		    Eigen::Translation3d(0.03, -0.05, 0.08) * Eigen::AngleAxisd(0.4, Eigen::Vector3d(1, 1, 0).normalized());
		std::mt19937 rng(seed);
		std::uniform_real_distribution<double> unit(-1.0, 1.0);
		for (int i = 0; i < count; ++i) {
			const Eigen::AffineCompact3d ref =
			    Eigen::Translation3d(unit(rng), 1.5 + 0.3 * unit(rng), unit(rng)) * Eigen::AffineCompact3d(RandomRotation(rng));
			const Eigen::AffineCompact3d target = s.calibration.inverse() * ref * mount;
			s.samples.emplace_back(Pose(ref), Pose(target), i * 0.01);
		}
		return s;
	}

	void TestOneshotRecoversAKnownCalibration()
	{
		const Scenario s = MakeScenario(100, 21);
		CalibrationCalc calc;
		for (const Sample& sample : s.samples)
			calc.PushSample(sample);
		CHECK(calc.ComputeOneshot(false));
		const double error = MaxAbs(calc.Transformation().matrix() - s.calibration.matrix());
		CHECK(error < 1e-6);
		std::printf("  oneshot recovers the synthetic calibration to %.3g\n", error);
	}

	void TestOneshotRejectsAnEmptySampleSet()
	{
		CalibrationCalc calc;
		const bool accepted = calc.ComputeOneshot(false);
		CHECK(!accepted);
		CHECK(!calc.isValid());
		if (accepted) std::printf("  empty set accepted, transform finite: %d\n", (int)AllFinite(calc.Transformation()));
	}

	void TestOneshotNeverAcceptsANonFiniteCalibration()
	{
		Scenario s = MakeScenario(100, 21);
		s.samples[40].target.trans(1) = kNaN;
		CalibrationCalc calc;
		for (const Sample& sample : s.samples)
			calc.PushSample(sample);
		const bool accepted = calc.ComputeOneshot(false);
		const bool finite = AllFinite(calc.Transformation());
		CHECK(!accepted || finite);
		if (accepted && !finite) std::printf("  one NaN sample in 100 produced an accepted NaN calibration\n");
	}

	void TestIncrementalNeverAcceptsANonFiniteCalibration()
	{
		Scenario s = MakeScenario(100, 33);
		CalibrationCalc calc;
		calc.enableStaticRecalibration = false;
		for (const Sample& sample : s.samples)
			calc.PushSample(sample);
		bool lerp = false;
		CHECK(calc.ComputeIncremental(lerp, 1.5, 0.005, false));
		CHECK(AllFinite(calc.Transformation()));
		calc.ShiftSample();
		Sample bad = s.samples[5];
		bad.ref.trans(0) = kNaN;
		calc.PushSample(bad);
		const bool accepted = calc.ComputeIncremental(lerp, 1.5, 0.005, false);
		const bool finite = AllFinite(calc.Transformation());
		CHECK(finite);
		if (!finite) std::printf("  continuous step with one NaN sample replaced the calibration with NaN (accepted=%d)\n", (int)accepted);
	}

	void TestJitterIsTheSampleStandardDeviation()
	{
		CalibrationCalc calc;
		const double xs[] = {0.0, 1.0, 2.0, 3.0};
		for (double x : xs) {
			Pose ref(x, 0.5, -0.25);
			ref.rot.setIdentity();
			Pose target(0.1, 0.2, 2.0 * x);
			target.rot.setIdentity();
			calc.PushSample(Sample(ref, target, 0.0));
		}
		const double expectedRef = std::sqrt(5.0 / 3.0);
		const double expectedTarget = std::sqrt(20.0 / 3.0);
		const double ref = calc.ReferenceJitter();
		const double target = calc.TargetJitter();
		CHECK(std::abs(ref - expectedRef) < 1e-9);
		CHECK(std::abs(target - expectedTarget) < 1e-9);
		std::printf("  jitter of x = 0,1,2,3: reference %.6g (expected %.6g), target %.6g (expected %.6g)\n", ref, expectedRef, target,
		            expectedTarget);
	}

	void TestJitterOfStillDevicesIsZero()
	{
		CalibrationCalc calc;
		for (int i = 0; i < 50; ++i) {
			Pose ref(0.25, 1.5, -0.75);
			ref.rot.setIdentity();
			calc.PushSample(Sample(ref, ref, i * 0.01));
		}
		CHECK(calc.ReferenceJitter() == 0.0);
		CHECK(calc.TargetJitter() == 0.0);
	}

	void TestSampleWithNaNPositionIsNotUsed()
	{
		vr::DriverPose_t pose{};
		pose.poseIsValid = true;
		pose.result = vr::TrackingResult_Running_OK;
		pose.qRotation = {1, 0, 0, 0};
		CHECK(spacecal::ClassifySamplePose(pose) == spacecal::SampleVerdict::Use);
		pose.vecPosition[1] = kNaN;
		CHECK(spacecal::ClassifySamplePose(pose) != spacecal::SampleVerdict::Use);
		pose.vecPosition[1] = 0.0;
		pose.qRotation.x = std::numeric_limits<double>::infinity();
		CHECK(spacecal::ClassifySamplePose(pose) != spacecal::SampleVerdict::Use);
	}

	void TestUpdateVersionEdges()
	{
		using namespace spacecal;
		UpdateVersion v;
		CHECK(ParseUpdateVersion("2026.010.001.000", v) && v.parts[1] == 10 && v.parts[2] == 1);
		CHECK(ParseUpdateVersion("999999999.0.0.0", v) && v.parts[0] == 999999999);
		CHECK(!ParseUpdateVersion("1000000000.0.0.0", v));
		CHECK(!ParseUpdateVersion("v", v));
		CHECK(!ParseUpdateVersion("2026..1.0", v));
		CHECK(!ParseUpdateVersion("2026.1.1.0.", v));
		CHECK(!ParseUpdateVersion("-2026.1.1.0", v));
		CHECK(!ParseUpdateVersion("2026.1.1.+1", v));
		CHECK(ParseUpdateVersion("\t2026.1.1.0-Beta\r\n", v) && v.beta);
		CHECK(ParseUpdateVersion("2026.1.1.0-", v) && !v.beta);
		CHECK(ParseUpdateVersion("2026.1.1.0-beta-2", v) && !v.beta);
		const char* ordered[] = {"2025.12.31.9", "2026.1.1.0-beta", "2026.1.1.0", "2026.1.1.1-beta", "2026.1.2.0", "2026.10.1.0"};
		for (size_t i = 0; i < 6; ++i) {
			for (size_t j = 0; j < 6; ++j) {
				UpdateVersion a, b;
				CHECK(ParseUpdateVersion(ordered[i], a) && ParseUpdateVersion(ordered[j], b));
				const int c = CompareUpdateVersion(a, b);
				CHECK((i < j && c < 0) || (i == j && c == 0) || (i > j && c > 0));
			}
			UpdateVersion a;
			ParseUpdateVersion(ordered[i], a);
			CHECK(FormatUpdateVersion(a) == ordered[i]);
		}
	}

}

int main()
{
	TestIsoTranslationConstructorKeepsTranslation();
	TestIsoComposeMatchesAffine();
	TestIsoInterpolateAroundEndpointsAndPivot();
	TestQuaternionProductMatchesEigen();
	TestEulerQuatIsZyxDegrees();
	TestOneshotRecoversAKnownCalibration();
	TestOneshotRejectsAnEmptySampleSet();
	TestOneshotNeverAcceptsANonFiniteCalibration();
	TestIncrementalNeverAcceptsANonFiniteCalibration();
	TestJitterIsTheSampleStandardDeviation();
	TestJitterOfStillDevicesIsZero();
	TestSampleWithNaNPositionIsNotUsed();
	TestUpdateVersionEdges();

	if (failures == 0) {
		std::printf("math_tests: all tests passed\n");
		return 0;
	}
	std::printf("math_tests: %d failure(s)\n", failures);
	return 1;
}
