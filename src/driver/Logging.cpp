#define _CRT_SECURE_NO_DEPRECATE
#include "Logging.h"
#include <chrono>
#include <cstdint>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

FILE* LogFile;

namespace {

	constexpr const char* kLogPath = "space_calibrator_driver.log";
	constexpr const char* kOldLogPath = "space_calibrator_driver.old.log";
	constexpr uint64_t kMaxLogBytes = 8ull * 1024 * 1024;

	void RotateLogIfLarge()
	{
		WIN32_FILE_ATTRIBUTE_DATA info;
		if (!GetFileAttributesExA(kLogPath, GetFileExInfoStandard, &info)) return;
		const uint64_t size = ((uint64_t)info.nFileSizeHigh << 32) | info.nFileSizeLow;
		if (size > kMaxLogBytes) {
			MoveFileExA(kLogPath, kOldLogPath, MOVEFILE_REPLACE_EXISTING);
		}
	}

}

void OpenLogFile()
{
	RotateLogIfLarge();
	LogFile = fopen(kLogPath, "a");
	if (LogFile == nullptr) {
		LogFile = stderr;
	}
}

tm TimeForLog()
{
	auto now = std::chrono::system_clock::now();
	auto nowTime = std::chrono::system_clock::to_time_t(now);
	tm value;
	auto tm = localtime_s(&value, &nowTime);
	return value;
}

void LogFlush()
{
	fflush(LogFile);
}
