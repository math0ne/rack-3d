#include <rack3d/settings.hpp>


namespace rack {
namespace rack3d {
namespace settings {


bool placementsEnabled = false;


json_t* toJson() {
	json_t* rootJ = json_object();
	json_object_set_new(rootJ, "placementsEnabled", json_boolean(placementsEnabled));
	return rootJ;
}


void fromJson(json_t* rootJ) {
	if (!rootJ)
		return;

	json_t* placementsEnabledJ = json_object_get(rootJ, "placementsEnabled");
	if (placementsEnabledJ)
		placementsEnabled = json_boolean_value(placementsEnabledJ);
}


} // namespace settings
} // namespace rack3d
} // namespace rack
