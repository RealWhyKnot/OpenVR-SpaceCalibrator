#include <Windows.h>

#include <openvr_driver.h>

#include <cstdio>
#include <filesystem>

namespace {

	int failures = 0;

#define CHECK(cond)                                                                                                                        \
	do {                                                                                                                                   \
		if (!(cond)) {                                                                                                                     \
			std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);                                                                     \
			++failures;                                                                                                                    \
		}                                                                                                                                  \
	} while (0)

	using DriverFactory = void* (*)(const char*, int*);

	bool OpensExclusively(const std::filesystem::path& path)
	{
		HANDLE file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE) return false;
		CloseHandle(file);
		return true;
	}

	void TestSteamWatchdogLoadReleasesDriver(const std::filesystem::path& driverPath)
	{
		const std::filesystem::path logPath = std::filesystem::current_path() / "space_calibrator_driver.log";

		HMODULE module = LoadLibraryW(driverPath.c_str());
		CHECK(module != nullptr);
		if (!module) {
			std::printf("  could not load %ls (error %lu)\n", driverPath.c_str(), GetLastError());
			return;
		}

		auto factory = reinterpret_cast<DriverFactory>(GetProcAddress(module, "HmdDriverFactory"));
		CHECK(factory != nullptr);
		if (factory) {
			int returnCode = vr::VRInitError_None;
			void* watchdog = factory(vr::IVRWatchdogProvider_Version, &returnCode);
			CHECK(watchdog == nullptr);
			CHECK(returnCode == vr::VRInitError_Init_InterfaceNotFound);
		}

		CHECK(!OpensExclusively(driverPath));
		CHECK(FreeLibrary(module));
		CHECK(GetModuleHandleW(driverPath.c_str()) == nullptr);
		CHECK(OpensExclusively(driverPath));
		CHECK(std::filesystem::exists(logPath));
		CHECK(OpensExclusively(logPath));
	}

	void TestServerProviderStillOffered(const std::filesystem::path& driverPath)
	{
		HMODULE module = LoadLibraryW(driverPath.c_str());
		CHECK(module != nullptr);
		if (!module) return;

		auto factory = reinterpret_cast<DriverFactory>(GetProcAddress(module, "HmdDriverFactory"));
		CHECK(factory != nullptr);
		if (factory) {
			int returnCode = vr::VRInitError_Unknown;
			CHECK(factory(vr::IServerTrackedDeviceProvider_Version, &returnCode) != nullptr);
			CHECK(factory("IVRNotARealInterface_001", &returnCode) == nullptr);
			CHECK(returnCode == vr::VRInitError_Init_InterfaceNotFound);
		}

		CHECK(FreeLibrary(module));
		CHECK(OpensExclusively(driverPath));
	}

}

int wmain(int argc, wchar_t** argv)
{
	if (argc < 2) {
		std::printf("usage: driver_unload_tests <path to driver_01spacecalibrator.dll>\n");
		return 1;
	}
	const std::filesystem::path driverPath = std::filesystem::absolute(argv[1]);

	TestSteamWatchdogLoadReleasesDriver(driverPath);
	TestServerProviderStillOffered(driverPath);

	if (failures == 0) {
		std::printf("driver_unload_tests: all tests passed\n");
		return 0;
	}
	std::printf("driver_unload_tests: %d failure(s)\n", failures);
	return 1;
}
