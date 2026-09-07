#include "BleClient.h"

#include <cstdio>

#ifdef __clang_analyzer__

namespace spacecal::basestations {

	namespace {

		class NullBleClient final : public IBleClient
		{
		public:
			bool StartScan() override { return false; }
			void StopScan() override {}
			bool ScanFailed() const override { return true; }
			void EnqueueWrite(BleWriteRequest) override {}
			std::vector<DiscoveredAdvert> DrainAdverts() override { return {}; }
			std::vector<BleWriteResult> DrainWriteResults() override { return {}; }
		};

	}

	std::unique_ptr<IBleClient> CreateWinRtBleClient()
	{
		return std::make_unique<NullBleClient>();
	}

}

#else

#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/base.h>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace spacecal::basestations {

	namespace {

		namespace winrt_ble = winrt::Windows::Devices::Bluetooth;
		namespace winrt_adv = winrt::Windows::Devices::Bluetooth::Advertisement;
		namespace winrt_gatt = winrt::Windows::Devices::Bluetooth::GenericAttributeProfile;
		namespace winrt_streams = winrt::Windows::Storage::Streams;

		constexpr int kWriteRetries = 2;
		constexpr auto kWriteRetryDelay = std::chrono::milliseconds(250);

		bool ParseGuid(std::string_view canonical, winrt::guid& out)
		{
			unsigned int d1 = 0;
			unsigned int d2 = 0;
			unsigned int d3 = 0;
			unsigned int d4[8] = {};
			if (sscanf_s(std::string(canonical).c_str(), "%8x-%4x-%4x-%2x%2x-%2x%2x%2x%2x%2x%2x", &d1, &d2, &d3, &d4[0], &d4[1], &d4[2],
			             &d4[3], &d4[4], &d4[5], &d4[6], &d4[7]) != 11) {
				return false;
			}
			out = winrt::guid(d1, static_cast<uint16_t>(d2), static_cast<uint16_t>(d3),
			                  {static_cast<uint8_t>(d4[0]), static_cast<uint8_t>(d4[1]), static_cast<uint8_t>(d4[2]),
			                   static_cast<uint8_t>(d4[3]), static_cast<uint8_t>(d4[4]), static_cast<uint8_t>(d4[5]),
			                   static_cast<uint8_t>(d4[6]), static_cast<uint8_t>(d4[7])});
			return true;
		}

		class WinRtBleClient final : public IBleClient
		{
		public:
			WinRtBleClient()
			{
				m_worker = std::thread([this] { WorkerMain(); });
			}

			~WinRtBleClient() override
			{
				{
					std::lock_guard<std::mutex> lock(m_mutex);
					m_shutdown = true;
				}
				m_cv.notify_all();
				if (m_worker.joinable()) m_worker.join();
				StopScan();
			}

			bool StartScan() override
			{
				try {
					if (m_watcher) return true;
					m_watcher = winrt_adv::BluetoothLEAdvertisementWatcher();
					m_watcher.ScanningMode(winrt_adv::BluetoothLEScanningMode::Active);
					m_receivedToken = m_watcher.Received([this](const winrt_adv::BluetoothLEAdvertisementWatcher&,
					                                            const winrt_adv::BluetoothLEAdvertisementReceivedEventArgs& args) {
						DiscoveredAdvert advert;
						advert.address = args.BluetoothAddress();
						advert.name = winrt::to_string(args.Advertisement().LocalName());
						const auto sections = args.Advertisement().ManufacturerData();
						if (sections.Size() > 0) {
							const auto data = sections.GetAt(0).Data();
							advert.manufacturerData.resize(data.Length());
							const auto reader = winrt_streams::DataReader::FromBuffer(data);
							reader.ReadBytes(advert.manufacturerData);
						}
						std::lock_guard<std::mutex> lock(m_mutex);
						m_adverts.push_back(std::move(advert));
					});
					m_watcher.Start();
					m_scanFailed = false;
					return true;
				}
				catch (const winrt::hresult_error& ex) {
					fprintf(stderr, "basestations: BLE scan start failed: 0x%08X %s\n", static_cast<unsigned>(ex.code()),
					        winrt::to_string(ex.message()).c_str());
					m_watcher = nullptr;
					m_scanFailed = true;
					return false;
				}
			}

			void StopScan() override
			{
				try {
					if (m_watcher) {
						m_watcher.Received(m_receivedToken);
						m_watcher.Stop();
					}
				}
				catch (const winrt::hresult_error&) {
				}
				m_watcher = nullptr;
			}

			bool ScanFailed() const override { return m_scanFailed; }

			void EnqueueWrite(BleWriteRequest request) override
			{
				{
					std::lock_guard<std::mutex> lock(m_mutex);
					m_writeQueue.push_back(std::move(request));
				}
				m_cv.notify_all();
			}

			std::vector<DiscoveredAdvert> DrainAdverts() override
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				std::vector<DiscoveredAdvert> out(m_adverts.begin(), m_adverts.end());
				m_adverts.clear();
				return out;
			}

			std::vector<BleWriteResult> DrainWriteResults() override
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				std::vector<BleWriteResult> out(m_writeResults.begin(), m_writeResults.end());
				m_writeResults.clear();
				return out;
			}

		private:
			void WorkerMain()
			{
				winrt::init_apartment(winrt::apartment_type::multi_threaded);
				for (;;) {
					BleWriteRequest request;
					{
						std::unique_lock<std::mutex> lock(m_mutex);
						m_cv.wait(lock, [this] { return m_shutdown || !m_writeQueue.empty(); });
						if (m_shutdown) break;
						request = std::move(m_writeQueue.front());
						m_writeQueue.pop_front();
					}

					BleWriteResult result;
					result.address = request.address;
					result.label = request.label;
					for (int attempt = 0; attempt <= kWriteRetries && !result.ok; ++attempt) {
						if (attempt > 0) std::this_thread::sleep_for(kWriteRetryDelay);
						result.ok = PerformWrite(request);
					}
					{
						std::lock_guard<std::mutex> lock(m_mutex);
						m_writeResults.push_back(std::move(result));
					}
				}
				winrt::uninit_apartment();
			}

			bool PerformWrite(const BleWriteRequest& request)
			{
				winrt::guid serviceGuid{};
				winrt::guid charGuid{};
				if (!ParseGuid(request.serviceUuid, serviceGuid) || !ParseGuid(request.characteristicUuid, charGuid)) {
					return false;
				}
				try {
					auto device = winrt_ble::BluetoothLEDevice::FromBluetoothAddressAsync(request.address).get();
					if (!device) return false;
					auto servicesResult = device.GetGattServicesForUuidAsync(serviceGuid, winrt_ble::BluetoothCacheMode::Uncached).get();
					if (servicesResult.Status() != winrt_gatt::GattCommunicationStatus::Success || servicesResult.Services().Size() == 0) {
						device.Close();
						return false;
					}
					auto service = servicesResult.Services().GetAt(0);
					auto charsResult = service.GetCharacteristicsForUuidAsync(charGuid).get();
					if (charsResult.Status() != winrt_gatt::GattCommunicationStatus::Success || charsResult.Characteristics().Size() == 0) {
						device.Close();
						return false;
					}
					auto characteristic = charsResult.Characteristics().GetAt(0);

					bool allOk = true;
					for (const std::vector<uint8_t>& payload : request.writes) {
						winrt_streams::DataWriter writer;
						writer.WriteBytes(payload);
						const auto status = characteristic.WriteValueAsync(writer.DetachBuffer()).get();
						if (status != winrt_gatt::GattCommunicationStatus::Success) {
							allOk = false;
							break;
						}
					}
					device.Close();
					return allOk;
				}
				catch (const winrt::hresult_error& ex) {
					fprintf(stderr, "basestations: BLE write '%s' failed: 0x%08X %s\n", request.label.c_str(),
					        static_cast<unsigned>(ex.code()), winrt::to_string(ex.message()).c_str());
					return false;
				}
			}

			std::thread m_worker;
			mutable std::mutex m_mutex;
			std::condition_variable m_cv;
			bool m_shutdown = false;
			bool m_scanFailed = false;
			std::deque<BleWriteRequest> m_writeQueue;
			std::deque<DiscoveredAdvert> m_adverts;
			std::deque<BleWriteResult> m_writeResults;
			winrt_adv::BluetoothLEAdvertisementWatcher m_watcher{nullptr};
			winrt::event_token m_receivedToken{};
		};

	}

	std::unique_ptr<IBleClient> CreateWinRtBleClient()
	{
		return std::make_unique<WinRtBleClient>();
	}

}

#endif
