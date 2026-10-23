#include <osdialog.h>

#include <rack3d/Placement.hpp>
#include <rack3d/Layout.hpp>
#include <rack3d/settings.hpp>
#include <app/RackWidget.hpp>
#include <app/Scene.hpp>
#include <context.hpp>
#include <string.hpp>


namespace rack {
namespace rack3d {


bool quietWarnings = false;
int noRoomWarnings = 0;


bool isStray(app::ModuleWidget* mw) {
	if (!settings::placementsEnabled)
		return false;
	return APP->scene->rack->getLayout()->placementOf(mw->getGridBox()) == NULL;
}


bool rejectIfStray(app::ModuleWidget* mw) {
	if (!isStray(mw))
		return false;
	INFO("No room in any rack for %s, removing it", mw->model->getFullName().c_str());
	APP->scene->rack->removeModule(mw);
	// Also removes and deletes its engine::Module
	delete mw;
	warnNoRoom();
	return true;
}


void warnNoRoom() {
	noRoomWarnings++;
	if (quietWarnings)
		return;
	osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, "There's no room in any rack for this. Make some space and try again.");
}


void warnOverflow(int count) {
	noRoomWarnings++;
	if (quietWarnings)
		return;
	std::string message = string::f("%d modules in this patch didn't fit in any rack. They're on a shelf below the racks, outside the 3D object.", count);
	osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, message.c_str());
}


} // namespace rack3d
} // namespace rack
