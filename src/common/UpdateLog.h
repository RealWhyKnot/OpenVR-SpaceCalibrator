#pragma once

#include "UpdateVersion.h"

#include <cctype>
#include <initializer_list>
#include <string>

namespace spacecal {

	struct UpdateLogSummary
	{
		std::string targetTag;
		std::string lastMessage;
		bool finished = false;
	};

	inline bool HasUpdateLogTimestamp(const std::string& line)
	{
		if (line.size() < 20) {
			return false;
		}
		for (size_t i = 0; i < 19; ++i) {
			const char c = line[i];
			const bool ok = (i == 4 || i == 7)     ? c == '-'
			                : i == 10              ? c == 'T'
			                : (i == 13 || i == 16) ? c == ':'
			                                       : std::isdigit(static_cast<unsigned char>(c)) != 0;
			if (!ok) {
				return false;
			}
		}
		return line[19] == ' ';
	}

	inline std::string TrimUpdateLogText(const std::string& text)
	{
		const size_t begin = text.find_first_not_of(" \t\r\n");
		if (begin == std::string::npos) {
			return std::string();
		}
		const size_t end = text.find_last_not_of(" \t\r\n");
		return text.substr(begin, end - begin + 1);
	}

	inline std::string UpdateLogTagFromMessage(const std::string& message)
	{
		if (message.rfind("downloading ", 0) == 0) {
			const size_t marker = message.find("/download/");
			if (marker == std::string::npos) {
				return std::string();
			}
			const size_t begin = marker + 10;
			const size_t end = message.find('/', begin);
			return end == std::string::npos ? std::string() : message.substr(begin, end - begin);
		}
		if (message.rfind("update ", 0) == 0) {
			const size_t to = message.rfind(" to ");
			return to == std::string::npos ? std::string() : message.substr(to + 4);
		}
		return std::string();
	}

	inline bool IsUpdateLogStep(const std::string& message)
	{
		for (const char* step : {"update ", "downloading ", "verified", "waiting for ", "running ", "setup exited with code "}) {
			if (message.rfind(step, 0) == 0) {
				return true;
			}
		}
		return false;
	}

	inline UpdateLogSummary SummarizeUpdateLog(const std::string& text)
	{
		UpdateLogSummary summary;
		size_t pos = text.rfind("\xEF\xBB\xBF", 0) == 0 ? 3 : 0;
		std::string fallback;
		while (pos < text.size()) {
			size_t end = text.find('\n', pos);
			if (end == std::string::npos) {
				end = text.size();
			}
			const std::string line = text.substr(pos, end - pos);
			pos = end + 1;
			const std::string trimmed = TrimUpdateLogText(line);
			if (trimmed.empty()) {
				continue;
			}
			if (!HasUpdateLogTimestamp(line)) {
				fallback = trimmed;
				continue;
			}
			summary.lastMessage = TrimUpdateLogText(line.substr(20));
			const std::string tag = UpdateLogTagFromMessage(summary.lastMessage);
			if (!tag.empty()) {
				summary.targetTag = tag;
			}
		}
		if (summary.lastMessage.empty()) {
			summary.lastMessage = fallback;
		}
		summary.finished = summary.lastMessage == "installed";
		return summary;
	}

	inline std::string UpdateLogNote(const UpdateLogSummary& summary, const UpdateVersion& installed)
	{
		if (summary.lastMessage.empty() || summary.finished) {
			return std::string();
		}
		UpdateVersion target;
		if (!summary.targetTag.empty() && ParseUpdateVersion(summary.targetTag, target) && CompareUpdateVersion(installed, target) >= 0) {
			return std::string();
		}
		const bool failed = summary.lastMessage.rfind("failed: ", 0) == 0;
		if (!failed && IsUpdateLogStep(summary.lastMessage)) {
			return std::string();
		}
		std::string reason = failed ? summary.lastMessage.substr(8) : summary.lastMessage;
		if (reason.size() > 100) {
			size_t cut = 97;
			while (cut > 0 && (static_cast<unsigned char>(reason[cut]) & 0xC0) == 0x80) {
				--cut;
			}
			reason = reason.substr(0, cut) + "...";
		}
		const std::string subject = summary.targetTag.empty() ? "the last update" : "the update to " + summary.targetTag;
		return subject + " did not finish: " + reason + "; see update.log";
	}

} // namespace spacecal
