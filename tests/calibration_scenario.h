#pragma once

#include "CalibrationCalc.h"

#include <random>
#include <vector>

inline Eigen::Quaterniond RandomRotation(std::mt19937& rng)
{
	std::uniform_real_distribution<double> unit(-1.0, 1.0);
	Eigen::Vector3d axis(unit(rng), unit(rng), unit(rng));
	if (axis.norm() < 1e-3) axis = Eigen::Vector3d::UnitX();
	return Eigen::Quaterniond(Eigen::AngleAxisd(unit(rng) * EIGEN_PI, axis.normalized()));
}

struct Scenario
{
	Eigen::AffineCompact3d calibration;
	Eigen::AffineCompact3d mount;
	std::vector<Sample> samples;
};

inline Scenario MakeScenario(int count, unsigned seed, double noise = 0.0)
{
	Scenario s;
	s.calibration = Eigen::Translation3d(0.5, 0.2, -1.0) * Eigen::AngleAxisd(30.0 * EIGEN_PI / 180.0, Eigen::Vector3d::UnitY());
	s.mount = Eigen::Translation3d(0.03, -0.05, 0.08) * Eigen::AngleAxisd(0.4, Eigen::Vector3d(1, 1, 0).normalized());
	std::mt19937 rng(seed);
	std::mt19937 noiseRng(seed + 1);
	std::uniform_real_distribution<double> unit(-1.0, 1.0);
	std::normal_distribution<double> jitter(0.0, noise > 0.0 ? noise : 1.0);
	for (int i = 0; i < count; ++i) {
		const Eigen::AffineCompact3d ref =
		    Eigen::Translation3d(unit(rng), 1.5 + 0.3 * unit(rng), unit(rng)) * Eigen::AffineCompact3d(RandomRotation(rng));
		Eigen::AffineCompact3d target = s.calibration.inverse() * ref * s.mount;
		if (noise > 0.0) target.translation() += Eigen::Vector3d(jitter(noiseRng), jitter(noiseRng), jitter(noiseRng));
		s.samples.emplace_back(Pose(ref), Pose(target), i * 0.01);
	}
	return s;
}
