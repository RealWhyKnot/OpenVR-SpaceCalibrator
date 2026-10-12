#pragma once

#include <Windows.h>

#include <string>

namespace spacecal::registry_text {

	inline constexpr wchar_t kEncodingMarkerName[] = L"TextEncoding";
	inline constexpr wchar_t kEncodingMarkerValue[] = L"utf-8";

	enum class ReadOutcome
	{
		Found,
		Missing,
		Failed
	};

	inline ReadOutcome ClassifyRead(LSTATUS status)
	{
		if (status == ERROR_SUCCESS) return ReadOutcome::Found;
		if (status == ERROR_FILE_NOT_FOUND) return ReadOutcome::Missing;
		return ReadOutcome::Failed;
	}

	inline bool BlocksWrites(ReadOutcome outcome)
	{
		return outcome == ReadOutcome::Failed;
	}

	inline std::wstring Utf8ToWide(const std::string& text)
	{
		if (text.empty()) return {};
		const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0);
		if (length <= 0) return {};
		std::wstring wide((size_t)length, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), wide.data(), length);
		return wide;
	}

	inline std::string WideToUtf8(const std::wstring& text)
	{
		if (text.empty()) return {};
		const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0, nullptr, nullptr);
		if (length <= 0) return {};
		std::string narrow((size_t)length, '\0');
		WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), narrow.data(), length, nullptr, nullptr);
		return narrow;
	}

	inline bool IsAscii(const std::wstring& text)
	{
		for (wchar_t c : text) {
			if (c >= 0x80) return false;
		}
		return true;
	}

	inline bool EncodeExactly(const std::wstring& text, unsigned codePage, std::string& bytes)
	{
		BOOL usedDefault = FALSE;
		const int length =
		    WideCharToMultiByte(codePage, WC_NO_BEST_FIT_CHARS, text.data(), (int)text.size(), nullptr, 0, nullptr, &usedDefault);
		if (length <= 0 || usedDefault) return false;
		bytes.assign((size_t)length, '\0');
		const int written =
		    WideCharToMultiByte(codePage, WC_NO_BEST_FIT_CHARS, text.data(), (int)text.size(), bytes.data(), length, nullptr, &usedDefault);
		return written == length && !usedDefault;
	}

	inline bool DecodeStrictUtf8(const std::string& bytes, std::wstring& text)
	{
		const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), (int)bytes.size(), nullptr, 0);
		if (length <= 0) return false;
		std::wstring decoded((size_t)length, L'\0');
		if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), (int)bytes.size(), decoded.data(), length) != length) {
			return false;
		}
		text = std::move(decoded);
		return true;
	}

	inline bool RepairLegacyText(const std::wstring& stored, unsigned legacyCodePage, std::wstring& repaired)
	{
		if (legacyCodePage == 0 || legacyCodePage == CP_UTF8 || IsAscii(stored)) return false;
		std::string bytes;
		if (!EncodeExactly(stored, legacyCodePage, bytes)) return false;
		if (bytes == WideToUtf8(stored)) return false;
		return DecodeStrictUtf8(bytes, repaired);
	}

	inline unsigned SystemAnsiCodePage()
	{
		DWORD codePage = 0;
		if (GetLocaleInfoEx(LOCALE_NAME_SYSTEM_DEFAULT, LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER, (LPWSTR)&codePage,
		                    sizeof(codePage) / sizeof(WCHAR)) == 0) {
			return 0;
		}
		return codePage;
	}

}
