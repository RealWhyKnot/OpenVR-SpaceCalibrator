#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace spacecal::basestations {

	struct DiscoveredAdvert
	{
		uint64_t address = 0;
		std::string name;
		std::vector<uint8_t> manufacturerData;
	};

	struct BleWriteRequest
	{
		uint64_t address = 0;
		std::string label;
		std::string serviceUuid;
		std::string characteristicUuid;
		std::vector<std::vector<uint8_t>> writes;
	};

	struct BleWriteResult
	{
		uint64_t address = 0;
		std::string label;
		bool ok = false;
	};

	class IBleClient
	{
	public:
		virtual ~IBleClient() = default;
		virtual bool StartScan() = 0;
		virtual void StopScan() = 0;
		virtual bool ScanFailed() const = 0;
		virtual void EnqueueWrite(BleWriteRequest request) = 0;
		virtual std::vector<DiscoveredAdvert> DrainAdverts() = 0;
		virtual std::vector<BleWriteResult> DrainWriteResults() = 0;
	};

	std::unique_ptr<IBleClient> CreateWinRtBleClient();

}
