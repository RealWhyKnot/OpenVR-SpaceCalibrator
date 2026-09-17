#pragma once

#include "HandshakeOutcome.h"

#include <picojson.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace spacecal {

	enum class ConflictTier
	{
		None,
		Warning,
		Blocking
	};

	enum class RivalKind
	{
		ExternalDriver,
		RuntimeFolder
	};

	struct RivalDriver
	{
		std::string path;
		RivalKind kind = RivalKind::ExternalDriver;
	};

	struct OpenVRPaths
	{
		std::vector<std::string> runtimes;
		std::vector<std::string> externalDrivers;
	};

	struct ConflictInputs
	{
		std::string ownDriverDir;
		std::vector<std::string> externalDrivers;
		std::vector<std::string> runtimeDriverFolders;
		HandshakeOutcome handshake = HandshakeOutcome::Unavailable;
	};

	struct ConflictReport
	{
		ConflictTier tier = ConflictTier::None;
		std::vector<RivalDriver> rivals;
		bool ownDriverStale = false;
		bool ownRegistered = false;

		bool CanUnregister() const
		{
			return std::any_of(rivals.begin(), rivals.end(),
			                   [](const RivalDriver& rival) { return rival.kind == RivalKind::ExternalDriver; });
		}
	};

	inline std::string NormalizePathKey(const std::string& path)
	{
		std::string key;
		key.reserve(path.size());
		for (char c : path) {
			char n = c == '/' ? '\\' : c;
			if (n >= 'A' && n <= 'Z') n = static_cast<char>(n - 'A' + 'a');
			if (n == '\\' && !key.empty() && key.back() == '\\') continue;
			key += n;
		}
		while (key.size() > 1 && key.back() == '\\')
			key.pop_back();
		return key;
	}

	inline std::string PathLeaf(const std::string& path)
	{
		const std::string key = NormalizePathKey(path);
		const auto slash = key.find_last_of('\\');
		return slash == std::string::npos ? key : key.substr(slash + 1);
	}

	inline bool IsSpaceCalibratorDriverLeaf(const std::string& leaf)
	{
		const std::string key = NormalizePathKey(leaf);
		return key == "01spacecalibrator" || key == "000spacecalibrator" || key == "driver_01spacecalibrator" ||
		       key == "driver_000spacecalibrator";
	}

	inline bool ParseOpenVRPaths(const std::string& json, OpenVRPaths& out)
	{
		picojson::value root;
		const std::string parseError = picojson::parse(root, json);
		if (!parseError.empty() || !root.is<picojson::object>()) return false;

		const picojson::object& obj = root.get<picojson::object>();
		auto readList = [&obj](const char* key, std::vector<std::string>& dest) {
			const auto it = obj.find(key);
			if (it == obj.end() || !it->second.is<picojson::array>()) return;
			for (const picojson::value& entry : it->second.get<picojson::array>()) {
				if (entry.is<std::string>()) dest.push_back(entry.get<std::string>());
			}
		};
		readList("runtime", out.runtimes);
		readList("external_drivers", out.externalDrivers);
		return true;
	}

	inline ConflictReport ClassifyDriverConflict(const ConflictInputs& in)
	{
		ConflictReport report;

		const std::string ownKey = NormalizePathKey(in.ownDriverDir);
		std::vector<std::string> seen;
		auto consider = [&](const std::string& path, RivalKind kind) {
			const std::string key = NormalizePathKey(path);
			if (key.empty()) return;
			if (key == ownKey) {
				report.ownRegistered = true;
				return;
			}
			if (!IsSpaceCalibratorDriverLeaf(PathLeaf(key))) return;
			if (std::find(seen.begin(), seen.end(), key) != seen.end()) return;
			seen.push_back(key);
			report.rivals.push_back({path, kind});
		};

		if (!ownKey.empty()) {
			for (const std::string& entry : in.externalDrivers)
				consider(entry, RivalKind::ExternalDriver);
			for (const std::string& entry : in.runtimeDriverFolders)
				consider(entry, RivalKind::RuntimeFolder);
		}

		if (in.handshake == HandshakeOutcome::WrongGeneration) {
			report.tier = ConflictTier::Blocking;
			report.ownDriverStale = report.rivals.empty();
		}
		else if (!report.rivals.empty()) {
			report.tier = ConflictTier::Warning;
		}
		return report;
	}

} // namespace spacecal
