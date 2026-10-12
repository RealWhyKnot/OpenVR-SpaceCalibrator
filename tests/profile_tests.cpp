#include <sstream>

#include "Configuration.cpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <random>
#include <string>
#include <system_error>
#include <typeinfo>
#include <vector>

namespace {

	namespace previous {

		picojson::array FloatArray(const float* buf, size_t numFloats)
		{
			picojson::array arr;

			for (int i = 0; i < numFloats; i++) {
				arr.push_back(picojson::value(double(buf[i])));
			}

			return arr;
		}

		void VisitAlignmentParams(CalibrationContext& ctx, std::function<void(const char*, double&)> MapParam)
		{
			MapParam("align_speed_tiny", ctx.alignmentSpeedParams.align_speed_tiny);
			MapParam("align_speed_small", ctx.alignmentSpeedParams.align_speed_small);
			MapParam("align_speed_large", ctx.alignmentSpeedParams.align_speed_large);
			MapParam("thr_trans_tiny", ctx.alignmentSpeedParams.thr_trans_tiny);
			MapParam("thr_trans_small", ctx.alignmentSpeedParams.thr_trans_small);
			MapParam("thr_trans_large", ctx.alignmentSpeedParams.thr_trans_large);
			MapParam("thr_rot_tiny", ctx.alignmentSpeedParams.thr_rot_tiny);
			MapParam("thr_rot_small", ctx.alignmentSpeedParams.thr_rot_small);
			MapParam("thr_rot_large", ctx.alignmentSpeedParams.thr_rot_large);

			double tmp = ctx.continuousCalibrationThreshold;
			MapParam("continuousCalibrationThreshold", tmp);
			ctx.continuousCalibrationThreshold = (float)tmp;
		}

		picojson::object SaveAlignmentParams(CalibrationContext& ctx)
		{
			picojson::object obj;

			previous::VisitAlignmentParams(ctx, [&](auto name, auto& param) { obj[name].set<double>(param); });

			return obj;
		}

		picojson::object SaveSmoothingParams(CalibrationContext& ctx)
		{
			picojson::object obj;

			const double strength = ctx.smoothingParams.strength;
			obj["strength"].set<double>(strength);
			obj["smooth_controllers"].set<bool>(ctx.smoothControllers);

			picojson::object fingers;
			const double fingerStrength = ctx.fingerSmoothing.strength;
			fingers["strength"].set<double>(fingerStrength);
			const double mask = ctx.fingerSmoothing.fingerMask;
			fingers["mask"].set<double>(mask);
			picojson::array perFinger;
			for (int i = 0; i < 10; ++i) {
				perFinger.push_back(picojson::value((double)ctx.fingerSmoothing.perFinger[i]));
			}
			fingers["per_finger"].set<picojson::array>(perFinger);
			obj["fingers"].set<picojson::object>(fingers);

			picojson::object sticks;
			sticks["left"] = spacecal::stick_profile::Save(ctx.stickSmoothing.sticks[0], ctx.stickOn[0]);
			sticks["right"] = spacecal::stick_profile::Save(ctx.stickSmoothing.sticks[1], ctx.stickOn[1]);
			obj["sticks"].set<picojson::object>(sticks);

			return obj;
		}

		void WriteStandby(StandbyDevice& device, picojson::value& value)
		{
			auto obj = picojson::object();

			obj["tracking_system"].set<std::string>(device.trackingSystem);
			obj["model"].set<std::string>(device.model);
			obj["serial"].set<std::string>(device.serial);

			value.set<picojson::object>(obj);
		}

		void WriteProfile(CalibrationContext& ctx, std::ostream& out)
		{
			if (!ctx.validProfile) {
				return;
			}

			picojson::object profile;
			profile["alignment_params"].set<picojson::object>(previous::SaveAlignmentParams(ctx));
			profile["smoothing"].set<picojson::object>(previous::SaveSmoothingParams(ctx));

			profile["reference_tracking_system"].set<std::string>(ctx.referenceTrackingSystem);
			profile["target_tracking_system"].set<std::string>(ctx.targetTrackingSystem);
			profile["roll"].set<double>(ctx.calibratedRotation(0));
			profile["yaw"].set<double>(ctx.calibratedRotation(1));
			profile["pitch"].set<double>(ctx.calibratedRotation(2));
			profile["x"].set<double>(ctx.calibratedTranslation(0));
			profile["y"].set<double>(ctx.calibratedTranslation(1));
			profile["z"].set<double>(ctx.calibratedTranslation(2));
			profile["scale"].set<double>(ctx.calibratedScale);
			previous::WriteStandby(ctx.referenceStandby, profile["reference_device"]);
			previous::WriteStandby(ctx.targetStandby, profile["target_device"]);
			bool isInContinuousCalibrationMode =
			    ctx.state == CalibrationState::Continuous || ctx.state == CalibrationState::ContinuousStandby;
			profile["autostart_continuous_calibration"].set<bool>(isInContinuousCalibrationMode);
			profile["quash_target_in_continuous"].set<bool>(ctx.quashTargetInContinuous);
			profile["require_trigger_press_to_apply"].set<bool>(ctx.requireTriggerPressToApply);
			profile["ignore_outliers"].set<bool>(ctx.ignoreOutliers);
			profile["continuous_calibration_target_offset_x"].set<double>(ctx.continuousCalibrationOffset(0));
			profile["continuous_calibration_target_offset_y"].set<double>(ctx.continuousCalibrationOffset(1));
			profile["continuous_calibration_target_offset_z"].set<double>(ctx.continuousCalibrationOffset(2));
			profile["static_calibration"].set<bool>(ctx.enableStaticRecalibration);
			double jitterThreshold = (double)ctx.jitterThreshold;
			profile["jitter_threshold"].set<double>(jitterThreshold);
			double maxRelErrorThresTmp = (double)ctx.maxRelativeErrorThreshold;
			profile["max_relative_error_threshold"].set<double>(maxRelErrorThresTmp);

			double speed = (int)ctx.calibrationSpeed;
			profile["calibration_speed"].set<double>(speed);

			if (ctx.chaperone.valid) {
				picojson::object chaperone;
				chaperone["auto_apply"].set<bool>(ctx.chaperone.autoApply);
				chaperone["play_space_size"].set<picojson::array>(previous::FloatArray(ctx.chaperone.playSpaceSize.v, 2));

				chaperone["standing_center"].set<picojson::array>(
				    previous::FloatArray((float*)ctx.chaperone.standingCenter.m, sizeof(ctx.chaperone.standingCenter.m) / sizeof(float)));

				chaperone["geometry"].set<picojson::array>(
				    previous::FloatArray((float*)ctx.chaperone.geometry.data(),
				                         sizeof(ctx.chaperone.geometry[0]) / sizeof(float) * ctx.chaperone.geometry.size()));

				profile["chaperone"].set<picojson::object>(chaperone);
			}

			Eigen::Vector3d refToTragetRoation = ctx.refToTargetPose.rotation().eulerAngles(0, 1, 2);
			Eigen::Vector3d refToTargetTranslation = ctx.refToTargetPose.translation();
			picojson::object refToTarget;
			refToTarget["x"].set<double>(refToTargetTranslation(0));
			refToTarget["y"].set<double>(refToTargetTranslation(1));
			refToTarget["z"].set<double>(refToTargetTranslation(2));
			refToTarget["roll"].set<double>(refToTragetRoation(0));
			refToTarget["yaw"].set<double>(refToTragetRoation(1));
			refToTarget["pitch"].set<double>(refToTragetRoation(2));
			profile["relative_pos_calibrated"].set<bool>(ctx.relativePosCalibrated);
			profile["lock_relative_position"].set<bool>(ctx.lockRelativePosition);
			profile["relative_transform"].set<picojson::object>(refToTarget);

			picojson::value profileV;
			profileV.set<picojson::object>(profile);

			picojson::array profiles;
			profiles.push_back(profileV);

			picojson::value profilesV;
			profilesV.set<picojson::array>(profiles);

			out << profilesV.serialize(true);
		}

	}

	int failures = 0;

#define CHECK(cond)                                                                                                                        \
	do {                                                                                                                                   \
		if (!(cond)) {                                                                                                                     \
			std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);                                                                     \
			++failures;                                                                                                                    \
		}                                                                                                                                  \
	} while (0)

	enum class Outcome
	{
		Loaded,
		Rejected,
		Escaped
	};

	std::unique_ptr<CalibrationContext> Fresh()
	{
		auto ctx = std::make_unique<CalibrationContext>();
		ctx->calibratedRotation.setZero();
		ctx->calibratedTranslation.setZero();
		return ctx;
	}

	std::string Write(CalibrationContext& ctx)
	{
		std::stringstream out;
		WriteProfile(ctx, out);
		return out.str();
	}

	std::string WritePrevious(CalibrationContext& ctx)
	{
		std::stringstream out;
		previous::WriteProfile(ctx, out);
		return out.str();
	}

	bool NumbersClose(const picojson::value& a, const picojson::value& b, double tolerance, double& worst)
	{
		if (a.is<double>() && b.is<double>()) {
			const double drift = std::abs(a.get<double>() - b.get<double>());
			worst = std::max(worst, drift);
			return drift < tolerance;
		}
		if (a.is<picojson::array>() && b.is<picojson::array>()) {
			const picojson::array& x = a.get<picojson::array>();
			const picojson::array& y = b.get<picojson::array>();
			if (x.size() != y.size()) return false;
			bool close = true;
			for (size_t i = 0; i < x.size(); ++i) {
				close = NumbersClose(x[i], y[i], tolerance, worst) && close;
			}
			return close;
		}
		if (a.is<picojson::object>() && b.is<picojson::object>()) {
			const picojson::object& x = a.get<picojson::object>();
			const picojson::object& y = b.get<picojson::object>();
			if (x.size() != y.size()) return false;
			bool close = true;
			for (const auto& [key, value] : x) {
				const auto other = y.find(key);
				if (other == y.end()) return false;
				close = NumbersClose(value, other->second, tolerance, worst) && close;
			}
			return close;
		}
		return a == b;
	}

	Outcome Parse(CalibrationContext& ctx, const std::string& text)
	{
		ctx.validProfile = false;
		std::stringstream in(text);
		try {
			ParseProfile(ctx, in);
			return Outcome::Loaded;
		}
		catch (const std::runtime_error&) {
			return Outcome::Rejected;
		}
		catch (...) {
			return Outcome::Escaped;
		}
	}

	bool IsValidJson(const std::string& text)
	{
		picojson::value v;
		return picojson::parse(v, text).empty();
	}

	void PrintLineDiff(const char* label, const std::string& a, const std::string& b)
	{
		std::stringstream sa(a), sb(b);
		std::string la, lb;
		while (true) {
			const bool moreA = (bool)std::getline(sa, la);
			const bool moreB = (bool)std::getline(sb, lb);
			if (!moreA && !moreB) return;
			if (la != lb) std::printf("  %s: [%s] vs [%s]\n", label, la.c_str(), lb.c_str());
		}
	}

	bool SameBits(double a, double b)
	{
		return std::memcmp(&a, &b, sizeof a) == 0;
	}

	bool SameBits(const Eigen::Vector3d& a, const Eigen::Vector3d& b)
	{
		return SameBits(a(0), b(0)) && SameBits(a(1), b(1)) && SameBits(a(2), b(2));
	}

	void Fill(CalibrationContext& ctx)
	{
		ctx.validProfile = true;
		ctx.referenceTrackingSystem = "lighthouse";
		ctx.targetTrackingSystem = "oculus";
		ctx.calibratedRotation = Eigen::Vector3d(12.345678901234567, -0.1, 179.99999999999997);
		ctx.calibratedTranslation = Eigen::Vector3d(-123.456789, 0.000123, 1e-300);
		ctx.calibratedScale = 1.0000001;
		ctx.referenceStandby = {"lighthouse", "VIVE Tracker Pro MV", "LHR-1234ABCD"};
		ctx.targetStandby = {"oculus", "Quest 3 J\xC3\xB6rg", "1WMHH\xE4\xB8\xAD\xE6\x96\x87"};
		ctx.state = CalibrationState::ContinuousStandby;
		ctx.quashTargetInContinuous = true;
		ctx.requireTriggerPressToApply = true;
		ctx.ignoreOutliers = true;
		ctx.continuousCalibrationOffset = Eigen::Vector3d(0.01, -0.02, 0.03);
		ctx.enableStaticRecalibration = true;
		ctx.jitterThreshold = 2.5f;
		ctx.maxRelativeErrorThreshold = 0.0125f;
		ctx.calibrationSpeed = CalibrationContext::SLOW;
		ctx.continuousCalibrationThreshold = 2.75f;
		ctx.alignmentSpeedParams.thr_trans_tiny = 0.0005;
		ctx.alignmentSpeedParams.thr_trans_small = 0.001;
		ctx.alignmentSpeedParams.thr_trans_large = 0.03125;
		ctx.alignmentSpeedParams.thr_rot_tiny = 0.0078125;
		ctx.alignmentSpeedParams.thr_rot_small = 0.015625;
		ctx.alignmentSpeedParams.thr_rot_large = 0.125;
		ctx.alignmentSpeedParams.align_speed_tiny = 0.5;
		ctx.alignmentSpeedParams.align_speed_small = 1.0;
		ctx.alignmentSpeedParams.align_speed_large = 1.75;
		ctx.smoothingParams.strength = 37;
		ctx.smoothControllers = true;
		ctx.fingerSmoothing.strength = 20;
		ctx.fingerSmoothing.fingerMask = 0x01F5;
		for (int i = 0; i < 10; ++i) {
			ctx.fingerSmoothing.perFinger[i] = (uint8_t)(i * 7);
		}
		ctx.stickSmoothing.sticks[0].pushMs = 1200;
		ctx.stickSmoothing.sticks[0].releaseMs = 900;
		ctx.stickSmoothing.sticks[0].strength = 80;
		ctx.stickSmoothing.sticks[0].delayMs = 150;
		ctx.stickOn[0] = true;
		ctx.stickOn[1] = false;
		ctx.chaperone.valid = true;
		ctx.chaperone.autoApply = false;
		ctx.chaperone.geometry.resize(3);
		float* g = (float*)ctx.chaperone.geometry.data();
		for (int i = 0; i < 36; ++i) {
			g[i] = (i % 2 ? -1.0f : 1.0f) * (0.1f + i * 0.3333333f);
		}
		g[5] = std::numeric_limits<float>::denorm_min();
		g[6] = 3.4e38f;
		for (int r = 0; r < 3; ++r) {
			for (int c = 0; c < 4; ++c) {
				ctx.chaperone.standingCenter.m[r][c] = (float)(r * 4 + c) * 0.7071f;
			}
		}
		ctx.chaperone.playSpaceSize.v[0] = 2.5f;
		ctx.chaperone.playSpaceSize.v[1] = 3.25f;
		ctx.relativePosCalibrated = true;
		ctx.lockRelativePosition = true;
		ctx.refToTargetPose = Eigen::AffineCompact3d::Identity();
		ctx.refToTargetPose.linear() =
		    (Eigen::AngleAxisd(0.1, Eigen::Vector3d::UnitX()) * Eigen::AngleAxisd(-0.2, Eigen::Vector3d::UnitY()) *
		     Eigen::AngleAxisd(0.3, Eigen::Vector3d::UnitZ()))
		        .toRotationMatrix();
		ctx.refToTargetPose.translation() = Eigen::Vector3d(0.05, -0.1, 0.2);
	}

	std::string ReplaceOnce(std::string text, const std::string& from, const std::string& to)
	{
		const size_t pos = text.find(from);
		if (pos != std::string::npos) text.replace(pos, from.size(), to);
		return text;
	}

	std::string SetLast(std::string text, const std::string& key, const std::string& value)
	{
		const std::string head = "\"" + key + "\": ";
		const size_t pos = text.rfind(head);
		if (pos == std::string::npos) return text;
		const size_t begin = pos + head.size();
		const size_t end = text.find_first_of(",\r\n}]", begin);
		text.replace(begin, end - begin, value);
		return text;
	}

	std::string SetAfter(std::string text, const std::string& anchor, const std::string& key, const std::string& value)
	{
		const size_t start = text.find(anchor);
		if (start == std::string::npos) return text;
		const std::string head = "\"" + key + "\": ";
		const size_t pos = text.find(head, start);
		if (pos == std::string::npos) return text;
		const size_t begin = pos + head.size();
		const size_t end = text.find_first_of(",\r\n}]", begin);
		text.replace(begin, end - begin, value);
		return text;
	}

	const char* kProfile2020 = R"([
  {
    "calibration_speed": 1,
    "chaperone": {
      "auto_apply": true,
      "geometry": [1,0,1, 1,2.4300000667572021,1, -1,2.4300000667572021,1, -1,0,1],
      "play_space_size": [2,2],
      "standing_center": [1,0,0,0.25, 0,1,0,0, 0,0,1,-0.5]
    },
    "pitch": 0.5,
    "reference_tracking_system": "lighthouse",
    "roll": -1.25,
    "target_tracking_system": "oculus",
    "x": 12.5,
    "y": -3,
    "yaw": 91.75,
    "z": 0.125
  }
])";

	const char* kProfile2023 = R"([
  {
    "alignment_params": {
      "align_speed_large": 2, "align_speed_small": 1, "align_speed_tiny": 1,
      "continuousCalibrationThreshold": 1.5,
      "thr_rot_large": 0.087266462599716474, "thr_rot_small": 0.0087266462599716474, "thr_rot_tiny": 0.0085521133347722145,
      "thr_trans_large": 0.02, "thr_trans_small": 0.001, "thr_trans_tiny": 0.00097999999999999997
    },
    "autostart_continuous_calibration": true,
    "calibration_speed": 1,
    "pitch": 0.5,
    "quash_target_in_continuous": false,
    "reference_device": {"model": "Quest 3", "serial": "1WMHH000", "tracking_system": "oculus"},
    "reference_tracking_system": "oculus",
    "relative_pos_calibrated": true,
    "relative_transform": {"pitch": 0, "roll": 0, "x": 0.01, "y": 0.02, "yaw": 0, "z": 0.03},
    "roll": -1.25,
    "scale": 1,
    "target_device": {"model": "VIVE Tracker 3.0", "serial": "LHR-0000", "tracking_system": "lighthouse"},
    "target_tracking_system": "lighthouse",
    "x": 12.5,
    "y": -3,
    "yaw": 91.75,
    "z": 0.125
  }
])";

	void TestFullRoundTripKeepsEveryField()
	{
		auto saved = Fresh();
		Fill(*saved);
		const std::string text = Write(*saved);
		CHECK(!text.empty());
		CHECK(IsValidJson(text));

		auto loaded = Fresh();
		CHECK(Parse(*loaded, text) == Outcome::Loaded);
		CHECK(loaded->validProfile);
		CHECK(loaded->referenceTrackingSystem == saved->referenceTrackingSystem);
		CHECK(loaded->targetTrackingSystem == saved->targetTrackingSystem);
		CHECK(SameBits(loaded->calibratedRotation, saved->calibratedRotation));
		CHECK(SameBits(loaded->calibratedTranslation, saved->calibratedTranslation));
		CHECK(SameBits(loaded->calibratedScale, saved->calibratedScale));
		CHECK(SameBits(loaded->continuousCalibrationOffset, saved->continuousCalibrationOffset));
		CHECK(loaded->referenceStandby.model == saved->referenceStandby.model);
		CHECK(loaded->referenceStandby.serial == saved->referenceStandby.serial);
		CHECK(loaded->targetStandby.trackingSystem == saved->targetStandby.trackingSystem);
		CHECK(loaded->targetStandby.model == saved->targetStandby.model);
		CHECK(loaded->targetStandby.serial == saved->targetStandby.serial);
		CHECK(loaded->state == CalibrationState::ContinuousStandby);
		CHECK(loaded->quashTargetInContinuous && loaded->requireTriggerPressToApply && loaded->ignoreOutliers);
		CHECK(loaded->enableStaticRecalibration);
		CHECK(loaded->jitterThreshold == saved->jitterThreshold);
		CHECK(loaded->maxRelativeErrorThreshold == saved->maxRelativeErrorThreshold);
		CHECK(loaded->calibrationSpeed == CalibrationContext::SLOW);
		CHECK(loaded->continuousCalibrationThreshold == saved->continuousCalibrationThreshold);
		CHECK(std::memcmp(&loaded->alignmentSpeedParams, &saved->alignmentSpeedParams, sizeof saved->alignmentSpeedParams) == 0);
		CHECK(loaded->smoothingParams.strength == 37);
		CHECK(loaded->smoothControllers);
		CHECK(loaded->fingerSmoothing.strength == 20);
		CHECK(loaded->fingerSmoothing.fingerMask == 0x01F5);
		CHECK(std::memcmp(loaded->fingerSmoothing.perFinger, saved->fingerSmoothing.perFinger, 10) == 0);
		CHECK(std::memcmp(&loaded->stickSmoothing.sticks[0], &saved->stickSmoothing.sticks[0], sizeof(protocol::StickRampConfig)) == 0);
		CHECK(loaded->stickOn[0] && !loaded->stickOn[1]);
		CHECK(loaded->chaperone.valid && !loaded->chaperone.autoApply);
		CHECK(loaded->chaperone.geometry.size() == 3);
		CHECK(std::memcmp(loaded->chaperone.geometry.data(), saved->chaperone.geometry.data(), 3 * sizeof(vr::HmdQuad_t)) == 0);
		CHECK(std::memcmp(&loaded->chaperone.standingCenter, &saved->chaperone.standingCenter, sizeof(vr::HmdMatrix34_t)) == 0);
		CHECK(std::memcmp(&loaded->chaperone.playSpaceSize, &saved->chaperone.playSpaceSize, sizeof(vr::HmdVector2_t)) == 0);
		CHECK(loaded->relativePosCalibrated && loaded->lockRelativePosition);
		CHECK((loaded->refToTargetPose.matrix() - saved->refToTargetPose.matrix()).cwiseAbs().maxCoeff() < 1e-12);
	}

	void TestSaveLoadSaveIsBitStable()
	{
		auto first = Fresh();
		Fill(*first);
		std::vector<std::string> texts = {Write(*first)};
		for (int cycle = 0; cycle < 5; ++cycle) {
			auto next = Fresh();
			CHECK(Parse(*next, texts.back()) == Outcome::Loaded);
			CHECK(next->validProfile);
			texts.push_back(Write(*next));
		}
		int fixedAfter = -1;
		for (size_t i = 1; i < texts.size(); ++i) {
			if (texts[i] == texts[i - 1]) {
				fixedAfter = (int)i - 1;
				break;
			}
		}
		CHECK(fixedAfter >= 0 && fixedAfter <= 4);
		picojson::value original;
		CHECK(picojson::parse(original, texts[0]).empty());
		double worst = 0.0;
		for (size_t i = 1; i < texts.size(); ++i) {
			picojson::value again;
			CHECK(picojson::parse(again, texts[i]).empty());
			const bool close = NumbersClose(original, again, 1e-12, worst);
			CHECK(close);
			if (!close) PrintLineDiff("save 1 vs later save", texts[0], texts[i]);
		}
		std::printf("  save/load reaches a fixed point after %d cycle(s); worst numeric drift %.3g\n", fixedAfter, worst);
	}

	void CheckSameAsPreviousWriter(const char* label, CalibrationContext& ctx)
	{
		const std::string before = WritePrevious(ctx);
		const std::string now = Write(ctx);
		const bool same = !before.empty() && now == before;
		CHECK(same);
		if (!same) {
			std::printf("  %s: writer output changed\n", label);
			PrintLineDiff(label, before, now);
		}
	}

	void Randomize(CalibrationContext& ctx, std::mt19937& rng)
	{
		std::uniform_real_distribution<double> wide(-1000.0, 1000.0);
		std::uniform_real_distribution<double> unit(-1.0, 1.0);
		std::uniform_int_distribution<int> byte(0, 255);
		std::uniform_int_distribution<int> coin(0, 1);
		auto exponent = [&]() {
			return std::ldexp(unit(rng), std::uniform_int_distribution<int>(-60, 60)(rng));
		};
		ctx.calibratedRotation = Eigen::Vector3d(wide(rng), wide(rng), wide(rng));
		ctx.calibratedTranslation = Eigen::Vector3d(exponent(), exponent(), exponent());
		ctx.calibratedScale = 1.0 + unit(rng) * 0.01;
		ctx.state = coin(rng) ? CalibrationState::Continuous : (coin(rng) ? CalibrationState::None : CalibrationState::ContinuousStandby);
		ctx.quashTargetInContinuous = coin(rng);
		ctx.requireTriggerPressToApply = coin(rng);
		ctx.ignoreOutliers = coin(rng);
		ctx.enableStaticRecalibration = coin(rng);
		ctx.relativePosCalibrated = coin(rng);
		ctx.lockRelativePosition = coin(rng);
		ctx.smoothControllers = coin(rng);
		ctx.continuousCalibrationOffset = Eigen::Vector3d(unit(rng), unit(rng), unit(rng)) * 0.1;
		ctx.jitterThreshold = (float)(5.0 + 5.0 * unit(rng));
		ctx.maxRelativeErrorThreshold = (float)(0.01 + 0.01 * unit(rng));
		ctx.continuousCalibrationThreshold = (float)(2.0 + unit(rng));
		ctx.calibrationSpeed = (CalibrationContext::Speed)std::uniform_int_distribution<int>(0, 2)(rng);
		ctx.alignmentSpeedParams.thr_trans_tiny = exponent();
		ctx.alignmentSpeedParams.thr_trans_small = exponent();
		ctx.alignmentSpeedParams.thr_trans_large = exponent();
		ctx.alignmentSpeedParams.thr_rot_tiny = exponent();
		ctx.alignmentSpeedParams.thr_rot_small = exponent();
		ctx.alignmentSpeedParams.thr_rot_large = exponent();
		ctx.alignmentSpeedParams.align_speed_tiny = unit(rng);
		ctx.alignmentSpeedParams.align_speed_small = unit(rng);
		ctx.alignmentSpeedParams.align_speed_large = unit(rng);
		ctx.smoothingParams.strength = (uint8_t)(byte(rng) % 101);
		ctx.fingerSmoothing.strength = (uint8_t)(byte(rng) % 101);
		ctx.fingerSmoothing.fingerMask = (uint16_t)(byte(rng) * 4) & protocol::kAllFingersMask;
		for (int i = 0; i < 10; ++i) {
			ctx.fingerSmoothing.perFinger[i] = (uint8_t)(byte(rng) % 101);
		}
		ctx.chaperone.valid = coin(rng);
		ctx.chaperone.autoApply = coin(rng);
		ctx.chaperone.geometry.resize(std::uniform_int_distribution<int>(0, 20)(rng));
		float* g = (float*)ctx.chaperone.geometry.data();
		for (size_t i = 0; i < ctx.chaperone.geometry.size() * 12; ++i) {
			g[i] = (float)exponent();
		}
		ctx.chaperone.playSpaceSize.v[0] = (float)wide(rng);
		ctx.chaperone.playSpaceSize.v[1] = (float)wide(rng);
		for (int r = 0; r < 3; ++r) {
			for (int c = 0; c < 4; ++c) {
				ctx.chaperone.standingCenter.m[r][c] = (float)unit(rng);
			}
		}
		const Eigen::Vector3d axis = Eigen::Vector3d(unit(rng), unit(rng), unit(rng)).normalized();
		ctx.refToTargetPose = Eigen::AffineCompact3d::Identity();
		ctx.refToTargetPose.linear() = Eigen::AngleAxisd(unit(rng) * EIGEN_PI, axis).toRotationMatrix();
		ctx.refToTargetPose.translation() = Eigen::Vector3d(unit(rng), unit(rng), unit(rng));
	}

	void TestValidProfileBytesMatchPreviousWriter()
	{
		{
			auto ctx = Fresh();
			Fill(*ctx);
			CheckSameAsPreviousWriter("filled", *ctx);
		}
		{
			auto ctx = Fresh();
			Fill(*ctx);
			ctx->chaperone.valid = false;
			CheckSameAsPreviousWriter("no chaperone", *ctx);
		}
		{
			auto ctx = Fresh();
			Fill(*ctx);
			ctx->chaperone.geometry.clear();
			CheckSameAsPreviousWriter("chaperone without quads", *ctx);
		}
		{
			auto ctx = Fresh();
			Fill(*ctx);
			ctx->ResetConfig();
			CheckSameAsPreviousWriter("default settings", *ctx);
		}
		{
			auto ctx = Fresh();
			Fill(*ctx);
			ctx->state = CalibrationState::None;
			ctx->relativePosCalibrated = false;
			ctx->lockRelativePosition = false;
			ctx->refToTargetPose = Eigen::AffineCompact3d::Identity();
			CheckSameAsPreviousWriter("idle", *ctx);
		}
		{
			auto ctx = Fresh();
			Fill(*ctx);
			ctx->chaperone.geometry.resize(2000);
			float* g = (float*)ctx->chaperone.geometry.data();
			for (size_t i = 0; i < ctx->chaperone.geometry.size() * 12; ++i) {
				g[i] = std::ldexp((float)(i % 977) - 488.0f, (int)(i % 40) - 20);
			}
			CheckSameAsPreviousWriter("2000 quads", *ctx);
		}
		{
			auto ctx = Fresh();
			ctx->validProfile = true;
			ctx->referenceTrackingSystem = "lighthouse";
			ctx->targetTrackingSystem = "oculus";
			CheckSameAsPreviousWriter("fresh context", *ctx);
		}
		for (const char* fixture : {kProfile2020, kProfile2023}) {
			auto ctx = Fresh();
			CHECK(Parse(*ctx, fixture) == Outcome::Loaded);
			CheckSameAsPreviousWriter("old fixture", *ctx);
		}
		std::mt19937 rng(20261011);
		int checked = 0;
		for (int i = 0; i < 300; ++i) {
			auto ctx = Fresh();
			Fill(*ctx);
			Randomize(*ctx, rng);
			const int before = failures;
			CheckSameAsPreviousWriter("random", *ctx);
			++checked;
			if (failures != before) break;
		}
		std::printf("  writer output matches the previous writer for %d random valid profiles\n", checked);
	}

	void TestRandomProfilesOnlyDriftInTheLastBits()
	{
		std::mt19937 rng(99);
		double worst = 0.0;
		int drifted = 0;
		for (int i = 0; i < 100; ++i) {
			auto ctx = Fresh();
			Fill(*ctx);
			Randomize(*ctx, rng);
			ctx->chaperone.valid = ctx->chaperone.valid && !ctx->chaperone.geometry.empty();
			const std::string first = Write(*ctx);
			picojson::value original;
			CHECK(picojson::parse(original, first).empty());
			std::string text = first;
			for (int cycle = 0; cycle < 6; ++cycle) {
				auto next = Fresh();
				CHECK(Parse(*next, text) == Outcome::Loaded);
				CHECK(next->validProfile);
				text = Write(*next);
				picojson::value again;
				CHECK(picojson::parse(again, text).empty());
				if (!NumbersClose(original, again, 1e-12, worst)) ++drifted;
			}
		}
		CHECK(drifted == 0);
		std::printf("  100 random profiles over 6 save/load cycles: worst numeric drift %.3g\n", worst);
	}

	void TestDefaultAlignmentParamsSurviveALoad()
	{
		auto fresh = Fresh();
		Fill(*fresh);
		fresh->ResetConfig();
		const protocol::AlignmentSpeedParams before = fresh->alignmentSpeedParams;
		auto loaded = Fresh();
		CHECK(Parse(*loaded, Write(*fresh)) == Outcome::Loaded);
		const protocol::AlignmentSpeedParams& after = loaded->alignmentSpeedParams;
		CHECK(SameBits(after.thr_rot_tiny, before.thr_rot_tiny));
		CHECK(SameBits(after.thr_rot_small, before.thr_rot_small));
		CHECK(SameBits(after.thr_rot_large, before.thr_rot_large));
		CHECK(SameBits(after.thr_trans_tiny, before.thr_trans_tiny));
		CHECK(SameBits(after.thr_trans_small, before.thr_trans_small));
		CHECK(SameBits(after.thr_trans_large, before.thr_trans_large));
	}

	void TestEmptyAndWhitespaceAreRejected()
	{
		for (const char* text : {"", " ", "\r\n", "\xEF\xBB\xBF"}) {
			auto ctx = Fresh();
			CHECK(Parse(*ctx, text) == Outcome::Rejected);
			CHECK(!ctx->validProfile);
		}
	}

	void TestEveryTruncationIsRejectedCleanly()
	{
		auto saved = Fresh();
		Fill(*saved);
		const std::string text = Write(*saved);
		const size_t end = text.find_last_of(']');
		CHECK(end != std::string::npos);
		int escaped = 0;
		int loaded = 0;
		for (size_t len = 0; len < end; ++len) {
			auto ctx = Fresh();
			const Outcome outcome = Parse(*ctx, text.substr(0, len));
			if (outcome == Outcome::Escaped) ++escaped;
			if (outcome == Outcome::Loaded || ctx->validProfile) ++loaded;
		}
		CHECK(escaped == 0);
		CHECK(loaded == 0);
	}

	void TestWrongShapesAreRejected()
	{
		for (const char* text : {"{}", "[]", "[1]", "[null]", "\"profile\"", "[[]]", "nul", "[{\"reference_tracking_system\":5}]",
		                         "{\"reference_tracking_system\":\"lighthouse\"}"}) {
			auto ctx = Fresh();
			CHECK(Parse(*ctx, text) == Outcome::Rejected);
			CHECK(!ctx->validProfile);
		}
		for (const char* text : {"[{}]", "[{\"x\":1}]"}) {
			auto ctx = Fresh();
			CHECK(Parse(*ctx, text) == Outcome::Loaded);
			CHECK(!ctx->validProfile);
		}
		auto saved = Fresh();
		Fill(*saved);
		const std::string text = Write(*saved);
		const std::vector<std::string> broken = {
		    SetLast(text, "x", "\"-123.5\""),
		    SetLast(text, "roll", "null"),
		    SetLast(text, "target_tracking_system", "[]"),
		    SetLast(text, "yaw", "{}"),
		    SetLast(text, "scale", "\"1\""),
		    SetAfter(text, "\"relative_transform\"", "x", "\"0\""),
		    SetAfter(text, "\"relative_transform\"", "yaw", "null"),
		    SetLast(text, "thr_rot_tiny", "1e400"),
		};
		for (size_t i = 0; i < broken.size(); ++i) {
			CHECK(broken[i] != text);
			auto ctx = Fresh();
			const Outcome outcome = Parse(*ctx, broken[i]);
			CHECK(outcome == Outcome::Rejected);
			CHECK(!ctx->validProfile);
			CHECK(ctx->smoothingParams.strength == 0);
			CHECK(ctx->referenceTrackingSystem.empty());
			CHECK(!ctx->chaperone.valid);
			if (outcome != Outcome::Rejected) std::printf("  broken edit %zu -> outcome %d\n", i, (int)outcome);
		}
	}

	void TestBrokenSettingsKeepTheCalibration()
	{
		auto saved = Fresh();
		Fill(*saved);
		const std::string text = Write(*saved);
		{
			auto ctx = Fresh();
			CHECK(Parse(*ctx, SetLast(text, "continuous_calibration_target_offset_z", "true")) == Outcome::Loaded);
			CHECK(ctx->validProfile);
			CHECK(ctx->continuousCalibrationOffset == Eigen::Vector3d(0.01, -0.02, 0.0));
			CHECK(ctx->chaperone.valid);
		}
		{
			auto ctx = Fresh();
			CHECK(Parse(*ctx, SetLast(text, "jitter_threshold", "\"high\"")) == Outcome::Loaded);
			CHECK(ctx->validProfile);
			CHECK(ctx->jitterThreshold == 3.0f);
		}
		{
			auto ctx = Fresh();
			CHECK(Parse(*ctx, SetLast(text, "jitter_threshold", "-1e39")) == Outcome::Loaded);
			CHECK(ctx->validProfile);
			CHECK(ctx->jitterThreshold == 3.0f);
		}
		{
			auto ctx = Fresh();
			CHECK(Parse(*ctx, SetLast(text, "thr_rot_tiny", "\"fast\"")) == Outcome::Loaded);
			CHECK(ctx->validProfile);
			CHECK(SameBits(ctx->alignmentSpeedParams.thr_rot_tiny, Fresh()->alignmentSpeedParams.thr_rot_tiny));
			CHECK(SameBits(ctx->alignmentSpeedParams.thr_rot_small, saved->alignmentSpeedParams.thr_rot_small));
			CHECK(IsValidJson(Write(*ctx)));
		}
		{
			auto ctx = Fresh();
			CHECK(Parse(*ctx, SetLast(text, "calibration_speed", "-3")) == Outcome::Loaded);
			CHECK(ctx->calibrationSpeed == CalibrationContext::FAST);
			CHECK(Parse(*ctx, SetLast(text, "calibration_speed", "1e300")) == Outcome::Loaded);
			CHECK(ctx->calibrationSpeed == CalibrationContext::VERY_SLOW);
		}
		const std::vector<std::string> brokenChaperone = {
		    ReplaceOnce(text, "\"auto_apply\": false", "\"auto_apply\": 0"),
		    ReplaceOnce(text, "\"play_space_size\": [", "\"play_space_size\": [1, "),
		    ReplaceOnce(text, "\"geometry\": [", "\"geometry\": \"none\", \"old_geometry\": ["),
		    ReplaceOnce(text, "\"standing_center\": [", "\"standing_center\": [\"1\", "),
		    ReplaceOnce(text, "\"geometry\": [", "\"geometry\": [-1e39, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, "),
		    ReplaceOnce(text, "\"geometry\": [", "\"geometry\": [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 1e39, "),
		    ReplaceOnce(text, "\"play_space_size\": [", "\"play_space_size\": [-1e39, 1], \"old_size\": ["),
		};
		for (size_t i = 0; i < brokenChaperone.size(); ++i) {
			CHECK(brokenChaperone[i] != text);
			auto ctx = Fresh();
			const Outcome outcome = Parse(*ctx, brokenChaperone[i]);
			CHECK(outcome == Outcome::Loaded);
			CHECK(ctx->validProfile);
			CHECK(SameBits(ctx->calibratedTranslation, saved->calibratedTranslation));
			CHECK(!ctx->chaperone.valid);
			CHECK(IsValidJson(Write(*ctx)));
			if (outcome != Outcome::Loaded || ctx->chaperone.valid)
				std::printf("  broken chaperone edit %zu -> outcome %d, chaperone valid %d\n", i, (int)outcome, (int)ctx->chaperone.valid);
		}
	}

	void TestChaperoneWithoutQuadsLoadsAsInvalid()
	{
		auto saved = Fresh();
		Fill(*saved);
		saved->chaperone.geometry.clear();
		const std::string text = Write(*saved);
		CHECK(text.find("\"geometry\": []") != std::string::npos);
		auto ctx = Fresh();
		CHECK(Parse(*ctx, text) == Outcome::Loaded);
		CHECK(ctx->validProfile);
		CHECK(!ctx->chaperone.valid);
		CHECK(ctx->chaperone.geometry.empty());
		CHECK(ctx->chaperone.autoApply == saved->chaperone.autoApply);
	}

	void TestRejectedProfileIsCopiedToBackup()
	{
		auto saved = Fresh();
		Fill(*saved);
		const std::string valid = Write(*saved);
		std::vector<std::string> backups;
		const auto backup = [&](const std::string& raw) {
			backups.push_back(raw);
		};

		auto ctx = Fresh();
		LoadProfileText(*ctx, valid, backup);
		CHECK(ctx->validProfile);
		CHECK(backups.empty());

		auto cleared = Fresh();
		cleared->Clear();
		cleared->smoothingParams.strength = 40;
		ctx = Fresh();
		LoadProfileText(*ctx, Write(*cleared), backup);
		CHECK(!ctx->validProfile);
		CHECK(ctx->smoothingParams.strength == 40);
		CHECK(backups.empty());

		for (const std::string& broken : {valid.substr(0, valid.size() / 2), SetLast(valid, "x", "\"-123.5\""), std::string("[1]")}) {
			backups.clear();
			ctx = Fresh();
			LoadProfileText(*ctx, broken, backup);
			CHECK(!ctx->validProfile);
			CHECK(backups.size() == 1 && backups[0] == broken);
			CHECK(!Write(*ctx).empty());
		}

		backups.clear();
		ctx = Fresh();
		LoadProfileText(*ctx, "", backup);
		CHECK(!ctx->validProfile);
		CHECK(backups.empty());
		CHECK(!Write(*ctx).empty());
	}

	void TestUnknownFieldsAreIgnored()
	{
		auto saved = Fresh();
		Fill(*saved);
		const std::string text =
		    ReplaceOnce(Write(*saved), "\"alignment_params\"", "\"from_the_future\": {\"a\": [1, 2, {\"b\": null}]}, \"alignment_params\"");
		auto ctx = Fresh();
		CHECK(Parse(*ctx, text) == Outcome::Loaded);
		CHECK(ctx->validProfile);
		CHECK(SameBits(ctx->calibratedTranslation, saved->calibratedTranslation));
	}

	void TestMissingOptionalFieldsUseDefaults()
	{
		const std::string text = R"([{"reference_tracking_system":"lighthouse","target_tracking_system":"oculus",
			"roll":1,"yaw":2,"pitch":3,"x":4,"y":5,"z":6,
			"continuous_calibration_target_offset_x":0,"continuous_calibration_target_offset_y":0,"continuous_calibration_target_offset_z":0}])";
		auto ctx = Fresh();
		ctx->calibratedScale = 7.0;
		ctx->smoothingParams.strength = 90;
		CHECK(Parse(*ctx, text) == Outcome::Loaded);
		CHECK(ctx->validProfile);
		CHECK(ctx->calibratedScale == 1.0);
		CHECK(ctx->maxRelativeErrorThreshold == 0.005f);
		CHECK(ctx->smoothingParams.strength == 0);
		CHECK(ctx->fingerSmoothing.fingerMask == protocol::kAllFingersMask);
		CHECK(!ctx->chaperone.valid);
		CHECK(ctx->state == CalibrationState::None);
		CHECK(ctx->calibratedRotation == Eigen::Vector3d(1, 2, 3));
		CHECK(ctx->calibratedTranslation == Eigen::Vector3d(4, 5, 6));
	}

	void TestRegistryProfileFrom2020Loads()
	{
		auto ctx = Fresh();
		const Outcome outcome = Parse(*ctx, kProfile2020);
		CHECK(outcome == Outcome::Loaded);
		CHECK(ctx->validProfile);
		CHECK(ctx->calibratedRotation == Eigen::Vector3d(-1.25, 91.75, 0.5));
		CHECK(ctx->calibratedTranslation == Eigen::Vector3d(12.5, -3, 0.125));
		CHECK(ctx->chaperone.valid && ctx->chaperone.geometry.size() == 1);
		CHECK(ctx->continuousCalibrationOffset == Eigen::Vector3d::Zero());
		CHECK(ctx->calibratedScale == 1.0);
		CHECK(ctx->jitterThreshold == 3.0f);
		CHECK(ctx->calibrationSpeed == CalibrationContext::SLOW);
		CHECK(IsValidJson(Write(*ctx)));
	}

	void TestRegistryProfileFrom2023Loads()
	{
		auto ctx = Fresh();
		const Outcome outcome = Parse(*ctx, kProfile2023);
		CHECK(outcome == Outcome::Loaded);
		CHECK(ctx->validProfile);
		CHECK(ctx->calibratedTranslation == Eigen::Vector3d(12.5, -3, 0.125));
		CHECK(ctx->state == CalibrationState::ContinuousStandby);
		CHECK(ctx->relativePosCalibrated);
		CHECK(ctx->refToTargetPose.translation() == Eigen::Vector3d(0.01, 0.02, 0.03));
	}

	void TestSmoothingSettingsPersistWithoutACalibration()
	{
		auto ctx = Fresh();
		ctx->Clear();
		ctx->smoothingParams.strength = 40;
		ctx->fingerSmoothing.strength = 25;
		ctx->stickOn[0] = true;
		const std::string text = Write(*ctx);
		CHECK(!text.empty());
		auto loaded = Fresh();
		Parse(*loaded, text);
		CHECK(loaded->smoothingParams.strength == 40);
		CHECK(loaded->fingerSmoothing.strength == 25);
		CHECK(loaded->stickOn[0]);
		CHECK(!loaded->validProfile);
	}

	void TestEverySettingSurvivesWithoutACalibration()
	{
		auto filled = Fresh();
		Fill(*filled);
		auto ctx = Fresh();
		ctx->Clear();
		ctx->alignmentSpeedParams = filled->alignmentSpeedParams;
		ctx->continuousCalibrationThreshold = filled->continuousCalibrationThreshold;
		ctx->smoothingParams = filled->smoothingParams;
		ctx->smoothControllers = true;
		ctx->fingerSmoothing = filled->fingerSmoothing;
		ctx->stickSmoothing = filled->stickSmoothing;
		ctx->stickOn[0] = true;
		ctx->quashTargetInContinuous = true;
		ctx->requireTriggerPressToApply = true;
		ctx->ignoreOutliers = true;
		ctx->continuousCalibrationOffset = filled->continuousCalibrationOffset;
		ctx->enableStaticRecalibration = true;
		ctx->jitterThreshold = filled->jitterThreshold;
		ctx->maxRelativeErrorThreshold = filled->maxRelativeErrorThreshold;
		ctx->calibrationSpeed = CalibrationContext::VERY_SLOW;
		ctx->chaperone = filled->chaperone;
		ctx->lockRelativePosition = true;
		ctx->state = CalibrationState::ContinuousStandby;

		const std::string text = Write(*ctx);
		CHECK(IsValidJson(text));
		for (const char* key :
		     {"\"reference_tracking_system\"", "\"target_tracking_system\"", "\"roll\"", "\"scale\"", "\"reference_device\"",
		      "\"autostart_continuous_calibration\"", "\"relative_transform\"", "\"relative_pos_calibrated\""}) {
			CHECK(text.find(key) == std::string::npos);
		}

		auto loaded = Fresh();
		CHECK(Parse(*loaded, text) == Outcome::Loaded);
		CHECK(!loaded->validProfile);
		CHECK(loaded->state == CalibrationState::None);
		CHECK(std::memcmp(&loaded->alignmentSpeedParams, &ctx->alignmentSpeedParams, sizeof ctx->alignmentSpeedParams) == 0);
		CHECK(loaded->continuousCalibrationThreshold == ctx->continuousCalibrationThreshold);
		CHECK(loaded->smoothingParams.strength == ctx->smoothingParams.strength);
		CHECK(loaded->smoothControllers);
		CHECK(loaded->fingerSmoothing.strength == ctx->fingerSmoothing.strength);
		CHECK(loaded->fingerSmoothing.fingerMask == ctx->fingerSmoothing.fingerMask);
		CHECK(std::memcmp(loaded->fingerSmoothing.perFinger, ctx->fingerSmoothing.perFinger, 10) == 0);
		CHECK(std::memcmp(&loaded->stickSmoothing.sticks[0], &ctx->stickSmoothing.sticks[0], sizeof(protocol::StickRampConfig)) == 0);
		CHECK(loaded->stickOn[0] && !loaded->stickOn[1]);
		CHECK(loaded->quashTargetInContinuous && loaded->requireTriggerPressToApply && loaded->ignoreOutliers);
		CHECK(SameBits(loaded->continuousCalibrationOffset, ctx->continuousCalibrationOffset));
		CHECK(loaded->enableStaticRecalibration);
		CHECK(loaded->jitterThreshold == ctx->jitterThreshold);
		CHECK(loaded->maxRelativeErrorThreshold == ctx->maxRelativeErrorThreshold);
		CHECK(loaded->calibrationSpeed == CalibrationContext::VERY_SLOW);
		CHECK(loaded->chaperone.valid && !loaded->chaperone.autoApply);
		CHECK(loaded->chaperone.geometry.size() == ctx->chaperone.geometry.size());
		CHECK(std::memcmp(loaded->chaperone.geometry.data(), ctx->chaperone.geometry.data(),
		                  ctx->chaperone.geometry.size() * sizeof(vr::HmdQuad_t)) == 0);
		CHECK(loaded->lockRelativePosition);
		CHECK(Write(*loaded) == text);
	}

	void TestOverflowingNumberCannotPoisonTheNextSave()
	{
		auto saved = Fresh();
		Fill(*saved);
		const std::string text = SetLast(Write(*saved), "x", "-1e400");
		CHECK(text.find("-1e400") != std::string::npos);
		auto ctx = Fresh();
		const Outcome outcome = Parse(*ctx, text);
		CHECK(outcome != Outcome::Escaped);
		if (outcome == Outcome::Loaded) {
			CHECK(std::isfinite(ctx->calibratedTranslation(0)));
			const std::string again = Write(*ctx);
			CHECK(IsValidJson(again));
		}
	}

	void TestNonFiniteCalibrationStillWritesReadableJson()
	{
		auto ctx = Fresh();
		Fill(*ctx);
		ctx->calibratedTranslation(1) = std::numeric_limits<double>::quiet_NaN();
		ctx->calibratedScale = std::numeric_limits<double>::infinity();
		const std::string text = Write(*ctx);
		const bool valid = IsValidJson(text);
		CHECK(valid);
		if (!valid) {
			const size_t y = text.rfind("\"y\": ");
			const size_t scale = text.rfind("\"scale\": ");
			std::printf("  NaN translation serialises as [%s], inf scale as [%s]\n",
			            text.substr(y, text.find_first_of(",\n", y) - y).c_str(),
			            text.substr(scale, text.find_first_of(",\n", scale) - scale).c_str());
		}
		auto loaded = Fresh();
		CHECK(Parse(*loaded, text) == Outcome::Loaded);
		CHECK(!loaded->validProfile);
		CHECK(loaded->smoothingParams.strength == 37);
		CHECK(loaded->chaperone.valid);
	}

	void TestNonFiniteChaperoneDoesNotThrowFromSave()
	{
		auto ctx = Fresh();
		Fill(*ctx);
		((float*)ctx->chaperone.geometry.data())[4] = std::numeric_limits<float>::quiet_NaN();
		bool threw = false;
		std::string what;
		try {
			Write(*ctx);
		}
		catch (const std::exception& e) {
			threw = true;
			what = typeid(e).name();
		}
		CHECK(!threw);
		if (threw) std::printf("  WriteProfile threw %s\n", what.c_str());
	}

	void TestPartialQuadGeometryIsRejected()
	{
		for (int floats : {13, 5, 25}) {
			std::string geometry = "[";
			for (int i = 0; i < floats; ++i) {
				geometry += (i ? ",1" : "1");
			}
			geometry += "]";
			std::string text = std::string(kProfile2020);
			text = ReplaceOnce(text, "[1,0,1, 1,2.4300000667572021,1, -1,2.4300000667572021,1, -1,0,1]", geometry);
			text = ReplaceOnce(text, "\"pitch\": 0.5,",
			                   "\"pitch\": 0.5, \"continuous_calibration_target_offset_x\": 0, "
			                   "\"continuous_calibration_target_offset_y\": 0, \"continuous_calibration_target_offset_z\": 0,");
			auto ctx = Fresh();
			ctx->chaperone.geometry.reserve(16);
			const Outcome outcome = Parse(*ctx, text);
			const size_t held = ctx->chaperone.geometry.size() * sizeof(vr::HmdQuad_t) / sizeof(float);
			const bool safe = outcome == Outcome::Loaded && ctx->validProfile && !ctx->chaperone.valid && held == 0;
			CHECK(safe);
			if (!safe)
				std::printf("  %d floats: outcome %d, calibration valid %d, chaperone valid %d, %zu floats held\n", floats, (int)outcome,
				            (int)ctx->validProfile, (int)ctx->chaperone.valid, held);
		}
	}

	void TestOutOfRangeValuesAreClamped()
	{
		auto saved = Fresh();
		Fill(*saved);
		std::string text = Write(*saved);
		text = ReplaceOnce(text, "\"strength\": 37", "\"strength\": 250");
		text = ReplaceOnce(text, "\"mask\": 501", "\"mask\": 65535");
		text = ReplaceOnce(text, "\"strength\": 20", "\"strength\": -4");
		text = ReplaceOnce(text, "\"calibration_speed\": 1", "\"calibration_speed\": 9");
		auto ctx = Fresh();
		CHECK(Parse(*ctx, text) == Outcome::Loaded);
		CHECK(ctx->smoothingParams.strength == 100);
		CHECK(ctx->fingerSmoothing.fingerMask == protocol::kAllFingersMask);
		CHECK(ctx->fingerSmoothing.strength == 0);
		const int speed = (int)ctx->calibrationSpeed;
		CHECK(speed >= CalibrationContext::FAST && speed <= CalibrationContext::VERY_SLOW);
	}

	void TestLargeGeometryRoundTripsExactly()
	{
		auto saved = Fresh();
		Fill(*saved);
		saved->chaperone.geometry.resize(2000);
		float* g = (float*)saved->chaperone.geometry.data();
		for (size_t i = 0; i < saved->chaperone.geometry.size() * 12; ++i) {
			g[i] = std::ldexp((float)(i % 977) - 488.0f, (int)(i % 40) - 20);
		}
		auto loaded = Fresh();
		CHECK(Parse(*loaded, Write(*saved)) == Outcome::Loaded);
		CHECK(loaded->chaperone.geometry.size() == 2000);
		CHECK(std::memcmp(loaded->chaperone.geometry.data(), saved->chaperone.geometry.data(), 2000 * sizeof(vr::HmdQuad_t)) == 0);
	}

	void TestWriteSkipsNothingWhenProfileValid()
	{
		auto ctx = Fresh();
		Fill(*ctx);
		ctx->chaperone.valid = false;
		const std::string text = Write(*ctx);
		CHECK(text.find("\"chaperone\"") == std::string::npos);
		auto loaded = Fresh();
		CHECK(Parse(*loaded, text) == Outcome::Loaded);
		CHECK(!loaded->chaperone.valid);
	}

	std::wstring LegacyStored(const std::string& utf8, unsigned codePage)
	{
		const int length = MultiByteToWideChar(codePage, 0, utf8.data(), (int)utf8.size(), nullptr, 0);
		std::wstring wide((size_t)std::max(length, 0), L'\0');
		MultiByteToWideChar(codePage, 0, utf8.data(), (int)utf8.size(), wide.data(), length);
		return wide;
	}

	void TestLegacyTextRepair()
	{
		using namespace spacecal::registry_text;
		const std::string kuche = "K\xC3\xBC"
		                          "che";
		const std::wstring stored = LegacyStored(kuche, 1252);
		CHECK(stored == L"KÃ¼che");
		std::wstring repaired;
		CHECK(RepairLegacyText(stored, 1252, repaired));
		CHECK(repaired == L"Küche");
		CHECK(WideToUtf8(repaired) == kuche);

		std::wstring untouched = L"unchanged";
		CHECK(!RepairLegacyText(L"Küche", 1252, untouched));
		CHECK(!RepairLegacyText(L"lighthouse LHR-1234ABCD", 1252, untouched));
		CHECK(!RepairLegacyText(L"", 1252, untouched));
		CHECK(!RepairLegacyText(L"Ã(", 1252, untouched));
		CHECK(!RepairLegacyText(L"日本", 1252, untouched));
		CHECK(!RepairLegacyText(stored, CP_UTF8, untouched));
		CHECK(!RepairLegacyText(stored, 0, untouched));
		CHECK(untouched == L"unchanged");

		for (const std::string& text :
		     {std::string("Quest 3 J\xC3\xB6rg"), std::string("\xC3\x81lvaro \xE2\x82\xAC"), std::string("1WMHH\xE4\xB8\xAD\xE6\x96\x87"),
		      std::string("\xF0\x9F\x98\x80 \xC5\x81ukasz \xCE\xA9"),
		      std::string("{\"nick\":\"Wohnzimmer \xC3\xA4\xC3\xB6\xC3\xBC\xC3\x9F\"}")}) {
			std::wstring fixed;
			const bool changed = RepairLegacyText(LegacyStored(text, 1252), 1252, fixed);
			CHECK(changed);
			CHECK(WideToUtf8(fixed) == text);
			if (!changed || WideToUtf8(fixed) != text) std::printf("  cp1252 fixture [%s] not repaired\n", text.c_str());
		}

		if (IsValidCodePage(932)) {
			int recovered = 0;
			int lossy = 0;
			for (const std::string& text : {std::string("\xE5\x8F\xB0\xE6\x89\x80"), std::string("\xE3\x83\x99\xE3\x83\xBC\xE3\x82\xB9"),
			                                std::string("K\xC3\xBC"
			                                            "che"),
			                                std::string("\xE6\x97\xA5\xE6\x9C\xAC")}) {
				const std::wstring legacy = LegacyStored(text, 932);
				std::string back;
				const bool lossless = EncodeExactly(legacy, 932, back) && back == text;
				std::wstring fixed;
				const bool changed = RepairLegacyText(legacy, 932, fixed);
				if (lossless) {
					CHECK(changed);
					CHECK(WideToUtf8(fixed) == text);
					++recovered;
				}
				else {
					CHECK(!changed);
					++lossy;
				}
			}
			CHECK(recovered >= 3);
			std::printf("  cp932 fixtures: %d repaired, %d left alone because the old save already lost bytes\n", recovered, lossy);
		}
		else {
			std::printf("  code page 932 is not installed, CJK fixture skipped\n");
		}
	}

	void TestRegistryReadDecisions()
	{
		using namespace spacecal::registry_text;
		CHECK(ClassifyRead(ERROR_SUCCESS) == ReadOutcome::Found);
		CHECK(ClassifyRead(ERROR_FILE_NOT_FOUND) == ReadOutcome::Missing);
		for (LSTATUS status : {ERROR_ACCESS_DENIED, ERROR_UNSUPPORTED_TYPE, ERROR_MORE_DATA, ERROR_PATH_NOT_FOUND, ERROR_INVALID_HANDLE,
		                       ERROR_NOT_ENOUGH_MEMORY, ERROR_BADDB}) {
			CHECK(ClassifyRead(status) == ReadOutcome::Failed);
		}
		CHECK(!BlocksWrites(ReadOutcome::Found));
		CHECK(!BlocksWrites(ReadOutcome::Missing));
		CHECK(BlocksWrites(ReadOutcome::Failed));
		const unsigned systemCodePage = SystemAnsiCodePage();
		CHECK(systemCodePage != 0);
		std::printf("  system ANSI code page %u (process GetACP %u)\n", systemCodePage, (unsigned)GetACP());
	}

	struct PrivateHive
	{
		std::filesystem::path path;
		HKEY root = nullptr;

		explicit PrivateHive(const wchar_t* name)
		{
			wchar_t temp[MAX_PATH] = {};
			GetTempPathW(MAX_PATH, temp);
			const std::filesystem::path dir = std::filesystem::path(temp) / L"wk" / L"profile-tests";
			std::error_code ec;
			std::filesystem::create_directories(dir, ec);
			path = dir / (std::wstring(name) + L"_" + std::to_wstring(GetCurrentProcessId()) + L".dat");
			RemoveFiles();
			if (RegLoadAppKeyW(path.c_str(), &root, KEY_ALL_ACCESS, 0, 0) != ERROR_SUCCESS) root = nullptr;
			CHECK(root != nullptr);
		}

		~PrivateHive()
		{
			RegistryStore detached;
			detached.root = nullptr;
			registryStore = detached;
			if (root) RegCloseKey(root);
			RemoveFiles();
		}

		void StartSession(unsigned legacyCodePage) const
		{
			RegistryStore session;
			session.root = root;
			session.legacyCodePage = legacyCodePage;
			registryStore = session;
		}

		void RemoveFiles() const
		{
			std::error_code ec;
			for (const wchar_t* suffix : {L"", L".LOG1", L".LOG2"}) {
				std::filesystem::remove(path.wstring() + suffix, ec);
			}
		}

		void Put(const wchar_t* name, const std::wstring& value) const
		{
			HKEY key = nullptr;
			if (RegCreateKeyExW(root, registryStore.subkey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return;
			RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
			               (DWORD)((value.size() + 1) * sizeof(wchar_t)));
			RegCloseKey(key);
		}

		void PutDword(const wchar_t* name, DWORD value) const
		{
			HKEY key = nullptr;
			if (RegCreateKeyExW(root, registryStore.subkey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return;
			RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof value);
			RegCloseKey(key);
		}

		DWORD Type(const wchar_t* name) const
		{
			DWORD type = REG_NONE;
			if (RegGetValueW(root, registryStore.subkey, name, RRF_RT_ANY, &type, nullptr, nullptr) != ERROR_SUCCESS) return REG_NONE;
			return type;
		}

		std::wstring Get(const wchar_t* name) const
		{
			DWORD size = 0;
			if (RegGetValueW(root, registryStore.subkey, name, RRF_RT_REG_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS)
				return L"<missing>";
			std::wstring value(size / sizeof(wchar_t), L'\0');
			if (RegGetValueW(root, registryStore.subkey, name, RRF_RT_REG_SZ, nullptr, value.data(), &size) != ERROR_SUCCESS)
				return L"<error>";
			value.resize(size / sizeof(wchar_t));
			while (!value.empty() && value.back() == L'\0') {
				value.pop_back();
			}
			return value;
		}
	};

	void TestLegacyRegistryTextIsRepairedBeforeTheFirstLoad()
	{
		using namespace spacecal::registry_text;
		PrivateHive hive(L"migration");
		if (!hive.root) return;
		hive.StartSession(1252);

		auto saved = Fresh();
		Fill(*saved);
		const std::string profile = Write(*saved);
		const std::string nicknames = "{\"nicknames\":{\"LHB-1\":\"K\xC3\xBC"
		                              "che\"}}";
		const std::wstring updates = L"{\"auto_install\":true,\"check_on_startup\":false,\"skipped_tag\":\"\"}";
		hive.Put(L"Config", LegacyStored(profile, 1252));
		hive.Put(L"BaseStations", LegacyStored(nicknames, 1252));
		hive.Put(L"Updates", updates);
		CHECK(hive.Get(L"BaseStations").find(L"KÃ¼che") != std::wstring::npos);

		auto loaded = Fresh();
		LoadProfile(*loaded);
		CHECK(loaded->validProfile);
		CHECK(loaded->targetStandby.model == saved->targetStandby.model);
		CHECK(loaded->targetStandby.serial == saved->targetStandby.serial);
		CHECK(hive.Get(L"Config") == Utf8ToWide(profile));
		CHECK(hive.Get(L"BaseStations").find(L"Küche") != std::wstring::npos);
		CHECK(hive.Get(L"Updates") == updates);
		CHECK(hive.Get(kEncodingMarkerName) == kEncodingMarkerValue);
		CHECK(ReadRegistryKey("BaseStations") == nicknames);

		hive.StartSession(1252);
		hive.Put(L"BaseStations", LegacyStored(nicknames, 1252));
		CHECK(ReadRegistryKey("BaseStations") == WideToUtf8(LegacyStored(nicknames, 1252)));
		CHECK(hive.Get(L"BaseStations") == LegacyStored(nicknames, 1252));

		SaveProfile(*loaded);
		CHECK(hive.Get(L"Config") == Utf8ToWide(Write(*loaded)));
		CHECK(hive.Get(L"Config").find(L"Quest 3 Jörg") != std::wstring::npos);
		hive.StartSession(1252);
		auto again = Fresh();
		LoadProfile(*again);
		CHECK(again->validProfile);
		CHECK(again->targetStandby.model == saved->targetStandby.model);
		CHECK(again->targetStandby.serial == saved->targetStandby.serial);
	}

	void TestFirstWriteAlsoRepairsLegacyText()
	{
		using namespace spacecal::registry_text;
		PrivateHive hive(L"migration-write");
		if (!hive.root) return;
		hive.StartSession(1252);

		const std::string devices = "[{\"model\":\"Quest 3 J\xC3\xB6rg\",\"serial\":\"1\",\"tracking_system\":\"oculus\"}]";
		hive.Put(L"Devices", LegacyStored(devices, 1252));
		UpdateSettings updates;
		SaveUpdateSettings(updates);
		CHECK(hive.Get(L"Devices") == Utf8ToWide(devices));
		CHECK(hive.Get(kEncodingMarkerName) == kEncodingMarkerValue);
		std::vector<KnownDevice> known;
		LoadKnownDevices(known);
		CHECK(known.size() == 1 && known[0].model == "Quest 3 J\xC3\xB6rg");
	}

	void TestEveryCorruptProfileIsBackedUp()
	{
		PrivateHive hive(L"backup");
		if (!hive.root) return;
		hive.StartSession(1252);

		const std::wstring first = L"[{\"reference_tracking_system\": 5}]";
		const std::wstring second = L"[{\"x\": \"broken";
		hive.Put(L"Config", first);
		auto ctx = Fresh();
		LoadProfile(*ctx);
		CHECK(!ctx->validProfile);
		CHECK(hive.Get(L"ConfigBackup") == first);

		hive.Put(L"Config", second);
		ctx = Fresh();
		LoadProfile(*ctx);
		CHECK(hive.Get(L"ConfigBackup") == second);

		hive.StartSession(1252);
		hive.Put(L"Config", first);
		ctx = Fresh();
		LoadProfile(*ctx);
		CHECK(hive.Get(L"ConfigBackup") == first);

		Fill(*ctx);
		SaveProfile(*ctx);
		auto reloaded = Fresh();
		CHECK(Parse(*reloaded, spacecal::registry_text::WideToUtf8(hive.Get(L"Config"))) == Outcome::Loaded);
		CHECK(reloaded->validProfile);
		CHECK(hive.Get(L"ConfigBackup") == first);
	}

	void TestUnreadableProfileIsNeverOverwritten()
	{
		PrivateHive hive(L"unreadable");
		if (!hive.root) return;
		hive.StartSession(1252);

		hive.PutDword(L"Config", 7);
		auto ctx = Fresh();
		LoadProfile(*ctx);
		CHECK(!ctx->validProfile);
		Fill(*ctx);
		SaveProfile(*ctx);
		CHECK(hive.Type(L"Config") == REG_DWORD);
		CHECK(hive.Get(L"ConfigBackup") == L"<missing>");

		UpdateSettings updates;
		updates.skippedTag = "2026.10.11.0";
		SaveUpdateSettings(updates);
		CHECK(hive.Get(L"Updates").find(L"2026.10.11.0") != std::wstring::npos);

		hive.StartSession(1252);
		RegDeleteKeyValueW(hive.root, registryStore.subkey, L"Config");
		ctx = Fresh();
		LoadProfile(*ctx);
		CHECK(!ctx->validProfile);
		Fill(*ctx);
		SaveProfile(*ctx);
		CHECK(hive.Type(L"Config") == REG_SZ);
		CHECK(hive.Get(L"Config") == spacecal::registry_text::Utf8ToWide(Write(*ctx)));
	}

}

int main()
{
	registryStore.root = nullptr;

	TestFullRoundTripKeepsEveryField();
	TestSaveLoadSaveIsBitStable();
	TestValidProfileBytesMatchPreviousWriter();
	TestRandomProfilesOnlyDriftInTheLastBits();
	TestDefaultAlignmentParamsSurviveALoad();
	TestEmptyAndWhitespaceAreRejected();
	TestEveryTruncationIsRejectedCleanly();
	TestWrongShapesAreRejected();
	TestBrokenSettingsKeepTheCalibration();
	TestChaperoneWithoutQuadsLoadsAsInvalid();
	TestRejectedProfileIsCopiedToBackup();
	TestUnknownFieldsAreIgnored();
	TestMissingOptionalFieldsUseDefaults();
	TestRegistryProfileFrom2020Loads();
	TestRegistryProfileFrom2023Loads();
	TestSmoothingSettingsPersistWithoutACalibration();
	TestEverySettingSurvivesWithoutACalibration();
	TestOverflowingNumberCannotPoisonTheNextSave();
	TestNonFiniteCalibrationStillWritesReadableJson();
	TestNonFiniteChaperoneDoesNotThrowFromSave();
	TestPartialQuadGeometryIsRejected();
	TestOutOfRangeValuesAreClamped();
	TestLargeGeometryRoundTripsExactly();
	TestWriteSkipsNothingWhenProfileValid();
	TestLegacyTextRepair();
	TestRegistryReadDecisions();
	TestLegacyRegistryTextIsRepairedBeforeTheFirstLoad();
	TestFirstWriteAlsoRepairsLegacyText();
	TestEveryCorruptProfileIsBackedUp();
	TestUnreadableProfileIsNeverOverwritten();

	if (failures == 0) {
		std::printf("profile_tests: all tests passed\n");
		return 0;
	}
	std::printf("profile_tests: %d failure(s)\n", failures);
	return 1;
}
