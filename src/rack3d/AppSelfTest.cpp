#include <rack3d/AppSelfTest.hpp>
#include <rack3d/Layout.hpp>
#include <rack3d/settings.hpp>
#include <app/RackWidget.hpp>
#include <app/Scene.hpp>
#include <engine/Engine.hpp>
#include <plugin.hpp>
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
	{
		for (int y = 0; y < 23; y++) {
			if (y % 4 == 3)
				continue;
			for (int x = 0; x + 4 <= 76; x += 4)
				addBlank(4, x, y);
		}
		// The 76th HP of every row is free but too narrow for a 4 HP module
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

	resetRack();
	settings::placementsEnabled = oldEnabled;
	INFO("rack3d app self-test: %d failure(s)", failures);
	return failures;
}


} // namespace rack3d
} // namespace rack
