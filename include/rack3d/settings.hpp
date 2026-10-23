#pragma once
#include <jansson.h>

#include <common.hpp>
#include <rack3d/CableSim.hpp>


namespace rack {
namespace rack3d {
/** Rack 3D settings, saved under the "rack3d" key of settings.json.
Kept out of rack::settings so the plugin SDK's settings namespace doesn't change.
*/
namespace settings {


/** Constrain modules to rack placements (experimental). */
extern bool placementsEnabled;
/** 3D cable physics, tunable from View > Rack 3D > Cable physics. */
extern RopeParams cableParams;
/** The 3D view's spin decays by e^(-spinDamping * seconds) after you let go. */
extern float spinDamping;
/** Show the concrete plaza, sky, sun shadows and fog around the model in the 3D view. */
extern bool environment;
/** Fog and light shaft density in the 3D view: 1 is the default haze, 0 clear air. */
extern float fog;
/** How much the 3D cables glow: 0 is plain cables. */
extern float cableGlow;
/** Overall brightness of the 3D view: 1 is the renderer's full light. */
extern float brightness;
/** The 3D view slowly turns and tips the model by itself, pausing while you use it (F8). */
extern bool autoOrbit;
/** Auto-orbit speed: 1 is one turn every ORBIT_TURN_SECONDS. */
extern float orbitSpeed;
static const float ORBIT_TURN_SECONDS = 90.f;

/** Cable physics defaults: the cable sandbox's tuning, at Rack scale (a ~4 mm cable on a 2-unit cube). */
RopeParams defaultCableParams();
static const float DEFAULT_SPIN_DAMPING = 3.f;
/** Puts the cable physics and spin damping back to their defaults. */
void resetCableSettings();

json_t* toJson();
void fromJson(json_t* rootJ);


} // namespace settings
} // namespace rack3d
} // namespace rack
