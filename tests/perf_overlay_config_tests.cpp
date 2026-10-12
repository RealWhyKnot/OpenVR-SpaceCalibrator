#include "perf_harness.h"

#include <functional>
#include <iostream>
#include <sstream>
#include <thread>

#include "../src/overlay/Configuration.cpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace {

	CalibrationContext RealisticProfile(size_t chaperoneQuads)
	{
		CalibrationContext ctx;
		ctx.validProfile = true;
		ctx.enabled = true;
		ctx.referenceTrackingSystem = "oculus";
		ctx.targetTrackingSystem = "lighthouse";
		ctx.calibratedRotation = Eigen::Vector3d(0.31234567891, 37.1234567891, -0.5123456789);
		ctx.calibratedTranslation = Eigen::Vector3d(42.123456789, -8.987654321, 130.55555555);
		ctx.calibratedScale = 1.0;
		ctx.referenceStandby = {"oculus", "Oculus Quest 3", "2G0YC1ZF8J0123"};
		ctx.targetStandby = {"lighthouse", "VIVE Tracker 3.0 MV", "LHR-1A2B3C4D"};
		ctx.state = CalibrationState::Continuous;
		ctx.smoothingParams.strength = 40;
		ctx.fingerSmoothing.strength = 30;
		ctx.stickOn[0] = true;
		ctx.chaperone.valid = true;
		ctx.chaperone.playSpaceSize = {3.2f, 2.8f};
		ctx.chaperone.geometry.resize(chaperoneQuads);
		for (size_t i = 0; i < chaperoneQuads; ++i) {
			for (int c = 0; c < 4; ++c) {
				const double a = (i + (c & 1)) * 6.2831853 / chaperoneQuads;
				ctx.chaperone.geometry[i].vCorners[c].v[0] = (float)(1.6 * std::cos(a));
				ctx.chaperone.geometry[i].vCorners[c].v[1] = c < 2 ? 0.0f : 2.43f;
				ctx.chaperone.geometry[i].vCorners[c].v[2] = (float)(1.4 * std::sin(a));
			}
		}
		Eigen::AffineCompact3d rel = Eigen::AffineCompact3d::Identity();
		rel.linear() =
		    (Eigen::AngleAxisd(0.2, Eigen::Vector3d::UnitX()) * Eigen::AngleAxisd(-0.4, Eigen::Vector3d::UnitY())).toRotationMatrix();
		rel.translation() = Eigen::Vector3d(0.05, -0.12, 0.08);
		ctx.refToTargetPose = rel;
		ctx.relativePosCalibrated = true;
		return ctx;
	}

	std::string Serialize(CalibrationContext& ctx)
	{
		std::stringstream io;
		WriteProfile(ctx, io);
		return io.str();
	}

	void TestProfileSerialization()
	{
		for (const size_t quads : {4u, 40u, 200u}) {
			CalibrationContext ctx = RealisticProfile(quads);
			const std::string json = Serialize(ctx);
			const auto writeHeap = perf::MeasureHeap([&] { Serialize(ctx); });
			const double writeUs = perf::BestSecondsPerOp(5, 50, [&](int) { Serialize(ctx); }) * 1e6;

			CalibrationContext parsed;
			const auto parseHeap = perf::MeasureHeap([&] {
				std::stringstream io(json);
				ParseProfile(parsed, io);
			});
			const double parseUs = perf::BestSecondsPerOp(5, 50,
			                                              [&](int) {
				                                              CalibrationContext scratch;
				                                              std::stringstream io(json);
				                                              ParseProfile(scratch, io);
			                                              }) *
			                       1e6;
			const std::string again = Serialize(parsed);

			spacecal::LastWriteFilter filter;
			filter.Record("Config", json);
			std::string probe = json;
			int unchanged = 0;
			const double compareUs = perf::BestSecondsPerOp(5, 2000,
			                                                [&](int i) {
				                                                probe[probe.size() / 2] = json[json.size() / 2] + (char)(i & 1);
				                                                unchanged += filter.Unchanged("Config", probe) ? 1 : 0;
			                                                }) *
			                         1e6;
			PERF_CHECK(unchanged > 0);

			std::printf(
			    "INFO profile, %zu chaperone quads: %zu bytes JSON; save path (build+serialize) %.1f us %llu allocs; load path (parse) "
			    "%.1f us %llu allocs; unchanged-check %.2f us\n",
			    quads, json.size(), writeUs, (unsigned long long)writeHeap.allocations, parseUs, (unsigned long long)parseHeap.allocations,
			    compareUs);
			PERF_CHECK(again.size() > 0);
			if (quads == 40u) {
				perf::Budget("profile save path, 40 quads (us)", writeUs, 20000.0, "us");
				perf::Report("profile save path at 2 continuous computes/s (% of a core)", writeUs * 2.0 / 1e6 * 100.0, "%");
			}
		}

		CalibrationContext ctx = RealisticProfile(40);
		const std::string first = Serialize(ctx);
		CalibrationContext parsed;
		std::stringstream io(first);
		ParseProfile(parsed, io);
		const std::string second = Serialize(parsed);
		CalibrationContext reparsed;
		std::stringstream io2(second);
		ParseProfile(reparsed, io2);
		const std::string third = Serialize(reparsed);
		std::printf("INFO profile round trip: write->parse->write %s, second round trip %s\n", first == second ? "identical" : "differs",
		            second == third ? "identical" : "differs");
		auto showDiff = [](const char* what, const std::string& a, const std::string& b) {
			size_t at = 0;
			while (at < a.size() && at < b.size() && a[at] == b[at])
				++at;
			if (at == a.size() && at == b.size()) return;
			const size_t from = at > 60 ? at - 60 : 0;
			auto flat = [from](const std::string& s) {
				std::string out = s.substr(from, 120);
				for (char& ch : out) {
					if (ch == '\n' || ch == '\r' || ch == '\t') ch = ' ';
				}
				return out;
			};
			std::printf("INFO %s first difference at byte %zu:\n  before: %s\n  after:  %s\n", what, at, flat(a).c_str(), flat(b).c_str());
		};
		showDiff("profile round trip 1", first, second);
		showDiff("profile round trip 2", second, third);
		const double drift = (reparsed.refToTargetPose.matrix() - parsed.refToTargetPose.matrix()).cwiseAbs().maxCoeff();
		const double paramLoss = std::abs(parsed.alignmentSpeedParams.thr_rot_large - ctx.alignmentSpeedParams.thr_rot_large);
		std::printf(
		    "INFO profile round trip: relative_transform moves %.3g per save/load cycle; alignment params lose %.3g on the first load "
		    "(float cast)\n",
		    drift, paramLoss);
		PERF_CHECK(drift < 1e-12);
		PERF_CHECK(paramLoss < 1e-6);
	}

	void TestRegistryWriteCost()
	{
		wchar_t temp[MAX_PATH] = {};
		GetTempPathW(MAX_PATH, temp);
		const std::filesystem::path dir = std::filesystem::path(temp) / L"wk" / L"polish-perf-overlay" / L"hive";
		std::error_code ec;
		std::filesystem::create_directories(dir, ec);
		const std::filesystem::path hivePath = dir / L"perf_profile.dat";
		HKEY hive = nullptr;
		const LSTATUS loaded = RegLoadAppKeyW(hivePath.c_str(), &hive, KEY_ALL_ACCESS, 0, 0);
		if (loaded != ERROR_SUCCESS) {
			std::printf("INFO registry write cost skipped: RegLoadAppKey failed with %ld\n", (long)loaded);
			return;
		}

		CalibrationContext ctx = RealisticProfile(40);
		std::string json = Serialize(ctx);
		int flip = 0;
		auto save = [&](const std::string& value) {
			HKEY key = nullptr;
			if (RegCreateKeyExA(hive, "Software\\OpenVR-SpaceCalibrator", 0, REG_NONE, 0, KEY_ALL_ACCESS, 0, &key, 0) != ERROR_SUCCESS)
				return;
			RegSetValueExA(key, "Config", 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), (DWORD)(value.size() + 1));
			RegCloseKey(key);
		};
		save(json);
		const double changedUs = perf::BestSecondsPerOp(5, 50,
		                                                [&](int) {
			                                                json[json.size() - 3] = (flip++ & 1) ? '1' : '2';
			                                                save(json);
		                                                }) *
		                         1e6;
		std::printf(
		    "INFO registry save of a %zu byte profile (private app hive, same calls as WriteRegistryKey): %.1f us per changed write\n",
		    json.size(), changedUs);
		RegCloseKey(hive);
	}

	void TestDebugOutputCost()
	{
		const bool debugger = IsDebuggerPresent() != FALSE;
		const double us = perf::BestSecondsPerOp(5, 200, [](int) { OutputDebugStringA("Waiting for trigger press...\n"); }) * 1e6;
		std::printf("INFO CalibrationContext::Log OutputDebugStringA: %.2f us per message (debugger attached: %s)\n", us,
		            debugger ? "yes" : "no");
	}

	void TestPipeRoundTrip()
	{
		const std::wstring name = L"\\\\.\\pipe\\SpaceCalPerfAudit_" + std::to_wstring(GetCurrentProcessId());
		HANDLE server = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX, PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, 1,
		                                 sizeof(protocol::Response), sizeof(protocol::Request), 0, nullptr);
		if (server == INVALID_HANDLE_VALUE) {
			std::printf("INFO pipe round trip skipped: CreateNamedPipe failed with %lu\n", GetLastError());
			return;
		}
		std::thread echo([server] {
			if (!ConnectNamedPipe(server, nullptr) && GetLastError() != ERROR_PIPE_CONNECTED) return;
			protocol::Request request;
			DWORD read = 0;
			while (ReadFile(server, &request, sizeof request, &read, nullptr) && read == sizeof request) {
				protocol::Response response(protocol::ResponseSmoothingStats);
				response.smoothingStats = {};
				DWORD written = 0;
				if (!WriteFile(server, &response, sizeof response, &written, nullptr)) break;
			}
		});

		HANDLE client = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
		if (client == INVALID_HANDLE_VALUE) {
			std::printf("INFO pipe round trip skipped: CreateFile failed with %lu\n", GetLastError());
			CloseHandle(server);
			echo.join();
			return;
		}
		DWORD mode = PIPE_READMODE_MESSAGE;
		SetNamedPipeHandleState(client, &mode, nullptr, nullptr);
		auto roundTrip = [client] {
			protocol::Request request(protocol::RequestGetSmoothingStats);
			request.getSmoothingStats.openVRID = 5;
			DWORD written = 0;
			WriteFile(client, &request, sizeof request, &written, nullptr);
			protocol::Response response;
			DWORD read = 0;
			ReadFile(client, &response, sizeof response, &read, nullptr);
		};
		roundTrip();
		const double us = perf::BestSecondsPerOp(5, 200, [&](int) { roundTrip(); }) * 1e6;
		std::printf("INFO blocking pipe round trip (%zu byte request, %zu byte response, in-process echo thread): %.1f us\n",
		            sizeof(protocol::Request), sizeof(protocol::Response), us);
		CloseHandle(client);
		echo.join();
		CloseHandle(server);
	}

}

int main()
{
	PERF_CHECK(perf::InstallHeapMeter());

	TestProfileSerialization();
	TestRegistryWriteCost();
	TestDebugOutputCost();
	TestPipeRoundTrip();

	perf::RemoveHeapMeter();
	MH_Uninitialize();
	return perf::Finish("perf_overlay_config_tests");
}
