// Runs the rack3d tests against libRack. Build and run with tests/rack3d/run.sh.
#include <cstdio>
#include <cmath>

#include <logger.hpp>
#include <rack3d/Layout.hpp>
#include <rack3d/settings.hpp>
#include <render3d/math.hpp>
#include <rack3d/CableSim.hpp>
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

	// Cable physics settings round-trip, and reset
	s::cableParams.segments = 17;
	s::cableParams.bend = 0.8f;
	s::cableParams.radius = 0.03f;
	s::spinDamping = 1.5f;
	json_t* cablesJ = s::toJson();
	s::resetCableSettings();
	if (s::cableParams.segments != s::defaultCableParams().segments || s::spinDamping != s::DEFAULT_SPIN_DAMPING) {
		WARN("rack3d settings: resetCableSettings didn't restore defaults");
		failures++;
	}
	s::fromJson(cablesJ);
	json_decref(cablesJ);
	if (s::cableParams.segments != 17 || std::fabs(s::cableParams.bend - 0.8f) > 1e-6f || std::fabs(s::cableParams.radius - 0.03f) > 1e-6f || std::fabs(s::spinDamping - 1.5f) > 1e-6f) {
		WARN("rack3d settings: cable physics settings round-trip failed");
		failures++;
	}
	s::resetCableSettings();

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
	failures += rack::render3d::mathSelfTest();
	failures += rack::rack3d::cableSimSelfTest();
	failures += settingsTest();
	std::printf("rack3d tests: %d failure(s)\n", failures);
	rack::logger::destroy();
	return failures == 0 ? 0 : 1;
}
