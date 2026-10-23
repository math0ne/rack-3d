#pragma once
#include <common.hpp>


namespace rack {
namespace rack3d {


/** Runs Rack 3D's in-app tests against the live RackWidget, with real modules.
Destroys the current patch: run it with a throwaway user directory (see tests/rack3d/run-app.sh).
Returns the number of failed checks and logs each failure.
*/
int runAppSelfTest();


} // namespace rack3d
} // namespace rack
