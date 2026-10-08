#include "CalibrationCalc.h"
#include "Calibration.h"
#include "CalibrationMath.h"
#include "CalibrationMetrics.h"
#include "Protocol.h"

const double CalibrationCalc::AxisVarianceThreshold = 0.001;

void CalibrationCalc::PushSample(const Sample& sample)
{
	m_samples.push_back(sample);
}

void CalibrationCalc::Clear()
{
	m_estimatedTransformation.setIdentity();
	m_isValid = false;
	m_samples.clear();
	m_axisVariance = 0.0;
	m_refToTargetPose = Eigen::AffineCompact3d::Identity();
	m_relativePosCalibrated = false;
}

namespace {
	class StreamingLeastSquares3
	{
	public:
		StreamingLeastSquares3() : block_(kHeaderRows + kBlockRows, 4), qr_(kHeaderRows + kBlockRows, 4) { block_.setZero(); }

		void AddRows(const Eigen::Matrix3d& coefficients, const Eigen::Vector3d& constants)
		{
			block_.block<3, 3>(kHeaderRows + used_, 0) = coefficients;
			block_.block<3, 1>(kHeaderRows + used_, 3) = constants;
			used_ += 3;
			if (used_ == kBlockRows) Fold();
		}

		Eigen::Vector3d Solve()
		{
			Fold();
			const Eigen::Matrix3d r = block_.topLeftCorner<3, 3>().triangularView<Eigen::Upper>();
			const Eigen::Vector3d c = block_.block<3, 1>(0, 3);
			return Eigen::JacobiSVD<Eigen::Matrix3d>(r, Eigen::ComputeFullU | Eigen::ComputeFullV).solve(c);
		}

	private:
		static constexpr int kHeaderRows = 4;
		static constexpr int kBlockRows = 384;

		void Fold()
		{
			if (used_ == 0) return;
			qr_.compute(block_);
			block_.topRows<kHeaderRows>() = qr_.matrixQR().topRows<kHeaderRows>().triangularView<Eigen::Upper>();
			block_.bottomRows(kBlockRows).setZero();
			used_ = 0;
		}

		Eigen::Matrix<double, Eigen::Dynamic, 4> block_;
		Eigen::HouseholderQR<Eigen::Matrix<double, Eigen::Dynamic, 4>> qr_;
		int used_ = 0;
	};

	struct TranslationTerms
	{
		Eigen::Matrix3d refInverse, targetInverse;
		Eigen::Vector3d refOffset, targetOffset;
	};
}

Eigen::Vector3d CalibrationCalc::CalibrateRotation(const bool ignoreOutliers) const
{
	std::vector<bool> valids;
	if (ignoreOutliers) valids = DetectOutliers();

	// Kabsch algorithm
	Eigen::Vector2d refCentroid(0, 0), targetCentroid(0, 0);
	Eigen::Matrix2d crossCV = Eigen::Matrix2d::Zero();
	double count = 0.0;

	for (size_t i = 0; i < m_samples.size(); i++) {
		for (size_t j = 0; j < i; j++) {
			if (ignoreOutliers && (!valids[i] || !valids[j])) {
				continue;
			}
			const auto delta = DeltaRotationSamples(m_samples[i], m_samples[j]);
			if (!delta.valid) continue;

			const Eigen::Vector2d ref(delta.ref[0], delta.ref[2]);
			const Eigen::Vector2d target(delta.target[0], delta.target[2]);
			count += 1.0;
			const Eigen::Vector2d refStep = ref - refCentroid;
			refCentroid += refStep / count;
			targetCentroid += (target - targetCentroid) / count;
			crossCV += refStep * (target - targetCentroid).transpose();
		}
	}

	// Singular Value Decomposition (SVD)
	Eigen::JacobiSVD<Eigen::Matrix2d> svd(crossCV, Eigen::ComputeFullU | Eigen::ComputeFullV);

	// Calculate 2D rotation matrix
	Eigen::Matrix2d i = Eigen::Matrix2d::Identity();
	Eigen::Matrix2d rot = svd.matrixV() * i * svd.matrixU().transpose();

	// Calculate yaw angle in radians
	double yaw = std::atan2(rot(1, 0), rot(0, 0));

	// Convert to degrees
	Eigen::Vector3d euler(0.0, yaw * 180.0 / EIGEN_PI, 0.0);

	return euler;
}

Eigen::Vector3d CalibrationCalc::CalibrateTranslation(const Eigen::Matrix3d& rotation) const
{
	std::vector<TranslationTerms> terms;
	terms.reserve(m_samples.size());
	for (const Sample& sample : m_samples) {
		const Eigen::Vector3d targetTrans = rotation * sample.target.trans;
		const Eigen::Matrix3d refInverse = sample.ref.rot.transpose();
		const Eigen::Matrix3d targetInverse = (rotation * sample.target.rot).transpose();
		const Eigen::Vector3d offset = sample.ref.trans - targetTrans;
		terms.push_back({refInverse, targetInverse, refInverse * offset, targetInverse * offset});
	}

	StreamingLeastSquares3 solver;
	for (size_t i = 0; i < terms.size(); i++) {
		for (size_t j = 0; j < i; j++) {
			solver.AddRows(terms[j].refInverse - terms[i].refInverse, terms[j].refOffset - terms[i].refOffset);
			solver.AddRows(terms[j].targetInverse - terms[i].targetInverse, terms[j].targetOffset - terms[i].targetOffset);
		}
	}

	return solver.Solve();
}

Eigen::AffineCompact3d CalibrationCalc::ComputeCalibration(const bool ignoreOutliers) const
{
	Eigen::Vector3d rotation = CalibrateRotation(ignoreOutliers);
	Eigen::Matrix3d rotationMat = quaternionRotateMatrix(VRRotationQuat(rotation));
	Eigen::Vector3d translation = CalibrateTranslation(rotationMat);

	Eigen::AffineCompact3d rot(rotationMat);
	Eigen::Translation3d trans(translation);

	return trans * rot;
}

// Given:
//   R - the reference pose (in reference world space)
//   T - the target pose (in target world space)
//   C - the true calibration (target world -> reference world)
// We assume that there is some "static target pose" S s.t.:
// R * S = C * T (we'll call this the static target pose)
// To compute S:
// S = R^-1 * C * T
// To compute C:
// R * S * T^-1 = C

namespace {
	class PoseAverager
	{
	private:
		Eigen::Matrix<double, 4, Eigen::Dynamic> quatAvg;
		Eigen::Vector3d accum = Eigen::Vector3d::Zero();
		int i = 0;

	public:
		PoseAverager(size_t n_samples) { quatAvg.resize(4, n_samples); }

		template <typename P> void Push(const P& pose)
		{
			const Eigen::Quaterniond rot(pose.rotation());
			quatAvg.col(i++) = Eigen::Vector4d(rot.w(), rot.x(), rot.y(), rot.z());
			accum += pose.translation();
		}

		Eigen::AffineCompact3d Average()
		{
			// https://stackoverflow.com/a/27410865/36723
			auto quatT = quatAvg.transpose();
			Eigen::Matrix4d quatMul = quatAvg * quatT;
			Eigen::SelfAdjointEigenSolver<Eigen::Matrix4d> solver;
			solver.compute(quatMul);

			Eigen::Vector4d quatAvgV = solver.eigenvectors().col(3).real().normalized();
			Eigen::Quaterniond avgQ(quatAvgV(0), quatAvgV(1), quatAvgV(2), quatAvgV(3));
			avgQ.normalize();

			Eigen::AffineCompact3d pose(avgQ);
			pose.pretranslate(accum * (1.0 / i));

			return pose;
		}

		template <typename XS, typename F> static Eigen::AffineCompact3d AverageFor(const XS& samples, const F& poseProvider)
		{
			int sampleCount = 0;

			for (auto& sample : samples) {
				if (!sample.valid) continue;

				sampleCount++;
			}

			PoseAverager accum(sampleCount);

			for (auto& sample : samples) {
				if (!sample.valid) continue;
				auto pose = poseProvider(sample);
				accum.Push(pose);
			}

			return accum.Average();
		}
	};
}

// S = R^-1 * C * T
Eigen::AffineCompact3d CalibrationCalc::EstimateRefToTargetPose(const Eigen::AffineCompact3d& calibration) const
{
	return PoseAverager::AverageFor(m_samples, [&](const auto& sample) {
		return Eigen::Affine3d(sample.ref.ToAffine().inverse() * calibration * sample.target.ToAffine());
	});
}

/*
 * This calibration routine attempts to use the estimated refToTargetPose to derive the
 * playspace calibration based on the relative position of reference and target device.
 * This computation can be performed even when the devices are not moving.
 */
bool CalibrationCalc::CalibrateByRelPose(Eigen::AffineCompact3d& out) const
{
	// R * S * T^-1 = C
	out = PoseAverager::AverageFor(m_samples, [&](const auto& sample) {
		return Eigen::AffineCompact3d(sample.ref.ToAffine() * m_refToTargetPose * sample.target.ToAffine().inverse());
	});

	return true;
}

bool CalibrationCalc::ComputeOneshot(const bool ignoreOutliers)
{
	auto calibration = ComputeCalibration(ignoreOutliers);

	bool valid = ValidateCalibration(calibration);

	if (valid) {
		m_estimatedTransformation = calibration;
		m_isValid = true;
		return true;
	}
	else {
		CalCtx.Log("Not updating: Low-quality calibration result\n");
		return false;
	}
}

void CalibrationCalc::ComputeInstantOffset()
{
	const auto& latestSample = m_samples.back();

	const auto updatedPose = ApplyTransform(latestSample.target, m_estimatedTransformation);

	// Now move the transform from world to HMD space
	const auto hmdOriginPos = updatedPose.trans - latestSample.ref.trans;
	const auto hmdSpace = latestSample.ref.rot.inverse() * hmdOriginPos;

	Metrics::posOffset_lastSample.Push(hmdSpace * 1000);
}

bool CalibrationCalc::ComputeIncremental(bool& lerp, double threshold, double relPoseMaxError, const bool ignoreOutliers)
{
	Metrics::RecordTimestamp();

	if (lockRelativePosition) {
		Eigen::AffineCompact3d byRelPose;
		double relPoseError = INFINITY;
		Eigen::Vector3d relPosOffset;
		if (CalibrateByRelPose(byRelPose) && ValidateCalibration(byRelPose, &relPoseError, &relPosOffset)) {
			Metrics::posOffset_byRelPose.Push(relPosOffset * 1000);
			Metrics::error_byRelPose.Push(relPoseError * 1000);

			m_isValid = true;
			m_estimatedTransformation = byRelPose;
			return true;
		}
	}

	double priorCalibrationError = INFINITY;
	Eigen::Vector3d priorPosOffset;
	if (m_isValid && ValidateCalibration(m_estimatedTransformation, &priorCalibrationError, &priorPosOffset)) {
		Metrics::posOffset_currentCal.Push(priorPosOffset * 1000);
		Metrics::error_currentCal.Push(priorCalibrationError * 1000);
	}

	double newError = INFINITY;
	bool newCalibrationValid = false;
	Eigen::AffineCompact3d byRelPose;
	Eigen::AffineCompact3d calibration;
	bool usingRelPose = false;
	double relPoseError = INFINITY;

	if (enableStaticRecalibration && CalibrateByRelPose(byRelPose)) {
		Eigen::Vector3d relPosOffset;
		if (ValidateCalibration(byRelPose, &relPoseError, &relPosOffset)) {
			Metrics::posOffset_byRelPose.Push(relPosOffset * 1000);
			Metrics::error_byRelPose.Push(relPoseError * 1000);

			if (relPoseError < 0.010 || m_relativePosCalibrated && relPoseError < 0.025) {
				if (relPoseError * threshold >= priorCalibrationError) {
					return false;
				}

				if (relPoseError > relPoseMaxError) {
					return false;
				}

				newCalibrationValid = true;
				usingRelPose = true;
				newError = relPoseError;
				calibration = byRelPose;
			}
		}
	}

	double newVariance = 0;
	bool shouldRapidCorrect = true;
	if (!newCalibrationValid) {
		calibration = ComputeCalibration(ignoreOutliers);

		newVariance = ComputeAxisVariance(calibration)(1);
		Metrics::axisIndependence.Push(newVariance);

		if (newVariance < AxisVarianceThreshold && newVariance < m_axisVariance) {
			newCalibrationValid = false;
			shouldRapidCorrect = false;
		}
		else {
			newCalibrationValid = ValidateCalibration(calibration, &newError, &m_posOffset);
			Metrics::posOffset_rawComputed.Push(m_posOffset * 1000);
		}

		if (m_isValid) {
			if (priorCalibrationError < newError * threshold) {
				// If we have a more noisy calibration than before, avoid updating.
				newCalibrationValid = false;
				shouldRapidCorrect = false;
			}
		}

		Metrics::error_rawComputed.Push(newError * 1000);

		ComputeInstantOffset();
	}

	// Now, can we use the relative pose to perform a rapid correction?
	if (!newCalibrationValid && shouldRapidCorrect) {
		double existingPoseErrorUsingRelPosition = RetargetingErrorRMS(m_refToTargetPose.translation(), m_estimatedTransformation);
		Metrics::error_currentCalRelPose.Push(existingPoseErrorUsingRelPosition * 1000);
		if (relPoseError * threshold < existingPoseErrorUsingRelPosition || newCalibrationValid && relPoseError < newError) {
			newCalibrationValid = true;
			usingRelPose = true;
			newError = relPoseError;
			calibration = byRelPose;
		}
	}

	if (newCalibrationValid) {
		lerp = m_isValid;
		m_relativePosCalibrated = m_relativePosCalibrated || newError < 0.005;
		if (!m_isValid) {
			CalCtx.Log("Applying initial transformation...");
		}
		else if (m_relativePosCalibrated) {
			CalCtx.Log("Applying updated transformation...");
		}
		else {
			CalCtx.Log("Applying temporary transformation...");
		}

		m_isValid = true;
		m_estimatedTransformation = calibration;
		m_axisVariance = newVariance;

		if (!usingRelPose) {
			m_refToTargetPose = EstimateRefToTargetPose(m_estimatedTransformation);
		}

		Metrics::calibrationApplied.Push(!usingRelPose);

		return true;
	}
	else {
		return false;
	}
}
