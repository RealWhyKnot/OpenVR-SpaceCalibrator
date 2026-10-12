#include "DriverConflict.h"
#include "RegistrationPlan.h"
#include "UninstallHelperScript.h"
#include "UnregisterDriverScript.h"
#include "UpdateHelperScript.h"

#include <cstdio>
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

	using namespace spacecal;

	const std::string kBom = "\xEF\xBB\xBF";
	const std::string kLeftQuote = "\xE2\x80\x98";
	const std::string kRightQuote = "\xE2\x80\x99";
	const std::string kLowQuote = "\xE2\x80\x9A";
	const std::string kReversedQuote = "\xE2\x80\x9B";

	std::vector<std::string> HostilePaths()
	{
		return {
		    "C:\\Users\\it's me\\AppData\\Local\\SpaceCalibrator",
		    "C:\\Users\\$env:USERNAME\\$(Remove-Item x)\\SpaceCalibrator",
		    "C:\\Users\\back`tick`n\\SpaceCalibrator",
		    "C:\\Users\\100%\\%TEMP%\\SpaceCalibrator",
		    "C:\\Users\\semi;colon & amp | pipe\\SpaceCalibrator",
		    "C:\\Users\\J\xC3\xB6rg \xE4\xB8\xAD\xE6\x96\x87\\SpaceCalibrator",
		    "C:\\Users\\O" + kRightQuote + "Brien\\SpaceCalibrator",
		    "D:\\" + std::string(4000, 'a') + "\\SpaceCalibrator",
		};
	}

	size_t CountOutsideQuotes(const std::string& script, char c)
	{
		size_t count = 0;
		bool inSingle = false;
		for (size_t i = 0; i < script.size(); ++i) {
			if (script[i] == '\'') {
				if (inSingle && i + 1 < script.size() && script[i + 1] == '\'') {
					++i;
					continue;
				}
				inSingle = !inSingle;
				continue;
			}
			if (!inSingle && script[i] == c) ++count;
		}
		return count;
	}

	size_t QuoteLength(const std::string& text, size_t i)
	{
		if (i >= text.size()) return 0;
		if (text[i] == '\'') return 1;
		for (const std::string& quote : {kLeftQuote, kRightQuote, kLowQuote, kReversedQuote}) {
			if (text.compare(i, quote.size(), quote) == 0) return quote.size();
		}
		return 0;
	}

	bool QuotesBalanced(const std::string& script)
	{
		bool inSingle = false;
		size_t i = 0;
		while (i < script.size()) {
			const size_t quote = QuoteLength(script, i);
			if (quote) {
				if (inSingle) {
					const size_t next = QuoteLength(script, i + quote);
					if (next) {
						i += quote + next;
						continue;
					}
					inSingle = false;
				}
				else {
					inSingle = true;
				}
				i += quote;
				continue;
			}
			if (script[i] == '\n' && inSingle) return false;
			++i;
		}
		return !inSingle;
	}

	void TestQuoteDoublesAsciiApostrophe()
	{
		CHECK(QuotePowerShell("") == "''");
		CHECK(QuotePowerShell("it's") == "'it''s'");
		CHECK(QuotePowerShell("''") == "''''''");
		CHECK(QuotePowerShell("$x `n %y%") == "'$x `n %y%'");
	}

	void TestQuoteDoublesTypographicApostrophes()
	{
		for (const std::string& quote : {kLeftQuote, kRightQuote, kLowQuote, kReversedQuote}) {
			const std::string quoted = QuotePowerShell("O" + quote + "Brien");
			const bool doubled = quoted.find(quote + quote) != std::string::npos;
			CHECK(doubled);
			if (!doubled)
				std::printf("  %02X %02X %02X left as a lone quote: PowerShell ends the string there\n", (unsigned char)quote[0],
				            (unsigned char)quote[1], (unsigned char)quote[2]);
		}
	}

	void TestUpdateScriptQuotesEveryPath()
	{
		for (const std::string& path : HostilePaths()) {
			UpdateHelperParams p;
			p.overlayPid = 1234;
			p.fromVersion = "2026.10.10.0-9029";
			p.tag = "v2026.10.11.0-beta";
			p.setupUrl = "https://example.invalid/OpenVR-SpaceCalibrator-Setup-2026.10.11.0-beta.exe";
			p.shaUrl = p.setupUrl + ".sha256";
			p.setupName = "OpenVR-SpaceCalibrator-Setup-2026.10.11.0-beta.exe";
			p.appMutex = "Global\\MUTEX__SpaceCalibrator";
			p.stagingDir = path + "\\update";
			p.installDir = path + "\\Programs";
			p.logPath = path + "\\update.log";
			const std::string script = BuildUpdateHelperScript(p);
			CHECK(script.find(QuotePowerShell(p.stagingDir)) != std::string::npos);
			CHECK(script.find(QuotePowerShell(p.installDir)) != std::string::npos);
			CHECK(script.find(QuotePowerShell(p.logPath)) != std::string::npos);
			CHECK(CountOutsideQuotes(script, '%') == 0);
			const bool balanced = QuotesBalanced(script);
			CHECK(balanced);
			if (!balanced) std::printf("  update script breaks for %.60s\n", path.c_str());
		}
	}

	void TestUninstallScriptQuotesEveryPath()
	{
		for (const std::string& path : HostilePaths()) {
			UninstallHelperParams p;
			p.overlayPid = 99;
			p.logPath = path + "\\uninstall-old.log";
			p.runtimeDriverDirs = {path + "\\drivers\\01spacecalibrator"};
			p.uninstallExe = path + "\\Uninstall.exe";
			p.programFilesDir = path;
			p.registryKeys = {"HKLM:\\Software\\SpaceCalibrator"};
			p.startMenuShortcut = path + "\\Space Calibrator.lnk";
			const std::string script = BuildUninstallHelperScript(p);
			CHECK(script.find("Remove-Item -LiteralPath " + QuotePowerShell(p.runtimeDriverDirs[0]) + " -Recurse -Force") !=
			      std::string::npos);
			CHECK(script.find("Start-Process -FilePath " + QuotePowerShell(p.uninstallExe)) != std::string::npos);
			CHECK(script.find("Remove-Item -LiteralPath " + QuotePowerShell(p.programFilesDir) + " -Recurse -Force") != std::string::npos);
			CHECK(script.find("Remove-Item -LiteralPath " + QuotePowerShell(p.startMenuShortcut) + " -Force") != std::string::npos);
			CHECK(script.find("-Path " + QuotePowerShell(path)) == std::string::npos);
			CHECK(QuotesBalanced(script));
		}
	}

	void TestUnregisterScriptQuotesEveryPath()
	{
		for (const std::string& path : HostilePaths()) {
			UnregisterDriverParams p;
			p.vrpathregExe = path + "\\SteamVR\\bin\\win64\\vrpathreg.exe";
			p.driverDirs = {path + "\\old\\01spacecalibrator"};
			p.addDriverDir = path + "\\Programs\\OpenVR-SpaceCalibrator\\01spacecalibrator";
			p.logPath = path + "\\register-driver.log";
			const std::string script = BuildUnregisterDriverScript(p);
			CHECK(script.find("& " + QuotePowerShell(p.vrpathregExe) + " removedriver " + QuotePowerShell(p.driverDirs[0])) !=
			      std::string::npos);
			CHECK(script.find("& " + QuotePowerShell(p.vrpathregExe) + " adddriver " + QuotePowerShell(p.addDriverDir)) !=
			      std::string::npos);
			CHECK(QuotesBalanced(script));
		}
	}

	void TestEveryHelperScriptIsUtf8WithBom()
	{
		UpdateHelperParams update;
		update.logPath = "C:\\Users\\J\xC3\xB6rg\\update.log";
		UninstallHelperParams uninstall;
		uninstall.logPath = "C:\\Users\\J\xC3\xB6rg\\uninstall-old.log";
		UnregisterDriverParams unregister;
		unregister.logPath = "C:\\Users\\J\xC3\xB6rg\\register-driver.log";
		unregister.vrpathregExe = "C:\\SteamVR\\bin\\win64\\vrpathreg.exe";
		unregister.addDriverDir = "C:\\Users\\J\xC3\xB6rg\\AppData\\Local\\Programs\\OpenVR-SpaceCalibrator\\01spacecalibrator";
		CHECK(BuildUpdateHelperScript(update).rfind(kBom, 0) == 0);
		CHECK(BuildUninstallHelperScript(uninstall).rfind(kBom, 0) == 0);
		CHECK(BuildUnregisterDriverScript(unregister).rfind(kBom, 0) == 0);
	}

	void TestScriptsUseCrlfOnly()
	{
		UninstallHelperParams uninstall;
		uninstall.runtimeDriverDirs = {"C:\\a", "C:\\b"};
		UnregisterDriverParams unregister;
		unregister.driverDirs = {"C:\\a"};
		for (const std::string& script :
		     {BuildUpdateHelperScript({}), BuildUninstallHelperScript(uninstall), BuildUnregisterDriverScript(unregister)}) {
			for (size_t i = 0; i < script.size(); ++i) {
				if (script[i] == '\n') CHECK(i > 0 && script[i - 1] == '\r');
			}
		}
	}

	void TestLongPathsAreNotTruncated()
	{
		const std::string longPath = "\\\\?\\D:\\" + std::string(32000, 'x') + "\\01spacecalibrator";
		UnregisterDriverParams p;
		p.vrpathregExe = "C:\\SteamVR\\bin\\win64\\vrpathreg.exe";
		p.driverDirs = {longPath};
		const std::string script = BuildUnregisterDriverScript(p);
		CHECK(script.find(QuotePowerShell(longPath)) != std::string::npos);
	}

	void TestVrpathEscapesAndRawUtf8AgreeWithOwnDir()
	{
		const std::string own = "C:\\Users\\J\xC3\xB6rg\\AppData\\Local\\Programs\\OpenVR-SpaceCalibrator\\01spacecalibrator";
		OpenVRPaths escaped;
		CHECK(ParseOpenVRPaths(
		    R"({"runtime":["C:\\SteamVR"],"external_drivers":["C:\\Users\\J\u00f6rg\\AppData\\Local\\Programs\\OpenVR-SpaceCalibrator\\01spacecalibrator"]})",
		    escaped));
		OpenVRPaths raw;
		CHECK(ParseOpenVRPaths(
		    "{\"runtime\":[\"C:\\\\SteamVR\"],\"external_drivers\":[\"" +
		        std::string("C:\\\\Users\\\\J\xC3\xB6rg\\\\AppData\\\\Local\\\\Programs\\\\OpenVR-SpaceCalibrator\\\\01spacecalibrator") +
		        "\"]}",
		    raw));
		CHECK(escaped.externalDrivers.size() == 1 && raw.externalDrivers.size() == 1);
		CHECK(escaped.externalDrivers == raw.externalDrivers);
		CHECK(BuildRegistrationPlan(escaped, own).alreadyRegistered);
		CHECK(OwnRegisteredDirs(raw, own).size() == 1);
		const std::string ownInCodePage1252 = "C:\\Users\\J\xF6rg\\AppData\\Local\\Programs\\OpenVR-SpaceCalibrator\\01spacecalibrator";
		CHECK(!BuildRegistrationPlan(escaped, ownInCodePage1252).alreadyRegistered);
	}

	void TestPlanBuildsOneVrpathregPerRuntime()
	{
		OpenVRPaths paths;
		paths.runtimes = {"C:\\SteamVR\\", "c:/steamvr", "D:\\Steam Library\\steamapps\\common\\SteamVR//", ""};
		const RegistrationPlan plan = BuildRegistrationPlan(paths, "C:\\own\\01spacecalibrator");
		CHECK(plan.vrpathregCandidates.size() == 2);
		if (plan.vrpathregCandidates.size() == 2) {
			CHECK(plan.vrpathregCandidates[0] == "C:\\SteamVR\\bin\\win64\\vrpathreg.exe");
			CHECK(plan.vrpathregCandidates[1] == "D:\\Steam Library\\steamapps\\common\\SteamVR\\bin\\win64\\vrpathreg.exe");
		}
	}

}

int main()
{
	TestQuoteDoublesAsciiApostrophe();
	TestQuoteDoublesTypographicApostrophes();
	TestUpdateScriptQuotesEveryPath();
	TestUninstallScriptQuotesEveryPath();
	TestUnregisterScriptQuotesEveryPath();
	TestEveryHelperScriptIsUtf8WithBom();
	TestScriptsUseCrlfOnly();
	TestLongPathsAreNotTruncated();
	TestVrpathEscapesAndRawUtf8AgreeWithOwnDir();
	TestPlanBuildsOneVrpathregPerRuntime();

	if (failures == 0) {
		std::printf("scripts_tests: all tests passed\n");
		return 0;
	}
	std::printf("scripts_tests: %d failure(s)\n", failures);
	return 1;
}
