#include <osdialog.h>

#include <rack3d/Placement.hpp>
#include <rack3d/Layout.hpp>
#include <rack3d/settings.hpp>
#include <app/RackWidget.hpp>
#include <app/Scene.hpp>
#include <context.hpp>


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


} // namespace rack3d
} // namespace rack
