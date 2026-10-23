// Runs the rack3d tests against libRack. Build and run with tests/rack3d/run.sh.
#include <cstdio>

#include <logger.hpp>
#include <rack3d/Layout.hpp>
#include <rack3d/settings.hpp>
#include <settings.hpp>


static int settingsTest() {
	int failures = 0;
	namespace s = rack::rack3d::settings;
	for (bool value : {true, false}) {
		s::placementsEnabled = value;
		json_t* j = s::toJson();
		s::placementsEnabled = !value;
		s::fromJson(j);
		json_decref(j);
		if (s::placementsEnabled != value) {
			WARN("rack3d settings round-trip failed for placementsEnabled = %d", value);
			failures++;
		}
	}
	// Through Rack's own settings.json serialization
	s::placementsEnabled = true;
	json_t* rootJ = rack::settings::toJson();
	json_t* placementsJ = json_object_get(json_object_get(rootJ, "rack3d"), "placementsEnabled");
	if (!json_is_true(placementsJ)) {
		WARN("rack3d settings: settings.json has no rack3d.placementsEnabled = true");
		failures++;
	}
	s::placementsEnabled = false;
	rack::settings::fromJson(rootJ);
	json_decref(rootJ);
	if (!s::placementsEnabled) {
		WARN("rack3d settings: settings.json round-trip lost placementsEnabled");
		failures++;
	}

	// A settings.json without a "rack3d" key leaves the defaults alone
	s::placementsEnabled = false;
	s::fromJson(NULL);
	if (s::placementsEnabled) {
		WARN("rack3d settings: missing key changed placementsEnabled");
		failures++;
	}
	return failures;
}


int main() {
	// logger::logPath is empty, so the logger writes to stderr
	rack::logger::init();
	int failures = 0;
	failures += rack::rack3d::layoutSelfTest();
	failures += settingsTest();
	std::printf("rack3d tests: %d failure(s)\n", failures);
	rack::logger::destroy();
	return failures == 0 ? 0 : 1;
}
