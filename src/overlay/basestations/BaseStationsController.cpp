#include "stdafx.h"
#include "BaseStationsController.h"
#include "Configuration.h"

#include <openvr.h>

#include <cstdio>
#include <thread>

namespace spacecal::basestations {

	namespace {
		constexpr int kCommandRetries = 4;
		constexpr double kOpenVrRefreshSec = 1.0;
	}

	BaseStationsController& BaseStationsController::Get()
	{
		static BaseStationsController instance;
		return instance;
	}

	BaseStationsController::BaseStationsController()
	{
		LoadBaseStationsSettings(settings_);
	}

	bool BaseStationsController::BluetoothFailed() const
	{
		return client_ && client_->ScanFailed();
	}

	void BaseStationsController::EnsureStarted()
	{
		if (client_) return;
		client_ = CreateWinRtBleClient();
		client_->StartScan();
	}

	void BaseStationsController::SaveSettings()
	{
		SaveBaseStationsSettings(settings_);
	}

	bool BaseStationsController::SetNickname(const std::string& serial, const std::string& value)
	{
		if (!ApplyNickname(settings_.nicknames, serial, value)) return false;
		SaveSettings();
		return true;
	}

	void BaseStationsController::Tick(bool vrConnected)
	{
		if (!client_) {
			prevVrConnected_ = vrConnected;
			return;
		}

		for (DiscoveredAdvert& advert : client_->DrainAdverts()) {
			const StationKind kind = ClassifyAdvertName(advert.name);
			if (kind == StationKind::Unknown) continue;
			NamedAdvert named;
			named.address = advert.address;
			named.name = std::move(advert.name);
			named.kind = kind;
			if (kind == StationKind::V2) named.v2State = DecodeV2ManufacturerData(advert.manufacturerData);
			adverts_[named.address] = std::move(named);
		}

		DrainResults();

		const auto now = std::chrono::steady_clock::now();
		if (vrConnected && std::chrono::duration<double>(now - lastOpenVrRefresh_).count() >= kOpenVrRefreshSec) {
			lastOpenVrRefresh_ = now;
			RefreshOpenVrReferences();
		}
		if (!vrConnected) openvrRefs_.clear();

		RebuildStations();
		TickIntents();

		const AutomationAction action = EvaluateAutomation(automationState_, prevVrConnected_, vrConnected, settings_.automation);
		prevVrConnected_ = vrConnected;
		if (action != AutomationAction::None) {
			const PowerCommand command = action == AutomationAction::WakeAll      ? PowerCommand::Wake
			                             : action == AutomationAction::StandbyAll ? PowerCommand::Standby
			                                                                      : PowerCommand::Sleep;
			for (const Station& station : stations_) {
				RequestPower(station, command);
			}
		}
	}

	void BaseStationsController::Shutdown()
	{
		if (!client_) return;

		const AutomationAction action = EvaluateAutomation(automationState_, prevVrConnected_, false, settings_.automation);
		prevVrConnected_ = false;
		if (action != AutomationAction::None) {
			const PowerCommand command = action == AutomationAction::StandbyAll ? PowerCommand::Standby : PowerCommand::Sleep;
			for (const Station& station : stations_) {
				RequestPower(station, command);
			}
		}

		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (pendingWrites_ > 0 && std::chrono::steady_clock::now() < deadline) {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			DrainResults();
		}
		client_.reset();
	}

	void BaseStationsController::RefreshOpenVrReferences()
	{
		auto* system = vr::VRSystem();
		if (!system) return;
		openvrRefs_.clear();
		char buffer[vr::k_unMaxPropertyStringSize] = {};
		for (uint32_t id = 0; id < vr::k_unMaxTrackedDeviceCount; ++id) {
			if (system->GetTrackedDeviceClass(id) != vr::TrackedDeviceClass_TrackingReference) continue;
			OpenVrReference ref;
			vr::ETrackedPropertyError err = vr::TrackedProp_Success;
			system->GetStringTrackedDeviceProperty(id, vr::Prop_SerialNumber_String, buffer, sizeof buffer, &err);
			if (err != vr::TrackedProp_Success) continue;
			ref.serial = buffer;
			err = vr::TrackedProp_Success;
			system->GetStringTrackedDeviceProperty(id, vr::Prop_ModelNumber_String, buffer, sizeof buffer, &err);
			if (err == vr::TrackedProp_Success) ref.model = buffer;
			openvrRefs_.push_back(std::move(ref));
		}
	}

	void BaseStationsController::RebuildStations()
	{
		std::vector<NamedAdvert> adverts;
		adverts.reserve(adverts_.size());
		for (const auto& [address, advert] : adverts_)
			adverts.push_back(advert);
		stations_ = MergeDiscovery(adverts, openvrRefs_);
		for (Station& station : stations_) {
			const auto nick = settings_.nicknames.find(station.serial);
			if (nick != settings_.nicknames.end()) station.nickname = nick->second;
		}
	}

	void BaseStationsController::TickIntents()
	{
		for (const Station& station : stations_) {
			auto it = intents_.find(station.serial);
			if (it == intents_.end()) continue;
			const bool observable = station.kind == StationKind::V2 && station.seenByBle;
			if (TickIntent(it->second, station.powerState, observable) == IntentAction::SendCommand) {
				SendPowerWrite(station, it->second.target);
			}
		}
	}

	void BaseStationsController::RequestPower(const Station& station, PowerCommand command)
	{
		if (!station.seenByBle || !client_) return;
		BeginIntent(intents_[station.serial], command, kCommandRetries);
		SendPowerWrite(station, command);
	}

	void BaseStationsController::RequestPowerAll(PowerCommand command)
	{
		for (const Station& station : stations_) {
			RequestPower(station, command);
		}
	}

	void BaseStationsController::SendPowerWrite(const Station& station, PowerCommand command)
	{
		BleWriteRequest request;
		request.address = station.address;
		if (station.kind == StationKind::V2) {
			request.label = "power:" + station.serial;
			request.serviceUuid = std::string(kV2ServiceUuid);
			request.characteristicUuid = std::string(kV2PowerCharUuid);
			request.writes = EncodeV2PowerWrites(command, station.standbySupported);
		}
		else {
			request.label = "power_v1:" + station.serial;
			request.serviceUuid = std::string(kV1ServiceUuid);
			request.characteristicUuid = std::string(kV1PowerCharUuid);
			const auto packet = command == PowerCommand::Wake ? EncodeV1PowerPacket(kV1CommandOn, 0, kV1GenericId)
			                                                  : EncodeV1PowerPacket(kV1CommandTimedOff, kV1SleepTimeoutSec, kV1GenericId);
			request.writes = {{packet.begin(), packet.end()}};
		}
		Enqueue(std::move(request));
	}

	void BaseStationsController::RequestChannel(const std::string& serial, uint8_t channel)
	{
		if (!client_) return;
		for (const Station& station : stations_) {
			if (station.serial != serial || station.kind != StationKind::V2 || !station.seenByBle) continue;
			const auto write = EncodeV2ChannelWrite(channel);
			if (!write) return;
			BleWriteRequest request;
			request.address = station.address;
			request.label = "channel:" + serial;
			request.serviceUuid = std::string(kV2ServiceUuid);
			request.characteristicUuid = std::string(kV2ChannelCharUuid);
			request.writes = {{write->begin(), write->end()}};
			Enqueue(std::move(request));
			return;
		}
	}

	void BaseStationsController::RequestIdentify(const Station& station)
	{
		if (!client_ || station.kind != StationKind::V2 || !station.seenByBle) return;
		const auto write = EncodeV2IdentifyWrite();
		BleWriteRequest request;
		request.address = station.address;
		request.label = "identify:" + station.serial;
		request.serviceUuid = std::string(kV2ServiceUuid);
		request.characteristicUuid = std::string(kV2IdentifyCharUuid);
		request.writes = {{write.begin(), write.end()}};
		Enqueue(std::move(request));
	}

	void BaseStationsController::Enqueue(BleWriteRequest request)
	{
		++pendingWrites_;
		client_->EnqueueWrite(std::move(request));
	}

	void BaseStationsController::DrainResults()
	{
		for (const BleWriteResult& result : client_->DrainWriteResults()) {
			--pendingWrites_;
			if (!result.ok) {
				fprintf(stderr, "basestations: command '%s' failed after retries\n", result.label.c_str());
			}
		}
	}

}
