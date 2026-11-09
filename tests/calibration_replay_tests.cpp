#include <iostream>

#include "Calibration.h"
#include "CalibrationMetrics.h"
#include "CalibrationRecording.h"
#include "calibration_scenario.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <sstream>
#include <string>
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

	constexpr size_t kWindow = 100;

	calrec::SolveOutcome Capture(const CalibrationCalc& calc, bool returned, bool lerp)
	{
		calrec::SolveOutcome outcome;
		outcome.returned = returned;
		outcome.lerp = lerp;
		outcome.valid = calc.isValid();
		outcome.relativeCalibrated = calc.isRelativeTransformationCalibrated();
		outcome.transformation = calc.Transformation();
		outcome.relative = calc.RelativeTransformation();
		return outcome;
	}

	struct Session
	{
		std::vector<calrec::SolveOutcome> outcomes;
		size_t accepted = 0;
	};

	void RunContinuous(CalibrationCalc& calc, const Scenario& s, size_t from, size_t to, Session& session)
	{
		for (size_t i = from; i < to; ++i) {
			calc.PushSample(s.samples[i]);
			if (calc.SampleCount() < kWindow) continue;
			while (calc.SampleCount() > kWindow)
				calc.ShiftSample();
			if (i % 5 != 0) continue;

			calc.lockRelativePosition = i >= 300 && i < 340;
			calc.enableStaticRecalibration = i < 360;
			bool lerp = false;
			const bool returned = calc.ComputeIncremental(lerp, 1.5, 0.005, false);
			session.outcomes.push_back(Capture(calc, returned, lerp));
			if (returned) ++session.accepted;

			if (i == 250) {
				for (size_t k = 0; k < kWindow / 10; ++k)
					calc.ShiftSample();
			}
		}
	}

	void StartContinuous(CalibrationCalc& calc)
	{
		calc.Clear();
		calc.setRelativeTransformation(Eigen::AffineCompact3d::Identity(), false);
	}

	calrec::ReplayReport ReplayText(const std::string& text, CalibrationCalc& calc)
	{
		std::istringstream in(text);
		return calrec::Replay(in, calc);
	}

	size_t CountMatches(const calrec::ReplayReport& report, const Session& session)
	{
		size_t matches = 0;
		for (size_t i = 0; i < report.solves.size() && i < session.outcomes.size(); ++i) {
			const bool recorded = calrec::SameOutcome(report.solves[i].recorded, session.outcomes[i]);
			const bool replayed = calrec::SameOutcome(report.solves[i].replayed, session.outcomes[i]);
			if (recorded && replayed) {
				++matches;
			}
			else if (matches == i) {
				std::printf("  solve %zu (line %zu) differs: recorded %d, replayed %d\n", i, report.solves[i].line, (int)recorded,
				            (int)replayed);
			}
		}
		return matches;
	}

	void TestContinuousSessionReplaysExactly()
	{
		const Scenario s = MakeScenario(400, 5, 0.0005);
		CalibrationCalc live;
		std::ostringstream log;
		calrec::Recorder recorder(log);
		recorder.Begin(live);
		live.observer = &recorder;
		StartContinuous(live);
		Session session;
		RunContinuous(live, s, 0, s.samples.size(), session);
		live.observer = nullptr;

		CalibrationCalc replayed;
		const calrec::ReplayReport report = ReplayText(log.str(), replayed);
		CHECK(report.ok);
		if (!report.ok) std::printf("  %s\n", report.error.c_str());
		CHECK(report.samples == s.samples.size());
		CHECK(report.solves.size() == session.outcomes.size());
		CHECK(CountMatches(report, session) == session.outcomes.size());
		CHECK(session.accepted > 0);
		CHECK(session.accepted < session.outcomes.size());
		CHECK(replayed.SampleCount() == live.SampleCount());

		const double mm = calrec::TranslationGapMm(live.Transformation(), s.calibration);
		const double deg = calrec::RotationGapDeg(live.Transformation(), s.calibration);
		CHECK(live.isValid());
		CHECK(mm < 5.0);
		CHECK(deg < 0.5);
		std::printf("  continuous: %zu solves, %zu accepted, all replayed exactly; %.3f mm and %.4f deg from the truth\n",
		            session.outcomes.size(), session.accepted, mm, deg);
	}

	void TestRecordingStartedMidSessionReplaysExactly()
	{
		const Scenario s = MakeScenario(400, 9, 0.0005);
		CalibrationCalc live;
		StartContinuous(live);
		Session before;
		RunContinuous(live, s, 0, 220, before);
		CHECK(live.isValid());

		std::ostringstream log;
		calrec::Recorder recorder(log);
		recorder.Begin(live);
		live.observer = &recorder;
		Session after;
		RunContinuous(live, s, 220, s.samples.size(), after);
		live.observer = nullptr;

		CalibrationCalc replayed;
		const calrec::ReplayReport report = ReplayText(log.str(), replayed);
		CHECK(report.ok);
		if (!report.ok) std::printf("  %s\n", report.error.c_str());
		CHECK(report.solves.size() == after.outcomes.size());
		CHECK(CountMatches(report, after) == after.outcomes.size());
		std::printf("  mid-session: %zu solves after the snapshot, %zu accepted\n", after.outcomes.size(), after.accepted);
	}

	void TestOneshotReplaysExactly()
	{
		const Scenario s = MakeScenario(100, 21, 0.0005);
		CalibrationCalc live;
		std::ostringstream log;
		calrec::Recorder recorder(log);
		recorder.Begin(live);
		live.observer = &recorder;
		live.Clear();
		live.enableStaticRecalibration = false;
		for (const Sample& sample : s.samples)
			live.PushSample(sample);
		Session session;
		for (const bool ignoreOutliers : {false, true}) {
			const bool returned = live.ComputeOneshot(ignoreOutliers);
			session.outcomes.push_back(Capture(live, returned, false));
		}
		live.observer = nullptr;

		CalibrationCalc replayed;
		const calrec::ReplayReport report = ReplayText(log.str(), replayed);
		CHECK(report.ok);
		CHECK(report.solves.size() == 2);
		CHECK(CountMatches(report, session) == 2);
		CHECK(!report.solves.empty() && !report.solves[0].call.incremental);
		CHECK(report.solves.size() == 2 && report.solves[1].call.ignoreOutliers);
	}

	void TestCrlfRecordingReplays()
	{
		const Scenario s = MakeScenario(120, 13, 0.0005);
		CalibrationCalc live;
		std::ostringstream log;
		calrec::Recorder recorder(log);
		recorder.Begin(live);
		live.observer = &recorder;
		StartContinuous(live);
		Session session;
		RunContinuous(live, s, 0, s.samples.size(), session);
		live.observer = nullptr;

		std::string text;
		for (const char c : log.str()) {
			if (c == '\n') text += '\r';
			text += c;
		}
		CalibrationCalc replayed;
		const calrec::ReplayReport report = ReplayText(text, replayed);
		CHECK(report.ok);
		CHECK(CountMatches(report, session) == session.outcomes.size());
	}

	void TestReplayDoesNotRecordItself()
	{
		const Scenario s = MakeScenario(120, 17, 0.0005);
		CalibrationCalc live;
		std::ostringstream log;
		calrec::Recorder recorder(log);
		recorder.Begin(live);
		live.observer = &recorder;
		StartContinuous(live);
		Session session;
		RunContinuous(live, s, 0, s.samples.size(), session);
		live.observer = nullptr;

		CalibrationCalc replayed;
		std::ostringstream echo;
		calrec::Recorder echoRecorder(echo);
		replayed.observer = &echoRecorder;
		const calrec::ReplayReport report = ReplayText(log.str(), replayed);
		CHECK(report.ok);
		CHECK(echo.str().empty());
		CHECK(replayed.observer == &echoRecorder);
	}

	void TestNumbersRoundTripBitForBit()
	{
		const double values[] = {0.1,
		                         -0.0,
		                         1e-300,
		                         std::numeric_limits<double>::denorm_min(),
		                         std::numeric_limits<double>::max(),
		                         -std::numeric_limits<double>::infinity(),
		                         EIGEN_PI,
		                         -1.0 / 3.0,
		                         2.5e-310,
		                         123456789.123456789,
		                         -7e22,
		                         std::numeric_limits<double>::quiet_NaN()};
		Sample sample;
		sample.valid = true;
		sample.timestamp = 1.0 / 7.0;
		int k = 0;
		for (int r = 0; r < 3; ++r) {
			for (int c = 0; c < 3; ++c) {
				sample.ref.rot(r, c) = values[k++ % 12];
				sample.target.rot(r, c) = values[(k + 5) % 12];
			}
		}
		for (int i = 0; i < 3; ++i) {
			sample.ref.trans(i) = values[k++ % 12];
			sample.target.trans(i) = values[(k + 5) % 12];
		}

		std::ostringstream log;
		calrec::Recorder recorder(log);
		CalibrationCalc empty;
		recorder.Begin(empty);
		recorder.OnSample(sample);

		CalibrationCalc replayed;
		const calrec::ReplayReport report = ReplayText(log.str(), replayed);
		CHECK(report.ok);
		CHECK(replayed.SampleCount() == 1);
		if (replayed.SampleCount() != 1) return;

		const Sample& back = replayed.Samples().front();
		auto same = [](double a, double b) {
			return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b) || (std::isnan(a) && std::isnan(b));
		};
		bool all = same(back.timestamp, sample.timestamp) && back.valid;
		for (int r = 0; r < 3; ++r) {
			for (int c = 0; c < 3; ++c) {
				all = all && same(back.ref.rot(r, c), sample.ref.rot(r, c)) && same(back.target.rot(r, c), sample.target.rot(r, c));
			}
		}
		for (int i = 0; i < 3; ++i) {
			all = all && same(back.ref.trans(i), sample.ref.trans(i)) && same(back.target.trans(i), sample.target.trans(i));
		}
		CHECK(all);
		CHECK(std::signbit(back.ref.rot(0, 1)));
	}

	std::string ValidRecording()
	{
		const Scenario s = MakeScenario(110, 23, 0.0005);
		CalibrationCalc live;
		std::ostringstream log;
		calrec::Recorder recorder(log);
		recorder.Begin(live);
		live.observer = &recorder;
		StartContinuous(live);
		Session session;
		RunContinuous(live, s, 0, s.samples.size(), session);
		return log.str();
	}

	std::string ReplaceLine(const std::string& text, size_t number, const std::string& replacement)
	{
		std::string out;
		size_t line = 1;
		size_t start = 0;
		while (start < text.size()) {
			size_t end = text.find('\n', start);
			if (end == std::string::npos) end = text.size();
			out += line == number ? replacement : text.substr(start, end - start);
			out += '\n';
			start = end + 1;
			++line;
		}
		return out;
	}

	std::string LineOf(const std::string& text, size_t number)
	{
		std::istringstream in(text);
		std::string line;
		for (size_t i = 0; i < number && std::getline(in, line); ++i) {}
		return line;
	}

	size_t FirstLine(const std::string& text, const std::string& prefix)
	{
		std::istringstream in(text);
		std::string line;
		for (size_t n = 1; std::getline(in, line); ++n) {
			if (line.rfind(prefix, 0) == 0) return n;
		}
		return 0;
	}

	void ExpectError(const std::string& text, const std::string& prefix)
	{
		CalibrationCalc calc;
		std::ostringstream echo;
		calrec::Recorder echoRecorder(echo);
		calc.observer = &echoRecorder;
		const calrec::ReplayReport report = ReplayText(text, calc);
		CHECK(!report.ok);
		CHECK(report.error.rfind(prefix, 0) == 0);
		CHECK(calc.observer == &echoRecorder);
		if (report.ok || report.error.rfind(prefix, 0) != 0)
			std::printf("  expected an error starting '%s', got '%s'\n", prefix.c_str(), report.error.c_str());
	}

	void TestMalformedRecordingsAreRejected()
	{
		const std::string good = ValidRecording();
		CalibrationCalc check;
		CHECK(ReplayText(good, check).ok);

		ExpectError("", "empty recording");
		ExpectError(ReplaceLine(good, 1, "spacecal-replay 2"), "line 1:");
		ExpectError(ReplaceLine(good, 1, "something else"), "line 1:");

		ExpectError(ReplaceLine(good, 2, "clear 1"), "line 2:");

		const size_t sampleLine = FirstLine(good, "sample ");
		CHECK(sampleLine > 0);
		if (sampleLine > 0) {
			const std::string sample = LineOf(good, sampleLine);
			const std::string where = "line " + std::to_string(sampleLine) + ":";
			ExpectError(ReplaceLine(good, sampleLine, sample.substr(0, sample.size() / 2)), where);
			ExpectError(ReplaceLine(good, sampleLine, sample + " 1"), where);
			ExpectError(ReplaceLine(good, sampleLine, "sample 2" + sample.substr(8)), where);
			ExpectError(ReplaceLine(good, sampleLine, "sample 1 0.5x" + sample.substr(sample.find(' ', 9))), where);
			ExpectError(ReplaceLine(good, sampleLine, "teleport 1 2 3"), where);
		}

		const size_t solveLine = FirstLine(good, "solve ");
		CHECK(solveLine > 0);
		if (solveLine > 0) {
			const std::string solve = LineOf(good, solveLine);
			const std::string where = "line " + std::to_string(solveLine) + ":";
			ExpectError(ReplaceLine(good, solveLine, solve.substr(0, solve.rfind(' '))), where);
		}
	}

	void TestLoggingOffAttachesNothing()
	{
		Metrics::enableLogs = false;
		CalibrationCalc calc;
		calrec::SyncWithLogging(calc);
		CHECK(calc.observer == nullptr);
	}

}

int main()
{
	Metrics::enableLogs = false;

	TestContinuousSessionReplaysExactly();
	TestRecordingStartedMidSessionReplaysExactly();
	TestOneshotReplaysExactly();
	TestCrlfRecordingReplays();
	TestReplayDoesNotRecordItself();
	TestNumbersRoundTripBitForBit();
	TestMalformedRecordingsAreRejected();
	TestLoggingOffAttachesNothing();

	if (failures) {
		std::printf("%d calibration replay check(s) failed\n", failures);
		return 1;
	}
	std::printf("calibration replay tests passed\n");
	return 0;
}
