#pragma once

#include "BaseStationsLogic.h"
#include "BaseStationsSettings.h"
#include "BleClient.h"

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace spacecal::basestations {

	class BaseStationsController
	{
	public:
		static BaseStationsController& Get();

		void EnsureStarted();
		bool Started() const { return client_ != nullptr; }
		bool BluetoothFailed() const;

		void Tick(bool vrConnected);
		void Shutdown();

		const std::vector<Station>& Stations() const { return stations_; }
		BaseStationsSettings& Settings() { return settings_; }
		void SaveSettings();
		bool SetNickname(const std::string& serial, const std::string& value);

		void RequestPower(const Station& station, PowerCommand command);
		void RequestPowerAll(PowerCommand command);
		void RequestChannel(const std::string& serial, uint8_t channel);
		void RequestIdentify(const Station& station);

	private:
		BaseStationsController();

		void RefreshOpenVrReferences();
		void RebuildStations();
		void TickIntents();
		void SendPowerWrite(const Station& station, PowerCommand command);
		void Enqueue(BleWriteRequest request);
		void DrainResults();

		BaseStationsSettings settings_;
		std::unique_ptr<IBleClient> client_;
		std::map<uint64_t, NamedAdvert> adverts_;
		std::vector<OpenVrReference> openvrRefs_;
		std::vector<Station> stations_;
		std::map<std::string, PowerIntent> intents_;
		AutomationState automationState_;
		bool prevVrConnected_ = false;
		int pendingWrites_ = 0;
		std::chrono::steady_clock::time_point lastOpenVrRefresh_{};
	};

}
