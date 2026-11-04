#pragma once

#include "CalibrationCalc.h"

#include <iosfwd>
#include <string>
#include <vector>

namespace calrec {

	inline constexpr int kFormatVersion = 1;

	struct SolveOutcome
	{
		bool returned = false;
		bool lerp = false;
		bool valid = false;
		bool relativeCalibrated = false;
		Eigen::AffineCompact3d transformation = Eigen::AffineCompact3d::Identity();
		Eigen::AffineCompact3d relative = Eigen::AffineCompact3d::Identity();
	};

	class Recorder final : public CalibrationObserver
	{
	public:
		explicit Recorder(std::ostream& out) : out_(out) {}

		void Begin(const CalibrationCalc& calc);

		void OnClear() override;
		void OnSample(const Sample& sample) override;
		void OnShift() override;
		void OnRelative(const Eigen::AffineCompact3d& transform, bool calibrated) override;
		void OnSolve(const CalibrationCalc& calc, const SolveCall& call, bool returned, bool lerp) override;

	private:
		std::ostream& out_;
	};

	struct ReplayedSolve
	{
		size_t line = 0;
		SolveCall call;
		SolveOutcome recorded;
		SolveOutcome replayed;
	};

	struct ReplayReport
	{
		bool ok = false;
		std::string error;
		size_t samples = 0;
		std::vector<ReplayedSolve> solves;
	};

	ReplayReport Replay(std::istream& in, CalibrationCalc& calc);

	bool SameOutcome(const SolveOutcome& a, const SolveOutcome& b);
	double TranslationGapMm(const Eigen::AffineCompact3d& a, const Eigen::AffineCompact3d& b);
	double RotationGapDeg(const Eigen::AffineCompact3d& a, const Eigen::AffineCompact3d& b);

	void SyncWithLogging(CalibrationCalc& calc);

}
