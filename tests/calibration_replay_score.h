#pragma once

#include "CalibrationRecording.h"
#include "calibration_replay_reference.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace calscore {

	struct Score
	{
		size_t scoredSamples = 0;
		size_t blocks = 0;
		double blockMedianMm = 0.0;
		double blockP95Mm = 0.0;
		double blockMedianDeg = 0.0;
		double blockP95Deg = 0.0;
		double wholeRmsMm = 0.0;
		double wholeP95Mm = 0.0;
		size_t accepted = 0;
		double churnTotalMm = 0.0;
		double churnMaxMm = 0.0;
		double churnTotalDeg = 0.0;
		double churnMaxDeg = 0.0;
		size_t referenceSamples = 0;
		double headMedianMm = 0.0;
		double headP95Mm = 0.0;
		double lowMedianMm = 0.0;
		double lowP95Mm = 0.0;
		double rotationMedianDeg = 0.0;
		double rotationP95Deg = 0.0;
	};

	namespace detail {

		struct Relative
		{
			double time = 0.0;
			Eigen::Vector3d position = Eigen::Vector3d::Zero();
			Eigen::Quaterniond rotation = Eigen::Quaterniond::Identity();
		};

		inline double Percentile(std::vector<double> values, double fraction)
		{
			if (values.empty()) return 0.0;
			std::sort(values.begin(), values.end());
			const size_t index = static_cast<size_t>(fraction * static_cast<double>(values.size() - 1) + 0.5);
			return values[std::min(index, values.size() - 1)];
		}

		inline void ScoreBlock(const std::vector<Relative>& all, size_t begin, size_t end, std::vector<double>& mm,
		                       std::vector<double>& deg)
		{
			const double count = static_cast<double>(end - begin);
			Eigen::Vector3d mean = Eigen::Vector3d::Zero();
			Eigen::Vector4d quatSum = Eigen::Vector4d::Zero();
			const Eigen::Vector4d first = all[begin].rotation.coeffs();
			for (size_t i = begin; i < end; ++i) {
				mean += all[i].position;
				const Eigen::Vector4d coeffs = all[i].rotation.coeffs();
				quatSum += coeffs.dot(first) < 0.0 ? Eigen::Vector4d(-coeffs) : coeffs;
			}
			mean /= count;
			Eigen::Quaterniond meanRotation;
			meanRotation.coeffs() = quatSum.normalized();

			double squaredMm = 0.0;
			double squaredDeg = 0.0;
			for (size_t i = begin; i < end; ++i) {
				squaredMm += (all[i].position - mean).squaredNorm();
				const double angle = all[i].rotation.angularDistance(meanRotation) * 180.0 / EIGEN_PI;
				squaredDeg += angle * angle;
			}
			mm.push_back(std::sqrt(squaredMm / count) * 1000.0);
			deg.push_back(std::sqrt(squaredDeg / count));
		}

	}

	inline Score ScoreReplay(const calrec::ReplayReport& report, const std::vector<calref::Fit>* reference = nullptr,
	                         double blockSeconds = 30.0, size_t minBlockSamples = 50)
	{
		Score score;
		std::vector<double> headMm, lowMm, rotationDeg;
		const Eigen::Vector3d below(0.0, -1.0, 0.0);
		const std::vector<calrec::ReplayedSolve>& solves = report.solves;
		const std::vector<Sample>& samples = report.kept;

		std::vector<detail::Relative> relatives;
		relatives.reserve(samples.size());

		bool havePrevious = false;
		Eigen::AffineCompact3d previous = Eigen::AffineCompact3d::Identity();
		for (size_t k = 0; k < solves.size(); ++k) {
			const calrec::ReplayedSolve& solve = solves[k];
			const Eigen::AffineCompact3d& calibration = solve.replayed.transformation;

			if (solve.replayed.returned) {
				++score.accepted;
				if (havePrevious && solve.samplesBefore > 0 && solve.samplesBefore <= samples.size()) {
					const Eigen::Vector3d at = samples[solve.samplesBefore - 1].target.trans;
					const double mm = ((calibration * at) - (previous * at)).norm() * 1000.0;
					const double deg = calrec::RotationGapDeg(calibration, previous);
					score.churnTotalMm += mm;
					score.churnTotalDeg += deg;
					score.churnMaxMm = std::max(score.churnMaxMm, mm);
					score.churnMaxDeg = std::max(score.churnMaxDeg, deg);
				}
			}
			if (!solve.replayed.valid) continue;
			previous = calibration;
			havePrevious = true;

			const size_t end = std::min(samples.size(), k + 1 < solves.size() ? solves[k + 1].samplesBefore : samples.size());
			for (size_t i = solve.samplesBefore; i < end; ++i) {
				const Sample& sample = samples[i];
				if (!sample.valid) continue;
				detail::Relative relative;
				relative.time = sample.timestamp;
				relative.position = sample.ref.rot.transpose() * (calibration * sample.target.trans - sample.ref.trans);
				relative.rotation =
				    Eigen::Quaterniond(Eigen::Matrix3d(sample.ref.rot.transpose() * calibration.linear() * sample.target.rot));
				relatives.push_back(relative);

				const calref::Fit* fit = reference ? calref::Nearest(*reference, sample.timestamp) : nullptr;
				if (!fit) continue;
				const Eigen::Vector3d head = sample.target.trans;
				headMm.push_back(((calibration * head) - (fit->calibration * head)).norm() * 1000.0);
				lowMm.push_back(((calibration * (head + below)) - (fit->calibration * (head + below))).norm() * 1000.0);
				rotationDeg.push_back(calrec::RotationGapDeg(calibration, fit->calibration));
			}
		}

		score.referenceSamples = headMm.size();
		score.headMedianMm = detail::Percentile(headMm, 0.5);
		score.headP95Mm = detail::Percentile(std::move(headMm), 0.95);
		score.lowMedianMm = detail::Percentile(lowMm, 0.5);
		score.lowP95Mm = detail::Percentile(std::move(lowMm), 0.95);
		score.rotationMedianDeg = detail::Percentile(rotationDeg, 0.5);
		score.rotationP95Deg = detail::Percentile(std::move(rotationDeg), 0.95);

		score.scoredSamples = relatives.size();
		if (relatives.empty()) return score;

		std::vector<double> blockMm, blockDeg;
		size_t begin = 0;
		while (begin < relatives.size()) {
			const double blockId = std::floor(relatives[begin].time / blockSeconds);
			size_t end = begin;
			while (end < relatives.size() && std::floor(relatives[end].time / blockSeconds) == blockId)
				++end;
			if (end - begin >= minBlockSamples) detail::ScoreBlock(relatives, begin, end, blockMm, blockDeg);
			begin = end;
		}
		score.blocks = blockMm.size();
		score.blockMedianMm = detail::Percentile(blockMm, 0.5);
		score.blockP95Mm = detail::Percentile(blockMm, 0.95);
		score.blockMedianDeg = detail::Percentile(blockDeg, 0.5);
		score.blockP95Deg = detail::Percentile(blockDeg, 0.95);

		Eigen::Vector3d median;
		for (int axis = 0; axis < 3; ++axis) {
			std::vector<double> values;
			values.reserve(relatives.size());
			for (const detail::Relative& relative : relatives)
				values.push_back(relative.position(axis));
			median(axis) = detail::Percentile(std::move(values), 0.5);
		}
		std::vector<double> distances;
		distances.reserve(relatives.size());
		double squared = 0.0;
		for (const detail::Relative& relative : relatives) {
			const double distance = (relative.position - median).norm() * 1000.0;
			distances.push_back(distance);
			squared += distance * distance;
		}
		score.wholeRmsMm = std::sqrt(squared / static_cast<double>(distances.size()));
		score.wholeP95Mm = detail::Percentile(std::move(distances), 0.95);
		return score;
	}

}
