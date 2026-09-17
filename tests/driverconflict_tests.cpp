#include "DriverConflict.h"
#include "RegistrationPlan.h"
#include "UnregisterDriverScript.h"

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

	const char* kRealVrPath = R"({
	"config" : [ "C:\\Program Files (x86)\\Steam\\config" ],
	"external_drivers" :
	[
		"C:\\Program Files\\Virtual Desktop Streamer\\OpenVRDriver",
		"D:\\Github\\OpenVR\\OpenVR-SpaceCalibrator\\build\\01spacecalibrator"
	],
	"jsonid" : "vrpathreg",
	"log" : [ "C:\\Program Files (x86)\\Steam\\logs" ],
	"runtime" :
	[
		"C:\\Program Files (x86)\\Steam\\steamapps\\common\\SteamVR",
		"C:\\program files (x86)\\steam\\steamapps\\common\\SteamVR"
	],
	"version" : 1
})";

	const char* kOwn = R"(D:\Github\OpenVR\OpenVR-SpaceCalibrator\build\01spacecalibrator)";
	const char* kOwnOtherCase = R"(d:\github\openvr\openvr-spacecalibrator\BUILD\01SpaceCalibrator)";
	const char* kRival = R"(C:\Users\me\Downloads\SpaceCalibrator\01spacecalibrator)";
	const char* kUpstream = R"(C:\Old\000spacecalibrator)";
	const char* kRuntimeFolder = R"(C:\Program Files (x86)\Steam\steamapps\common\SteamVR\drivers\000spacecalibrator)";
	const char* kUnrelated = R"(C:\Program Files\Virtual Desktop Streamer\OpenVRDriver)";

	ConflictInputs Inputs(const char* own, std::vector<std::string> external, HandshakeOutcome handshake)
	{
		ConflictInputs in;
		in.ownDriverDir = own ? own : "";
		in.externalDrivers = std::move(external);
		in.handshake = handshake;
		return in;
	}

	void TestParse()
	{
		OpenVRPaths paths;
		CHECK(ParseOpenVRPaths(kRealVrPath, paths));
		CHECK(paths.runtimes.size() == 2);
		CHECK(paths.externalDrivers.size() == 2);
		CHECK(paths.externalDrivers[0] == kUnrelated);
		CHECK(paths.externalDrivers[1] == kOwn);
		CHECK(paths.runtimes[0] != paths.runtimes[1]);
		CHECK(NormalizePathKey(paths.runtimes[0]) == NormalizePathKey(paths.runtimes[1]));

		OpenVRPaths missing;
		CHECK(ParseOpenVRPaths(R"({"runtime":["a"]})", missing));
		CHECK(missing.runtimes.size() == 1);
		CHECK(missing.externalDrivers.empty());

		OpenVRPaths notArray;
		CHECK(ParseOpenVRPaths(R"({"external_drivers":"nope"})", notArray));
		CHECK(notArray.externalDrivers.empty());

		OpenVRPaths mixed;
		CHECK(ParseOpenVRPaths(R"({"external_drivers":["a",5,null,"b"]})", mixed));
		CHECK(mixed.externalDrivers.size() == 2);
		CHECK(mixed.externalDrivers[0] == "a" && mixed.externalDrivers[1] == "b");

		OpenVRPaths broken;
		CHECK(!ParseOpenVRPaths("not json at all", broken));
		CHECK(!ParseOpenVRPaths(R"([1,2,3])", broken));
		CHECK(!ParseOpenVRPaths("", broken));
	}

	void TestNormalize()
	{
		CHECK(NormalizePathKey(R"(C:/Foo/Bar/)") == R"(c:\foo\bar)");
		CHECK(NormalizePathKey(R"(C:\Foo\\Bar\)") == R"(c:\foo\bar)");
		CHECK(NormalizePathKey(R"(C:\FOO\bar)") == NormalizePathKey(R"(c:\foo\BAR)"));
		CHECK(NormalizePathKey("") == "");
		CHECK(PathLeaf(R"(C:\Old\000SpaceCalibrator\)") == "000spacecalibrator");
		CHECK(PathLeaf("01spacecalibrator") == "01spacecalibrator");
	}

	void TestLeaf()
	{
		CHECK(IsSpaceCalibratorDriverLeaf("01spacecalibrator"));
		CHECK(IsSpaceCalibratorDriverLeaf("000spacecalibrator"));
		CHECK(IsSpaceCalibratorDriverLeaf("driver_01spacecalibrator"));
		CHECK(IsSpaceCalibratorDriverLeaf("driver_000spacecalibrator"));
		CHECK(IsSpaceCalibratorDriverLeaf("01SpaceCalibrator"));
		CHECK(!IsSpaceCalibratorDriverLeaf("OpenVRDriver"));
		CHECK(!IsSpaceCalibratorDriverLeaf("01spacecalibrator_old"));
		CHECK(!IsSpaceCalibratorDriverLeaf(""));
	}

	void TestClassify()
	{
		{
			const ConflictReport r = ClassifyDriverConflict(Inputs(kOwn, {kUnrelated, kOwn}, HandshakeOutcome::Ok));
			CHECK(r.tier == ConflictTier::None);
			CHECK(r.rivals.empty());
			CHECK(r.ownRegistered);
			CHECK(!r.CanUnregister());
		}
		{
			const ConflictReport r = ClassifyDriverConflict(Inputs(kOwn, {kOwn, kRival}, HandshakeOutcome::Ok));
			CHECK(r.tier == ConflictTier::Warning);
			CHECK(r.rivals.size() == 1);
			CHECK(r.rivals[0].path == kRival);
			CHECK(r.rivals[0].kind == RivalKind::ExternalDriver);
			CHECK(r.ownRegistered);
			CHECK(r.CanUnregister());
		}
		{
			const ConflictReport r = ClassifyDriverConflict(Inputs(kOwn, {kOwn}, HandshakeOutcome::WrongGeneration));
			CHECK(r.tier == ConflictTier::Blocking);
			CHECK(r.ownDriverStale);
			CHECK(r.rivals.empty());
			CHECK(!r.CanUnregister());
		}
		{
			const ConflictReport r = ClassifyDriverConflict(Inputs(kOwn, {kOwn, kUpstream}, HandshakeOutcome::WrongGeneration));
			CHECK(r.tier == ConflictTier::Blocking);
			CHECK(!r.ownDriverStale);
			CHECK(r.rivals.size() == 1);
			CHECK(r.rivals[0].path == kUpstream);
			CHECK(r.CanUnregister());
		}
		{
			const ConflictReport r = ClassifyDriverConflict(Inputs(kOwn, {kRival}, HandshakeOutcome::WrongGeneration));
			CHECK(r.tier == ConflictTier::Blocking);
			CHECK(!r.ownRegistered);
			CHECK(r.rivals.size() == 1);
		}
		{
			const ConflictReport r = ClassifyDriverConflict(Inputs(nullptr, {kRival}, HandshakeOutcome::Ok));
			CHECK(r.tier == ConflictTier::None);
			CHECK(r.rivals.empty());
		}
		{
			const ConflictReport r = ClassifyDriverConflict(Inputs(nullptr, {kRival}, HandshakeOutcome::WrongGeneration));
			CHECK(r.tier == ConflictTier::Blocking);
			CHECK(r.ownDriverStale);
			CHECK(r.rivals.empty());
			CHECK(!r.CanUnregister());
		}
		{
			const ConflictReport r = ClassifyDriverConflict(Inputs(kOwn, {kOwn, kOwnOtherCase}, HandshakeOutcome::Ok));
			CHECK(r.tier == ConflictTier::None);
			CHECK(r.rivals.empty());
		}
		{
			const ConflictReport r = ClassifyDriverConflict(Inputs(kOwn, {kRival, kRival}, HandshakeOutcome::Ok));
			CHECK(r.rivals.size() == 1);
		}
		{
			const ConflictReport r = ClassifyDriverConflict(Inputs(kOwn, {kOwn}, HandshakeOutcome::Unavailable));
			CHECK(r.tier == ConflictTier::None);
		}
		{
			ConflictInputs in = Inputs(kOwn, {kOwn, kRival}, HandshakeOutcome::Unavailable);
			const ConflictReport r = ClassifyDriverConflict(in);
			CHECK(r.tier == ConflictTier::Warning);
			CHECK(r.rivals.size() == 1);
		}
		{
			ConflictInputs in = Inputs(kOwn, {kOwn}, HandshakeOutcome::Ok);
			in.runtimeDriverFolders.push_back(kRuntimeFolder);
			const ConflictReport r = ClassifyDriverConflict(in);
			CHECK(r.tier == ConflictTier::Warning);
			CHECK(r.rivals.size() == 1);
			CHECK(r.rivals[0].kind == RivalKind::RuntimeFolder);
			CHECK(!r.CanUnregister());
		}
	}

	void TestRegistrationPlan()
	{
		OpenVRPaths paths;
		CHECK(ParseOpenVRPaths(kRealVrPath, paths));

		{
			const RegistrationPlan plan = BuildRegistrationPlan(paths, kOwn);
			CHECK(plan.alreadyRegistered);
			CHECK(plan.removeDirs.empty());
			CHECK(plan.vrpathregCandidates.size() == 1);
			CHECK(plan.vrpathregCandidates[0] == R"(C:\Program Files (x86)\Steam\steamapps\common\SteamVR\bin\win64\vrpathreg.exe)");
		}
		{
			const RegistrationPlan plan = BuildRegistrationPlan(paths, kOwnOtherCase);
			CHECK(plan.alreadyRegistered);
			CHECK(plan.removeDirs.empty());
		}
		{
			const RegistrationPlan plan = BuildRegistrationPlan(paths, kRival);
			CHECK(!plan.alreadyRegistered);
			CHECK(plan.removeDirs.size() == 1);
			CHECK(plan.removeDirs[0] == kOwn);
		}
		{
			OpenVRPaths dupes;
			dupes.externalDrivers = {kRival, kRival, kUpstream, kUnrelated};
			dupes.runtimes = {R"(C:\SteamVR\)", R"(c:\steamvr)"};
			const RegistrationPlan plan = BuildRegistrationPlan(dupes, kOwn);
			CHECK(!plan.alreadyRegistered);
			CHECK(plan.removeDirs.size() == 2);
			CHECK(plan.removeDirs[0] == kRival);
			CHECK(plan.removeDirs[1] == kUpstream);
			CHECK(plan.vrpathregCandidates.size() == 1);
			CHECK(plan.vrpathregCandidates[0] == R"(C:\SteamVR\bin\win64\vrpathreg.exe)");
		}
		{
			const RegistrationPlan plan = BuildRegistrationPlan(OpenVRPaths{}, kOwn);
			CHECK(!plan.alreadyRegistered);
			CHECK(plan.removeDirs.empty());
			CHECK(plan.vrpathregCandidates.empty());
		}
		{
			OpenVRPaths noOwn;
			noOwn.externalDrivers = {kRival};
			const RegistrationPlan plan = BuildRegistrationPlan(noOwn, "");
			CHECK(!plan.alreadyRegistered);
			CHECK(plan.removeDirs.size() == 1);
		}
	}

	void TestOwnRegisteredDirs()
	{
		OpenVRPaths paths;
		CHECK(ParseOpenVRPaths(kRealVrPath, paths));
		{
			const std::vector<std::string> dirs = OwnRegisteredDirs(paths, kOwn);
			CHECK(dirs.size() == 1);
			CHECK(dirs[0] == kOwn);
		}
		{
			const std::vector<std::string> dirs = OwnRegisteredDirs(paths, kOwnOtherCase);
			CHECK(dirs.size() == 1);
			CHECK(dirs[0] == kOwn);
		}
		CHECK(OwnRegisteredDirs(paths, kRival).empty());
		CHECK(OwnRegisteredDirs(paths, "").empty());
	}

	void TestUnregisterScript()
	{
		UnregisterDriverParams params;
		params.vrpathregExe = R"(C:\Program Files (x86)\Steam\steamapps\common\SteamVR\bin\win64\vrpathreg.exe)";
		params.logPath = R"(C:\Users\me\AppData\Local\SpaceCalibrator\unregister-driver.log)";
		params.driverDirs = {kRival, R"(C:\Users\it's me\01spacecalibrator)"};

		const std::string script = BuildUnregisterDriverScript(params);

		CHECK(script.find("Wait-Process") == std::string::npos);

		const auto vrserver = script.find("Get-Process vrserver");
		const auto vrmonitor = script.find("Get-Process vrmonitor");
		const auto firstRemove = script.find("removedriver");
		CHECK(vrserver != std::string::npos);
		CHECK(vrmonitor != std::string::npos);
		CHECK(firstRemove != std::string::npos);
		CHECK(vrserver < vrmonitor);
		CHECK(vrmonitor < firstRemove);

		size_t removes = 0;
		for (size_t at = script.find("removedriver"); at != std::string::npos; at = script.find("removedriver", at + 1))
			++removes;
		size_t resets = 0;
		for (size_t at = script.find("$global:LASTEXITCODE = 0"); at != std::string::npos;
		     at = script.find("$global:LASTEXITCODE = 0", at + 1))
			++resets;
		CHECK(removes == params.driverDirs.size());
		CHECK(resets == params.driverDirs.size());

		CHECK(script.find(R"('C:\Users\it''s me\01spacecalibrator')") != std::string::npos);
		CHECK(script.find("$ErrorActionPreference = 'Stop'") == 0);

		UnregisterDriverParams empty;
		empty.vrpathregExe = params.vrpathregExe;
		empty.logPath = params.logPath;
		CHECK(BuildUnregisterDriverScript(empty).find("removedriver") == std::string::npos);
		CHECK(BuildUnregisterDriverScript(empty).find("adddriver") == std::string::npos);

		UnregisterDriverParams reg;
		reg.vrpathregExe = params.vrpathregExe;
		reg.logPath = params.logPath;
		reg.driverDirs = {kRival};
		reg.addDriverDir = kOwn;
		const std::string regScript = BuildUnregisterDriverScript(reg);
		const auto remove = regScript.find("removedriver");
		const auto add = regScript.find("adddriver");
		CHECK(remove != std::string::npos);
		CHECK(add != std::string::npos);
		CHECK(remove < add);
		CHECK(regScript.find("Get-Process vrserver") < remove);
		CHECK(regScript.find("adddriver exited with code") != std::string::npos);
		CHECK(regScript.find(std::string("adddriver '") + kOwn + "'") != std::string::npos);
	}

} // namespace

int main()
{
	TestParse();
	TestNormalize();
	TestLeaf();
	TestClassify();
	TestRegistrationPlan();
	TestOwnRegisteredDirs();
	TestUnregisterScript();

	if (failures == 0) {
		std::printf("driverconflict tests passed\n");
		return 0;
	}
	std::printf("%d driverconflict test failure(s)\n", failures);
	return 1;
}
