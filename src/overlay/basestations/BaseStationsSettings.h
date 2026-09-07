#pragma once

#include "BaseStationsLogic.h"

#include <picojson.h>

#include <map>
#include <string>

namespace spacecal::basestations {

	struct BaseStationsSettings
	{
		AutomationSettings automation;
		std::map<std::string, std::string> nicknames;
	};

	inline std::string SerializeBaseStationsSettings(const BaseStationsSettings& settings)
	{
		picojson::object obj;
		obj["power_management"].set<bool>(settings.automation.powerManagement);
		obj["wake_on_start"].set<bool>(settings.automation.wakeOnStart);
		obj["sleep_on_exit"].set<bool>(settings.automation.sleepOnExit);
		obj["use_standby"].set<bool>(settings.automation.useStandby);
		picojson::object nicknames;
		for (const auto& [serial, nickname] : settings.nicknames) {
			std::string value = nickname;
			nicknames[serial].set<std::string>(value);
		}
		obj["nicknames"].set<picojson::object>(nicknames);
		picojson::value v;
		v.set<picojson::object>(obj);
		return v.serialize();
	}

	inline BaseStationsSettings ParseBaseStationsSettings(const std::string& text)
	{
		BaseStationsSettings settings;
		picojson::value v;
		auto err = picojson::parse(v, text);
		if (!err.empty() || !v.is<picojson::object>()) {
			return settings;
		}
		auto obj = v.get<picojson::object>();
		settings.automation.powerManagement = obj["power_management"].evaluate_as_boolean();
		settings.automation.wakeOnStart = obj["wake_on_start"].evaluate_as_boolean();
		settings.automation.sleepOnExit = obj["sleep_on_exit"].evaluate_as_boolean();
		settings.automation.useStandby = obj["use_standby"].evaluate_as_boolean();
		if (obj["nicknames"].is<picojson::object>()) {
			for (auto& [serial, value] : obj["nicknames"].get<picojson::object>()) {
				if (value.is<std::string>() && !value.get<std::string>().empty()) {
					settings.nicknames[serial] = value.get<std::string>();
				}
			}
		}
		return settings;
	}

}
