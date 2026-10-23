#pragma once
#include <jansson.h>

#include <common.hpp>


namespace rack {
namespace rack3d {
/** Rack 3D settings, saved under the "rack3d" key of settings.json.
Kept out of rack::settings so the plugin SDK's settings namespace doesn't change.
*/
namespace settings {


/** Constrain modules to rack placements (experimental). */
extern bool placementsEnabled;

json_t* toJson();
void fromJson(json_t* rootJ);


} // namespace settings
} // namespace rack3d
} // namespace rack
