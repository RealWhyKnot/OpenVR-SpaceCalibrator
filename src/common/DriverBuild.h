#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace spacecal {

	constexpr std::size_t kBuildIdLength = 16;
	constexpr std::string_view kBuildIdMarker = "SPACECAL_BUILD_ID=";

	inline bool IsBuildId(std::string_view id)
	{
		if (id.size() != kBuildIdLength) return false;
		for (char c : id) {
			const bool digit = c >= '0' && c <= '9';
			const bool hex = c >= 'a' && c <= 'f';
			if (!digit && !hex) return false;
		}
		return true;
	}

	inline std::string ReadHandshakeBuildId(uint32_t magic, uint32_t expectedMagic, const char* field, std::size_t fieldSize)
	{
		if (magic != expectedMagic) return {};
		std::size_t length = 0;
		while (length < fieldSize && field[length] != '\0')
			++length;
		if (length == fieldSize) return {};
		const std::string_view id(field, length);
		return IsBuildId(id) ? std::string(id) : std::string();
	}

	inline bool WriteHandshakeBuildId(std::string_view id, char* field, std::size_t fieldSize)
	{
		if (!IsBuildId(id) || id.size() >= fieldSize) return false;
		std::memset(field, 0, fieldSize);
		std::memcpy(field, id.data(), id.size());
		return true;
	}

	inline std::string FindBuildIdMarker(std::string_view bytes)
	{
		std::size_t pos = 0;
		while ((pos = bytes.find(kBuildIdMarker, pos)) != std::string_view::npos) {
			const std::size_t start = pos + kBuildIdMarker.size();
			const std::string_view candidate = bytes.substr(start, kBuildIdLength);
			const std::size_t end = start + kBuildIdLength;
			if (IsBuildId(candidate) && (end == bytes.size() || bytes[end] == '\0')) {
				return std::string(candidate);
			}
			pos = start;
		}
		return {};
	}

	enum class StaleDriverFix
	{
		None,
		PendingOnClose,
		LoadsOnRestart,
		NeedsInstall
	};

	struct DriverBuilds
	{
		std::string app;
		std::string loaded;
		std::string onDisk;
		bool driverAnswered = false;
		bool pendingStaged = false;
	};

	inline StaleDriverFix ClassifyStaleDriver(const DriverBuilds& builds)
	{
		if (builds.pendingStaged) return StaleDriverFix::PendingOnClose;
		if (!builds.driverAnswered || builds.app.empty() || builds.loaded == builds.app) return StaleDriverFix::None;
		if (builds.onDisk == builds.app) return StaleDriverFix::LoadsOnRestart;
		return StaleDriverFix::NeedsInstall;
	}

	inline const char* StaleDriverFixName(StaleDriverFix fix)
	{
		switch (fix) {
			case StaleDriverFix::None: return "none";
			case StaleDriverFix::PendingOnClose: return "staged build waits for SteamVR to close";
			case StaleDriverFix::LoadsOnRestart: return "matching driver loads at next SteamVR start";
			case StaleDriverFix::NeedsInstall: return "driver on disk is from another build";
		}
		return "unknown";
	}

} // namespace spacecal
