#include "ReleaseSelector.h"
#include "UpdateHelperScript.h"
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
			r.zipUrl = "https://example.invalid/" + ReleaseZipName(tag);
			r.shaUrl = r.zipUrl + ".sha256";
		}
		return r;
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
		    "\"assets\":[{\"name\":\"OpenVR-SpaceCalibrator-2026.9.2.0-beta.zip\",\"browser_download_url\":\"https://example.invalid/z\"},"
		    "{\"name\":\"OpenVR-SpaceCalibrator-2026.9.2.0-beta.zip.sha256\",\"browser_download_url\":\"https://example.invalid/s\"}]},"
		    "{\"tag_name\":\"v1.5.1\",\"draft\":false,\"prerelease\":false,\"assets\":[]}]";
		std::vector<GithubRelease> releases;
		CHECK(ParseReleasesJson(json, releases).empty());
		CHECK(releases.size() == 2);
		CHECK(releases[0].tag == "v2026.9.2.0-beta" && releases[0].prerelease && !releases[0].draft);
		CHECK(releases[0].zipUrl == "https://example.invalid/z" && releases[0].shaUrl == "https://example.invalid/s");
		CHECK(releases[0].htmlUrl == "https://example.invalid/r");
		CHECK(releases[1].zipUrl.empty());
		CHECK(!ParseReleasesJson("{\"message\":\"rate limited\"}", releases).empty());
		CHECK(!ParseReleasesJson("not json", releases).empty());
	}

	void TestScript()
	{
		UpdateHelperParams p;
		p.overlayPid = 4242;
		p.zipUrl = "https://example.invalid/OpenVR-SpaceCalibrator-2026.9.2.0-beta.zip";
		p.shaUrl = p.zipUrl + ".sha256";
		p.zipName = "OpenVR-SpaceCalibrator-2026.9.2.0-beta.zip";
		p.stagingDir = "C:\\Users\\it's me\\AppData\\Local\\SpaceCalibrator\\update";
		p.installDir = "D:\\VR\\SpaceCal";
		p.logPath = "C:\\Users\\it's me\\AppData\\Local\\SpaceCalibrator\\update.log";
		std::string script = BuildUpdateHelperScript(p);
		CHECK(script.find("Wait-Process -Id 4242") != std::string::npos);
		CHECK(script.find("Get-Process vrserver") != std::string::npos);
		CHECK(script.find("Get-FileHash") != std::string::npos);
		CHECK(script.find("Expand-Archive") != std::string::npos);
		CHECK(script.find("'C:\\Users\\it''s me\\AppData\\Local\\SpaceCalibrator\\update'") != std::string::npos);
		CHECK(script.find("-Destination 'D:\\VR\\SpaceCal'") != std::string::npos);
		CHECK(script.find(p.zipUrl) != std::string::npos);
		CHECK(script.find("Wait-Process") < script.find("Copy-Item"));
		CHECK(script.find("Copy-Item") < script.find("Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction"));
		CHECK(script.find("\r\n") != std::string::npos);
		for (char c : script) {
			CHECK(static_cast<unsigned char>(c) < 128);
		}
	}

} // namespace

int main()
{
	TestParse();
	TestCompare();
	TestSelect();
	TestJson();
	TestScript();
	if (failures) {
		std::printf("%d failure(s)\n", failures);
		return 1;
	}
	std::printf("update tests passed\n");
	return 0;
}
