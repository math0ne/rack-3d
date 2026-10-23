#include <algorithm>

#include <rack3d/settings.hpp>


namespace rack {
namespace rack3d {
namespace settings {


bool placementsEnabled = false;
RopeParams cableParams = defaultCableParams();
float spinDamping = DEFAULT_SPIN_DAMPING;
bool environment = true;
float fog = 1.f;
float cableGlow = 0.35f;
float contrast = 0.6f;
bool autoOrbit = false;
float orbitSpeed = 1.f;


RopeParams defaultCableParams() {
	RopeParams p;
	// Rack scale: a ~4 mm cable is ~12 rack px, and the cube's 760 px faces are 2 units
	p.radius = 0.016f;
	p.plugLen = 0.1f;
	return p;
}


void resetCableSettings() {
	cableParams = defaultCableParams();
	spinDamping = DEFAULT_SPIN_DAMPING;
}


json_t* toJson() {
	json_t* rootJ = json_object();
	json_object_set_new(rootJ, "placementsEnabled", json_boolean(placementsEnabled));

	const RopeParams& p = cableParams;
	json_t* cablesJ = json_object();
	json_object_set_new(cablesJ, "segments", json_integer(p.segments));
	json_object_set_new(cablesJ, "slack", json_real(p.slack));
	json_object_set_new(cablesJ, "stiffness", json_real(p.stiffness));
	json_object_set_new(cablesJ, "bend", json_real(p.bend));
	json_object_set_new(cablesJ, "iterations", json_integer(p.iterations));
	json_object_set_new(cablesJ, "damping", json_real(p.damping));
	json_object_set_new(cablesJ, "internalDamping", json_real(p.internalDamping));
	json_object_set_new(cablesJ, "gravity", json_real(p.gravity));
	json_object_set_new(cablesJ, "friction", json_real(p.friction));
	json_object_set_new(cablesJ, "bounce", json_real(p.bounce));
	json_object_set_new(cablesJ, "radius", json_real(p.radius));
	json_object_set_new(rootJ, "cables", cablesJ);
	json_object_set_new(rootJ, "spinDamping", json_real(spinDamping));
	json_object_set_new(rootJ, "environment", json_boolean(environment));
	json_object_set_new(rootJ, "fog", json_real(fog));
	json_object_set_new(rootJ, "cableGlow", json_real(cableGlow));
	json_object_set_new(rootJ, "contrast", json_real(contrast));
	json_object_set_new(rootJ, "autoOrbit", json_boolean(autoOrbit));
	json_object_set_new(rootJ, "orbitSpeed", json_real(orbitSpeed));
	return rootJ;
}


void fromJson(json_t* rootJ) {
	if (!rootJ)
		return;

	json_t* placementsEnabledJ = json_object_get(rootJ, "placementsEnabled");
	if (placementsEnabledJ)
		placementsEnabled = json_boolean_value(placementsEnabledJ);

	json_t* cablesJ = json_object_get(rootJ, "cables");
	if (cablesJ) {
		RopeParams& p = cableParams;
		auto getInt = [&](const char* key, int& value) {
			json_t* j = json_object_get(cablesJ, key);
			if (j)
				value = json_integer_value(j);
		};
		auto getFloat = [&](const char* key, float& value) {
			json_t* j = json_object_get(cablesJ, key);
			if (j)
				value = json_number_value(j);
		};
		getInt("segments", p.segments);
		getFloat("slack", p.slack);
		getFloat("stiffness", p.stiffness);
		getFloat("bend", p.bend);
		getInt("iterations", p.iterations);
		getFloat("damping", p.damping);
		getFloat("internalDamping", p.internalDamping);
		getFloat("gravity", p.gravity);
		getFloat("friction", p.friction);
		getFloat("bounce", p.bounce);
		getFloat("radius", p.radius);
		// Keep values the physics can't handle out
		p.segments = std::max(4, std::min(p.segments, 200));
		p.iterations = std::max(1, std::min(p.iterations, 100));
	}

	json_t* spinDampingJ = json_object_get(rootJ, "spinDamping");
	if (spinDampingJ)
		spinDamping = json_number_value(spinDampingJ);

	json_t* environmentJ = json_object_get(rootJ, "environment");
	if (environmentJ)
		environment = json_boolean_value(environmentJ);
	json_t* fogJ = json_object_get(rootJ, "fog");
	if (fogJ)
		fog = std::max(0.f, std::min((float) json_number_value(fogJ), 3.f));
	json_t* cableGlowJ = json_object_get(rootJ, "cableGlow");
	if (cableGlowJ)
		cableGlow = std::max(0.f, std::min((float) json_number_value(cableGlowJ), 1.f));
	json_t* contrastJ = json_object_get(rootJ, "contrast");
	if (contrastJ)
		contrast = std::max(0.f, std::min((float) json_number_value(contrastJ), 1.f));
	json_t* autoOrbitJ = json_object_get(rootJ, "autoOrbit");
	if (autoOrbitJ)
		autoOrbit = json_boolean_value(autoOrbitJ);
	json_t* orbitSpeedJ = json_object_get(rootJ, "orbitSpeed");
	if (orbitSpeedJ)
		orbitSpeed = std::max(0.1f, std::min((float) json_number_value(orbitSpeedJ), 4.f));
}


} // namespace settings
} // namespace rack3d
} // namespace rack
