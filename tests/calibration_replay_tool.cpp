#include <iostream>

#include "Calibration.h"
#include "CalibrationRecording.h"

#include <algorithm>
#include <cstdio>
#include <fstream>

CalibrationContext CalCtx;

int main(int argc, char** argv)
{
	if (argc < 2) {
		std::fprintf(stderr, "usage: calibration_replay_tool <spacecal_replay.*.txt>...\n");
		return 2;
	}

	int status = 0;
	for (int i = 1; i < argc; ++i) {
		std::ifstream in(argv[i], std::ios::binary);
		if (!in) {
			std::printf("%s: can't open\n", argv[i]);
			status = 2;
			continue;
		}

		CalibrationCalc calc;
		const calrec::ReplayReport report = calrec::Replay(in, calc);

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
