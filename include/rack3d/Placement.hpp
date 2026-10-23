#pragma once
#include <string>

#include <common.hpp>


namespace rack {
namespace app {
struct ModuleWidget;
} // namespace app

namespace rack3d {


/** Whether placements are on and `mw` isn't entirely inside one placement. */
bool isStray(app::ModuleWidget* mw);

/** For a module that was just added: if it couldn't be placed in any rack, removes and deletes it (and its engine
Module) and tells the user. Returns whether it was rejected. Call before pushing any history for it.
*/
bool rejectIfStray(app::ModuleWidget* mw);

/** Tells the user there's no room in any rack. */
void warnNoRoom();

/** Tells the user that `count` modules of a loaded patch didn't fit in any rack. */
void warnOverflow(int count);

/** A placement's display name, e.g. "Rack 4 (top)". `index` is its position in Layout::placements. */
std::string getPlacementName(int index);

/** Moves a module into a placement (the free spot nearest its top-left corner) and pushes undo history.
Does nothing if it's already there. If there's no room, leaves the module where it was, tells the user and returns false.
*/
bool moveModuleToPlacement(app::ModuleWidget* mw, int placementId);

/** Self-test support: when true, warnNoRoom() only counts instead of opening a dialog. */
extern bool quietWarnings;
extern int noRoomWarnings;


} // namespace rack3d
} // namespace rack
