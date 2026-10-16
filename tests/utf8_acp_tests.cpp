#include <Windows.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {

	int failures = 0;

#define CHECK(cond)                                                                                                                        \
	do {                                                                                                                                   \
		if (!(cond)) {                                                                                                                     \
			std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);                                                                     \
			++failures;                                                                                                                    \
		}                                                                                                                                  \
	} while (0)

	constexpr int kSkipped = 77;

	struct NamedPath
	{
		std::wstring wide;
		std::string utf8;
	};

	std::vector<NamedPath> UserPaths()
	{
		return {
		    {L"C:\\Users\\\u0141ukasz\\x", "C:\\Users\\\xC5\x81ukasz\\x"},
		    {L"C:\\Users\\J\u00F6rg\\AppData\\Local", "C:\\Users\\J\xC3\xB6rg\\AppData\\Local"},
		    {L"C:\\Users\\O\u2019Brien\\x", "C:\\Users\\O\xE2\x80\x99"
			                                "Brien\\x"},
		    {L"C:\\Users\\\u4E2D\u6587\\x", "C:\\Users\\\xE4\xB8\xAD\xE6\x96\x87\\x"},
		    {L"C:\\Users\\\U0001F600\\x", "C:\\Users\\\xF0\x9F\x98\x80\\x"},
		};
	}

	std::string EmbeddedManifest(HMODULE module)
	{
		HRSRC resource = FindResource(module, CREATEPROCESS_MANIFEST_RESOURCE_ID, RT_MANIFEST);
		if (!resource) return {};
		HGLOBAL loaded = LoadResource(module, resource);
		const DWORD size = SizeofResource(module, resource);
		const void* data = loaded ? LockResource(loaded) : nullptr;
		if (!data || size == 0) return {};
		return std::string(static_cast<const char*>(data), size);
	}

	bool DeclaresUtf8CodePage(const std::string& manifest)
	{
		if (manifest.find("http://schemas.microsoft.com/SMI/2019/WindowsSettings") == std::string::npos) return false;
		const size_t element = manifest.find("activeCodePage");
		if (element == std::string::npos) return false;
		const size_t content = manifest.find('>', element);
		if (content == std::string::npos) return false;
		return manifest.compare(content + 1, 6, "UTF-8<") == 0;
	}

	void TestManifestDeclaresUtf8()
	{
		const std::string manifest = EmbeddedManifest(GetModuleHandle(nullptr));
		CHECK(!manifest.empty());
		const bool declared = DeclaresUtf8CodePage(manifest);
		CHECK(declared);
		if (!declared) std::printf("  embedded manifest has no activeCodePage UTF-8:\n%s\n", manifest.c_str());
	}

	void TestAnsiCodePageIsUtf8()
	{
		const UINT acp = GetACP();
		CHECK(acp == CP_UTF8);
		if (acp != CP_UTF8) std::printf("  GetACP() returned %u\n", acp);
	}

	void TestPathStringIsUtf8()
	{
		for (const NamedPath& named : UserPaths()) {
			const std::filesystem::path path(named.wide);
			std::string narrow;
			bool threw = false;
			try {
				narrow = path.string();
			}
			catch (const std::exception& e) {
				threw = true;
				std::printf("  path::string() threw: %s\n", e.what());
			}
			CHECK(!threw);
			CHECK(narrow == named.utf8);
			CHECK(std::filesystem::path(named.utf8).wstring() == named.wide);
		}
	}

	void TestNarrowWin32CallsAreUtf8()
	{
		const wchar_t* name = L"SPACECAL_UTF8_ACP_PROBE";
		for (const NamedPath& named : UserPaths()) {
			CHECK(SetEnvironmentVariable(name, named.wide.c_str()));
			char buffer[256] = {};
			const DWORD length = GetEnvironmentVariableA("SPACECAL_UTF8_ACP_PROBE", buffer, sizeof(buffer));
			CHECK(length == named.utf8.size());
			CHECK(std::string(buffer) == named.utf8);
		}
		SetEnvironmentVariable(name, nullptr);
	}

	int CheckOtherExe(const wchar_t* exePath)
	{
		if (!std::filesystem::is_regular_file(exePath)) {
			std::printf("utf8_acp_tests: %ls is not built, skipping\n", exePath);
			return kSkipped;
		}
		HMODULE module = LoadLibraryEx(exePath, nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
		CHECK(module != nullptr);
		if (!module) return 1;
		const std::string manifest = EmbeddedManifest(module);
		FreeLibrary(module);
		const bool declared = DeclaresUtf8CodePage(manifest);
		CHECK(declared);
		if (!declared) std::printf("  %ls carries no activeCodePage UTF-8 in its manifest:\n%s\n", exePath, manifest.c_str());
		return failures == 0 ? 0 : 1;
	}

}

int wmain(int argc, wchar_t** argv)
{
	if (argc > 1) {
		const int result = CheckOtherExe(argv[1]);
		if (result == 0) std::printf("utf8_acp_tests: %ls declares the UTF-8 code page\n", argv[1]);
		return result;
	}

	TestManifestDeclaresUtf8();
	TestAnsiCodePageIsUtf8();
	TestPathStringIsUtf8();
	TestNarrowWin32CallsAreUtf8();

	if (failures == 0) {
		std::printf("utf8_acp_tests: all tests passed\n");
		return 0;
	}
	std::printf("utf8_acp_tests: %d failure(s)\n", failures);
	return 1;
}
