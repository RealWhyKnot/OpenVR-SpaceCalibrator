#include "perf_harness.h"

#include <iostream>

#include "Calibration.h"
#include "CalibrationCalc.h"
#include "CalibrationMath.h"
#include "DriverRequestCache.h"
#include "LoopPacing.h"
#include "ProcessWatch.h"
#include "WriteFilter.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <random>
#include <string>
#include <vector>

CalibrationContext CalCtx;

namespace previous {

	std::vector<bool> DetectOutliers(const std::deque<Sample>& samples)
	{
		std::vector<DSample> deltas;
		const size_t step = 5;
		for (size_t i = 0; i < samples.size(); i += step) {
			for (size_t j = 0; j < i; j += step) {
				auto delta = DeltaRotationSamples(samples[i], samples[j]);
				if (delta.valid) deltas.push_back(delta);
			}
		}

		Eigen::MatrixXd refPoints(deltas.size(), 3), targetPoints(deltas.size(), 3);
		Eigen::Vector3d refCentroid(0, 0, 0), targetCentroid(0, 0, 0);
		for (size_t i = 0; i < deltas.size(); i++) {
			refPoints.row(i) = deltas[i].ref;
			refCentroid += deltas[i].ref;
			targetPoints.row(i) = deltas[i].target;
			targetCentroid += deltas[i].target;
		}
		refCentroid /= (double)deltas.size();
		targetCentroid /= (double)deltas.size();
		for (size_t i = 0; i < deltas.size(); i++) {
			refPoints.row(i) -= refCentroid;
			targetPoints.row(i) -= targetCentroid;
		}

		auto crossCV = refPoints.transpose() * targetPoints;
		Eigen::BDCSVD<Eigen::MatrixXd> bdcsvd;
		auto svd = bdcsvd.compute(crossCV, Eigen::ComputeThinU | Eigen::ComputeThinV);
		Eigen::Matrix3d i = Eigen::Matrix3d::Identity();
		if ((svd.matrixU() * svd.matrixV().transpose()).determinant() < 0) i(2, 2) = -1;
		Eigen::Matrix3d rot = svd.matrixV() * i * svd.matrixU().transpose();
		rot.transposeInPlace();

		Eigen::MatrixXd coefficients(samples.size() * 4, 4);
		Eigen::VectorXd constraints(samples.size() * 4);
		std::vector<bool> valids(samples.size());
		for (size_t k = 0; k < samples.size(); k++) {
			Eigen::Matrix3d rotExtTmp = (samples[k].ref.rot.transpose() * rot * samples[k].target.rot);
			Eigen::Quaterniond quatExtTmp(rotExtTmp);
			quatExtTmp.normalize();
			coefficients.block<4, 4>(4 * k, 0) = Eigen::Matrix4d::Identity();
			constraints.block<4, 1>(4 * k, 0) = Eigen::Vector4d(quatExtTmp.w(), quatExtTmp.x(), quatExtTmp.y(), quatExtTmp.z());
		}
		Eigen::Vector4d result = coefficients.bdcSvd(Eigen::ComputeThinU | Eigen::ComputeThinV).solve(constraints);
		Eigen::Quaterniond quatExt(result(0), result(1), result(2), result(3));
		quatExt.normalize();

		for (size_t k = 0; k < samples.size(); k++) {
			Eigen::Matrix3d rotExtTmp = (samples[k].ref.rot.transpose() * rot * samples[k].target.rot);
			Eigen::Quaterniond quatExtTmp(rotExtTmp);
			double cosHalfAngle = quatExtTmp.w() * quatExt.w() + quatExtTmp.vec().dot(quatExt.vec());
			valids[k] = !(std::abs(cosHalfAngle) < 0.99);
		}
		return valids;
	}

	Eigen::Vector3d CalibrateRotation(const std::deque<Sample>& samples, bool ignoreOutliers)
	{
		std::vector<DSample> deltas;
		std::vector<bool> valids = DetectOutliers(samples);
		for (size_t i = 0; i < samples.size(); i++) {
			for (size_t j = 0; j < i; j++) {
				if (ignoreOutliers && (!valids[i] || !valids[j])) continue;
				auto delta = DeltaRotationSamples(samples[i], samples[j]);
				if (delta.valid) deltas.push_back(delta);
			}
		}

		Eigen::MatrixXd refPoints(deltas.size(), 2), targetPoints(deltas.size(), 2);
		Eigen::Vector2d refCentroid(0, 0), targetCentroid(0, 0);
		for (size_t i = 0; i < deltas.size(); i++) {
			refPoints.row(i) << deltas[i].ref[0], deltas[i].ref[2];
			refCentroid += refPoints.row(i);
			targetPoints.row(i) << deltas[i].target[0], deltas[i].target[2];
			targetCentroid += targetPoints.row(i);
		}
		refCentroid /= (double)deltas.size();
		targetCentroid /= (double)deltas.size();
		for (size_t i = 0; i < deltas.size(); i++) {
			refPoints.row(i) -= refCentroid;
			targetPoints.row(i) -= targetCentroid;
		}

		auto crossCV = refPoints.transpose() * targetPoints;
		Eigen::JacobiSVD<Eigen::MatrixXd> svd(crossCV, Eigen::ComputeThinU | Eigen::ComputeThinV);
		Eigen::Matrix2d rot = svd.matrixV() * Eigen::Matrix2d::Identity() * svd.matrixU().transpose();
		double yaw = std::atan2(rot(1, 0), rot(0, 0));
		return Eigen::Vector3d(0.0, yaw * 180.0 / EIGEN_PI, 0.0);
	}

	Eigen::Vector3d CalibrateTranslation(const std::deque<Sample>& samples, const Eigen::Matrix3d& rotation)
	{
		std::vector<std::pair<Eigen::Vector3d, Eigen::Matrix3d>> deltas;
		for (size_t i = 0; i < samples.size(); i++) {
			Sample s_i = samples[i];
			s_i.target.rot = rotation * s_i.target.rot;
			s_i.target.trans = rotation * s_i.target.trans;
			for (size_t j = 0; j < i; j++) {
				Sample s_j = samples[j];
				s_j.target.rot = rotation * s_j.target.rot;
				s_j.target.trans = rotation * s_j.target.trans;

				auto QAi = s_i.ref.rot.transpose();
				auto QAj = s_j.ref.rot.transpose();
				auto dQA = QAj - QAi;
				auto CA = QAj * (s_j.ref.trans - s_j.target.trans) - QAi * (s_i.ref.trans - s_i.target.trans);
				deltas.push_back(std::make_pair(CA, dQA));

				auto QBi = s_i.target.rot.transpose();
				auto QBj = s_j.target.rot.transpose();
				auto dQB = QBj - QBi;
				auto CB = QBj * (s_j.ref.trans - s_j.target.trans) - QBi * (s_i.ref.trans - s_i.target.trans);
				deltas.push_back(std::make_pair(CB, dQB));
			}
		}

		Eigen::VectorXd constants(deltas.size() * 3);
		Eigen::MatrixXd coefficients(deltas.size() * 3, 3);
		for (size_t i = 0; i < deltas.size(); i++) {
			for (int axis = 0; axis < 3; axis++) {
				constants(i * 3 + axis) = deltas[i].first(axis);
				coefficients.row(i * 3 + axis) = deltas[i].second.row(axis);
			}
		}
		return coefficients.bdcSvd(Eigen::ComputeThinU | Eigen::ComputeThinV).solve(constants);
	}

	Eigen::AffineCompact3d ComputeCalibration(const std::deque<Sample>& samples, bool ignoreOutliers)
	{
		Eigen::Vector3d rotation = CalibrateRotation(samples, ignoreOutliers);
		Eigen::Matrix3d rotationMat = quaternionRotateMatrix(VRRotationQuat(rotation));
		Eigen::Vector3d translation = CalibrateTranslation(samples, rotationMat);
		return Eigen::Translation3d(translation) * Eigen::AffineCompact3d(rotationMat);
	}

}

namespace {

	struct Scenario
	{
		Eigen::AffineCompact3d calibration;
		std::deque<Sample> samples;
	};

	Eigen::AffineCompact3d MakeTransform(double yawDeg, double pitchDeg, double rollDeg, const Eigen::Vector3d& t)
	{
		const double d = EIGEN_PI / 180.0;
		Eigen::Quaterniond q = Eigen::AngleAxisd(yawDeg * d, Eigen::Vector3d::UnitY()) *
		                       Eigen::AngleAxisd(pitchDeg * d, Eigen::Vector3d::UnitX()) *
		                       Eigen::AngleAxisd(rollDeg * d, Eigen::Vector3d::UnitZ());
		Eigen::AffineCompact3d x = Eigen::AffineCompact3d::Identity();
		x.linear() = q.toRotationMatrix();
		x.translation() = t;
		return x;
	}

	Scenario MakeScenario(size_t count, unsigned seed, double noiseM, size_t outlierEvery = 0)
	{
		std::mt19937 rng(seed);
		std::uniform_real_distribution<double> angle(-60.0, 60.0);
		std::uniform_real_distribution<double> pos(-0.3, 0.3);
		std::normal_distribution<double> noise(0.0, noiseM);
		const Eigen::AffineCompact3d slip = MakeTransform(0.0, 25.0, 0.0, Eigen::Vector3d::Zero());

		Scenario s;
		s.calibration = MakeTransform(37.0, 0.0, 0.0, Eigen::Vector3d(0.42, -0.08, 1.3));
		const Eigen::AffineCompact3d mount = MakeTransform(10.0, -20.0, 5.0, Eigen::Vector3d(0.05, -0.12, 0.08));
		const Eigen::AffineCompact3d inverseCalibration = s.calibration.inverse();

		for (size_t i = 0; i < count; ++i) {
			const Eigen::AffineCompact3d ref =
			    MakeTransform(angle(rng) * 2.5, angle(rng), angle(rng) * 0.5, Eigen::Vector3d(pos(rng), 1.6 + pos(rng) * 0.3, pos(rng)));
			Eigen::AffineCompact3d target = inverseCalibration * ref * mount;
			if (outlierEvery != 0 && i % outlierEvery == outlierEvery - 1) target = target * slip;
			target.translation() += Eigen::Vector3d(noise(rng), noise(rng), noise(rng));
			s.samples.push_back(Sample(Pose(ref), Pose(target), 0.05 * (double)i));
		}
		return s;
	}

	void Load(CalibrationCalc& calc, const Scenario& s)
	{
		calc.Clear();
		for (const auto& sample : s.samples)
			calc.PushSample(sample);
	}

	double TranslationError(const Eigen::AffineCompact3d& a, const Eigen::AffineCompact3d& b)
	{
		return (a.translation() - b.translation()).norm();
	}

	double RotationErrorDeg(const Eigen::AffineCompact3d& a, const Eigen::AffineCompact3d& b)
	{
		const Eigen::Quaterniond qa(a.rotation());
		const Eigen::Quaterniond qb(b.rotation());
		return qa.angularDistance(qb) * 180.0 / EIGEN_PI;
	}

	void TestSolverAccuracy()
	{
		for (size_t n : {100u, 250u, 500u}) {
			const Scenario s = MakeScenario(n, 7u + (unsigned)n, 0.0005);
			CalibrationCalc calc;
			Load(calc, s);
			PERF_CHECK(calc.ComputeOneshot(false));
			PERF_CHECK(TranslationError(calc.Transformation(), s.calibration) < 0.002);
			PERF_CHECK(RotationErrorDeg(calc.Transformation(), s.calibration) < 0.2);

			Load(calc, s);
			PERF_CHECK(calc.ComputeOneshot(true));
			PERF_CHECK(TranslationError(calc.Transformation(), s.calibration) < 0.002);
		}
	}

	void TestSolverMatchesPreviousImplementation()
	{
		for (const bool ignoreOutliers : {false, true}) {
			for (const size_t n : {40u, 100u, 250u}) {
				const Scenario s = MakeScenario(n, 11u + (unsigned)n, 0.0005, ignoreOutliers ? 25 : 0);
				CalibrationCalc calc;
				Load(calc, s);
				PERF_CHECK(calc.ComputeOneshot(ignoreOutliers));
				const Eigen::AffineCompact3d expected = previous::ComputeCalibration(s.samples, ignoreOutliers);
				const double dt = TranslationError(calc.Transformation(), expected);
				const double dr = RotationErrorDeg(calc.Transformation(), expected);
				if (dt > 1e-9 || dr > 1e-9)
					std::printf("solver mismatch n=%zu outliers=%d: %.3g m, %.3g deg\n", n, (int)ignoreOutliers, dt, dr);
				PERF_CHECK(dt < 1e-9);
				PERF_CHECK(dr < 1e-9);
			}
		}
	}

	void TestSolverCost()
	{
		struct Case
		{
			size_t samples;
			const char* timeName;
			const char* heapName;
			double msBudget;
			double peakMiBBudget;
		};
		const Case cases[] = {
		    {100, "solver: one-shot, 100 samples (ms)", "solver: one-shot, 100 samples peak heap (MiB)", 5.0, 1.0},
		    {250, "solver: one-shot, 250 samples (ms)", "solver: one-shot, 250 samples peak heap (MiB)", 25.0, 1.0},
		    {500, "solver: one-shot, 500 samples (ms)", "solver: one-shot, 500 samples peak heap (MiB)", 60.0, 2.0},
		};

		for (const Case& c : cases) {
			const Scenario s = MakeScenario(c.samples, 99u + (unsigned)c.samples, 0.0005);
			CalibrationCalc calc;
			Load(calc, s);
			calc.ComputeOneshot(false);
			const double seconds = perf::BestSecondsPerOp(3, 1, [&](int) { calc.ComputeOneshot(false); });
			perf::Budget(c.timeName, seconds * 1000.0, c.msBudget, "ms");

			const auto heap = perf::MeasureHeap([&] { calc.ComputeOneshot(false); });
			perf::Budget(c.heapName, (double)heap.peakBytes / (1024.0 * 1024.0), c.peakMiBBudget, "MiB");
			PERF_CHECK(heap.liveBytes <= 64 * 1024);
		}

		const Scenario outliers = MakeScenario(500, 17u, 0.0005, 25);
		CalibrationCalc outlierCalc;
		Load(outlierCalc, outliers);
		const auto outlierHeap = perf::MeasureHeap([&] { outlierCalc.ComputeOneshot(true); });
		perf::Budget("solver: one-shot ignoring outliers, 500 samples peak heap (MiB)", (double)outlierHeap.peakBytes / (1024.0 * 1024.0),
		             4.0, "MiB");

		const Scenario s = MakeScenario(100, 5u, 0.0005);
		CalibrationCalc calc;
		Load(calc, s);
		calc.setRelativeTransformation(Eigen::AffineCompact3d::Identity(), false);
		calc.enableStaticRecalibration = false;
		bool lerp = false;
		calc.ComputeIncremental(lerp, 1.5, 0.005, false);
		const double incremental = perf::BestSecondsPerOp(3, 1, [&](int) { calc.ComputeIncremental(lerp, 1.5, 0.005, false); });
		perf::Budget("solver: continuous step, 100 samples (ms)", incremental * 1000.0, 5.0, "ms");
		perf::Report("solver: continuous FAST core share (%, one step per 0.5 s)", incremental / 0.5 * 100.0, "%");
	}

	struct LoopRun
	{
		double wakesPerSecond;
		double ticksPerSecond;
	};

	template <class WaitFn> LoopRun SimulateLoop(WaitFn waitSeconds, double wanted, bool connected, double timerGranularity)
	{
		const double seconds = 60.0;
		const double work = 0.0002;
		double now = 0.0;
		double lastTick = -1.0;
		int wakes = 0;
		int ticks = 0;
		while (now < seconds) {
			if (connected && now - lastTick >= spacecal::pacing::kCalibrationTickSeconds) {
				lastTick = now;
				++ticks;
			}
			const double requested = std::floor(waitSeconds(now + work, lastTick, wanted) * 1e3) / 1e3;
			const double due = now + work + requested;
			now = std::ceil(due / timerGranularity - 1e-9) * timerGranularity;
			++wakes;
		}
		return {wakes / seconds, ticks / seconds};
	}

	void TestLoopPacing()
	{
		auto previousWait = [](double, double, double wanted) {
			return std::max(wanted, 1.0 / 90.0);
		};
		auto hiddenWait = [](double now, double lastTick, double wanted) {
			return spacecal::pacing::WaitSeconds(now, lastTick, wanted, false, false);
		};

		for (const double granularity : {0.001, 1.0 / 64.0}) {
			const LoopRun before = SimulateLoop(previousWait, 0.0, true, granularity);
			const LoopRun after = SimulateLoop(hiddenWait, 0.0, true, granularity);
			const bool coarse = granularity > 0.01;
			perf::Report(coarse ? "loop: continuous, hidden, 15.6 ms timer, wakes/s before"
			                    : "loop: continuous, hidden, 1 ms timer, wakes/s before",
			             before.wakesPerSecond, "/s");
			perf::Budget(coarse ? "loop: continuous, hidden, 15.6 ms timer, wakes/s" : "loop: continuous, hidden, 1 ms timer, wakes/s",
			             after.wakesPerSecond, 21.0, "/s");
			perf::Report(coarse ? "loop: continuous, 15.6 ms timer, samples/s before" : "loop: continuous, 1 ms timer, samples/s before",
			             before.ticksPerSecond, "/s");
			perf::Report(coarse ? "loop: continuous, 15.6 ms timer, samples/s after" : "loop: continuous, 1 ms timer, samples/s after",
			             after.ticksPerSecond, "/s");
			PERF_CHECK(after.ticksPerSecond >= before.ticksPerSecond * 0.98);

			const LoopRun disconnected = SimulateLoop(hiddenWait, 0.0, false, granularity);
			PERF_CHECK(disconnected.wakesPerSecond <= 21.0);
			const LoopRun idle = SimulateLoop(hiddenWait, 1.0, true, granularity);
			PERF_CHECK(idle.wakesPerSecond <= 1.1);
			const LoopRun editing = SimulateLoop(hiddenWait, 0.1, true, granularity);
			PERF_CHECK(editing.wakesPerSecond <= 10.1);
		}

		PERF_CHECK(spacecal::pacing::WaitSeconds(10.0, 9.99, 1.0, true, false) == spacecal::pacing::kDashboardFrameSeconds);
		PERF_CHECK(spacecal::pacing::WaitSeconds(10.0, 9.99, 0.0, true, false) == spacecal::pacing::kDashboardFrameSeconds);
		PERF_CHECK(spacecal::pacing::WaitSeconds(10.0, 9.99, 1.0, false, true) == 0.0);
		PERF_CHECK(spacecal::pacing::WaitSeconds(10.0, 0.0, 0.0, false, false) >= spacecal::pacing::kCalibrationTickSeconds);
	}

	void TestProcessWatch()
	{
		wchar_t self[MAX_PATH] = {};
		wchar_t tempDir[MAX_PATH] = {};
		PERF_CHECK(GetModuleFileNameW(nullptr, self, MAX_PATH) > 0);
		PERF_CHECK(GetTempPathW(MAX_PATH, tempDir) > 0);
		const std::wstring childName = L"spacecal_perf_child_" + std::to_wstring(GetCurrentProcessId()) + L".exe";
		const std::wstring childPath = std::wstring(tempDir) + childName;
		if (!CopyFileW(self, childPath.c_str(), FALSE)) {
			PERF_CHECK(!"could not copy the test binary for the child process");
			return;
		}

		std::wstring command = L"\"" + childPath + L"\" --sleep-child";
		STARTUPINFOW startup = {};
		startup.cb = sizeof startup;
		PROCESS_INFORMATION child = {};
		if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child)) {
			PERF_CHECK(!"could not start the child process");
			DeleteFileW(childPath.c_str());
			return;
		}

		spacecal::ProcessWatch watch(childName.c_str());
		PERF_CHECK(watch.Running());
		const double cached = perf::BestSecondsPerOp(5, 2000, [&](int) { watch.Running(); });
		const double snapshot = perf::BestSecondsPerOp(5, 20, [&](int) { spacecal::FindProcessId(childName.c_str()); });
		perf::Budget("vrserver check: cached process handle (us/check)", cached * 1e6, 20.0, "us");
		perf::Report("vrserver check: process snapshot, previous method (us/check)", snapshot * 1e6, "us");
		PERF_CHECK(cached * 10.0 < snapshot);

		TerminateProcess(child.hProcess, 0);
		WaitForSingleObject(child.hProcess, 10000);
		PERF_CHECK(!watch.Running());
		CloseHandle(child.hThread);
		CloseHandle(child.hProcess);
		watch.Release();
		DeleteFileW(childPath.c_str());
	}

	protocol::Request TransformRequest(uint32_t id, double y)
	{
		protocol::Request request(protocol::RequestSetDeviceTransform);
		std::memset(&request.setDeviceTransform, 0xCD, sizeof request.setDeviceTransform);
		request.setDeviceTransform = {id, true, vr::HmdVector3d_t{0.1, y, 0.3}, vr::HmdQuaternion_t{1, 0, 0, 0}, 1.0};
		return request;
	}

	void TestDriverRequestCache()
	{
		spacecal::DriverRequestCache cache;
		const protocol::Request transform = TransformRequest(3, 0.2);
		PERF_CHECK(cache.ShouldSend(transform));
		PERF_CHECK(!cache.ShouldSend(transform));

		protocol::Request clean(protocol::RequestSetDeviceTransform);
		clean.setDeviceTransform = {3, true, vr::HmdVector3d_t{0.1, 0.2, 0.3}, vr::HmdQuaternion_t{1, 0, 0, 0}, 1.0};
		PERF_CHECK(!cache.ShouldSend(clean));

		const protocol::Request moved = TransformRequest(3, 0.25);
		PERF_CHECK(cache.ShouldSend(moved));
		PERF_CHECK(!cache.ShouldSend(moved));
		PERF_CHECK(cache.ShouldSend(TransformRequest(4, 0.25)));

		protocol::Request lerped = moved;
		lerped.setDeviceTransform.lerp = true;
		PERF_CHECK(cache.ShouldSend(lerped));
		protocol::Request quashed = lerped;
		quashed.setDeviceTransform.quash = true;
		PERF_CHECK(cache.ShouldSend(quashed));
		protocol::Request smoothed = quashed;
		smoothed.setDeviceTransform.smooth = true;
		PERF_CHECK(cache.ShouldSend(smoothed));
		protocol::Request scaled = smoothed;
		scaled.setDeviceTransform.scale = 1.01;
		PERF_CHECK(cache.ShouldSend(scaled));
		protocol::Request turned = scaled;
		turned.setDeviceTransform.rotation.y = 0.1;
		PERF_CHECK(cache.ShouldSend(turned));
		protocol::Request disabled = turned;
		disabled.setDeviceTransform.enabled = false;
		PERF_CHECK(cache.ShouldSend(disabled));

		protocol::Request alignment(protocol::RequestSetAlignmentSpeedParams);
		alignment.setAlignmentSpeedParams.align_speed_large = 2.0;
		PERF_CHECK(cache.ShouldSend(alignment));
		PERF_CHECK(!cache.ShouldSend(alignment));
		alignment.setAlignmentSpeedParams.thr_rot_tiny = 0.01;
		PERF_CHECK(cache.ShouldSend(alignment));

		protocol::Request smoothing(protocol::RequestSetSmoothingParams);
		smoothing.setSmoothingParams.strength = 40;
		PERF_CHECK(cache.ShouldSend(smoothing));
		PERF_CHECK(!cache.ShouldSend(smoothing));
		smoothing.setSmoothingParams.strength = 41;
		PERF_CHECK(cache.ShouldSend(smoothing));

		protocol::Request fingers(protocol::RequestSetFingerSmoothing);
		fingers.setFingerSmoothing = protocol::FingerSmoothingConfig{};
		fingers.setFingerSmoothing.fingerMask = protocol::kAllFingersMask;
		PERF_CHECK(cache.ShouldSend(fingers));
		PERF_CHECK(!cache.ShouldSend(fingers));
		fingers.setFingerSmoothing.perFinger[7] = 20;
		PERF_CHECK(cache.ShouldSend(fingers));
		fingers.setFingerSmoothing.fingerMask = 0x01FF;
		PERF_CHECK(cache.ShouldSend(fingers));

		protocol::Request sticks(protocol::RequestSetStickSmoothing);
		sticks.setStickSmoothing = protocol::StickSmoothingConfig{{{400, 150, 100, 0}, {}}};
		PERF_CHECK(cache.ShouldSend(sticks));
		PERF_CHECK(!cache.ShouldSend(sticks));
		sticks.setStickSmoothing.sticks[1].strength = 5;
		PERF_CHECK(cache.ShouldSend(sticks));
		sticks.setStickSmoothing.sticks[0].releaseMs = 900;
		PERF_CHECK(cache.ShouldSend(sticks));
		PERF_CHECK(!cache.ShouldSend(sticks));

		protocol::Request stats(protocol::RequestGetSmoothingStats);
		PERF_CHECK(cache.ShouldSend(stats));
		PERF_CHECK(cache.ShouldSend(stats));

		cache.Clear();
		PERF_CHECK(cache.ShouldSend(transform));

		std::vector<protocol::Request> profile = {alignment, smoothing, fingers, sticks};
		profile.reserve(profile.size() + 16);
		for (uint32_t id = 0; id < 16; ++id)
			profile.push_back(TransformRequest(id, 0.01 * id));
		cache.Clear();
		int firstPass = 0;
		for (const auto& request : profile)
			firstPass += cache.ShouldSend(request) ? 1 : 0;
		int repeats = 0;
		const auto heap = perf::MeasureHeap([&] {
			for (int second = 1; second < 600; ++second) {
				for (const auto& request : profile)
					repeats += cache.ShouldSend(request) ? 1 : 0;
			}
		});
		PERF_CHECK(firstPass == (int)profile.size());
		perf::Budget("idle profile re-apply x600: driver round trips after the first", (double)repeats, 0.0, "sends");
		perf::Budget("idle profile re-apply x600: heap allocations", (double)heap.allocations, 0.0, "allocs");
	}

	void TestRegistryWriteFilter()
	{
		spacecal::LastWriteFilter filter;
		PERF_CHECK(!filter.Unchanged("Config", ""));
		PERF_CHECK(!filter.Unchanged("Config", "{}"));
		filter.Record("Config", "{}");
		PERF_CHECK(filter.Unchanged("Config", "{}"));
		PERF_CHECK(!filter.Unchanged("Config", ""));
		PERF_CHECK(!filter.Unchanged("Config", "{\"x\":1}"));
		PERF_CHECK(!filter.Unchanged("Updates", "{}"));
		filter.Record("Config", "");
		PERF_CHECK(filter.Unchanged("Config", ""));
		PERF_CHECK(!filter.Unchanged("Config", "{}"));
	}

}

int main(int argc, char** argv)
{
	if (argc > 1 && std::strcmp(argv[1], "--sleep-child") == 0) {
		Sleep(60000);
		return 0;
	}

	PERF_CHECK(perf::InstallHeapMeter());

	TestSolverAccuracy();
	TestSolverMatchesPreviousImplementation();
	TestSolverCost();
	TestLoopPacing();
	TestProcessWatch();
	TestDriverRequestCache();
	TestRegistryWriteFilter();

	perf::RemoveHeapMeter();
	MH_Uninitialize();
	return perf::Finish("perf_overlay_tests");
}
