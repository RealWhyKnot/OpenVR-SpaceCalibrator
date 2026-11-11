#include "stdafx.h"
#include "CalibrationMetrics.h"
#include "Version.h"
#include <shlobj_core.h>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <vector>

namespace Metrics {
	double TimeSpan = 30, CurrentTime = 0;

	TimeSeries<Eigen::Vector3d> posOffset_rawComputed; // , rotOffset_rawComputed;
	TimeSeries<Eigen::Vector3d> posOffset_currentCal;  // , rotOffset_currentCal;
	TimeSeries<Eigen::Vector3d> posOffset_lastSample;  // , rotOffset_lastSample;
	TimeSeries<Eigen::Vector3d> posOffset_byRelPose;

	TimeSeries<double> error_rawComputed, error_currentCal, error_byRelPose, error_currentCalRelPose;
	TimeSeries<double> axisIndependence;
	TimeSeries<double> computationTime;
	TimeSeries<double> jitterRef, jitterTarget;

	// true - full calibration, false - static calibration
	TimeSeries<bool> calibrationApplied;

	// https://stackoverflow.com/a/17827724
	bool IsBrowsePath(const std::wstring& path)
	{
		return (path == L"." || path == L"..");
	}

	uint64_t CalculateDirSize(const std::wstring& path, uint64_t size = 0)
	{
		WIN32_FIND_DATA data;
		HANDLE sh = NULL;
		sh = FindFirstFile((path + L"\\*").c_str(), &data);

		if (sh == INVALID_HANDLE_VALUE) {
			// We should probably return an error, but we don't for the sake of minimising memory allocations
			return size;
		}

		do {
			// skip current and parent
			if (!IsBrowsePath(data.cFileName)) {
				// if found object is ...
				if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY)
					// directory, then search it recursievly
					size = CalculateDirSize(path + L"\\" + data.cFileName, size);
				else
					// otherwise get object size and add it to directory size
					size += (uint64_t)(data.nFileSizeHigh * (MAXDWORD) + data.nFileSizeLow);
			}

		} while (FindNextFile(sh, &data)); // do

		FindClose(sh);

		return size;
	}

	double timestamp()
	{
		static long long ts_start = ~0LL;

		LARGE_INTEGER ts, freq;
		QueryPerformanceCounter(&ts);
		QueryPerformanceFrequency(&freq);

		if (ts_start == ~0LL) ts_start = ts.QuadPart;

		ts.QuadPart -= ts_start;

		return ts.QuadPart / (double)freq.QuadPart;
	}

	void RecordTimestamp()
	{
		CurrentTime = timestamp();
	}

	bool LogsForcedOn()
	{
		return std::strcmp(SPACECAL_CHANNEL, "dev") == 0;
	}

	bool enableLogs = LogsForcedOn();

	static std::ofstream logFile;
	static bool logFileIsOpen = false;
	static bool failedToOpenLogFile = false;

	struct CsvField
	{
		const char* name;
		void (*writer)(std::ofstream& s);
	};

#define TS_FIELD(n) {#n, [](auto& s) { s << n.last(); }}

#define TS_VECTOR_FIELD(n)                                                                                                                 \
	{#n ".x", [](auto& s) { s << n.last()(0); }}, {#n ".y", [](auto& s) { s << n.last()(1); }},                                            \
	{                                                                                                                                      \
		#n ".z", [](auto& s) {                                                                                                             \
			s << n.last()(2);                                                                                                              \
		}                                                                                                                                  \
	}

	static const CsvField fields[] = {{"Timestamp", [](auto& s) { s << CurrentTime; }},

	                                  TS_VECTOR_FIELD(posOffset_rawComputed),
	                                  TS_VECTOR_FIELD(posOffset_currentCal),
	                                  TS_VECTOR_FIELD(posOffset_lastSample),
	                                  TS_VECTOR_FIELD(posOffset_byRelPose),

	                                  TS_FIELD(error_rawComputed),
	                                  TS_FIELD(error_currentCal),
	                                  TS_FIELD(error_byRelPose),
	                                  TS_FIELD(error_currentCalRelPose),
	                                  TS_FIELD(axisIndependence),
	                                  TS_FIELD(computationTime),
	                                  TS_FIELD(jitterRef),
	                                  TS_FIELD(jitterTarget),

	                                  {"calibrationApplied", [](auto& s) {
		                                   if (calibrationApplied.lastTs() == CurrentTime) {
			                                   if (calibrationApplied.last()) {
				                                   s << "FULL";
			                                   }
			                                   else {
				                                   s << "STATIC";
			                                   }
		                                   }
	                                   }}};


	void PruneLogs(const std::wstring& folder, const wchar_t* prefix, uint64_t maxAgeSeconds, uint64_t maxBytes)
	{
		struct Entry
		{
			std::wstring name;
			uint64_t written = 0;
			uint64_t bytes = 0;
		};
		std::vector<Entry> entries;

		const std::wstring searchPath = folder + L"\\" + prefix + L".*.txt";
		WIN32_FIND_DATAW found;
		HANDLE handle = FindFirstFileW(searchPath.c_str(), &found);
		if (handle == INVALID_HANDLE_VALUE) return;
		do {
			ULARGE_INTEGER written, bytes;
			written.HighPart = found.ftLastWriteTime.dwHighDateTime;
			written.LowPart = found.ftLastWriteTime.dwLowDateTime;
			bytes.HighPart = found.nFileSizeHigh;
			bytes.LowPart = found.nFileSizeLow;
			entries.push_back({found.cFileName, written.QuadPart, bytes.QuadPart});
		} while (FindNextFileW(handle, &found));
		FindClose(handle);

		FILETIME nowFile;
		GetSystemTimeAsFileTime(&nowFile);
		ULARGE_INTEGER now;
		now.HighPart = nowFile.dwHighDateTime;
		now.LowPart = nowFile.dwLowDateTime;
		const uint64_t maxAgeTicks = maxAgeSeconds * 10ULL * 1000ULL * 1000ULL;
		const uint64_t oldest = now.QuadPart > maxAgeTicks ? now.QuadPart - maxAgeTicks : 0;

		std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.written > b.written; });

		uint64_t kept = 0;
		for (const Entry& entry : entries) {
			if (entry.written >= oldest && kept + entry.bytes <= maxBytes) {
				kept += entry.bytes;
				continue;
			}
			const std::wstring filePath = folder + L"\\" + entry.name;
			DeleteFileW(filePath.c_str());
		}
	}

	bool OpenLogStream(const wchar_t* prefix, std::ofstream& out)
	{
		PWSTR RootPath = nullptr;
		if (S_OK != SHGetKnownFolderPath(FOLDERID_LocalAppDataLow, 0, nullptr, &RootPath)) {
			CoTaskMemFree(RootPath);
			return false;
		}

		std::wstring path(RootPath);
		CoTaskMemFree(RootPath);

		path += LR"(\SpaceCalibrator)";
		if (CreateDirectoryW(path.c_str(), 0) == 0 && GetLastError() != ERROR_ALREADY_EXISTS) {
			return false;
		}

		path += LR"(\Logs)";
		if (CreateDirectoryW(path.c_str(), 0) == 0 && GetLastError() != ERROR_ALREADY_EXISTS) {
			return false;
		}

		PruneLogs(path, prefix, kLogKeepSeconds, kLogKeepBytes);

		SYSTEMTIME now{};
		GetSystemTime(&now);

		size_t dateBufLen = GetDateFormatW(LOCALE_USER_DEFAULT, 0, &now, L"yyyy-MM-dd", nullptr, 0);
		std::vector<WCHAR> dateBuf(dateBufLen);
		if (!GetDateFormatEx(LOCALE_NAME_INVARIANT, 0, &now, L"yyyy-MM-dd", &dateBuf[0], static_cast<int>(dateBufLen), nullptr))
			return false;

		size_t timeBufLen = GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &now, L"HH-mm-ss", nullptr, 0);
		std::vector<WCHAR> timeBuf(timeBufLen);
		if (!GetTimeFormatEx(LOCALE_NAME_INVARIANT, 0, &now, L"HH-mm-ss", &timeBuf[0], static_cast<int>(timeBufLen))) return false;

		path += L'\\';
		path += prefix;
		path += L'.';
		path += &dateBuf[0];
		path += L'T';
		path += &timeBuf[0];
		path += L".txt";

		out.open(path);
		return !out.fail();
	}

	static bool OpenLogFile()
	{
		if (!OpenLogStream(L"spacecal_log", logFile)) {
			return false;
		}

		for (int i = 0; i < sizeof fields / sizeof fields[0]; i++) {
			if (i > 0) logFile << ",";
			logFile << fields[i].name;
		}
		logFile << "\n";

		logFileIsOpen = true;

		return true;
	}

	static bool CheckLogOpen()
	{
		if (!enableLogs) {
			if (logFileIsOpen) {
				logFile.close();
			}
			logFileIsOpen = false;
			failedToOpenLogFile = false;

			return false;
		}

		if (failedToOpenLogFile) return false;
		if (!logFileIsOpen && !OpenLogFile()) {
			failedToOpenLogFile = true;
			return false;
		}
		return true;
	}

	void WriteLogAnnotation(const char* s)
	{
		if (!CheckLogOpen()) return;

		logFile << "# [" << timestamp() << "] " << s << "\n";
		logFile.flush();
	}

	void WriteLogEntry()
	{
		if (!CheckLogOpen()) return;

		if (logFileIsOpen) {
			for (int i = 0; i < sizeof fields / sizeof fields[0]; i++) {
				if (i > 0) logFile << ",";
				fields[i].writer(logFile);
			}
			logFile << "\n";
		}
		logFile.flush();
	}
}
