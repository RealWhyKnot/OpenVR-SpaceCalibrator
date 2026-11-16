#pragma once

#include "CalibrationCalc.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace calref {

	struct Row
	{
		double time = 0.0;
		Eigen::Matrix3d refRot;
		Eigen::Vector3d refTrans;
		Eigen::Vector3d targetTrans;
		Eigen::Vector3d targetVelocity;
	};

	struct Fit
	{
		bool ok = false;
		double time = 0.0;
		size_t used = 0;
		Eigen::AffineCompact3d calibration = Eigen::AffineCompact3d::Identity();
		Eigen::Vector3d offset = Eigen::Vector3d::Zero();
		double latency = 0.0;
		double rmsMm = 0.0;
		double conditioning = 0.0;
		double yawDeg = 0.0;
		double tiltDeg = 0.0;
	};

	inline std::vector<Row> BuildRows(const std::vector<Sample>& samples, double maxGap = 0.2)
	{
		std::vector<Row> rows;
		rows.reserve(samples.size());
		for (size_t i = 1; i + 1 < samples.size(); ++i) {
			const Sample& before = samples[i - 1];
			const Sample& here = samples[i];
			const Sample& after = samples[i + 1];
			if (!before.valid || !here.valid || !after.valid) continue;
			const double back = here.timestamp - before.timestamp;
			const double ahead = after.timestamp - here.timestamp;
			if (!(back > 0.0 && back < maxGap && ahead > 0.0 && ahead < maxGap)) continue;
			Row row;
			row.time = here.timestamp;
			row.refRot = here.ref.rot;
			row.refTrans = here.ref.trans;
			row.targetTrans = here.target.trans;
			row.targetVelocity = (after.target.trans - before.target.trans) / (back + ahead);
			rows.push_back(row);
		}
		return rows;
	}

	namespace detail {

		using Vector7 = Eigen::Matrix<double, 7, 1>;
		using Matrix7 = Eigen::Matrix<double, 7, 7>;

		struct Linear
		{
			Vector7 solution = Vector7::Zero();
			double squaredError = 0.0;
			double conditioning = 0.0;
			size_t used = 0;
		};

		inline Eigen::Vector3d Residual(const Row& row, const Eigen::Matrix3d& rotation, const Vector7& x)
		{
			const Eigen::Vector3d offset = x.head<3>();
			const Eigen::Vector3d translation = x.segment<3>(3);
			return row.refRot * offset + row.refTrans - rotation * (row.targetTrans + x(6) * row.targetVelocity) - translation;
		}

		inline Linear SolveLinear(const Row* rows, size_t count, const std::vector<char>* skip, const Eigen::Matrix3d& rotation,
		                          bool withLatency)
		{
			Matrix7 normal = Matrix7::Zero();
			Vector7 rhs = Vector7::Zero();
			double constant = 0.0;
			size_t used = 0;
			for (size_t i = 0; i < count; ++i) {
				if (skip && (*skip)[i]) continue;
				const Row& row = rows[i];
				Eigen::Matrix<double, 3, 7> a;
				a.block<3, 3>(0, 0) = row.refRot;
				a.block<3, 3>(0, 3) = -Eigen::Matrix3d::Identity();
				a.col(6) = -(rotation * row.targetVelocity);
				const Eigen::Vector3d b = rotation * row.targetTrans - row.refTrans;
				normal.noalias() += a.transpose() * a;
				rhs.noalias() += a.transpose() * b;
				constant += b.squaredNorm();
				++used;
			}

			Linear result;
			result.used = used;
			if (used < 16) return result;
			if (withLatency) {
				result.solution = normal.ldlt().solve(rhs);
			}
			else {
				result.solution.head<6>() = normal.topLeftCorner<6, 6>().ldlt().solve(rhs.head<6>());
			}
			result.squaredError = std::max(0.0, constant - 2.0 * result.solution.dot(rhs) + result.solution.dot(normal * result.solution));
			const Eigen::SelfAdjointEigenSolver<Eigen::Matrix<double, 6, 6>> eigen(normal.topLeftCorner<6, 6>() /
			                                                                       static_cast<double>(used));
			result.conditioning = eigen.eigenvalues()(0);
			return result;
		}

		inline Eigen::Matrix3d Rotate(const Eigen::Vector3d& omega, const Eigen::Matrix3d& base)
		{
			const double angle = omega.norm();
			if (angle < 1e-15) return base;
			return Eigen::AngleAxisd(angle, omega / angle).toRotationMatrix() * base;
		}

		inline Eigen::Matrix3d RefineRotation(const Row* rows, size_t count, const std::vector<char>* skip, Eigen::Matrix3d rotation,
		                                      bool withLatency)
		{
			const double step = 1e-3;
			const auto cost = [&](const Eigen::Vector3d& omega) {
				return SolveLinear(rows, count, skip, Rotate(omega, rotation), withLatency).squaredError;
			};
			for (int iteration = 0; iteration < 8; ++iteration) {
				const double centre = cost(Eigen::Vector3d::Zero());
				Eigen::Vector3d gradient;
				Eigen::Matrix3d hessian;
				double plus[3], minus[3];
				for (int j = 0; j < 3; ++j) {
					Eigen::Vector3d omega = Eigen::Vector3d::Zero();
					omega(j) = step;
					plus[j] = cost(omega);
					omega(j) = -step;
					minus[j] = cost(omega);
					gradient(j) = (plus[j] - minus[j]) / (2.0 * step);
					hessian(j, j) = (plus[j] - 2.0 * centre + minus[j]) / (step * step);
				}
				for (int j = 0; j < 3; ++j) {
					for (int k = j + 1; k < 3; ++k) {
						Eigen::Vector3d omega = Eigen::Vector3d::Zero();
						omega(j) = step;
						omega(k) = step;
						const double both = cost(omega);
						hessian(j, k) = hessian(k, j) = (both - plus[j] - plus[k] + centre) / (step * step);
					}
				}
				const Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> eigen(hessian);
				if (eigen.eigenvalues()(0) <= 0.0) break;
				Eigen::Vector3d move = -hessian.ldlt().solve(gradient);
				bool improved = false;
				for (int attempt = 0; attempt < 6; ++attempt) {
					if (cost(move) < centre) {
						improved = true;
						break;
					}
					move *= 0.5;
				}
				if (!improved) break;
				rotation = Rotate(move, rotation);
				if (move.norm() < 1e-7) break;
			}
			return rotation;
		}

	}

	inline Fit FitRows(const Row* rows, size_t count, const Eigen::Matrix3d& start, bool withLatency, bool fullRotation = true)
	{
		Fit fit;
		if (count < 64) return fit;
		fit.time = rows[count / 2].time;

		std::vector<char> skip(count, 0);
		Eigen::Matrix3d rotation = start;
		detail::Linear linear;
		for (int pass = 0; pass < 2; ++pass) {
			if (fullRotation) rotation = detail::RefineRotation(rows, count, &skip, rotation, withLatency);
			linear = detail::SolveLinear(rows, count, &skip, rotation, withLatency);
			if (linear.used < 64) return fit;
			if (pass == 1) break;
			const double limit = 9.0 * linear.squaredError / static_cast<double>(linear.used);
			for (size_t i = 0; i < count; ++i) {
				skip[i] = detail::Residual(rows[i], rotation, linear.solution).squaredNorm() > limit ? 1 : 0;
			}
		}

		fit.used = linear.used;
		fit.calibration.linear() = rotation;
		fit.calibration.translation() = linear.solution.segment<3>(3);
		fit.offset = linear.solution.head<3>();
		fit.latency = linear.solution(6);
		fit.rmsMm = std::sqrt(linear.squaredError / static_cast<double>(linear.used)) * 1000.0;
		fit.conditioning = linear.conditioning;
		const Eigen::Vector3d up = rotation * Eigen::Vector3d::UnitY();
		fit.tiltDeg = std::acos(std::clamp(up.y(), -1.0, 1.0)) * 180.0 / EIGEN_PI;
		fit.yawDeg = std::atan2(rotation(0, 2), rotation(2, 2)) * 180.0 / EIGEN_PI;
		fit.ok = linear.conditioning > 0.01;
		return fit;
	}

	inline std::vector<Fit> FitWindows(const std::vector<Row>& rows, const Eigen::Matrix3d& start, double halfWidthSeconds,
	                                   double strideSeconds, bool withLatency)
	{
		std::vector<Fit> fits;
		if (rows.empty()) return fits;
		Eigen::Matrix3d rotation = start;
		size_t begin = 0, end = 0;
		const size_t strides = static_cast<size_t>(std::floor((rows.back().time - rows.front().time) / strideSeconds + 1e-9));
		for (size_t i = 0; i <= strides; ++i) {
			const double centre = rows.front().time + static_cast<double>(i) * strideSeconds;
			while (begin < rows.size() && rows[begin].time < centre - halfWidthSeconds)
				++begin;
			while (end < rows.size() && rows[end].time <= centre + halfWidthSeconds)
				++end;
			if (end <= begin) continue;
			Fit fit = FitRows(rows.data() + begin, end - begin, rotation, withLatency);
			fit.time = centre;
			if (fit.ok) {
				rotation = fit.calibration.linear();
				fits.push_back(fit);
			}
		}
		return fits;
	}

	inline const Fit* Nearest(const std::vector<Fit>& fits, double time)
	{
		if (fits.empty()) return nullptr;
		const auto after = std::lower_bound(fits.begin(), fits.end(), time, [](const Fit& fit, double t) { return fit.time < t; });
		if (after == fits.begin()) return &*after;
		if (after == fits.end()) return &fits.back();
		const auto before = after - 1;
		return (time - before->time) <= (after->time - time) ? &*before : &*after;
	}

}
