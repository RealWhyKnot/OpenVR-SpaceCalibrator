#include "stdafx.h"
#include "Configuration.h"
#include "basestations/BaseStationsSettings.h"
#include "WriteFilter.h"
#include "RegistryText.h"
#include "StickProfile.h"

#include <picojson.h>

#include <cmath>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <limits>

static picojson::array FloatArray(const float* buf, size_t numFloats)
{
	picojson::array arr;

	for (int i = 0; i < numFloats; i++) {
		arr.push_back(picojson::value(double(buf[i])));
	}

	return arr;
}

static bool FitsFloat(double value)
{
	return std::abs(value) <= std::numeric_limits<float>::max();
}

static bool ReadFinite(const picojson::value& node, double& out)
{
	if (!node.is<double>()) return false;
	const double value = node.get<double>();
	if (!std::isfinite(value)) return false;
	out = value;
	return true;
}

static double FiniteOr(const picojson::value& node, double fallback)
{
	double value = fallback;
	ReadFinite(node, value);
	return value;
}

static float FloatOr(const picojson::value& node, float fallback)
{
	double value = 0.0;
	if (!ReadFinite(node, value) || !FitsFloat(value)) return fallback;
	return (float)value;
}

static bool BoolOr(const picojson::value& node, bool fallback)
{
	return node.is<bool>() ? node.get<bool>() : fallback;
}

static bool FloatsFinite(const float* buf, size_t numFloats)
{
	for (size_t i = 0; i < numFloats; i++) {
		if (!std::isfinite(buf[i])) return false;
	}
	return true;
}

static bool LoadFloatArray(const picojson::value& obj, float* buf, size_t numFloats)
{
	if (!obj.is<picojson::array>()) {
		return false;
	}

	auto& arr = obj.get<picojson::array>();
	if (arr.size() != numFloats) {
		return false;
	}

	for (size_t i = 0; i < numFloats; i++) {
		double value = 0.0;
		if (!ReadFinite(arr[i], value) || !FitsFloat(value)) {
			return false;
		}
		buf[i] = (float)value;
	}

	return true;
}

static void LoadStandby(StandbyDevice& device, picojson::value& value)
{
	if (!value.is<picojson::object>()) {
		return;
	}
	auto& obj = value.get<picojson::object>();

	const auto& system = obj["tracking_system"];
	if (system.is<std::string>()) {
		device.trackingSystem = system.get<std::string>();
	}

	const auto& model = obj["model"];
	if (model.is<std::string>()) {
		device.model = model.get<std::string>();
	}

	const auto& serial = obj["serial"];
	if (serial.is<std::string>()) {
		device.serial = serial.get<std::string>();
	}
}

static void VisitAlignmentParams(CalibrationContext& ctx, std::function<void(const char*, double&)> MapParam)
{
#define P(s) MapParam(#s, ctx.alignmentSpeedParams.s)
	P(align_speed_tiny);
	P(align_speed_small);
	P(align_speed_large);
	P(thr_trans_tiny);
	P(thr_trans_small);
	P(thr_trans_large);
	P(thr_rot_tiny);
	P(thr_rot_small);
	P(thr_rot_large);

	// Convert to double and back
	double tmp = ctx.continuousCalibrationThreshold;
	MapParam("continuousCalibrationThreshold", tmp);
	if (FitsFloat(tmp)) ctx.continuousCalibrationThreshold = (float)tmp;
}

static void LoadAlignmentParams(CalibrationContext& ctx, picojson::value& value)
{
	ctx.ResetConfig();

	if (!value.is<picojson::object>()) {
		return;
	}
	auto& obj = value.get<picojson::object>();

	VisitAlignmentParams(ctx, [&](auto name, auto& param) { ReadFinite(obj[name], param); });
}

static picojson::object SaveAlignmentParams(CalibrationContext& ctx)
{
	picojson::object obj;

	VisitAlignmentParams(ctx, [&](auto name, auto& param) {
		if (std::isfinite(param)) obj[name].set<double>(param);
	});

	return obj;
}

static uint8_t ClampStrength(double value)
{
	return (uint8_t)(value < 0.0 ? 0.0 : (value > 100.0 ? 100.0 : value));
}

static void LoadSmoothingParams(CalibrationContext& ctx, picojson::value& value)
{
	ctx.ResetSmoothingConfig();

	if (!value.is<picojson::object>()) {
		return;
	}
	auto& obj = value.get<picojson::object>();

	ctx.smoothControllers = BoolOr(obj["smooth_controllers"], false);
	if (obj["strength"].is<double>()) {
		ctx.smoothingParams.strength = ClampStrength(obj["strength"].get<double>());
	}
	else if (obj["enabled"].evaluate_as_boolean()) {
		ctx.smoothingParams.strength = 50;
	}

	if (obj["fingers"].is<picojson::object>()) {
		auto fingers = obj["fingers"].get<picojson::object>();
		if (fingers["strength"].is<double>()) {
			ctx.fingerSmoothing.strength = ClampStrength(fingers["strength"].get<double>());
		}
		double mask = 0.0;
		if (ReadFinite(fingers["mask"], mask) && mask >= 0.0 && mask <= 65535.0) {
			ctx.fingerSmoothing.fingerMask = (uint16_t)mask & protocol::kAllFingersMask;
		}
		if (fingers["per_finger"].is<picojson::array>()) {
			auto& perFinger = fingers["per_finger"].get<picojson::array>();
			for (size_t i = 0; i < perFinger.size() && i < 10; ++i) {
				if (perFinger[i].is<double>()) {
					ctx.fingerSmoothing.perFinger[i] = ClampStrength(perFinger[i].get<double>());
				}
			}
		}
	}

	if (obj["sticks"].is<picojson::object>()) {
		auto sticks = obj["sticks"].get<picojson::object>();
		spacecal::stick_profile::Load(ctx.stickSmoothing.sticks[0], ctx.stickOn[0], ctx.stickLocked[0], sticks["left"]);
		spacecal::stick_profile::Load(ctx.stickSmoothing.sticks[1], ctx.stickOn[1], ctx.stickLocked[1], sticks["right"]);
	}
}

static picojson::object SaveSmoothingParams(CalibrationContext& ctx)
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
	sticks["left"] = spacecal::stick_profile::Save(ctx.stickSmoothing.sticks[0], ctx.stickOn[0], ctx.stickLocked[0]);
	sticks["right"] = spacecal::stick_profile::Save(ctx.stickSmoothing.sticks[1], ctx.stickOn[1], ctx.stickLocked[1]);
	obj["sticks"].set<picojson::object>(sticks);

	return obj;
}

static double RequireFinite(picojson::object& obj, const char* name)
{
	double value = 0.0;
	if (!ReadFinite(obj[name], value)) {
		throw std::runtime_error(std::string(name) + " is not a finite number");
	}
	return value;
}

static std::string RequireString(picojson::object& obj, const char* name)
{
	const picojson::value& node = obj[name];
	if (!node.is<std::string>()) {
		throw std::runtime_error(std::string(name) + " is not a string");
	}
	return node.get<std::string>();
}

static CalibrationContext::Speed ClampSpeed(double value)
{
	if (value <= (double)CalibrationContext::FAST) return CalibrationContext::FAST;
	if (value >= (double)CalibrationContext::VERY_SLOW) return CalibrationContext::VERY_SLOW;
	return (CalibrationContext::Speed)(int)value;
}

namespace {
	struct ProfileCalibration
	{
		std::string referenceTrackingSystem, targetTrackingSystem;
		Eigen::Vector3d rotation = Eigen::Vector3d::Zero();
		Eigen::Vector3d translation = Eigen::Vector3d::Zero();
		double scale = 1.0;
		StandbyDevice referenceStandby, targetStandby;
		bool autostart = false;
		bool relativePosCalibrated = false;
		Eigen::AffineCompact3d refToTargetPose = Eigen::AffineCompact3d::Identity();
	};
}

static bool ParseCalibration(picojson::object& obj, ProfileCalibration& cal)
{
	if (obj["reference_tracking_system"].is<picojson::null>()) {
		return false;
	}

	cal.referenceTrackingSystem = RequireString(obj, "reference_tracking_system");
	cal.targetTrackingSystem = RequireString(obj, "target_tracking_system");
	cal.rotation(0) = RequireFinite(obj, "roll");
	cal.rotation(1) = RequireFinite(obj, "yaw");
	cal.rotation(2) = RequireFinite(obj, "pitch");
	cal.translation(0) = RequireFinite(obj, "x");
	cal.translation(1) = RequireFinite(obj, "y");
	cal.translation(2) = RequireFinite(obj, "z");
	if (!obj["scale"].is<picojson::null>()) {
		cal.scale = RequireFinite(obj, "scale");
	}
	LoadStandby(cal.referenceStandby, obj["reference_device"]);
	LoadStandby(cal.targetStandby, obj["target_device"]);
	cal.autostart = BoolOr(obj["autostart_continuous_calibration"], false);
	cal.relativePosCalibrated = BoolOr(obj["relative_pos_calibrated"], false);

	if (obj["relative_transform"].is<picojson::object>()) {
		auto& relTransform = obj["relative_transform"].get<picojson::object>();
		Eigen::Vector3d refToTragetRoation;
		Eigen::Vector3d refToTargetTranslation;

		refToTragetRoation(0) = RequireFinite(relTransform, "roll");
		refToTragetRoation(1) = RequireFinite(relTransform, "yaw");
		refToTragetRoation(2) = RequireFinite(relTransform, "pitch");
		refToTargetTranslation(0) = RequireFinite(relTransform, "x");
		refToTargetTranslation(1) = RequireFinite(relTransform, "y");
		refToTargetTranslation(2) = RequireFinite(relTransform, "z");

		Eigen::Matrix3d rotationMatrix;
		rotationMatrix = Eigen::AngleAxisd(refToTragetRoation[0], Eigen::Vector3d::UnitX()) *
		                 Eigen::AngleAxisd(refToTragetRoation[1], Eigen::Vector3d::UnitY()) *
		                 Eigen::AngleAxisd(refToTragetRoation[2], Eigen::Vector3d::UnitZ());

		cal.refToTargetPose = Eigen::AffineCompact3d::Identity();
		cal.refToTargetPose.linear() = rotationMatrix;
		cal.refToTargetPose.translation() = refToTargetTranslation;
	}

	return true;
}

static bool LoadGeometry(std::vector<vr::HmdQuad_t>& geometry, const picojson::value& value)
{
	constexpr size_t quadFloats = sizeof(vr::HmdQuad_t) / sizeof(float);
	if (!value.is<picojson::array>()) return false;
	const size_t floats = value.get<picojson::array>().size();
	if (floats == 0 || floats % quadFloats != 0) return false;

	std::vector<vr::HmdQuad_t> quads(floats / quadFloats);
	if (!LoadFloatArray(value, (float*)quads.data(), floats)) return false;
	geometry = std::move(quads);
	return true;
}

static CalibrationContext::Chaperone LoadChaperone(picojson::value& value)
{
	CalibrationContext::Chaperone chaperone;
	if (!value.is<picojson::object>()) {
		return chaperone;
	}
	auto& obj = value.get<picojson::object>();

	CalibrationContext::Chaperone loaded;
	if (!obj["auto_apply"].is<bool>()) {
		return chaperone;
	}
	loaded.autoApply = obj["auto_apply"].get<bool>();
	if (!LoadFloatArray(obj["play_space_size"], loaded.playSpaceSize.v, 2)) {
		return chaperone;
	}
	if (!LoadFloatArray(obj["standing_center"], (float*)loaded.standingCenter.m, sizeof(loaded.standingCenter.m) / sizeof(float))) {
		return chaperone;
	}
	loaded.valid = LoadGeometry(loaded.geometry, obj["geometry"]);
	return loaded;
}

static void ParseProfile(CalibrationContext& ctx, std::istream& stream)
{
	picojson::value v;
	std::string err = picojson::parse(v, stream);
	if (!err.empty()) {
		throw std::runtime_error(err);
	}

	if (!v.is<picojson::array>()) {
		throw std::runtime_error("profile is not an array");
	}
	auto& arr = v.get<picojson::array>();
	if (arr.size() < 1) {
		throw std::runtime_error("no profiles in file");
	}
	if (!arr[0].is<picojson::object>()) {
		throw std::runtime_error("profile is not an object");
	}

	auto& obj = arr[0].get<picojson::object>();

	ProfileCalibration calibration;
	const bool hasCalibration = ParseCalibration(obj, calibration);

	LoadAlignmentParams(ctx, obj["alignment_params"]);
	LoadSmoothingParams(ctx, obj["smoothing"]);
	ctx.quashTargetInContinuous = BoolOr(obj["quash_target_in_continuous"], false);
	ctx.requireTriggerPressToApply = BoolOr(obj["require_trigger_press_to_apply"], false);
	ctx.ignoreOutliers = BoolOr(obj["ignore_outliers"], false);
	ctx.continuousCalibrationOffset(0) = FiniteOr(obj["continuous_calibration_target_offset_x"], 0.0);
	ctx.continuousCalibrationOffset(1) = FiniteOr(obj["continuous_calibration_target_offset_y"], 0.0);
	ctx.continuousCalibrationOffset(2) = FiniteOr(obj["continuous_calibration_target_offset_z"], 0.0);
	ctx.enableStaticRecalibration = BoolOr(obj["static_calibration"], ctx.enableStaticRecalibration);
	ctx.jitterThreshold = FloatOr(obj["jitter_threshold"], ctx.jitterThreshold);
	ctx.maxRelativeErrorThreshold = FloatOr(obj["max_relative_error_threshold"], ctx.maxRelativeErrorThreshold);
	ctx.calibrationSpeed = ClampSpeed(FiniteOr(obj["calibration_speed"], CalibrationContext::FAST));
	ctx.chaperone = LoadChaperone(obj["chaperone"]);
	ctx.lockRelativePosition = BoolOr(obj["lock_relative_position"], false);

	if (!hasCalibration) {
		ctx.validProfile = false;
		return;
	}

	ctx.referenceTrackingSystem = calibration.referenceTrackingSystem;
	ctx.targetTrackingSystem = calibration.targetTrackingSystem;
	ctx.calibratedRotation = calibration.rotation;
	ctx.calibratedTranslation = calibration.translation;
	ctx.calibratedScale = calibration.scale;
	ctx.referenceStandby = calibration.referenceStandby;
	ctx.targetStandby = calibration.targetStandby;
	if (calibration.autostart) {
		ctx.state = CalibrationState::ContinuousStandby;
	}
	ctx.relativePosCalibrated = calibration.relativePosCalibrated;
	ctx.refToTargetPose = calibration.refToTargetPose;

	ctx.validProfile = true;
}


static void WriteStandby(StandbyDevice& device, picojson::value& value)
{
	auto obj = picojson::object();

	obj["tracking_system"].set<std::string>(device.trackingSystem);
	obj["model"].set<std::string>(device.model);
	obj["serial"].set<std::string>(device.serial);

	value.set<picojson::object>(obj);
}


static bool CalibrationIsWritable(const CalibrationContext& ctx)
{
	return ctx.validProfile && ctx.calibratedRotation.allFinite() && ctx.calibratedTranslation.allFinite() &&
	       std::isfinite(ctx.calibratedScale) && ctx.refToTargetPose.matrix().allFinite();
}

static bool ChaperoneIsWritable(const CalibrationContext::Chaperone& chaperone)
{
	return chaperone.valid && FloatsFinite(chaperone.playSpaceSize.v, 2) &&
	       FloatsFinite((const float*)chaperone.standingCenter.m, sizeof(chaperone.standingCenter.m) / sizeof(float)) &&
	       FloatsFinite((const float*)chaperone.geometry.data(), sizeof(vr::HmdQuad_t) / sizeof(float) * chaperone.geometry.size());
}

static void SetFinite(picojson::object& obj, const char* name, double value)
{
	if (std::isfinite(value)) obj[name].set<double>(value);
}

static void WriteProfile(CalibrationContext& ctx, std::ostream& out)
{
	picojson::object profile;
	profile["alignment_params"].set<picojson::object>(SaveAlignmentParams(ctx));
	profile["smoothing"].set<picojson::object>(SaveSmoothingParams(ctx));

	if (CalibrationIsWritable(ctx)) {
		profile["reference_tracking_system"].set<std::string>(ctx.referenceTrackingSystem);
		profile["target_tracking_system"].set<std::string>(ctx.targetTrackingSystem);
		profile["roll"].set<double>(ctx.calibratedRotation(0));
		profile["yaw"].set<double>(ctx.calibratedRotation(1));
		profile["pitch"].set<double>(ctx.calibratedRotation(2));
		profile["x"].set<double>(ctx.calibratedTranslation(0));
		profile["y"].set<double>(ctx.calibratedTranslation(1));
		profile["z"].set<double>(ctx.calibratedTranslation(2));
		profile["scale"].set<double>(ctx.calibratedScale);
		WriteStandby(ctx.referenceStandby, profile["reference_device"]);
		WriteStandby(ctx.targetStandby, profile["target_device"]);
		bool isInContinuousCalibrationMode = ctx.state == CalibrationState::Continuous || ctx.state == CalibrationState::ContinuousStandby;
		profile["autostart_continuous_calibration"].set<bool>(isInContinuousCalibrationMode);

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
		profile["relative_transform"].set<picojson::object>(refToTarget);
	}

	profile["quash_target_in_continuous"].set<bool>(ctx.quashTargetInContinuous);
	profile["require_trigger_press_to_apply"].set<bool>(ctx.requireTriggerPressToApply);
	profile["ignore_outliers"].set<bool>(ctx.ignoreOutliers);
	SetFinite(profile, "continuous_calibration_target_offset_x", ctx.continuousCalibrationOffset(0));
	SetFinite(profile, "continuous_calibration_target_offset_y", ctx.continuousCalibrationOffset(1));
	SetFinite(profile, "continuous_calibration_target_offset_z", ctx.continuousCalibrationOffset(2));
	profile["static_calibration"].set<bool>(ctx.enableStaticRecalibration);
	SetFinite(profile, "jitter_threshold", (double)ctx.jitterThreshold);
	SetFinite(profile, "max_relative_error_threshold", (double)ctx.maxRelativeErrorThreshold);

	double speed = (int)ctx.calibrationSpeed;
	profile["calibration_speed"].set<double>(speed);

	if (ChaperoneIsWritable(ctx.chaperone)) {
		picojson::object chaperone;
		chaperone["auto_apply"].set<bool>(ctx.chaperone.autoApply);
		chaperone["play_space_size"].set<picojson::array>(FloatArray(ctx.chaperone.playSpaceSize.v, 2));

		chaperone["standing_center"].set<picojson::array>(
		    FloatArray((float*)ctx.chaperone.standingCenter.m, sizeof(ctx.chaperone.standingCenter.m) / sizeof(float)));

		chaperone["geometry"].set<picojson::array>(FloatArray(
		    (float*)ctx.chaperone.geometry.data(), sizeof(ctx.chaperone.geometry[0]) / sizeof(float) * ctx.chaperone.geometry.size()));

		profile["chaperone"].set<picojson::object>(chaperone);
	}

	profile["lock_relative_position"].set<bool>(ctx.lockRelativePosition);

	picojson::value profileV;
	profileV.set<picojson::object>(profile);

	picojson::array profiles;
	profiles.push_back(profileV);

	picojson::value profilesV;
	profilesV.set<picojson::array>(profiles);

	out << profilesV.serialize(true);
}

static void LogRegistryResult(const char* action, const std::string& valueName, LSTATUS result)
{
	char* message = nullptr;
	FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, result,
	               LANG_USER_DEFAULT, (LPSTR)&message, 0, nullptr);
	std::string text = message ? message : "";
	if (message) LocalFree(message);
	while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) {
		text.pop_back();
	}
	std::cerr << action << " registry value " << valueName << " failed with " << result << (text.empty() ? "" : ": ") << text << '\n';
}

namespace {
	struct RegistryStore
	{
		HKEY root = HKEY_CURRENT_USER_LOCAL_SETTINGS;
		const wchar_t* subkey = L"Software\\OpenVR-SpaceCalibrator";
		unsigned legacyCodePage = 0;
		bool migrated = false;
		std::set<std::string> unreadable;
		spacecal::LastWriteFilter writes;
	};

	RegistryStore registryStore;
	std::mutex registryMutex;

	const wchar_t* const kLegacyTextValues[] = {L"Config", L"ConfigBackup", L"Updates", L"BaseStations", L"Devices"};
}

static LSTATUS ReadRegistryWide(const wchar_t* valueName, std::wstring& out)
{
	for (int attempt = 0; attempt < 3; ++attempt) {
		DWORD size = 0;
		LSTATUS result = RegGetValueW(registryStore.root, registryStore.subkey, valueName, RRF_RT_REG_SZ, nullptr, nullptr, &size);
		if (result != ERROR_SUCCESS) return result;

		std::wstring buffer(size / sizeof(wchar_t) + 1, L'\0');
		DWORD bytes = (DWORD)(buffer.size() * sizeof(wchar_t));
		result = RegGetValueW(registryStore.root, registryStore.subkey, valueName, RRF_RT_REG_SZ, nullptr, buffer.data(), &bytes);
		if (result == ERROR_MORE_DATA) continue;
		if (result != ERROR_SUCCESS) return result;

		buffer.resize(bytes / sizeof(wchar_t));
		while (!buffer.empty() && buffer.back() == L'\0') {
			buffer.pop_back();
		}
		out = std::move(buffer);
		return ERROR_SUCCESS;
	}
	return ERROR_MORE_DATA;
}

static LSTATUS WriteRegistryWide(const wchar_t* valueName, const std::wstring& text)
{
	HKEY hkey = nullptr;
	LSTATUS result = RegCreateKeyExW(registryStore.root, registryStore.subkey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hkey, nullptr);
	if (result != ERROR_SUCCESS) return result;

	const DWORD size = static_cast<DWORD>((text.size() + 1) * sizeof(wchar_t));
	result = RegSetValueExW(hkey, valueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(text.c_str()), size);
	RegCloseKey(hkey);
	return result;
}

static void MigrateLegacyText()
{
	using namespace spacecal::registry_text;

	std::wstring marker;
	const LSTATUS markerResult = ReadRegistryWide(kEncodingMarkerName, marker);
	if (markerResult == ERROR_SUCCESS && marker == kEncodingMarkerValue) return;
	if (ClassifyRead(markerResult) == ReadOutcome::Failed) {
		LogRegistryResult("Reading", "TextEncoding", markerResult);
		return;
	}

	const unsigned codePage = registryStore.legacyCodePage ? registryStore.legacyCodePage : SystemAnsiCodePage();
	if (codePage == 0) {
		std::cerr << "Registry text migration skipped: the system ANSI code page is unknown" << '\n';
		return;
	}

	bool complete = true;
	int repairedCount = 0;
	for (const wchar_t* name : kLegacyTextValues) {
		std::wstring stored;
		const LSTATUS readResult = ReadRegistryWide(name, stored);
		const ReadOutcome outcome = ClassifyRead(readResult);
		if (outcome == ReadOutcome::Missing) continue;
		if (outcome == ReadOutcome::Failed) {
			LogRegistryResult("Migrating", WideToUtf8(name), readResult);
			complete = false;
			continue;
		}

		std::wstring repaired;
		if (!RepairLegacyText(stored, codePage, repaired)) continue;
		const LSTATUS writeResult = WriteRegistryWide(name, repaired);
		if (writeResult != ERROR_SUCCESS) {
			LogRegistryResult("Migrating", WideToUtf8(name), writeResult);
			complete = false;
			continue;
		}
		++repairedCount;
	}

	if (!complete) {
		std::cerr << "Registry text migration incomplete, retrying next start" << '\n';
		return;
	}
	const LSTATUS markerWrite = WriteRegistryWide(kEncodingMarkerName, kEncodingMarkerValue);
	if (markerWrite != ERROR_SUCCESS) {
		LogRegistryResult("Writing", "TextEncoding", markerWrite);
		return;
	}
	std::cout << "Registry text migration from code page " << codePage << ": " << repairedCount << " value(s) repaired" << '\n';
}

static void EnsureRegistryTextMigrated()
{
	if (registryStore.migrated) return;
	registryStore.migrated = true;
	MigrateLegacyText();
}

namespace {
	struct RegistryRead
	{
		spacecal::registry_text::ReadOutcome outcome;
		std::string text;
	};
}

static RegistryRead ReadRegistryValue(const char* valueName)
{
	using namespace spacecal::registry_text;

	std::lock_guard<std::mutex> lock(registryMutex);
	EnsureRegistryTextMigrated();

	std::wstring wide;
	const LSTATUS result = ReadRegistryWide(Utf8ToWide(valueName).c_str(), wide);
	const ReadOutcome outcome = ClassifyRead(result);
	if (BlocksWrites(outcome)) {
		LogRegistryResult("Reading", valueName, result);
		if (registryStore.unreadable.insert(valueName).second) {
			std::cerr << "Not saving " << valueName << " this session so its stored data is kept" << '\n';
		}
	}
	if (outcome != ReadOutcome::Found) return {outcome, ""};
	return {outcome, WideToUtf8(wide)};
}

static std::string ReadRegistryKey(const char* valueName)
{
	return ReadRegistryValue(valueName).text;
}

static void WriteRegistryKey(const char* valueName, const std::string& str)
{
	using namespace spacecal::registry_text;

	std::lock_guard<std::mutex> lock(registryMutex);
	EnsureRegistryTextMigrated();

	if (registryStore.unreadable.count(valueName)) return;
	if (registryStore.writes.Unchanged(valueName, str)) return;

	const LSTATUS result = WriteRegistryWide(Utf8ToWide(valueName).c_str(), Utf8ToWide(str));
	if (result != ERROR_SUCCESS) {
		LogRegistryResult("Writing", valueName, result);
		return;
	}
	registryStore.writes.Record(valueName, str);
}

static void LoadProfileText(CalibrationContext& ctx, const std::string& str, const std::function<void(const std::string&)>& backup)
{
	ctx.validProfile = false;

	if (str == "") {
		std::cout << "Profile is empty" << '\n';
		ctx.Clear();
		return;
	}

	try {
		std::stringstream io(str);
		ParseProfile(ctx, io);
		std::cout << (ctx.validProfile ? "Loaded profile" : "Loaded settings, profile has no calibration") << '\n';
	}
	catch (const std::exception& e) {
		std::cerr << "Error loading profile: " << e.what() << '\n';
		ctx.validProfile = false;
		backup(str);
	}
}

void LoadProfile(CalibrationContext& ctx)
{
	// @TODO: Rewrite this to migrate configs from the registry to the spacecal directory
	//        I don't know why whoever wrote this thought writing to the registry in the 2020s was a good idea...
	//        NOTE: HKEY_CURRENT_USER_LOCAL_SETTINGS evaluates to	HKCU\Software\Classes\Local Settings
	//              Settings are currently stored at				HKCU\Software\Classes\Local Settings\Software\OpenVR-SpaceCalibrator

	ctx.validProfile = false;

	const RegistryRead config = ReadRegistryValue("Config");
	if (config.outcome == spacecal::registry_text::ReadOutcome::Failed) {
		std::cerr << "Profile could not be read, starting with defaults" << '\n';
		return;
	}

	LoadProfileText(ctx, config.text, [](const std::string& raw) {
		std::cerr << "Copying the unreadable profile to ConfigBackup" << '\n';
		WriteRegistryKey("ConfigBackup", raw);
	});
}

void SaveProfile(CalibrationContext& ctx)
{
	std::cout << "Saving profile to registry" << '\n';

	static bool warnedNonFinite = false;
	if (ctx.validProfile && !CalibrationIsWritable(ctx) && !warnedNonFinite) {
		std::cerr << "Calibration has non-finite values, saving settings only" << '\n';
		warnedNonFinite = true;
	}

	std::stringstream io;
	WriteProfile(ctx, io);
	const std::string text = io.str();
	if (text.empty()) {
		std::cerr << "Not saving an empty profile" << '\n';
		return;
	}
	WriteRegistryKey("Config", text);
}

void LoadUpdateSettings(UpdateSettings& settings)
{
	settings = UpdateSettings();
	auto str = ReadRegistryKey("Updates");
	if (str.empty()) {
		return;
	}
	picojson::value v;
	auto err = picojson::parse(v, str);
	if (!err.empty() || !v.is<picojson::object>()) {
		std::cerr << "Error loading update settings: " << err << '\n';
		return;
	}
	auto obj = v.get<picojson::object>();
	if (obj["check_on_startup"].is<bool>()) {
		settings.checkOnStartup = obj["check_on_startup"].get<bool>();
	}
	if (obj["auto_install"].is<bool>()) {
		settings.autoInstall = obj["auto_install"].get<bool>();
	}
	if (obj["skipped_tag"].is<std::string>()) {
		settings.skippedTag = obj["skipped_tag"].get<std::string>();
	}
}

void SaveUpdateSettings(const UpdateSettings& settings)
{
	picojson::object obj;
	obj["check_on_startup"].set<bool>(settings.checkOnStartup);
	obj["auto_install"].set<bool>(settings.autoInstall);
	obj["skipped_tag"].set<std::string>(settings.skippedTag);
	WriteRegistryKey("Updates", picojson::value(obj).serialize());
}

void LoadBaseStationsSettings(spacecal::basestations::BaseStationsSettings& settings)
{
	settings = spacecal::basestations::ParseBaseStationsSettings(ReadRegistryKey("BaseStations"));
}

void SaveBaseStationsSettings(const spacecal::basestations::BaseStationsSettings& settings)
{
	WriteRegistryKey("BaseStations", spacecal::basestations::SerializeBaseStationsSettings(settings));
}

void LoadKnownDevices(std::vector<KnownDevice>& devices)
{
	devices.clear();
	auto str = ReadRegistryKey("Devices");
	if (str.empty()) {
		return;
	}
	picojson::value v;
	auto err = picojson::parse(v, str);
	if (!err.empty() || !v.is<picojson::array>()) {
		std::cerr << "Error loading known devices: " << err << '\n';
		return;
	}
	for (auto& entry : v.get<picojson::array>()) {
		if (!entry.is<picojson::object>()) continue;
		auto obj = entry.get<picojson::object>();
		KnownDevice device;
		if (obj["tracking_system"].is<std::string>()) device.trackingSystem = obj["tracking_system"].get<std::string>();
		if (obj["model"].is<std::string>()) device.model = obj["model"].get<std::string>();
		if (obj["serial"].is<std::string>()) device.serial = obj["serial"].get<std::string>();
		if (obj["device_class"].is<double>()) device.deviceClass = (vr::TrackedDeviceClass)(int)obj["device_class"].get<double>();
		if (!device.serial.empty() || !device.model.empty()) {
			devices.push_back(std::move(device));
		}
	}
}

void SaveKnownDevices(const std::vector<KnownDevice>& devices)
{
	picojson::array arr;
	for (const auto& device : devices) {
		picojson::object obj;
		obj["tracking_system"].set<std::string>(device.trackingSystem);
		obj["model"].set<std::string>(device.model);
		obj["serial"].set<std::string>(device.serial);
		const double deviceClass = (double)device.deviceClass;
		obj["device_class"].set<double>(deviceClass);
		picojson::value entry;
		entry.set<picojson::object>(obj);
		arr.push_back(entry);
	}
	picojson::value v;
	v.set<picojson::array>(arr);
	WriteRegistryKey("Devices", v.serialize());
}
