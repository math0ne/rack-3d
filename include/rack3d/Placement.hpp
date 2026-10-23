#pragma once
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

/** Self-test support: when true, warnNoRoom() only counts instead of opening a dialog. */
extern bool quietWarnings;
extern int noRoomWarnings;


} // namespace rack3d
} // namespace rack
