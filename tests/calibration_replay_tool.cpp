#include <iostream>

#include "Calibration.h"
#include "CalibrationRecording.h"
#include "calibration_replay_score.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string_view>

CalibrationContext CalCtx;

namespace {

	bool ParseSwitch(std::string_view arg, std::string_view name, std::optional<bool>& out)
	{
		if (arg.size() != name.size() + 1 || arg.substr(0, name.size()) != name) return false;
		const char value = arg.back();
		if (value != '0' && value != '1') return false;
		out = value == '1';
		return true;
	}

	void PrintScore(const calscore::Score& score)
	{
		std::printf("  held-out offset spread per 30 s block: median %.2f mm, p95 %.2f mm (%zu blocks)\n", score.blockMedianMm,
		            score.blockP95Mm, score.blocks);
		std::printf("  held-out rotation spread per 30 s block: median %.3f deg, p95 %.3f deg\n", score.blockMedianDeg, score.blockP95Deg);
		std::printf("  offset from the whole-run median: rms %.2f mm, p95 %.2f mm (%zu samples)\n", score.wholeRmsMm, score.wholeP95Mm,
		            score.scoredSamples);
		std::printf("  calibration moved %zu times: total %.1f mm / %.2f deg, largest %.2f mm / %.3f deg\n", score.accepted,
		            score.churnTotalMm, score.churnTotalDeg, score.churnMaxMm, score.churnMaxDeg);
		std::printf("  gap to the reference fit at the headset: median %.2f mm, p95 %.2f mm (%zu samples)\n", score.headMedianMm,
		            score.headP95Mm, score.referenceSamples);
		std::printf("  gap to the reference fit 1 m below the headset: median %.2f mm, p95 %.2f mm\n", score.lowMedianMm, score.lowP95Mm);
		std::printf("  rotation gap to the reference fit: median %.3f deg, p95 %.3f deg\n", score.rotationMedianDeg, score.rotationP95Deg);
	}

	double Middle(std::vector<double> values, double fraction)
	{
		return calscore::detail::Percentile(std::move(values), fraction);
	}

	std::vector<calref::Fit> BuildReference(const calrec::ReplayReport& report)
	{
		const std::vector<calref::Row> rows = calref::BuildRows(report.kept);
		Eigen::Matrix3d start = Eigen::Matrix3d::Identity();
		for (const calrec::ReplayedSolve& solve : report.solves) {
			if (solve.recorded.valid) {
				start = solve.recorded.transformation.linear();
				break;
			}
		}

		const calref::Fit whole = calref::FitRows(rows.data(), rows.size(), start, true);
		const calref::Fit wholeNoLatency = calref::FitRows(rows.data(), rows.size(), start, false);
		const Eigen::Matrix3d yawOnly = Eigen::AngleAxisd(whole.yawDeg * EIGEN_PI / 180.0, Eigen::Vector3d::UnitY()).toRotationMatrix();
		const calref::Fit wholeYawOnly = calref::FitRows(rows.data(), rows.size(), yawOnly, true, false);
		std::printf("  whole-run fit: yaw %.3f deg, tilt %.3f deg, target lag %.1f ms, rms %.2f mm over %zu samples\n", whole.yawDeg,
		            whole.tiltDeg, whole.latency * 1000.0, whole.rmsMm, whole.used);
		const Eigen::Vector3d refUp = whole.calibration.linear().transpose() * Eigen::Vector3d::UnitY();
		std::printf("  reference up seen from target space: x %.5f, z %.5f\n", refUp.x(), refUp.z());
		std::printf("  whole-run fit without the lag term: rms %.2f mm; with tilt forced to zero: rms %.2f mm\n", wholeNoLatency.rmsMm,
		            wholeYawOnly.rmsMm);

		std::vector<calref::Fit> fits = calref::FitWindows(rows, whole.calibration.linear(), 120.0, 30.0, true);
		std::vector<double> tilt, lag, rms;
		for (const calref::Fit& fit : fits) {
			tilt.push_back(fit.tiltDeg);
			lag.push_back(fit.latency * 1000.0);
			rms.push_back(fit.rmsMm);
		}
		std::printf("  4-minute window fits (%zu): tilt median %.3f deg (p5 %.3f, p95 %.3f), lag median %.1f ms (p5 %.1f, p95 %.1f), rms "
		            "median %.2f mm\n",
		            fits.size(), Middle(tilt, 0.5), Middle(tilt, 0.05), Middle(tilt, 0.95), Middle(lag, 0.5), Middle(lag, 0.05),
		            Middle(lag, 0.95), Middle(rms, 0.5));
		return fits;
	}

}

int main(int argc, char** argv)
{
	calrec::ReplayOptions options;
	std::optional<bool> tilt;
	bool score = false;
	int firstFile = 1;
	for (; firstFile < argc; ++firstFile) {
		const std::string_view arg = argv[firstFile];
		if (arg == "--score") {
			score = true;
		}
		else if (!ParseSwitch(arg, "--lock=", options.lockRelative) && !ParseSwitch(arg, "--static=", options.enableStatic) &&
		         !ParseSwitch(arg, "--tilt=", tilt)) {
			break;
		}
	}
	options.keepSamples = score;

	if (firstFile >= argc || std::string_view(argv[firstFile]).substr(0, 2) == "--") {
		std::fprintf(stderr,
		             "usage: calibration_replay_tool [--lock=0|1] [--static=0|1] [--tilt=0|1] [--score] <spacecal_replay.*.txt>...\n");
		return 2;
	}

	int status = 0;
	for (int i = firstFile; i < argc; ++i) {
		std::ifstream in(argv[i], std::ios::binary);
		if (!in) {
			std::printf("%s: can't open\n", argv[i]);
			status = 2;
			continue;
		}

		CalibrationCalc calc;
		if (tilt) calc.solveTilt = *tilt;
		const calrec::ReplayReport report = calrec::Replay(in, calc, options);

		size_t matches = 0, accepted = 0, firstMismatch = 0;
		double worstMm = 0.0, worstDeg = 0.0;
		for (const calrec::ReplayedSolve& solve : report.solves) {
			if (solve.replayed.returned) ++accepted;
			if (calrec::SameOutcome(solve.recorded, solve.replayed)) {
				++matches;
			}
			else if (firstMismatch == 0) {
				firstMismatch = solve.line;
			}
			worstMm = std::max(worstMm, calrec::TranslationGapMm(solve.recorded.transformation, solve.replayed.transformation));
			worstDeg = std::max(worstDeg, calrec::RotationGapDeg(solve.recorded.transformation, solve.replayed.transformation));
		}

		std::printf("%s: %zu samples, %zu solves (%zu accepted now), %zu match the recording, worst gap %.4f mm / %.4f deg\n", argv[i],
		            report.samples, report.solves.size(), accepted, matches, worstMm, worstDeg);
		if (firstMismatch) std::printf("  first difference on line %zu\n", firstMismatch);
		if (score) {
			const std::vector<calref::Fit> reference = BuildReference(report);
			PrintScore(calscore::ScoreReplay(report, &reference));
		}
		if (!report.ok) {
			std::printf("  stopped: %s\n", report.error.c_str());
			status = 2;
		}
		else if (matches != report.solves.size()) {
			status = std::max(status, 1);
		}
	}
	return status;
}
