#pragma once
#include <ui/Menu.hpp>


namespace rack {
namespace rack3d {


/** Adds the Rack 3D section to the View menu: the placements toggle and the "Cable physics" submenu of sliders
(the cable sandbox's controls), all saved in settings.json.
*/
void appendViewMenu(ui::Menu* menu);


} // namespace rack3d
} // namespace rack
