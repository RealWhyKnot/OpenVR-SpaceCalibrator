#include "StickProfile.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

	using namespace spacecal::stick_settings;

	int failures = 0;

#define CHECK(cond)                                                                                                                        \
	do {                                                                                                                                   \
		if (!(cond)) {                                                                                                                     \
			std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);                                                                     \
			++failures;                                                                                                                    \
		}                                                                                                                                  \
	} while (0)

	struct Loaded
	{
		protocol::StickRampConfig stick;
		bool on;
		bool locked;
	};

	Loaded LoadOver(const char* json, protocol::StickRampConfig stick, bool on, bool locked = false)
	{
		picojson::value value;
		const std::string err = picojson::parse(value, json);
		CHECK(err.empty());
		spacecal::stick_profile::Load(stick, on, locked, value);
		return {stick, on, locked};
	}

	Loaded LoadJson(const char* json)
	{
		return LoadOver(json, DefaultStick(), false);
	}

	bool Same(const protocol::StickRampConfig& a, uint16_t pushMs, uint16_t releaseMs, uint8_t strength)
	{
		return a.pushMs == pushMs && a.releaseMs == releaseMs && a.strength == strength;
	}

	bool IsDefault(const protocol::StickRampConfig& a)
	{
		return Same(a, 4000, 1500, 75) && a.delayMs == 0;
	}

	void TestOldSliderNumberKeepsItsFeel()
	{
		const Loaded sixty = LoadJson("60");
		CHECK(Same(sixty.stick, 2400, 900, 75));
		CHECK(sixty.on);
		const Loaded full = LoadJson("100");
		CHECK(Same(full.stick, 4000, 1500, 75));
		CHECK(full.on);
		const Loaded capped = LoadJson("250");
		CHECK(Same(capped.stick, 4000, 1500, 75));
		CHECK(capped.on);
	}

	void TestOldSliderAtZeroLoadsOffWithDefaults()
	{
		for (const char* json : {"0", "-5"}) {
			const Loaded off = LoadJson(json);
			CHECK(!off.on);
			CHECK(IsDefault(off.stick));
		}
	}

	void TestOldSliderNumberClearsTheDelay()
	{
		const Loaded loaded = LoadOver("60", {0, 0, 100, 0, 500}, false);
		CHECK(Same(loaded.stick, 2400, 900, 75));
		CHECK(loaded.stick.delayMs == 0);
	}

	void TestRoundTrip()
	{
		const protocol::StickRampConfig saved{3000, 1250, 60, 0, 350};
		for (bool on : {true, false}) {
			const std::string text = spacecal::stick_profile::Save(saved, on, false).serialize();
			CHECK(text.find(on ? "\"on\":true" : "\"on\":false") != std::string::npos);
			CHECK(text.find("\"timer_ms\":3000") != std::string::npos);
			CHECK(text.find("\"stop_ms\":1250") != std::string::npos);
			CHECK(text.find("\"strength\":60") != std::string::npos);
			CHECK(text.find("\"start_pct\":40") != std::string::npos);
			CHECK(text.find("\"delay_ms\":350") != std::string::npos);
			const Loaded loaded = LoadOver(text.c_str(), DefaultStick(), !on);
			CHECK(Same(loaded.stick, 3000, 1250, 60));
			CHECK(loaded.stick.delayMs == 350);
			CHECK(loaded.on == on);
		}
	}

	void TestProfilesWithoutOnKeepTheirFeel()
	{
		const Loaded timed = LoadJson("{\"timer_ms\":2000,\"strength\":60,\"delay_ms\":300,\"stop_ms\":0}");
		CHECK(timed.on);
		CHECK(Same(timed.stick, 2000, 0, 60));
		CHECK(timed.stick.delayMs == 300);
		const Loaded glideOnly = LoadJson("{\"timer_ms\":0,\"strength\":100,\"delay_ms\":0,\"stop_ms\":700}");
		CHECK(glideOnly.on);
		CHECK(Same(glideOnly.stick, 0, 700, 75));
		const Loaded off = LoadJson("{\"timer_ms\":0,\"strength\":100,\"delay_ms\":0,\"stop_ms\":0}");
		CHECK(!off.on);
		CHECK(IsDefault(off.stick));
	}

	void TestOnKeyWins()
	{
		const Loaded off = LoadJson("{\"on\":false,\"timer_ms\":3000,\"strength\":80,\"stop_ms\":1000}");
		CHECK(!off.on);
		CHECK(Same(off.stick, 3000, 1000, 80));
		const Loaded emptyOn = LoadJson("{\"on\":true,\"timer_ms\":0,\"stop_ms\":0}");
		CHECK(!emptyOn.on);
		CHECK(IsDefault(emptyOn.stick));
	}

	void TestDelayIsClampedAndOptional()
	{
		CHECK(LoadJson("{\"delay_ms\":99999}").stick.delayMs == 2000);
		CHECK(LoadJson("{\"delay_ms\":-40}").stick.delayMs == 0);
		CHECK(LoadJson("{\"timer_ms\":2000}").stick.delayMs == 0);
	}

	void TestMissingKeysKeepCurrentValues()
	{
		const protocol::StickRampConfig zero{0, 0, 100, 0};
		const Loaded timerOnly = LoadOver("{\"timer_ms\":2000}", zero, false);
		CHECK(Same(timerOnly.stick, 2000, 0, 75));
		CHECK(timerOnly.on);
		const Loaded stopOnly = LoadOver("{\"stop_ms\":700}", zero, false);
		CHECK(Same(stopOnly.stick, 0, 700, 75));
		CHECK(stopOnly.on);
		const Loaded empty = LoadJson("{}");
		CHECK(IsDefault(empty.stick));
		CHECK(!empty.on);
		for (const char* json : {"\"fast\"", "null"}) {
			const Loaded untouched = LoadOver(json, {2000, 0, 60, 0}, true);
			CHECK(Same(untouched.stick, 2000, 0, 60));
			CHECK(untouched.on);
			const Loaded fresh = LoadJson(json);
			CHECK(IsDefault(fresh.stick));
			CHECK(!fresh.on);
		}
	}

	void TestOutOfRangeValuesAreClamped()
	{
		CHECK(Same(LoadJson("{\"timer_ms\":99999,\"stop_ms\":-5,\"strength\":140}").stick, 8000, 0, 75));
		CHECK(Same(LoadJson("{\"timer_ms\":1234.4,\"stop_ms\":6000,\"strength\":-3}").stick, 1234, 6000, 0));
		CHECK(Same(LoadJson("{\"timer_ms\":8000,\"stop_ms\":99999}").stick, 8000, 6000, 75));
	}

	void TestStrengthMapsToBuildUpTime()
	{
		CHECK(PushMsForStrength(50) == 4000);
		CHECK(PushMsForStrength(100) == 8000);
		CHECK(PushMsForStrength(5) == 400);
		CHECK(PushMsForStrength(0) == 0);
		CHECK(PushMsForStrength(150) == 8000);
		CHECK(PushMsForStrength(-10) == 0);
		CHECK(StrengthForPushMs(4000) == 50);
		CHECK(StrengthForPushMs(8000) == 100);
		CHECK(StrengthForPushMs(65535) == 100);
		CHECK(StrengthForPushMs(1234) == 15);
		CHECK(StrengthForPushMs(0) == 0);
		for (int percent = 0; percent <= 100; percent += kPercentStep) {
			CHECK(StrengthForPushMs(PushMsForStrength(percent)) == percent);
		}
	}

	void TestSnapPercent()
	{
		CHECK(SnapPercent(52) == 50);
		CHECK(SnapPercent(53) == 55);
		CHECK(SnapPercent(2) == 0);
		CHECK(SnapPercent(3) == 5);
		CHECK(SnapPercent(101) == 100);
		CHECK(SnapPercent(-4) == 0);
	}

	void TestDefaultBuildsUpLikeTheOldFullSlider()
	{
		const protocol::StickRampConfig stick = DefaultStick();
		CHECK(StrengthForPushMs(stick.pushMs) == kDefaultStrengthPercent);
		CHECK(stick.pushMs == spacecal::stick::LegacyPushMs(100));
		CHECK(stick.releaseMs == spacecal::stick::LegacyReleaseMs(100));
		CHECK(stick.strength == 100 - kDefaultStartPercent);
		CHECK(stick.delayMs == 0);
	}

	void TestStartSpeedMovesOffZeroOnce()
	{
		CHECK(LoadJson("{\"timer_ms\":4000,\"strength\":100,\"stop_ms\":1500}").stick.strength == 100 - kDefaultStartPercent);
		CHECK(LoadJson("{\"timer_ms\":4000,\"strength\":100,\"start_pct\":0,\"stop_ms\":1500}").stick.strength == 100);
		CHECK(LoadJson("{\"timer_ms\":4000,\"strength\":60,\"stop_ms\":1500}").stick.strength == 60);
		CHECK(LoadJson("{\"timer_ms\":4000,\"strength\":60,\"start_pct\":10}").stick.strength == 90);
		CHECK(LoadJson("{\"timer_ms\":4000,\"start_pct\":150}").stick.strength == 0);
		CHECK(LoadJson("{\"timer_ms\":4000,\"start_pct\":-5}").stick.strength == 100);
		const protocol::StickRampConfig chosenZero{4000, 1500, 100, 0, 0};
		const std::string text = spacecal::stick_profile::Save(chosenZero, true, false).serialize();
		CHECK(LoadOver(text.c_str(), DefaultStick(), false).stick.strength == 100);
	}

	void TestTurningOnFillsDefaultsOnlyWhenEmpty()
	{
		protocol::StickRampConfig empty{0, 0, 40, 0, 900};
		PrepareToTurnOn(empty, false);
		CHECK(IsDefault(empty));
		protocol::StickRampConfig set{2000, 0, 60, 0, 300};
		PrepareToTurnOn(set, false);
		CHECK(Same(set, 2000, 0, 60));
		CHECK(set.delayMs == 300);
		protocol::StickRampConfig lockedNoStop{0, 0, 40, 70, 900};
		PrepareToTurnOn(lockedNoStop, true);
		CHECK(Same(lockedNoStop, 0, 0, 40));
		CHECK(lockedNoStop.lockedPct == 70);
		CHECK(lockedNoStop.delayMs == 900);
	}

	void TestOffSticksReachTheDriverAsOff()
	{
		protocol::StickSmoothingConfig config{};
		config.sticks[0] = {3000, 1200, 60, 0, 300};
		config.sticks[1] = {3000, 1200, 60, 0, 300};
		const bool leftOnly[2] = {true, false};
		const bool neither[2] = {false, false};
		const protocol::StickSmoothingConfig sent = ForDriver(config, leftOnly, neither);
		CHECK(std::memcmp(&sent.sticks[0], &config.sticks[0], sizeof config.sticks[0]) == 0);
		CHECK(sent.sticks[1].pushMs == 0 && sent.sticks[1].releaseMs == 0);
		const protocol::StickRampConfig& right = sent.sticks[1];
		CHECK(spacecal::stick::IsOff(spacecal::stick::RampFromSettings(right.pushMs, right.releaseMs, right.strength, right.delayMs)));
		CHECK(config.sticks[1].pushMs == 3000);
		const bool both[2] = {true, true};
		const protocol::StickSmoothingConfig all = ForDriver(config, both, neither);
		CHECK(std::memcmp(&all, &config, sizeof config) == 0);
	}

	void TestLockedRoundTrip()
	{
		const protocol::StickRampConfig saved{3000, 1250, 60, 70, 350};
		for (bool locked : {true, false}) {
			const std::string text = spacecal::stick_profile::Save(saved, true, locked).serialize();
			CHECK(text.find(locked ? "\"locked\":true" : "\"locked\":false") != std::string::npos);
			CHECK(text.find("\"locked_pct\":70") != std::string::npos);
			const Loaded loaded = LoadOver(text.c_str(), DefaultStick(), false, !locked);
			CHECK(loaded.locked == locked);
			CHECK(loaded.on);
			CHECK(loaded.stick.lockedPct == 70);
			CHECK(Same(loaded.stick, 3000, 1250, 60));
			CHECK(loaded.stick.delayMs == 350);
		}
	}

	void TestOlderProfilesLoadAsBuildUp()
	{
		const Loaded timed = LoadOver("{\"on\":true,\"timer_ms\":3000,\"stop_ms\":1000}", DefaultStick(), false, true);
		CHECK(timed.on);
		CHECK(!timed.locked);
		CHECK(timed.stick.lockedPct == kDefaultLockedPercent);
		const Loaded legacy = LoadOver("60", DefaultStick(), false, true);
		CHECK(!legacy.locked);
		CHECK(legacy.stick.lockedPct == kDefaultLockedPercent);
		const Loaded fromZero = LoadOver("{\"timer_ms\":3000}", {0, 0, 100, 0, 0}, false);
		CHECK(fromZero.stick.lockedPct == kDefaultLockedPercent);
		const Loaded emptyBuildUp = LoadJson("{\"on\":true,\"locked\":false,\"timer_ms\":0,\"stop_ms\":0}");
		CHECK(!emptyBuildUp.on);
		CHECK(IsDefault(emptyBuildUp.stick));
	}

	void TestLockedWithoutStopSurvivesALoad()
	{
		const protocol::StickRampConfig saved{0, 0, 60, 70, 350};
		const std::string text = spacecal::stick_profile::Save(saved, true, true).serialize();
		const Loaded loaded = LoadOver(text.c_str(), DefaultStick(), false);
		CHECK(loaded.on);
		CHECK(loaded.locked);
		CHECK(Same(loaded.stick, 0, 0, 60));
		CHECK(loaded.stick.lockedPct == 70);
		CHECK(loaded.stick.delayMs == 350);
		const Loaded noPct = LoadJson("{\"on\":true,\"locked\":true,\"timer_ms\":0,\"stop_ms\":0}");
		CHECK(noPct.on);
		CHECK(noPct.locked);
		CHECK(noPct.stick.lockedPct == kDefaultLockedPercent);
		CHECK(noPct.stick.pushMs == 0 && noPct.stick.releaseMs == 0);
	}

	void TestLockedSpeedIsClamped()
	{
		CHECK(LoadJson("{\"timer_ms\":3000,\"locked_pct\":10}").stick.lockedPct == kMinLockedPercent);
		CHECK(LoadJson("{\"timer_ms\":3000,\"locked_pct\":-5}").stick.lockedPct == kMinLockedPercent);
		CHECK(LoadJson("{\"timer_ms\":3000,\"locked_pct\":140}").stick.lockedPct == 100);
		CHECK(LoadJson("{\"timer_ms\":3000,\"locked_pct\":65}").stick.lockedPct == 65);
		CHECK(LoadJson("{\"timer_ms\":3000,\"locked_pct\":\"fast\"}").stick.lockedPct == kDefaultLockedPercent);
		CHECK(ClampLockedPercent(0) == kMinLockedPercent);
		CHECK(ClampLockedPercent(255) == 100);
		CHECK(ClampLockedPercent(55) == 55);
	}

	void TestDefaultLockedSpeed()
	{
		CHECK(DefaultStick().lockedPct == kDefaultLockedPercent);
		protocol::StickRampConfig empty{0, 0, 40, 90, 900};
		PrepareToTurnOn(empty, false);
		CHECK(empty.lockedPct == kDefaultLockedPercent);
		protocol::StickRampConfig set{2000, 0, 60, 80, 300};
		PrepareToTurnOn(set, false);
		CHECK(set.lockedPct == 80);
	}

	void TestLockedSpeedReachesTheDriverOnlyWhenOnAndLocked()
	{
		protocol::StickSmoothingConfig config{};
		config.sticks[0] = {3000, 1200, 60, 70, 300};
		config.sticks[1] = {3000, 1200, 60, 70, 300};
		const bool both[2] = {true, true};
		const bool leftLocked[2] = {true, false};
		const protocol::StickSmoothingConfig mixed = ForDriver(config, both, leftLocked);
		CHECK(mixed.sticks[0].lockedPct == 70);
		CHECK(mixed.sticks[1].lockedPct == 0);
		CHECK(mixed.sticks[0].pushMs == 3000 && mixed.sticks[0].releaseMs == 1200);
		const protocol::StickRampConfig& l = mixed.sticks[0];
		const spacecal::stick::Ramp lockedRamp =
		    spacecal::stick::RampFromSettings(l.pushMs, l.releaseMs, l.strength, l.delayMs, l.lockedPct);
		CHECK(std::fabs(lockedRamp.scale - 0.7) < 1e-12);
		CHECK(!spacecal::stick::PushIsInstant(lockedRamp));
		CHECK(lockedRamp.pushSeconds == 3.0 && std::fabs(lockedRamp.delaySeconds - 0.3) < 1e-12);

		const bool rightOn[2] = {false, true};
		const bool bothLocked[2] = {true, true};
		const protocol::StickSmoothingConfig offLeft = ForDriver(config, rightOn, bothLocked);
		const protocol::StickRampConfig& off = offLeft.sticks[0];
		CHECK(off.pushMs == 0 && off.releaseMs == 0 && off.lockedPct == 0);
		const spacecal::stick::Ramp offRamp =
		    spacecal::stick::RampFromSettings(off.pushMs, off.releaseMs, off.strength, off.delayMs, off.lockedPct);
		CHECK(spacecal::stick::IsOff(offRamp) && offRamp.scale == 1.0);
		CHECK(offLeft.sticks[1].lockedPct == 70);
		CHECK(config.sticks[0].lockedPct == 70);

		config.sticks[1].lockedPct = 5;
		CHECK(ForDriver(config, both, bothLocked).sticks[1].lockedPct == kMinLockedPercent);
	}

}

int main()
{
	TestOldSliderNumberKeepsItsFeel();
	TestOldSliderAtZeroLoadsOffWithDefaults();
	TestOldSliderNumberClearsTheDelay();
	TestRoundTrip();
	TestProfilesWithoutOnKeepTheirFeel();
	TestOnKeyWins();
	TestDelayIsClampedAndOptional();
	TestMissingKeysKeepCurrentValues();
	TestOutOfRangeValuesAreClamped();
	TestStrengthMapsToBuildUpTime();
	TestSnapPercent();
	TestDefaultBuildsUpLikeTheOldFullSlider();
	TestStartSpeedMovesOffZeroOnce();
	TestTurningOnFillsDefaultsOnlyWhenEmpty();
	TestOffSticksReachTheDriverAsOff();
	TestLockedRoundTrip();
	TestOlderProfilesLoadAsBuildUp();
	TestLockedWithoutStopSurvivesALoad();
	TestLockedSpeedIsClamped();
	TestDefaultLockedSpeed();
	TestLockedSpeedReachesTheDriverOnlyWhenOnAndLocked();

	if (failures == 0) {
		std::printf("stick_profile_tests: all tests passed\n");
		return 0;
	}
	std::printf("stick_profile_tests: %d failure(s)\n", failures);
	return 1;
}
