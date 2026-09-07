#pragma once

#include "CalibrationCalc.h"

#include <Eigen/Dense>
#include <openvr.h>

inline vr::HmdQuaternion_t operator*(const vr::HmdQuaternion_t& lhs, const vr::HmdQuaternion_t& rhs)
{
	return {(lhs.w * rhs.w) - (lhs.x * rhs.x) - (lhs.y * rhs.y) - (lhs.z * rhs.z),
	        (lhs.w * rhs.x) + (lhs.x * rhs.w) + (lhs.y * rhs.z) - (lhs.z * rhs.y),
	        (lhs.w * rhs.y) + (lhs.y * rhs.w) + (lhs.z * rhs.x) - (lhs.x * rhs.z),
	        (lhs.w * rhs.z) + (lhs.z * rhs.w) + (lhs.x * rhs.y) - (lhs.y * rhs.x)};
}

inline Eigen::Matrix3d quaternionRotateMatrix(const vr::HmdQuaternion_t& quat)
{
	return Eigen::Quaterniond(quat.w, quat.x, quat.y, quat.z).toRotationMatrix();
}

inline Eigen::Vector3d AxisFromRotationMatrix3(const Eigen::Matrix3d& rot)
{
	return Eigen::Vector3d(rot(2, 1) - rot(1, 2), rot(0, 2) - rot(2, 0), rot(1, 0) - rot(0, 1));
}

inline double AngleFromRotationMatrix3(const Eigen::Matrix3d& rot)
{
	return acos((rot(0, 0) + rot(1, 1) + rot(2, 2) - 1.0) / 2.0);
}

inline vr::HmdQuaternion_t VRRotationQuat(const Eigen::Quaterniond& rotQuat)
{
	vr::HmdQuaternion_t vrRotQuat;
	vrRotQuat.x = rotQuat.coeffs()[0];
	vrRotQuat.y = rotQuat.coeffs()[1];
	vrRotQuat.z = rotQuat.coeffs()[2];
	vrRotQuat.w = rotQuat.coeffs()[3];
	return vrRotQuat;
}

inline vr::HmdQuaternion_t VRRotationQuat(const Eigen::Vector3d& eulerdeg)
{
	auto euler = eulerdeg * EIGEN_PI / 180.0;

	Eigen::Quaterniond rotQuat = Eigen::AngleAxisd(euler(0), Eigen::Vector3d::UnitZ()) *
	                             Eigen::AngleAxisd(euler(1), Eigen::Vector3d::UnitY()) *
	                             Eigen::AngleAxisd(euler(2), Eigen::Vector3d::UnitX());

	return VRRotationQuat(rotQuat);
}

inline vr::HmdVector3d_t VRTranslationVec(const Eigen::Vector3d& transcm)
{
	auto trans = transcm * 0.01;
	vr::HmdVector3d_t vrTrans;
	vrTrans.v[0] = trans[0];
	vrTrans.v[1] = trans[1];
	vrTrans.v[2] = trans[2];
	return vrTrans;
}

struct DSample
{
	bool valid;
	Eigen::Vector3d ref, target;
};

inline DSample DeltaRotationSamples(const Sample& s1, const Sample& s2)
{
	// Difference in rotation between samples.
	auto dref = s1.ref.rot * s2.ref.rot.transpose();
	auto dtarget = s1.target.rot * s2.target.rot.transpose();

	// When stuck together, the two tracked objects rotate as a pair,
	// therefore their axes of rotation must be equal between any given pair of samples.
	DSample ds;
	ds.ref = AxisFromRotationMatrix3(dref);
	ds.target = AxisFromRotationMatrix3(dtarget);

	// Reject samples that were too close to each other.
	auto refA = AngleFromRotationMatrix3(dref);
	auto targetA = AngleFromRotationMatrix3(dtarget);
	ds.valid = refA > 0.4 && targetA > 0.4 && ds.ref.norm() > 0.01 && ds.target.norm() > 0.01;

	ds.ref.normalize();
	ds.target.normalize();
	return ds;
}

inline Pose ApplyTransform(const Pose& originalPose, const Eigen::AffineCompact3d& transform)
{
	Pose pose(originalPose);
	pose.rot = transform.rotation() * pose.rot;
	pose.trans = transform * pose.trans;
	return pose;
}
