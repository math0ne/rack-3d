#include <rack3d/AppSelfTest.hpp>
#include <rack3d/Layout.hpp>
#include <rack3d/settings.hpp>
#include <rack3d/Placement.hpp>
#include <app/RackWidget.hpp>
#include <system.hpp>
#include <app/Scene.hpp>
#include <engine/Engine.hpp>
#include <plugin.hpp>
#include <patch.hpp>
#include <context.hpp>


namespace rack {
namespace rack3d {


using app::RACK_GRID_SIZE;
using app::RACK_OFFSET;


/** Rack px of a grid cell. */
static math::Vec gridPx(float x, float y) {
	return math::Vec(x, y) * RACK_GRID_SIZE + RACK_OFFSET;
}


static math::Vec gridPos(app::ModuleWidget* mw) {
	return mw->getGridPosition();
}


static void resetRack() {
	APP->scene->rack->clear();
	APP->engine->clear();
}


/** Adds a Core Blank of `hp` HP at a grid cell, bypassing placement rules (test setup). */
static app::ModuleWidget* addBlank(int hp, float x, float y) {
	plugin::Model* model = plugin::getModel("Core", "Blank");
	assert(model);
	engine::Module* module = model->createModule();
	json_t* dataJ = json_object();
	json_object_set_new(dataJ, "width", json_integer(hp));
	module->dataFromJson(dataJ);
	json_decref(dataJ);
	APP->engine->addModule(module);
	app::ModuleWidget* mw = model->createModuleWidget(module);
	mw->box.pos = gridPx(x, y);
	APP->scene->rack->addModule(mw);
	return mw;
}


static bool isInPlacement(app::ModuleWidget* mw) {
	return APP->scene->rack->getLayout()->placementOf(mw->getGridBox()) != NULL;
}


/** Fills every row of the cube with 4 HP Blanks, leaving no room anywhere. */
static void fillLayout() {
	for (int y = 0; y < 23; y++) {
		if (y % 4 == 3)
			continue;
		for (int x = 0; x + 4 <= 76; x += 4)
			addBlank(4, x, y);
	}
}


/** JSON for a Core Blank module, as copied to the clipboard. */
static json_t* blankJson(int hp, float x, float y) {
	json_t* moduleJ = json_object();
	json_object_set_new(moduleJ, "id", json_integer(1000 + (int64_t) x));
	json_object_set_new(moduleJ, "plugin", json_string("Core"));
	json_object_set_new(moduleJ, "model", json_string("Blank"));
	json_object_set_new(moduleJ, "version", json_string(APP_VERSION.c_str()));
	json_t* dataJ = json_object();
	json_object_set_new(dataJ, "width", json_integer(hp));
	json_object_set_new(moduleJ, "data", dataJ);
	json_object_set_new(moduleJ, "pos", json_pack("[f, f]", x, y));
	return moduleJ;
}


/** JSON for a Core module with id and grid position. */
static json_t* coreModuleJson(int64_t id, const char* model, float x, float y) {
	json_t* moduleJ = json_object();
	json_object_set_new(moduleJ, "id", json_integer(id));
	json_object_set_new(moduleJ, "plugin", json_string("Core"));
	json_object_set_new(moduleJ, "model", json_string(model));
	json_object_set_new(moduleJ, "version", json_string(APP_VERSION.c_str()));
	json_object_set_new(moduleJ, "pos", json_pack("[f, f]", x, y));
	return moduleJ;
}


/** A stock patch (no "rack3d" key) too wide for one rack: a run of two touching Blanks (an expander pair), a wide
Blank, and a MIDI-CV -> Audio cable across rows.
*/
static json_t* stockPatchJson() {
	json_t* rootJ = json_object();
	json_object_set_new(rootJ, "version", json_string(APP_VERSION.c_str()));
	json_t* modulesJ = json_array();
	json_t* a = coreModuleJson(1, "Blank", 0, 0);
	json_object_set_new(a, "data", json_pack("{s:i}", "width", 10));
	json_array_append_new(modulesJ, a);
	json_t* b = coreModuleJson(2, "Blank", 10, 0);
	json_object_set_new(b, "data", json_pack("{s:i}", "width", 10));
	json_array_append_new(modulesJ, b);
	json_t* c = coreModuleJson(3, "Blank", 40, 0);
	json_object_set_new(c, "data", json_pack("{s:i}", "width", 60));
	json_array_append_new(modulesJ, c);
	json_array_append_new(modulesJ, coreModuleJson(4, "MIDIToCVInterface", 0, 1));
	json_array_append_new(modulesJ, coreModuleJson(5, "AudioInterface2", 120, 1));
	json_object_set_new(rootJ, "modules", modulesJ);
	json_t* cablesJ = json_array();
	json_array_append_new(cablesJ, json_pack("{s:i, s:i, s:i, s:i, s:i}", "id", 1, "outputModuleId", 4, "outputId", 0, "inputModuleId", 5, "inputId", 0));
	json_object_set_new(rootJ, "cables", cablesJ);
	return rootJ;
}


static std::map<int64_t, math::Vec> modulePositions() {
	std::map<int64_t, math::Vec> positions;
	for (app::ModuleWidget* mw : APP->scene->rack->getModules())
		positions[mw->module->id] = gridPos(mw);
	return positions;
}


/** JSON for a 2-module selection, as copied to the clipboard. */
static json_t* selectionJson() {
	json_t* rootJ = json_object();
	json_t* modulesJ = json_array();
	json_array_append_new(modulesJ, blankJson(10, 0, 0));
	json_array_append_new(modulesJ, blankJson(10, 10, 0));
	json_object_set_new(rootJ, "modules", modulesJ);
	json_object_set_new(rootJ, "cables", json_array());
	return rootJ;
}


static bool allInPlacements() {
	for (app::ModuleWidget* mw : APP->scene->rack->getModules()) {
		if (!isInPlacement(mw))
			return false;
	}
	return true;
}


static bool noOverlaps() {
	std::vector<app::ModuleWidget*> mws = APP->scene->rack->getModules();
	for (size_t i = 0; i < mws.size(); i++) {
		for (size_t j = i + 1; j < mws.size(); j++) {
			if (mws[i]->box.intersects(mws[j]->box))
				return false;
		}
	}
	return true;
}


int runAppSelfTest() {
	int failures = 0;
	auto check = [&](bool ok, const char* what) {
		if (!ok) {
			WARN("rack3d app self-test failed: %s", what);
			failures++;
		}
		else {
			INFO("rack3d app self-test ok: %s", what);
		}
	};

	app::RackWidget* rack = APP->scene->rack;
	bool oldEnabled = settings::placementsEnabled;
	// The layout is a cube: 6 blocks of 3 rows x 76 HP at grid rows 0-2, 4-6, ..., 20-22, x 0..75.

	// Placements off: stock behaviour
	settings::placementsEnabled = false;
	resetRack();
	{
		app::ModuleWidget* a = addBlank(10, 0, 0);
		check(rack->requestModulePos(a, gridPx(10, 3)), "off: a gutter row is allowed");
		check(rack->requestModulePos(a, gridPx(70, 0)), "off: past HP 76 is allowed");
		rack->setModulePosNearest(a, gridPx(500, 50));
		check(gridPos(a).equals(math::Vec(500, 50)), "off: nearest goes anywhere");
	}

	// Placements on: single module
	settings::placementsEnabled = true;
	resetRack();
	{
		app::ModuleWidget* a = addBlank(10, 0, 0);
		check(!rack->requestModulePos(a, gridPx(10, 3)), "on: a gutter row is rejected");
		check(!rack->requestModulePos(a, gridPx(70, 0)), "on: crossing face 0's right edge is rejected");
		check(rack->requestModulePos(a, gridPx(66, 0)), "on: touching face 0's right edge is allowed");
		check(!rack->requestModulePos(a, gridPx(-1, 0)), "on: left of HP 0 is rejected");
		check(rack->requestModulePos(a, gridPx(5, 4)), "on: face 1 is allowed");

		rack->setModulePosNearest(a, gridPx(10, 3));
		check(isInPlacement(a), "on: nearest from a gutter lands in a placement");
		rack->setModulePosNearest(a, gridPx(500, 9));
		check(isInPlacement(a) && gridPos(a).y >= 8 && gridPos(a).y < 11, "on: nearest from far right lands on face 2");
		check(gridPos(a).x == 66, "on: ...at its right edge");
	}

	// Full row: nearest moves to another row instead of hanging
	resetRack();
	{
		for (int x = 0; x + 4 <= 76; x += 4)
			addBlank(4, x, 0);
		app::ModuleWidget* b = addBlank(10, 0, 1);
		rack->setModulePosNearest(b, gridPx(30, 0));
		check(isInPlacement(b) && gridPos(b).y != 0, "on: full row 0 sends the module to another row");
	}

	// Full layout: nearest gives up and leaves the module where it was
	resetRack();
	fillLayout();
	{
		app::ModuleWidget* b = addBlank(4, 200, 0);
		rack->setModulePosNearest(b, gridPx(30, 0));
		check(gridPos(b).equals(math::Vec(200, 0)), "on: full layout leaves the module where it was");
	}

	// Blank resize past the edge is rejected
	resetRack();
	{
		app::ModuleWidget* a = addBlank(10, 60, 0);
		math::Rect oldBox = a->box;
		a->box.size.x = 20 * app::RACK_GRID_WIDTH;
		check(!rack->requestModulePos(a, a->box.pos), "on: resizing past face 0's edge is rejected");
		a->box = oldBox;
		a->box.size.x = 16 * app::RACK_GRID_WIDTH;
		check(rack->requestModulePos(a, a->box.pos), "on: resizing up to face 0's edge is allowed");
	}

	// Selection
	resetRack();
	{
		app::ModuleWidget* a = addBlank(10, 0, 0);
		app::ModuleWidget* b = addBlank(10, 20, 0);
		rack->select(a);
		rack->select(b);
		check(!rack->requestSelectionPos(math::Vec(0, 3) * RACK_GRID_SIZE), "on: moving a selection into a gutter is rejected");
		rack->setSelectionPosNearest(math::Vec(0, 3) * RACK_GRID_SIZE);
		check(isInPlacement(a) && isInPlacement(b), "on: nearest selection move lands in placements");
		check(gridPos(b).x - gridPos(a).x == 20 && gridPos(b).y == gridPos(a).y, "on: ...keeping its shape");
		rack->setSelectionPosNearest(math::Vec(60, 0) * RACK_GRID_SIZE);
		check(isInPlacement(a) && isInPlacement(b), "on: selection pushed past the edge stays in a placement");
		rack->deselectAll();
	}

	// Force (Ctrl-drag): shoving within the placement is allowed
	resetRack();
	{
		addBlank(10, 0, 0);
		addBlank(10, 10, 0);
		app::ModuleWidget* c = addBlank(10, 60, 0);
		app::ModuleWidget* x = addBlank(10, 0, 1);
		rack->setModulePosForce(x, gridPx(55, 0));
		check(gridPos(x).equals(math::Vec(55, 0)) && gridPos(c).x == 65, "on: force shoves a neighbour within the placement");
		check(allInPlacements() && noOverlaps(), "on: ...leaving everything placed, no overlaps");
	}

	// Force: a shove past the edge is undone and falls back to nearest
	resetRack();
	{
		std::vector<app::ModuleWidget*> row;
		for (int x = 0; x <= 60; x += 10)
			row.push_back(addBlank(10, x, 0));
		app::ModuleWidget* x = addBlank(10, 0, 1);
		rack->setModulePosForce(x, gridPx(30, 0));
		check(allInPlacements() && noOverlaps(), "on: force past the edge leaves everything placed, no overlaps");
		bool rowUnmoved = true;
		for (size_t i = 0; i < row.size(); i++)
			rowUnmoved &= gridPos(row[i]).equals(math::Vec(i * 10, 0));
		check(rowUnmoved, "on: ...and the row it would have shoved is untouched");
	}

	// Squeeze: fits into a gap
	resetRack();
	{
		addBlank(10, 0, 0);
		addBlank(10, 20, 0);
		app::ModuleWidget* x = addBlank(10, 0, 1);
		rack->squeezeModulePos(x, gridPx(12, 0));
		check(gridPos(x).equals(math::Vec(10, 0)), "on: squeeze into a gap");
		check(allInPlacements() && noOverlaps(), "on: ...leaving everything placed, no overlaps");
	}

	// Squeeze: a shove past the edge is undone and falls back to nearest
	resetRack();
	{
		for (int x = 0; x <= 60; x += 10)
			addBlank(10, x, 0);
		app::ModuleWidget* x = addBlank(10, 0, 1);
		rack->squeezeModulePos(x, gridPx(35, 0));
		check(allInPlacements() && noOverlaps(), "on: squeeze past the edge leaves everything placed, no overlaps");
	}

	// Placements off: force shoves past HP 76 like stock
	settings::placementsEnabled = false;
	resetRack();
	{
		std::vector<app::ModuleWidget*> row;
		for (int x = 0; x <= 60; x += 10)
			row.push_back(addBlank(10, x, 0));
		app::ModuleWidget* x = addBlank(10, 0, 1);
		rack->setModulePosForce(x, gridPx(30, 0));
		check(gridPos(x).equals(math::Vec(30, 0)) && gridPos(row.back()).x == 70, "off: force shoves past HP 76 like stock");
	}

	settings::placementsEnabled = true;
	quietWarnings = true;

	// Stray-module check: a module a plugin put in a gutter is moved into a rack on the next step
	resetRack();
	{
		app::ModuleWidget* a = addBlank(10, 10, 3);
		check(isStray(a), "on: a module in a gutter is stray");
		rack->step();
		check(!isStray(a) && isInPlacement(a), "on: the stray check moves it into a rack");
	}

	// Stray check with no room: gives up once, then doesn't search again every frame
	resetRack();
	fillLayout();
	{
		app::ModuleWidget* a = addBlank(4, 200, 0);
		rack->step();
		check(isStray(a) && gridPos(a).equals(math::Vec(200, 0)), "on: with no room, the stray stays where it was");
		double t0 = system::getTime();
		rack->step();
		double dt = system::getTime() - t0;
		check(dt < 0.02, "on: ...and later frames don't redo the search");
	}

	// Paste a module with no room: rejected with a warning, nothing left behind
	resetRack();
	fillLayout();
	{
		json_t* moduleJ = blankJson(10, 0, 0);
		size_t n = rack->getModules().size();
		int warnings = noRoomWarnings;
		rack->pasteModuleJsonAction(moduleJ);
		json_decref(moduleJ);
		check(rack->getModules().size() == n && noRoomWarnings == warnings + 1, "on: pasting a module with no room is rejected with a warning");
	}

	// Paste a module with room: lands in a rack
	resetRack();
	{
		json_t* moduleJ = blankJson(10, 0, 0);
		rack->pasteModuleJsonAction(moduleJ);
		json_decref(moduleJ);
		check(rack->getModules().size() == 1 && allInPlacements(), "on: pasting a module lands it in a rack");
	}

	// Paste a selection with room / without room
	resetRack();
	{
		json_t* selJ = selectionJson();
		rack->pasteJsonAction(selJ);
		json_decref(selJ);
		check(rack->getModules().size() == 2 && allInPlacements() && noOverlaps(), "on: pasting a selection lands it in racks");
	}
	resetRack();
	fillLayout();
	{
		json_t* selJ = selectionJson();
		size_t n = rack->getModules().size();
		int warnings = noRoomWarnings;
		rack->pasteJsonAction(selJ);
		json_decref(selJ);
		check(rack->getModules().size() == n && noRoomWarnings == warnings + 1, "on: pasting a selection with no room pastes none of it");
	}

	// Clone with room / without room
	resetRack();
	{
		app::ModuleWidget* a = addBlank(10, 0, 0);
		a->cloneAction(false);
		check(rack->getModules().size() == 2 && allInPlacements() && noOverlaps(), "on: cloning lands the copy in a rack");
	}
	resetRack();
	fillLayout();
	{
		app::ModuleWidget* a = rack->getModules().front();
		size_t n = rack->getModules().size();
		a->cloneAction(false);
		check(rack->getModules().size() == n, "on: cloning with no room is rejected");
	}

	// Patch files: importing a stock patch
	resetRack();
	{
		json_t* stockJ = stockPatchJson();
		APP->patch->fromJson(stockJ);
		json_decref(stockJ);
		check(rack->getModules().size() == 5 && rack->getCables().size() == 1, "load: stock patch loads every module and its cable");
		check(allInPlacements() && noOverlaps(), "load: a stock patch too wide for one rack is fitted into racks, no overlaps");
		app::ModuleWidget* a = rack->getModule(1);
		app::ModuleWidget* b = rack->getModule(2);
		check(a && b && gridPos(b).x - gridPos(a).x == 10 && gridPos(b).y == gridPos(a).y, "load: touching modules stay touching");
		check(a && b && a->module->rightExpander.module == b->module, "load: ...so the expander pair still works");
	}

	// Patch files: save -> load round-trip
	{
		std::map<int64_t, math::Vec> before = modulePositions();
		json_t* savedJ = APP->patch->toJson();
		json_t* placementsJ = json_object_get(json_object_get(savedJ, "rack3d"), "placements");
		check(json_array_size(placementsJ) == 6, "save: the patch stores 6 placements");
		APP->patch->fromJson(savedJ);
		check(modulePositions() == before && rack->getCables().size() == 1, "load: save -> load keeps every position and cable");

		// Stock Rack ignores "rack3d" and reads "pos": nothing moves
		json_object_del(savedJ, "rack3d");
		settings::placementsEnabled = false;
		APP->patch->fromJson(savedJ);
		check(modulePositions() == before && rack->getCables().size() == 1, "load: our patch opened like stock Rack would keeps every position");

		// Resaved by stock Rack (no "rack3d" key) and reopened with placements on: still fits as-is
		settings::placementsEnabled = true;
		APP->patch->fromJson(savedJ);
		check(modulePositions() == before, "load: our patch without its rack3d key reopens unchanged");
		json_decref(savedJ);
	}

	// Patch files: placements off loads a stock patch like stock
	settings::placementsEnabled = false;
	{
		json_t* stockJ = stockPatchJson();
		APP->patch->fromJson(stockJ);
		json_decref(stockJ);
		app::ModuleWidget* e = rack->getModule(5);
		check(e && gridPos(e).equals(math::Vec(120, 1)), "off: a stock patch loads with stock positions");
	}
	settings::placementsEnabled = true;

	quietWarnings = false;
	resetRack();
	settings::placementsEnabled = oldEnabled;
	INFO("rack3d app self-test: %d failure(s)", failures);
	return failures;
}


} // namespace rack3d
} // namespace rack
