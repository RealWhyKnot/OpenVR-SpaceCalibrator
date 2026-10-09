#include "StickProfile.h"

#include <cstdio>
#include <string>

namespace {

	int failures = 0;

#define CHECK(cond)                                                                                                                        \
	do {                                                                                                                                   \
		if (!(cond)) {                                                                                                                     \
			std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);                                                                     \
			++failures;                                                                                                                    \
		}                                                                                                                                  \
	} while (0)

	protocol::StickRampConfig Defaults()
	{
		return {0, 0, 100, 0};
	}

	protocol::StickRampConfig LoadJson(const char* json)
	{
		picojson::value value;
		const std::string err = picojson::parse(value, json);
		CHECK(err.empty());
		protocol::StickRampConfig stick = Defaults();
		spacecal::stick_profile::Load(stick, value);
		return stick;
	}

	bool Same(const protocol::StickRampConfig& a, uint16_t pushMs, uint16_t releaseMs, uint8_t strength)
	{
		return a.pushMs == pushMs && a.releaseMs == releaseMs && a.strength == strength;
	}

	void TestOldSliderNumberKeepsItsFeel()
	{
		CHECK(Same(LoadJson("60"), 2400, 900, 100));
		CHECK(Same(LoadJson("100"), 4000, 1500, 100));
		CHECK(Same(LoadJson("0"), 0, 0, 100));
		CHECK(Same(LoadJson("250"), 4000, 1500, 100));
		CHECK(Same(LoadJson("-5"), 0, 0, 100));
	}

	void TestRoundTrip()
	{
		const protocol::StickRampConfig saved{3000, 1250, 60, 0};
		const std::string text = spacecal::stick_profile::Save(saved).serialize();
		CHECK(text.find("\"timer_ms\":3000") != std::string::npos);
		CHECK(text.find("\"stop_ms\":1250") != std::string::npos);
		CHECK(text.find("\"strength\":60") != std::string::npos);
		CHECK(Same(LoadJson(text.c_str()), 3000, 1250, 60));
	}

	void TestMissingKeysKeepDefaults()
	{
		CHECK(Same(LoadJson("{\"timer_ms\":2000}"), 2000, 0, 100));
		CHECK(Same(LoadJson("{\"stop_ms\":700}"), 0, 700, 100));
		CHECK(Same(LoadJson("{}"), 0, 0, 100));
		CHECK(Same(LoadJson("\"fast\""), 0, 0, 100));
		CHECK(Same(LoadJson("null"), 0, 0, 100));
	}

	void TestOutOfRangeValuesAreClamped()
	{
		CHECK(Same(LoadJson("{\"timer_ms\":99999,\"stop_ms\":-5,\"strength\":140}"), 6000, 0, 100));
		CHECK(Same(LoadJson("{\"timer_ms\":1234.4,\"stop_ms\":6000,\"strength\":-3}"), 1234, 6000, 0));
	}

}

int main()
{
	TestOldSliderNumberKeepsItsFeel();
	TestRoundTrip();
	TestMissingKeysKeepDefaults();
	TestOutOfRangeValuesAreClamped();

	if (failures == 0) {
		std::printf("stick_profile_tests: all tests passed\n");
		return 0;
	}
	std::printf("stick_profile_tests: %d failure(s)\n", failures);
	return 1;
}
