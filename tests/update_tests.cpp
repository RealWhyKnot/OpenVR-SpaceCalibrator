#include "ReleaseSelector.h"
#include "UninstallHelperScript.h"
#include "UpdateHelperScript.h"
#include "UpdateLog.h"
#include "UpdateVersion.h"

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

	UpdateVersion V(int a, int b, int c, int d, bool beta = false)
	{
		UpdateVersion v;
		v.parts[0] = a;
		v.parts[1] = b;
		v.parts[2] = c;
		v.parts[3] = d;
		v.beta = beta;
		return v;
	}

	GithubRelease R(const std::string& tag, bool prerelease = false, bool draft = false, bool assets = true)
	{
		GithubRelease r;
		r.tag = tag;
		r.prerelease = prerelease;
		r.draft = draft;
		if (assets) {
			r.setupUrl = "https://example.invalid/" + ReleaseSetupName(tag);
			r.setupShaUrl = r.setupUrl + ".sha256";
		}
		return r;
	}

	void TestSetupName()
	{
		CHECK(ReleaseSetupName("v2026.9.2.0-beta") == "OpenVR-SpaceCalibrator-Setup-2026.9.2.0-beta.exe");
		CHECK(ReleaseSetupName("V2026.9.2.0") == "OpenVR-SpaceCalibrator-Setup-2026.9.2.0.exe");
		CHECK(ReleaseSetupName("2026.9.2.0") == "OpenVR-SpaceCalibrator-Setup-2026.9.2.0.exe");
		CHECK(ReleaseSetupName("") == "OpenVR-SpaceCalibrator-Setup-.exe");
	}

	void TestParse()
	{
		UpdateVersion v;
		CHECK(ParseUpdateVersion("v2026.9.1.0-beta", v) && CompareUpdateVersion(v, V(2026, 9, 1, 0, true)) == 0);
		CHECK(ParseUpdateVersion("2026.9.1.0", v) && CompareUpdateVersion(v, V(2026, 9, 1, 0)) == 0);
		CHECK(ParseUpdateVersion("2026.9.1.0-760F", v) && !v.beta);
		CHECK(ParseUpdateVersion("0.0.0.0-DEV", v) && !v.beta);
		CHECK(ParseUpdateVersion(" V2026.12.31.7-BETA ", v) && v.beta && v.parts[3] == 7);
		CHECK(!ParseUpdateVersion("v1.5", v));
		CHECK(!ParseUpdateVersion("v1.4-bd_-r0", v));
		CHECK(!ParseUpdateVersion("2026.9.1", v));
		CHECK(!ParseUpdateVersion("2026.9.1.0.5", v));
		CHECK(!ParseUpdateVersion("2026.9.x.0", v));
		CHECK(!ParseUpdateVersion("", v));
		CHECK(FormatUpdateVersion(V(2026, 9, 1, 0, true)) == "2026.9.1.0-beta");
	}

	void TestCompare()
	{
		CHECK(CompareUpdateVersion(V(2026, 9, 1, 0), V(2026, 9, 1, 0, true)) > 0);
		CHECK(CompareUpdateVersion(V(2026, 9, 1, 0, true), V(2026, 8, 31, 5)) > 0);
		CHECK(CompareUpdateVersion(V(2026, 8, 31, 5), V(2026, 9, 1, 0)) < 0);
		CHECK(CompareUpdateVersion(V(2026, 9, 1, 1, true), V(2026, 9, 1, 0)) > 0);
		CHECK(CompareUpdateVersion(V(2026, 9, 1, 0, true), V(2026, 9, 1, 0, true)) == 0);
	}

	void TestSelect()
	{
		UpdateVersion current = V(2026, 9, 1, 0, true);
		std::vector<GithubRelease> releases = {R("v2026.9.3.0-beta", true), R("v2026.9.4.0", false, true), R("v2026.9.2.0"), R("v1.5.1"),
		                                       R("v2026.9.5.0-beta", true, false, false)};
		const GithubRelease* pick = SelectRelease(releases, current, UpdateChannel::Release);
		CHECK(pick && pick->tag == "v2026.9.2.0");
		pick = SelectRelease(releases, current, UpdateChannel::Beta);
		CHECK(pick && pick->tag == "v2026.9.3.0-beta");
		CHECK(SelectRelease(releases, current, UpdateChannel::Dev) == nullptr);
		std::vector<GithubRelease> same = {R("v2026.9.1.0-beta", true), R("v2026.8.1.0")};
		CHECK(SelectRelease(same, current, UpdateChannel::Beta) == nullptr);
		std::vector<GithubRelease> stable = {R("v2026.9.1.0")};
		pick = SelectRelease(stable, current, UpdateChannel::Beta);
		CHECK(pick && pick->tag == "v2026.9.1.0");
	}

	void TestJson()
	{
		const char* json =
		    "[{\"tag_name\":\"v2026.9.2.0-beta\",\"html_url\":\"https://example.invalid/r\",\"draft\":false,\"prerelease\":true,"
		    "\"assets\":[{\"name\":\"OpenVR-SpaceCalibrator-Setup-2026.9.2.0-beta.exe\",\"browser_download_url\":\"https://"
		    "example.invalid/z\"},"
		    "{\"name\":\"OpenVR-SpaceCalibrator-Setup-2026.9.2.0-beta.exe.sha256\",\"browser_download_url\":\"https://example.invalid/"
		    "s\"},"
		    "{\"name\":\"OpenVR-SpaceCalibrator-2026.9.2.0-beta.zip\",\"browser_download_url\":\"https://example.invalid/old\"}]},"
		    "{\"tag_name\":\"v1.5.1\",\"draft\":false,\"prerelease\":false,\"assets\":[]}]";
		std::vector<GithubRelease> releases;
		CHECK(ParseReleasesJson(json, releases).empty());
		CHECK(releases.size() == 2);
		CHECK(releases[0].tag == "v2026.9.2.0-beta" && releases[0].prerelease && !releases[0].draft);
		CHECK(releases[0].setupUrl == "https://example.invalid/z" && releases[0].setupShaUrl == "https://example.invalid/s");
		CHECK(releases[0].htmlUrl == "https://example.invalid/r");
		CHECK(releases[1].setupUrl.empty());
		CHECK(!ParseReleasesJson("{\"message\":\"rate limited\"}", releases).empty());
		CHECK(!ParseReleasesJson("not json", releases).empty());
	}

	void TestScript()
	{
		UpdateHelperParams p;
		p.overlayPid = 4242;
		p.fromVersion = "2026.9.1.0-beta";
		p.tag = "v2026.9.2.0-beta";
		p.setupUrl = "https://example.invalid/OpenVR-SpaceCalibrator-Setup-2026.9.2.0-beta.exe";
		p.shaUrl = p.setupUrl + ".sha256";
		p.setupName = "OpenVR-SpaceCalibrator-Setup-2026.9.2.0-beta.exe";
		p.stagingDir = "C:\\Users\\it's me\\AppData\\Local\\SpaceCalibrator\\update";
		p.installDir = "D:\\VR\\Space Cal";
		p.logPath = "C:\\Users\\it's me\\AppData\\Local\\SpaceCalibrator\\update.log";
		p.appMutex = "Global\\MUTEX__SpaceCalibratorSmoothing";
		std::string script = BuildUpdateHelperScript(p);
		const std::string bom = "\xEF\xBB\xBF";
		CHECK(script.rfind(bom, 0) == 0);
		CHECK(script.find("Wait-Process -Id 4242") != std::string::npos);
		CHECK(script.find("Get-Process vrserver") != std::string::npos);
		CHECK(script.find("Get-FileHash") == std::string::npos);
		CHECK(script.find("[System.Security.Cryptography.SHA256]::Create()") != std::string::npos);
		CHECK(script.find("catch [System.UnauthorizedAccessException] { return $true }") != std::string::npos);
		CHECK(script.find("$attempt -ge 5") != std::string::npos);
		CHECK(script.find("'C:\\Users\\it''s me\\AppData\\Local\\SpaceCalibrator\\update'") != std::string::npos);
		CHECK(script.find("$psi.Arguments = '/S /D=' + 'D:\\VR\\Space Cal'") != std::string::npos);
		CHECK(script.find("\"/D=") == std::string::npos);
		CHECK(script.find(p.setupUrl) != std::string::npos);
		CHECK(script.find("Log 'update 2026.9.1.0-beta to v2026.9.2.0-beta'") != std::string::npos);
		CHECK(script.find(std::string("Mutex($false, '") + UpdateHelperMutexName + "')") != std::string::npos);
		CHECK(script.find("AbandonedMutexException") != std::string::npos);
		CHECK(script.find("TryOpenExisting('Global\\MUTEX__SpaceCalibratorSmoothing', [ref]$app)") != std::string::npos);
		CHECK(script.find("$psi.UseShellExecute = $false") != std::string::npos);
		CHECK(script.find("$run.WaitForExit()") != std::string::npos);
		CHECK(script.find("-Wait") == std::string::npos);
		CHECK(script.find("Start-Process") == std::string::npos);
		CHECK(script.find("-Encoding UTF8") != std::string::npos);
		CHECK(script.find("'failed: '") != std::string::npos);
		CHECK(script.find("$code -eq 5") != std::string::npos && script.find("$code -eq 6") != std::string::npos);
		CHECK(script.find("WaitOne(0)") < script.find("Invoke-WebRequest"));
		CHECK(script.find("Invoke-WebRequest") < script.find("ComputeHash"));
		CHECK(script.find("ComputeHash") < script.find("Wait-Process"));
		CHECK(script.find("Wait-Process") < script.find("while (Busy)"));
		CHECK(script.find("while (Busy)") < script.find("$run.WaitForExit()"));
		CHECK(script.find("$run.WaitForExit()") < script.find("$run.ExitCode"));
		CHECK(script.find("$run.ExitCode") < script.find("Log 'installed'"));
		CHECK(script.find("Log 'installed'") < script.find("Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction"));
		CHECK(script.find("\r\n") != std::string::npos);
		for (size_t i = bom.size(); i < script.size(); ++i) {
			CHECK(static_cast<unsigned char>(script[i]) < 128);
		}

		UpdateHelperParams wide = p;
		wide.installDir = "C:\\Users\\J\xC3\xB6s\xC3\xA9\\Programs\\Space Cal";
		CHECK(BuildUpdateHelperScript(wide).find("'/S /D=' + 'C:\\Users\\J\xC3\xB6s\xC3\xA9\\Programs\\Space Cal'") != std::string::npos);
	}

	void TestUpdateLog()
	{
		const std::string url = "https://github.com/RealWhyKnot/OpenVR-SpaceCalibrator/releases/download/v2026.10.9.0-beta/"
		                        "OpenVR-SpaceCalibrator-Setup-2026.10.9.0-beta.exe";
		const std::string head = "\xEF\xBB\xBF"
		                         "2026-10-10T18:00:00 update 2026.10.8.2-beta to v2026.10.9.0-beta\r\n"
		                         "2026-10-10T18:00:00 downloading " +
		                         url +
		                         "\r\n"
		                         "2026-10-10T18:00:02 verified\r\n"
		                         "2026-10-10T18:00:02 waiting for overlay 4242\r\n"
		                         "2026-10-10T18:00:30 waiting for SteamVR and Space Calibrator to close\r\n";
		const UpdateVersion older = V(2026, 10, 8, 2, true);
		const UpdateVersion target = V(2026, 10, 9, 0, true);

		UpdateLogSummary done = SummarizeUpdateLog(head + "2026-10-10T18:00:40 running C:\\x\\setup.exe\r\n"
		                                                  "2026-10-10T18:00:42 setup exited with code 0\r\n"
		                                                  "2026-10-10T18:00:42 installed\r\n");
		CHECK(done.finished);
		CHECK(done.targetTag == "v2026.10.9.0-beta");
		CHECK(done.lastMessage == "installed");
		CHECK(UpdateLogNote(done, older).empty());

		UpdateLogSummary failed = SummarizeUpdateLog(head + "2026-10-10T18:00:40 running C:\\x\\setup.exe\r\n"
		                                                    "2026-10-10T18:00:42 setup exited with code 2\r\n"
		                                                    "2026-10-10T18:00:42 setup exited with code 2\r\n"
		                                                    "At line:1 char:1\r\n"
		                                                    "    + FullyQualifiedErrorId : setup exited with code 2\r\n"
		                                                    " \r\n"
		                                                    "2026-10-10T18:00:42 failed: setup exited with code 2\r\n");
		CHECK(!failed.finished);
		CHECK(failed.lastMessage == "failed: setup exited with code 2");
		CHECK(UpdateLogNote(failed, older) == "The update to v2026.10.9.0-beta did not finish: setup exited with code 2; see update.log");
		CHECK(UpdateLogNote(failed, target).empty());

		UpdateLogSummary killed = SummarizeUpdateLog(head);
		CHECK(!killed.finished);
		CHECK(UpdateLogNote(killed, older).empty());
		CHECK(UpdateLogNote(killed, target).empty());
		CHECK(UpdateLogNote(SummarizeUpdateLog(head + "2026-10-10T18:00:40 running C:\\x\\setup.exe\r\n"), older).empty());
		CHECK(UpdateLogNote(SummarizeUpdateLog("2026-10-10T18:00:00 downloading " + url + "\r\n"), older).empty());
		CHECK(UpdateLogNote(SummarizeUpdateLog("2026-10-10T18:00:00 waiting for vrserver\r\n"), older).empty());

		UpdateLogSummary oldFormat = SummarizeUpdateLog(
		    "2026-10-09T09:00:00 downloading " + url +
		    "\n"
		    "2026-10-09T09:00:01 Get-FileHash : The term 'Get-FileHash' is not recognized as the name of a cmdlet, function, script "
		    "file, or operable \n"
		    "program. Check the spelling of the name, or if a path was included, verify that the path is correct and try again.\n"
		    "    + FullyQualifiedErrorId : CommandNotFoundException\n"
		    " \n");
		CHECK(oldFormat.targetTag == "v2026.10.9.0-beta");
		CHECK(oldFormat.lastMessage.rfind("Get-FileHash : The term", 0) == 0);
		std::string oldNote = UpdateLogNote(oldFormat, older);
		CHECK(oldNote.rfind("The update to v2026.10.9.0-beta did not finish: Get-FileHash : The term", 0) == 0);
		CHECK(oldNote.find("...; see update.log") != std::string::npos);
		CHECK(oldNote.size() < 200);

		UpdateLogSummary zip =
		    SummarizeUpdateLog("2026-09-04T20:59:42 downloading "
			                   "https://github.com/RealWhyKnot/OpenVR-SpaceCalibrator/releases/download/v2026.9.4.0-beta/"
			                   "OpenVR-SpaceCalibrator-2026.9.4.0-beta.zip\r\n"
			                   "2026-09-04T20:59:45 waiting for overlay 28364\r\n"
			                   "2026-09-04T20:59:51 waiting for vrserver\r\n"
			                   "2026-09-04T20:59:53 installed\r\n");
		CHECK(zip.finished && zip.targetTag == "v2026.9.4.0-beta");

		UpdateLogSummary laterRun = SummarizeUpdateLog(head + "2026-10-10T18:00:42 failed: setup exited with code 2\r\n"
		                                                      "2026-10-11T08:00:00 update 2026.10.8.2-beta to v2026.10.11.0-beta\r\n"
		                                                      "2026-10-11T08:00:01 failed: Unable to connect to the remote server\r\n");
		CHECK(laterRun.targetTag == "v2026.10.11.0-beta");
		CHECK(UpdateLogNote(laterRun, target) ==
		      "The update to v2026.10.11.0-beta did not finish: Unable to connect to the remote server; see update.log");

		UpdateLogSummary noTag = SummarizeUpdateLog("garbage line\n");
		CHECK(noTag.targetTag.empty());
		CHECK(UpdateLogNote(noTag, older) == "The last update did not finish: garbage line; see update.log");

		const std::string utf8Tail = std::string(96, 'a') + "\xC3\xA9" + std::string(20, 'b');
		std::string cut = UpdateLogNote(SummarizeUpdateLog("2026-10-10T18:00:00 failed: " + utf8Tail + "\n"), older);
		CHECK(cut == "The last update did not finish: " + std::string(96, 'a') + "...; see update.log");

		CHECK(UpdateLogNote(SummarizeUpdateLog(""), older).empty());
		CHECK(UpdateLogNote(SummarizeUpdateLog("\xEF\xBB\xBF\r\n \r\n"), older).empty());
	}

	void TestDecideUpdateAction()
	{
		GithubRelease release = R("v2026.9.9.0-beta", true);
		CHECK(DecideUpdateAction(nullptr, false, true, "") == UpdateAction::None);
		CHECK(DecideUpdateAction(&release, false, true, "v2026.9.9.0-beta") == UpdateAction::Skip);
		CHECK(DecideUpdateAction(&release, true, true, "v2026.9.9.0-beta") == UpdateAction::Prompt);
		CHECK(DecideUpdateAction(&release, false, true, "") == UpdateAction::AutoInstall);
		CHECK(DecideUpdateAction(&release, false, false, "") == UpdateAction::Prompt);
		CHECK(DecideUpdateAction(&release, true, true, "") == UpdateAction::Prompt);
	}

	void TestUninstallScript()
	{
		UninstallHelperParams p;
		p.overlayPid = 777;
		p.logPath = "C:\\Users\\it's me\\AppData\\Local\\SpaceCalibrator\\uninstall-old.log";
		p.runtimeDriverDirs = {"C:\\Program Files (x86)\\Steam\\steamapps\\common\\SteamVR\\drivers\\01spacecalibrator"};
		p.uninstallExe = "C:\\Program Files\\SpaceCalibrator\\Uninstall.exe";
		p.programFilesDir = "C:\\Program Files\\SpaceCalibrator";
		p.registryKeys = {"HKLM:\\Software\\SpaceCalibrator"};
		p.startMenuShortcut = "C:\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs\\Space Calibrator.lnk";
		std::string script = BuildUninstallHelperScript(p);
		CHECK(script.find("Wait-Process -Id 777") != std::string::npos);
		CHECK(script.find("Get-Process vrserver") != std::string::npos);
		CHECK(script.find("Get-Process vrmonitor") != std::string::npos);
		CHECK(script.find("drivers\\01spacecalibrator") != std::string::npos);
		CHECK(script.find("-ArgumentList '/S' -Wait") != std::string::npos);
		CHECK(script.find("HKLM:\\Software\\SpaceCalibrator") != std::string::npos);
		CHECK(script.find("Space Calibrator.lnk") != std::string::npos);
		CHECK(script.find("'C:\\Users\\it''s me\\AppData\\Local\\SpaceCalibrator\\uninstall-old.log'") != std::string::npos);
		CHECK(script.find("Wait-Process") < script.find("Remove-Item"));

		UninstallHelperParams noExe = p;
		noExe.uninstallExe.clear();
		std::string scriptNoExe = BuildUninstallHelperScript(noExe);
		CHECK(scriptNoExe.find("Uninstall.exe") == std::string::npos);
		CHECK(scriptNoExe.find("'C:\\Program Files\\SpaceCalibrator'") != std::string::npos);
	}

} // namespace

int main()
{
	TestParse();
	TestCompare();
	TestSetupName();
	TestSelect();
	TestJson();
	TestScript();
	TestUpdateLog();
	TestDecideUpdateAction();
	TestUninstallScript();
	if (failures) {
		std::printf("%d failure(s)\n", failures);
		return 1;
	}
	std::printf("update tests passed\n");
	return 0;
}
