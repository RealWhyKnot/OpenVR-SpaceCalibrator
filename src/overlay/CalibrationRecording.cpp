#include "CalibrationRecording.h"

#include "CalibrationMetrics.h"

#include <charconv>
#include <cmath>
#include <fstream>
#include <istream>
#include <memory>
#include <ostream>
#include <string_view>

namespace calrec {

	namespace {

		constexpr std::string_view kMagic = "spacecal-replay";

		void PutNumber(std::ostream& out, double value)
		{
			char buffer[64];
			const auto result = std::to_chars(buffer, buffer + sizeof buffer, value);
			out << ' ' << std::string_view(buffer, result.ptr - buffer);
		}

		void PutFlag(std::ostream& out, bool value)
		{
			out << ' ' << (value ? '1' : '0');
		}

		void PutPose(std::ostream& out, const Eigen::Matrix3d& rot, const Eigen::Vector3d& trans)
		{
			for (int r = 0; r < 3; ++r) {
				for (int c = 0; c < 3; ++c) {
					PutNumber(out, rot(r, c));
				}
			}
			for (int i = 0; i < 3; ++i) {
				PutNumber(out, trans(i));
			}
		}

		void PutTransform(std::ostream& out, const Eigen::AffineCompact3d& transform)
		{
			PutPose(out, transform.linear(), transform.translation());
		}

		void PutOutcome(std::ostream& out, const SolveOutcome& outcome)
		{
			PutFlag(out, outcome.returned);
			PutFlag(out, outcome.lerp);
			PutFlag(out, outcome.valid);
			PutFlag(out, outcome.relativeCalibrated);
			PutTransform(out, outcome.transformation);
			PutTransform(out, outcome.relative);
		}

		SolveOutcome Capture(const CalibrationCalc& calc, bool returned, bool lerp)
		{
			SolveOutcome outcome;
			outcome.returned = returned;
			outcome.lerp = lerp;
			outcome.valid = calc.isValid();
			outcome.relativeCalibrated = calc.isRelativeTransformationCalibrated();
			outcome.transformation = calc.Transformation();
			outcome.relative = calc.RelativeTransformation();
			return outcome;
		}

		class LineReader
		{
		public:
			explicit LineReader(std::string_view line) : rest_(line) {}

			bool Word(std::string_view& out)
			{
				const size_t start = rest_.find_first_not_of(' ');
				if (start == std::string_view::npos) return false;
				rest_.remove_prefix(start);
				const size_t end = rest_.find(' ');
				out = rest_.substr(0, end);
				rest_.remove_prefix(end == std::string_view::npos ? rest_.size() : end);
				return true;
			}

			bool Number(double& out)
			{
				std::string_view word;
				if (!Word(word)) return false;
				const auto result = std::from_chars(word.data(), word.data() + word.size(), out);
				return result.ec == std::errc() && result.ptr == word.data() + word.size();
			}

			bool Flag(bool& out)
			{
				std::string_view word;
				if (!Word(word) || (word != "0" && word != "1")) return false;
				out = word == "1";
				return true;
			}

			bool PoseParts(Eigen::Matrix3d& rot, Eigen::Vector3d& trans)
			{
				for (int r = 0; r < 3; ++r) {
					for (int c = 0; c < 3; ++c) {
						if (!Number(rot(r, c))) return false;
					}
				}
				for (int i = 0; i < 3; ++i) {
					if (!Number(trans(i))) return false;
				}
				return true;
			}

			bool SamplePose(Pose& out) { return PoseParts(out.rot, out.trans); }

			bool Transform(Eigen::AffineCompact3d& out)
			{
				Eigen::Matrix3d rot;
				Eigen::Vector3d trans;
				if (!PoseParts(rot, trans)) return false;
				out.linear() = rot;
				out.translation() = trans;
				return true;
			}

			bool Outcome(SolveOutcome& out)
			{
				return Flag(out.returned) && Flag(out.lerp) && Flag(out.valid) && Flag(out.relativeCalibrated) &&
				       Transform(out.transformation) && Transform(out.relative);
			}

			bool AtEnd()
			{
				std::string_view word;
				return !Word(word);
			}

		private:
			std::string_view rest_;
		};

		bool SameNumber(double a, double b)
		{
			return a == b || (std::isnan(a) && std::isnan(b));
		}

		bool SameTransform(const Eigen::AffineCompact3d& a, const Eigen::AffineCompact3d& b)
		{
			for (int r = 0; r < 3; ++r) {
				for (int c = 0; c < 4; ++c) {
					if (!SameNumber(a.matrix()(r, c), b.matrix()(r, c))) return false;
				}
			}
			return true;
		}

		std::string ReplayLine(std::string_view line, CalibrationCalc& calc, ReplayReport& report, size_t number)
		{
			LineReader reader(line);
			std::string_view kind;
			if (!reader.Word(kind)) return {};

			if (kind == "clear") {
				if (!reader.AtEnd()) return "clear takes no values";
				calc.Clear();
				return {};
			}
			if (kind == "shift") {
				if (!reader.AtEnd()) return "shift takes no values";
				calc.ShiftSample();
				return {};
			}
			if (kind == "sample") {
				Sample sample;
				if (!reader.Flag(sample.valid) || !reader.Number(sample.timestamp) || !reader.SamplePose(sample.ref) ||
				    !reader.SamplePose(sample.target) || !reader.AtEnd()) {
					return "sample needs a valid flag, a timestamp and two poses of 12 numbers";
				}
				calc.PushSample(sample);
				++report.samples;
				return {};
			}
			if (kind == "relative") {
				bool calibrated = false;
				Eigen::AffineCompact3d transform;
				if (!reader.Flag(calibrated) || !reader.Transform(transform) || !reader.AtEnd()) {
					return "relative needs a calibrated flag and 12 numbers";
				}
				calc.setRelativeTransformation(transform, calibrated);
				return {};
			}
			if (kind == "state") {
				CalibrationCalc::SolveState state;
				if (!reader.Flag(state.valid) || !reader.Number(state.axisVariance) || !reader.Transform(state.transformation) ||
				    !reader.AtEnd()) {
					return "state needs a valid flag, an axis variance and 12 numbers";
				}
				calc.SetSolveState(state);
				return {};
			}
			if (kind == "solve") {
				ReplayedSolve solve;
				solve.line = number;
				bool enableStatic = false;
				bool lockRelative = false;
				if (!reader.Flag(solve.call.incremental) || !reader.Flag(enableStatic) || !reader.Flag(lockRelative) ||
				    !reader.Number(solve.call.threshold) || !reader.Number(solve.call.relPoseMaxError) ||
				    !reader.Flag(solve.call.ignoreOutliers) || !reader.Outcome(solve.recorded) || !reader.AtEnd()) {
					return "solve needs 6 call values and 28 outcome values";
				}
				calc.enableStaticRecalibration = enableStatic;
				calc.lockRelativePosition = lockRelative;
				bool lerp = false;
				const bool returned =
				    solve.call.incremental
				        ? calc.ComputeIncremental(lerp, solve.call.threshold, solve.call.relPoseMaxError, solve.call.ignoreOutliers)
				        : calc.ComputeOneshot(solve.call.ignoreOutliers);
				solve.replayed = Capture(calc, returned, lerp);
				report.solves.push_back(solve);
				return {};
			}
			return "unknown record '" + std::string(kind) + "'";
		}

	}

	void Recorder::Begin(const CalibrationCalc& calc)
	{
		out_ << kMagic << ' ' << kFormatVersion << '\n';
		OnClear();
		OnRelative(calc.RelativeTransformation(), calc.isRelativeTransformationCalibrated());
		for (const Sample& sample : calc.Samples()) {
			OnSample(sample);
		}
		const CalibrationCalc::SolveState state = calc.GetSolveState();
		out_ << "state";
		PutFlag(out_, state.valid);
		PutNumber(out_, state.axisVariance);
		PutTransform(out_, state.transformation);
		out_ << '\n';
		out_.flush();
	}

	void Recorder::OnClear()
	{
		out_ << "clear\n";
	}

	void Recorder::OnSample(const Sample& sample)
	{
		out_ << "sample";
		PutFlag(out_, sample.valid);
		PutNumber(out_, sample.timestamp);
		PutPose(out_, sample.ref.rot, sample.ref.trans);
		PutPose(out_, sample.target.rot, sample.target.trans);
		out_ << '\n';
	}

	void Recorder::OnShift()
	{
		out_ << "shift\n";
	}

	void Recorder::OnRelative(const Eigen::AffineCompact3d& transform, bool calibrated)
	{
		out_ << "relative";
		PutFlag(out_, calibrated);
		PutTransform(out_, transform);
		out_ << '\n';
	}

	void Recorder::OnSolve(const CalibrationCalc& calc, const SolveCall& call, bool returned, bool lerp)
	{
		out_ << "solve";
		PutFlag(out_, call.incremental);
		PutFlag(out_, calc.enableStaticRecalibration);
		PutFlag(out_, calc.lockRelativePosition);
		PutNumber(out_, call.threshold);
		PutNumber(out_, call.relPoseMaxError);
		PutFlag(out_, call.ignoreOutliers);
		PutOutcome(out_, Capture(calc, returned, lerp));
		out_ << '\n';
		out_.flush();
	}

	ReplayReport Replay(std::istream& in, CalibrationCalc& calc)
	{
		ReplayReport report;
		CalibrationObserver* const observer = calc.observer;
		calc.observer = nullptr;

		std::string line;
		size_t number = 0;
		bool sawHeader = false;
		while (std::getline(in, line)) {
			++number;
			if (!line.empty() && line.back() == '\r') line.pop_back();
			if (!sawHeader) {
				const std::string expected = std::string(kMagic) + ' ' + std::to_string(kFormatVersion);
				if (line != expected) {
					report.error = "line 1: expected '" + expected + "'";
					break;
				}
				sawHeader = true;
				continue;
			}
			const std::string problem = ReplayLine(line, calc, report, number);
			if (!problem.empty()) {
				report.error = "line " + std::to_string(number) + ": " + problem;
				break;
			}
		}
		if (!sawHeader && report.error.empty()) report.error = "empty recording";

		calc.observer = observer;
		report.ok = report.error.empty();
		return report;
	}

	bool SameOutcome(const SolveOutcome& a, const SolveOutcome& b)
	{
		return a.returned == b.returned && a.lerp == b.lerp && a.valid == b.valid && a.relativeCalibrated == b.relativeCalibrated &&
		       SameTransform(a.transformation, b.transformation) && SameTransform(a.relative, b.relative);
	}

	double TranslationGapMm(const Eigen::AffineCompact3d& a, const Eigen::AffineCompact3d& b)
	{
		return (a.translation() - b.translation()).norm() * 1000.0;
	}

	double RotationGapDeg(const Eigen::AffineCompact3d& a, const Eigen::AffineCompact3d& b)
	{
		const Eigen::Quaterniond qa(Eigen::Matrix3d(a.linear()));
		const Eigen::Quaterniond qb(Eigen::Matrix3d(b.linear()));
		return qa.angularDistance(qb) * 180.0 / EIGEN_PI;
	}

	void SyncWithLogging(CalibrationCalc& calc)
	{
		static std::ofstream file;
		static std::unique_ptr<Recorder> recorder;
		static bool failed = false;

		if (!Metrics::enableLogs) {
			if (recorder) {
				if (calc.observer == recorder.get()) calc.observer = nullptr;
				recorder.reset();
				file.close();
			}
			failed = false;
			return;
		}
		if (recorder || failed) return;
		if (!Metrics::OpenLogStream(L"spacecal_replay", file)) {
			failed = true;
			return;
		}
		recorder = std::make_unique<Recorder>(file);
		recorder->Begin(calc);
		calc.observer = recorder.get();
	}

}
