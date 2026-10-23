#include <osdialog.h>

#include <rack3d/Placement.hpp>
#include <rack3d/Layout.hpp>
#include <rack3d/settings.hpp>
#include <app/RackWidget.hpp>
#include <app/Scene.hpp>
#include <context.hpp>
#include <string.hpp>
#include <history.hpp>


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


std::string getPlacementName(int index) {
	static const char* const cubeFaceNames[] = {"front", "right", "back", "left", "top", "bottom"};
	Layout* layout = APP->scene->rack->getLayout();
	if (index < 0 || index >= (int) layout->placements.size())
		return "";
	std::string name = string::f("Rack %d", layout->placements[index].id);
	if (layout->placements.size() == 6)
		name += string::f(" (%s)", cubeFaceNames[index]);
	return name;
}


bool moveModuleToPlacement(app::ModuleWidget* mw, int placementId) {
	app::RackWidget* rack = APP->scene->rack;
	Layout* layout = rack->getLayout();
	Placement* p = layout->getPlacement(placementId);
	if (!p)
		return false;
	if (layout->placementOf(mw->getGridBox()) == p)
		return true;

	rack->updateModuleOldPositions();
	math::Vec oldPos = mw->getPosition();
	rack->setModulePosNearest(mw, Layout::getRackRect(*p).pos);
	if (layout->placementOf(mw->getGridBox()) != p) {
		// The nearest free spot was in another rack, or there was none
		mw->setPosition(oldPos);
		rack->updateExpanders();
		warnNoRoom();
		return false;
	}

	history::ComplexAction* h = rack->getModuleDragAction();
	h->name = "move module to rack";
	if (!h->isEmpty())
		APP->history->push(h);
	else
		delete h;
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
