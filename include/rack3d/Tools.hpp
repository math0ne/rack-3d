#pragma once
#include <common.hpp>


namespace rack {
namespace rack3d {


/** Developer tools, run instead of the window when RACK3D_TOOL is set (after the window exists, before patches load):

RACK3D_TOOL=dump RACK3D_TOOL_PLUGINS=Fundamental,AudibleInstruments RACK3D_TOOL_OUT=models.json
	Writes every module of those plugins as JSON: width (HP), params (id, name, range, default, display scaling,
	unit, switch labels), inputs and outputs (id, name), and default saved "data". For writing patch files by script.

RACK3D_TOOL=preview RACK3D_TOOL_PATCH=patch.vcv RACK3D_TOOL_OUT=preview
	Loads the patch, checks every module and cable came through, logs each module's output peak, and saves 3D-view
	screenshots from four angles (preview-1.png ... preview-4.png), zoomed out (preview-5.png) and close up
	(preview-6.png), and the 2D rack (preview-7.png). With RACK3D_TOOL_FOCUS=<model slug>, also the rack holding that
	module turned to face the camera, straight on and from the side (preview-focus-1.png, preview-focus-2.png).

RACK3D_TOOL=devices
	Logs every audio driver and its devices (id, name, channels; * marks the default), for naming a device in a patch.

Returns the process exit code: 0 on success.
*/
int runTool(const std::string& tool);


} // namespace rack3d
} // namespace rack
