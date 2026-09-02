#pragma once

#include <cctype>
#include <string>

namespace spacecal {

	struct UpdateVersion
	{
		int parts[4] = {0, 0, 0, 0};
		bool beta = false;
	};

	inline bool ParseUpdateVersion(const std::string& text, UpdateVersion& out)
	{
		size_t begin = 0;
		size_t end = text.size();
		while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) {
			++begin;
		}
		while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
			--end;
		}
		if (begin < end && (text[begin] == 'v' || text[begin] == 'V')) {
			++begin;
		}
		std::string trimmed = text.substr(begin, end - begin);
		size_t dash = trimmed.find('-');
		std::string numeric = dash == std::string::npos ? trimmed : trimmed.substr(0, dash);
		std::string suffix = dash == std::string::npos ? std::string() : trimmed.substr(dash + 1);

		UpdateVersion parsed;
		int index = 0;
		size_t pos = 0;
		while (true) {
			size_t dot = numeric.find('.', pos);
			std::string part = numeric.substr(pos, dot == std::string::npos ? std::string::npos : dot - pos);
			if (index >= 4 || part.empty() || part.size() > 9) {
				return false;
			}
			int value = 0;
			for (char c : part) {
				if (!std::isdigit(static_cast<unsigned char>(c))) {
					return false;
				}
				value = value * 10 + (c - '0');
			}
			parsed.parts[index++] = value;
			if (dot == std::string::npos) {
				break;
			}
			pos = dot + 1;
		}
		if (index != 4) {
			return false;
		}
		std::string lowered = suffix;
		for (char& c : lowered) {
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}
		parsed.beta = lowered == "beta";
		out = parsed;
		return true;
	}

	inline int CompareUpdateVersion(const UpdateVersion& a, const UpdateVersion& b)
	{
		for (int i = 0; i < 4; ++i) {
			if (a.parts[i] != b.parts[i]) {
				return a.parts[i] < b.parts[i] ? -1 : 1;
			}
		}
		if (a.beta == b.beta) {
			return 0;
		}
		return a.beta ? -1 : 1;
	}

	inline std::string FormatUpdateVersion(const UpdateVersion& v)
	{
		std::string text = std::to_string(v.parts[0]) + "." + std::to_string(v.parts[1]) + "." + std::to_string(v.parts[2]) + "." +
		                   std::to_string(v.parts[3]);
		if (v.beta) {
			text += "-beta";
		}
		return text;
	}

} // namespace spacecal
