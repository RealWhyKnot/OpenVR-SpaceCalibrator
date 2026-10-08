#pragma once

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>

#include <cwchar>

namespace spacecal {

	inline DWORD FindProcessId(const wchar_t* exeName)
	{
		HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (snapshot == INVALID_HANDLE_VALUE) return 0;

		DWORD pid = 0;
		PROCESSENTRY32W entry = {};
		entry.dwSize = sizeof(entry);
		if (Process32FirstW(snapshot, &entry)) {
			do {
				if (_wcsicmp(entry.szExeFile, exeName) == 0) {
					pid = entry.th32ProcessID;
					break;
				}
			} while (Process32NextW(snapshot, &entry));
		}
		CloseHandle(snapshot);
		return pid;
	}

	class ProcessWatch
	{
	public:
		explicit ProcessWatch(const wchar_t* exeName) : exeName_(exeName) {}
		~ProcessWatch() { Release(); }
		ProcessWatch(const ProcessWatch&) = delete;
		ProcessWatch& operator=(const ProcessWatch&) = delete;

		bool Running()
		{
			if (process_ && WaitForSingleObject(process_, 0) == WAIT_TIMEOUT) return true;
			Release();
			const DWORD pid = FindProcessId(exeName_);
			if (pid == 0) return false;
			process_ = OpenProcess(SYNCHRONIZE, FALSE, pid);
			return true;
		}

		void Release()
		{
			if (process_) CloseHandle(process_);
			process_ = nullptr;
		}

	private:
		const wchar_t* exeName_;
		HANDLE process_ = nullptr;
	};

}
