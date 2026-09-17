#pragma once

#include "DriverConflict.h"

#include <algorithm>
#include <string>
#include <vector>

namespace spacecal {

	struct RegistrationPlan
	{
		std::vector<std::string> vrpathregCandidates;
		std::vector<std::string> removeDirs;
		bool alreadyRegistered = false;
	};

	inline RegistrationPlan BuildRegistrationPlan(const OpenVRPaths& paths, const std::string& ownDriverDir)
	{
		RegistrationPlan plan;
		const std::string ownKey = NormalizePathKey(ownDriverDir);

		std::vector<std::string> seen;
		for (const std::string& entry : paths.externalDrivers) {
			const std::string key = NormalizePathKey(entry);
			if (key.empty()) continue;
			if (!ownKey.empty() && key == ownKey) {
				plan.alreadyRegistered = true;
				continue;
			}
			if (!IsSpaceCalibratorDriverLeaf(PathLeaf(key))) continue;
			if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
			seen.push_back(key);
			plan.removeDirs.push_back(entry);
		}

		std::vector<std::string> seenRuntimes;
		for (const std::string& runtime : paths.runtimes) {
			const std::string key = NormalizePathKey(runtime);
			if (key.empty()) continue;
			if (std::find(seenRuntimes.begin(), seenRuntimes.end(), key) != seenRuntimes.end()) continue;
			seenRuntimes.push_back(key);
			std::string base = runtime;
			while (!base.empty() && (base.back() == '\\' || base.back() == '/'))
				base.pop_back();
			plan.vrpathregCandidates.push_back(base + "\\bin\\win64\\vrpathreg.exe");
		}
		return plan;
	}

	inline std::vector<std::string> OwnRegisteredDirs(const OpenVRPaths& paths, const std::string& ownDriverDir)
	{
		std::vector<std::string> dirs;
		const std::string ownKey = NormalizePathKey(ownDriverDir);
		if (ownKey.empty()) return dirs;
		for (const std::string& entry : paths.externalDrivers) {
			if (NormalizePathKey(entry) == ownKey) dirs.push_back(entry);
		}
		return dirs;
	}

} // namespace spacecal
