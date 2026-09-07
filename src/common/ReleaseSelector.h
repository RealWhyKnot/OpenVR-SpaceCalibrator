#pragma once

#include "UpdateVersion.h"

#include <picojson.h>
#include <string>
#include <vector>

namespace spacecal {

	enum class UpdateChannel
	{
		Dev,
		Beta,
		Release
	};

	inline UpdateChannel ParseUpdateChannel(const std::string& text)
	{
		if (text == "beta") {
			return UpdateChannel::Beta;
		}
		if (text == "release") {
			return UpdateChannel::Release;
		}
		return UpdateChannel::Dev;
	}

	inline const char* UpdateChannelName(UpdateChannel channel)
	{
		switch (channel) {
			case UpdateChannel::Beta: return "beta";
			case UpdateChannel::Release: return "release";
			default: return "dev";
		}
	}

	struct GithubRelease
	{
		std::string tag;
		std::string htmlUrl;
		std::string zipUrl;
		std::string shaUrl;
		bool draft = false;
		bool prerelease = false;
	};

	inline std::string ReleaseZipName(const std::string& tag)
	{
		std::string version = (!tag.empty() && (tag[0] == 'v' || tag[0] == 'V')) ? tag.substr(1) : tag;
		return "OpenVR-SpaceCalibrator-" + version + ".zip";
	}

	inline std::string ParseReleasesJson(const std::string& json, std::vector<GithubRelease>& out)
	{
		picojson::value root;
		std::string error = picojson::parse(root, json);
		if (!error.empty()) {
			return error;
		}
		if (!root.is<picojson::array>()) {
			return "releases response is not an array";
		}
		out.clear();
		for (const auto& item : root.get<picojson::array>()) {
			if (!item.is<picojson::object>()) {
				continue;
			}
			const auto& obj = item.get<picojson::object>();
			GithubRelease release;
			auto tagIt = obj.find("tag_name");
			if (tagIt == obj.end() || !tagIt->second.is<std::string>()) {
				continue;
			}
			release.tag = tagIt->second.get<std::string>();
			auto urlIt = obj.find("html_url");
			if (urlIt != obj.end() && urlIt->second.is<std::string>()) {
				release.htmlUrl = urlIt->second.get<std::string>();
			}
			auto draftIt = obj.find("draft");
			release.draft = draftIt != obj.end() && draftIt->second.is<bool>() && draftIt->second.get<bool>();
			auto preIt = obj.find("prerelease");
			release.prerelease = preIt != obj.end() && preIt->second.is<bool>() && preIt->second.get<bool>();
			std::string zipName = ReleaseZipName(release.tag);
			auto assetsIt = obj.find("assets");
			if (assetsIt != obj.end() && assetsIt->second.is<picojson::array>()) {
				for (const auto& asset : assetsIt->second.get<picojson::array>()) {
					if (!asset.is<picojson::object>()) {
						continue;
					}
					const auto& assetObj = asset.get<picojson::object>();
					auto nameIt = assetObj.find("name");
					auto downloadIt = assetObj.find("browser_download_url");
					if (nameIt == assetObj.end() || downloadIt == assetObj.end() || !nameIt->second.is<std::string>() ||
					    !downloadIt->second.is<std::string>()) {
						continue;
					}
					const std::string& name = nameIt->second.get<std::string>();
					if (name == zipName) {
						release.zipUrl = downloadIt->second.get<std::string>();
					}
					else if (name == zipName + ".sha256") {
						release.shaUrl = downloadIt->second.get<std::string>();
					}
				}
			}
			out.push_back(release);
		}
		return std::string();
	}

	inline const GithubRelease* SelectRelease(const std::vector<GithubRelease>& releases, const UpdateVersion& current,
	                                          UpdateChannel channel)
	{
		if (channel == UpdateChannel::Dev) {
			return nullptr;
		}
		const GithubRelease* best = nullptr;
		UpdateVersion bestVersion = current;
		for (const auto& release : releases) {
			if (release.draft || (channel == UpdateChannel::Release && release.prerelease)) {
				continue;
			}
			if (release.zipUrl.empty() || release.shaUrl.empty()) {
				continue;
			}
			UpdateVersion version;
			if (!ParseUpdateVersion(release.tag, version) || CompareUpdateVersion(version, bestVersion) <= 0) {
				continue;
			}
			best = &release;
			bestVersion = version;
		}
		return best;
	}

	enum class UpdateAction
	{
		None,
		Skip,
		Prompt,
		AutoInstall
	};

	inline UpdateAction DecideUpdateAction(const GithubRelease* selected, bool manual, bool autoInstall, const std::string& skippedTag)
	{
		if (!selected) {
			return UpdateAction::None;
		}
		if (!manual && !skippedTag.empty() && selected->tag == skippedTag) {
			return UpdateAction::Skip;
		}
		if (!manual && autoInstall) {
			return UpdateAction::AutoInstall;
		}
		return UpdateAction::Prompt;
	}

} // namespace spacecal
