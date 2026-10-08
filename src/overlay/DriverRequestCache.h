#pragma once

#include "Protocol.h"

#include <cstdint>
#include <cstring>
#include <map>

namespace spacecal {

	inline bool SameDeviceTransform(const protocol::SetDeviceTransform& a, const protocol::SetDeviceTransform& b)
	{
		return a.openVRID == b.openVRID && a.enabled == b.enabled && a.updateTranslation == b.updateTranslation &&
		       a.updateRotation == b.updateRotation && a.updateScale == b.updateScale && a.translation.v[0] == b.translation.v[0] &&
		       a.translation.v[1] == b.translation.v[1] && a.translation.v[2] == b.translation.v[2] && a.rotation.w == b.rotation.w &&
		       a.rotation.x == b.rotation.x && a.rotation.y == b.rotation.y && a.rotation.z == b.rotation.z && a.scale == b.scale &&
		       a.lerp == b.lerp && a.quash == b.quash && a.smooth == b.smooth;
	}

	inline bool SameDriverState(const protocol::Request& a, const protocol::Request& b)
	{
		if (a.type != b.type) return false;
		switch (a.type) {
			case protocol::RequestSetDeviceTransform: return SameDeviceTransform(a.setDeviceTransform, b.setDeviceTransform);
			case protocol::RequestSetAlignmentSpeedParams:
				return std::memcmp(&a.setAlignmentSpeedParams, &b.setAlignmentSpeedParams, sizeof a.setAlignmentSpeedParams) == 0;
			case protocol::RequestSetSmoothingParams: return a.setSmoothingParams.strength == b.setSmoothingParams.strength;
			case protocol::RequestSetFingerSmoothing:
				return a.setFingerSmoothing.strength == b.setFingerSmoothing.strength &&
				       a.setFingerSmoothing.fingerMask == b.setFingerSmoothing.fingerMask &&
				       std::memcmp(a.setFingerSmoothing.perFinger, b.setFingerSmoothing.perFinger, sizeof a.setFingerSmoothing.perFinger) ==
				           0;
			case protocol::RequestSetStickSmoothing:
				return a.setStickSmoothing.strength[0] == b.setStickSmoothing.strength[0] &&
				       a.setStickSmoothing.strength[1] == b.setStickSmoothing.strength[1];
			default: return false;
		}
	}

	class DriverRequestCache
	{
	public:
		bool ShouldSend(const protocol::Request& request)
		{
			const uint64_t key = KeyFor(request);
			const auto it = sent_.find(key);
			if (it != sent_.end() && SameDriverState(it->second, request)) return false;
			sent_.insert_or_assign(key, request);
			return true;
		}

		void Clear() { sent_.clear(); }

	private:
		static uint64_t KeyFor(const protocol::Request& request)
		{
			const uint64_t id = request.type == protocol::RequestSetDeviceTransform ? request.setDeviceTransform.openVRID : 0;
			return ((uint64_t)request.type << 32) | id;
		}

		std::map<uint64_t, protocol::Request> sent_;
	};

}
