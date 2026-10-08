#pragma once

#include <map>
#include <string>

namespace spacecal {

	class LastWriteFilter
	{
	public:
		bool Unchanged(const std::string& name, const std::string& value) const
		{
			const auto it = written_.find(name);
			return it != written_.end() && it->second == value;
		}

		void Record(const std::string& name, const std::string& value) { written_[name] = value; }

	private:
		std::map<std::string, std::string> written_;
	};

}
