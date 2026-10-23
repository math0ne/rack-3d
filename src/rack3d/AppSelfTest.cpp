#include <rack3d/AppSelfTest.hpp>
#include <rack3d/Layout.hpp>
#include <rack3d/settings.hpp>
#include <rack3d/Placement.hpp>
#include <app/RackWidget.hpp>
#include <app/CableWidget.hpp>
#include <app/ParamWidget.hpp>
#include <app/PortWidget.hpp>
#include <widget/event.hpp>
#include <color.hpp>
#include <system.hpp>
#include <app/Scene.hpp>
#include <engine/Engine.hpp>
#include <rack3d/PlacementRenderer.hpp>
#include <rack3d/TilesView.hpp>
#include <app/RackScrollWidget.hpp>
#include <stb_image_write.h>
#include <plugin.hpp>
#include <patch.hpp>
#include <asset.hpp>
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


/** Reads a pixel of a placement texture, (x, y) in NanoVG's top-left coordinates. */
static NVGcolor readPixel(const PlacementRenderer::Target* t, int x, int y) {
	uint8_t px[4] = {};
	nvgluBindFramebuffer(t->fb);
	glReadPixels(x, (int) t->size.y - 1 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
	nvgluBindFramebuffer(NULL);
	return nvgRGBA(px[0], px[1], px[2], px[3]);
}


/** Saves a placement texture as a PNG, top row first. */
static void saveTexture(const PlacementRenderer::Target* t, const std::string& path) {
	int w = t->size.x, h = t->size.y;
	std::vector<uint8_t> pixels(w * h * 4);
	nvgluBindFramebuffer(t->fb);
	glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
	nvgluBindFramebuffer(NULL);
	// GL rows go bottom-up
	std::vector<uint8_t> flipped(pixels.size());
	for (int y = 0; y < h; y++)
		std::memcpy(&flipped[y * w * 4], &pixels[(h - 1 - y) * w * 4], w * 4);
	stbi_write_png(path.c_str(), w, h, 4, flipped.data(), w * 4);
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
		// updateExpanders() sets the ids; the engine resolves them to Module pointers on its own thread
		check(a && b && a->module->rightExpander.moduleId == b->module->id, "load: ...so the expander pair still works");
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

	// Placement textures
	resetRack();
	{
		addBlank(10, 0, 0);
		addBlank(10, 5, 13);
		PlacementRenderer r;
		r.scale = 0.5f;
		while (glGetError() != GL_NO_ERROR) {}
		// Panel caches render one per frame before the window loop starts, so render a few "frames"
		for (int i = 0; i < 20; i++) {
			// Like a real frame: step (e.g. Blank sizes its panel in step()), then render
			rack->step();
			APP->window->fbCount() = 0;
			r.render(APP->window->vg, rack);
		}
		check(glGetError() == GL_NO_ERROR, "render: no GL errors");
		check(r.targets.size() == 6, "render: one texture per placement");
		const PlacementRenderer::Target* t0 = r.getTarget(0);
		const PlacementRenderer::Target* t3 = r.getTarget(3);
		check(t0 && t0->size.equals(math::Vec(570, 570)), "render: a cube face texture is 1140 px x scale");
		if (t0 && t3) {
			// Blank at face 0 (0, 0): 10 HP wide, so its centre is at (75, 190) rack px
			NVGcolor module0 = readPixel(t0, 75 * 0.5, 190 * 0.5);
			NVGcolor empty0 = readPixel(t0, 900 * 0.5, 190 * 0.5);
			check(module0.a == 1.f && module0.r > 0.8f, "render: a Blank shows up light grey on face 0");
			check(empty0.a == 1.f && std::fabs(empty0.r - module0.r) > 0.1f, "render: empty rack space looks different");
			// Blank at grid (5, 13) is face 3 (rows 12-14), local row 1: centre (150, 570) rack px
			NVGcolor module3 = readPixel(t3, 150 * 0.5, 570 * 0.5);
			check(module3.a == 1.f && module3.r > 0.8f, "render: a Blank on face 3 shows up in face 3's texture");
			saveTexture(t0, asset::user("placement0.png"));
			saveTexture(t3, asset::user("placement3.png"));
		}
		APP->window->fbCount() = 0;
		r.render(APP->window->vg, rack);
		check(APP->window->fbCount() == 0, "render: rendering again at the same scale redraws no panel caches");
		r.clear();
	}

	// Tiles view: real frames through Window::step()
	resetRack();
	{
		app::ModuleWidget* a = addBlank(10, 10, 16);
		setTilesViewShown(true);
		check(isTilesViewShown() && !APP->scene->rackScroll->isVisible(), "tiles: F6 view replaces the 2D rack");
		for (int i = 0; i < 30; i++)
			APP->window->step();
		const PlacementRenderer::Target* t4 = getRenderer()->getTarget(4);
		check(t4 != NULL, "tiles: the view requests placement textures");
		if (t4) {
			// Blank at grid (10, 16) is face 4 (rows 16-18), local (10 HP, row 0): centre (225, 190) rack px
			float s = getRenderer()->scale;
			check(readPixel(t4, 225 * s, 190 * s).r > 0.8f, "tiles: face 4's texture shows the Blank");
			APP->window->screenshot(asset::user("tiles.png"));

			// Move it between frames: the texture follows
			rack->requestModulePos(a, gridPx(50, 16));
			for (int i = 0; i < 5; i++)
				APP->window->step();
			s = getRenderer()->scale;
			check(readPixel(t4, 225 * s, 190 * s).r < 0.5f && readPixel(t4, 825 * s, 190 * s).r > 0.8f, "tiles: textures update live when a module moves");
		}
		check(glGetError() == GL_NO_ERROR, "tiles: no GL errors over 35 frames");
		setTilesViewShown(false);
		check(!isTilesViewShown() && APP->scene->rackScroll->isVisible(), "tiles: hiding the view brings back the 2D rack");
	}

	// Move to rack, with undo/redo
	resetRack();
	APP->history->clear();
	{
		app::ModuleWidget* a = addBlank(10, 30, 1);
		math::Vec start = gridPos(a);
		bool allMoved = true;
		for (int id = 1; id < 6; id++) {
			bool ok = moveModuleToPlacement(a, id);
			rack3d::Placement* p = rack->getLayout()->placementOf(a->getGridBox());
			allMoved = allMoved && ok && p && p->id == id;
		}
		check(allMoved, "move: a module can be moved to each rack");
		math::Vec end = gridPos(a);
		APP->history->undo();
		check(!gridPos(a).equals(end) && isInPlacement(a), "move: undo moves it back one step");
		for (int i = 0; i < 4; i++)
			APP->history->undo();
		check(gridPos(a).equals(start), "move: undoing every move restores where it started");
		for (int i = 0; i < 5; i++)
			APP->history->redo();
		check(gridPos(a).equals(end), "move: redo replays every move");

		// No room in the target rack: nothing moves
		resetRack();
		for (int x = 0; x + 4 <= 76; x += 4) {
			for (int y = 4; y < 7; y++)
				addBlank(4, x, y);
		}
		app::ModuleWidget* b = addBlank(10, 0, 0);
		int warnings = noRoomWarnings;
		check(!moveModuleToPlacement(b, 1) && gridPos(b).equals(math::Vec(0, 0)) && noRoomWarnings == warnings + 1, "move: a full rack leaves the module where it was, with a warning");
	}

	// Chrome in the 2D view: screenshot for a visual check
	resetRack();
	{
		addBlank(10, 0, 0);
		addBlank(20, 30, 5);
		addBlank(10, 200, 1);
		APP->scene->rackScroll->setZoom(0.25);
		APP->scene->rackScroll->setGridOffset(math::Vec(-2, -1));
		for (int i = 0; i < 20; i++)
			APP->window->step();
		check(glGetError() == GL_NO_ERROR, "chrome: no GL errors");
		APP->window->screenshot(asset::user("chrome.png"));
	}

	// Phase 2 acceptance patch: a voice spread over all six racks, every cable crossing racks
	resetRack();
	{
		std::string path = asset::system("tests/rack3d/acceptance.vcv");
		auto loadAcceptance = [&]() -> bool {
			try {
				APP->patch->load(path);
				return true;
			}
			catch (Exception& e) {
				WARN("%s", e.what());
				return false;
			}
		};
		// The engine's view of the patch, minus runtime state ("data"), which changes while it runs
		auto engineJson = [&]() {
			json_t* engineJ = APP->engine->toJson();
			size_t i;
			json_t* moduleJ;
			json_array_foreach(json_object_get(engineJ, "modules"), i, moduleJ)
				json_object_del(moduleJ, "data");
			return engineJ;
		};

		settings::placementsEnabled = true;
		check(loadAcceptance(), "acceptance: the patch loads");
		check(rack->getModules().size() == 7 && rack->getCables().size() == 7, "acceptance: all 7 modules and 7 cables are there");
		std::map<int64_t, int> expected = {{1, 0}, {3, 1}, {6, 1}, {2, 2}, {5, 3}, {4, 4}, {7, 5}};
		bool inRacks = true;
		for (auto pair : expected) {
			app::ModuleWidget* mw = rack->getModule(pair.first);
			rack3d::Placement* p = mw ? rack->getLayout()->placementOf(mw->getGridBox()) : NULL;
			inRacks = inRacks && p && p->id == pair.second;
		}
		check(inRacks, "acceptance: every module is in its rack (VCO front ... Audio bottom)");
		bool crossing = true;
		for (app::CableWidget* cw : rack->getCables()) {
			rack3d::Placement* in = rack->getLayout()->placementOf(rack->getModule(cw->cable->inputModule->id)->getGridBox());
			rack3d::Placement* out = rack->getLayout()->placementOf(rack->getModule(cw->cable->outputModule->id)->getGridBox());
			crossing = crossing && in && out && in != out;
		}
		check(crossing, "acceptance: every cable crosses between racks");

		// It makes sound: the VCA's output moves (the engine's fallback thread runs it without an audio device)
		app::ModuleWidget* vca = rack->getModule(4);
		float peak = 0;
		for (int i = 0; i < 150 && vca; i++) {
			peak = std::fmax(peak, std::fabs(vca->module->outputs[0].getVoltage()));
			system::sleep(0.01);
		}
		check(peak > 0.1f, "acceptance: the voice produces signal at the VCA");

		// Re-patch across racks: LFO (right) triangle -> VCO (front) FM
		app::ModuleWidget* lfo = rack->getModule(3);
		app::ModuleWidget* vco = rack->getModule(1);
		if (lfo && vco) {
			engine::Cable* cable = new engine::Cable;
			cable->outputModule = lfo->module;
			cable->outputId = 1;
			cable->inputModule = vco->module;
			cable->inputId = 1;
			APP->engine->addCable(cable);
			app::CableWidget* cw = new app::CableWidget;
			cw->setCable(cable);
			cw->color = color::fromHexString("#ffb437");
			rack->addCable(cw);
		}
		check(rack->getCables().size() == 8, "acceptance: a new cable can be patched across racks");

		// Tiles screenshot, for a visual check
		setTilesViewShown(true);
		for (int i = 0; i < 30; i++)
			APP->window->step();
		APP->window->screenshot(asset::user("acceptance-tiles.png"));
		setTilesViewShown(false);
		check(glGetError() == GL_NO_ERROR, "acceptance: no GL errors");

		// Placements change nothing the engine sees: same modules, params, cables and expander links as stock
		loadAcceptance();
		json_t* onJ = engineJson();
		settings::placementsEnabled = false;
		loadAcceptance();
		json_t* offJ = engineJson();
		check(json_equal(onJ, offJ), "acceptance: the engine's patch is identical with placements on and off (so is the audio)");
		json_decref(onJ);
		json_decref(offJ);
		settings::placementsEnabled = true;
	}

	// Interactive tiles: real mouse input through EventState
	resetRack();
	try {
		APP->patch->load(asset::system("tests/rack3d/acceptance.vcv"));
	}
	catch (Exception& e) {
		WARN("%s", e.what());
	}
	setTilesViewShown(true);
	for (int i = 0; i < 10; i++)
		APP->window->step();
	{
		TilesView* view = getTilesView();
		// Rack coordinates -> screen (root widget) coordinates
		auto screenPos = [&](math::Vec rackPos) {
			math::Vec pos;
			view->fromRackPos(rackPos, &pos);
			return pos.plus(view->box.pos);
		};
		auto drag = [&](math::Vec from, math::Vec to) {
			APP->event->handleHover(from, math::Vec());
			APP->event->handleButton(from, GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
			math::Vec last = from;
			for (int i = 1; i <= 20; i++) {
				math::Vec p = from.crossfade(to, i / 20.f);
				APP->event->handleHover(p, p.minus(last));
				last = p;
				APP->window->step();
			}
			APP->event->handleButton(to, GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
			APP->window->step();
		};

		// Knob: drag VCO's frequency knob up
		app::ModuleWidget* vco = rack->getModule(1);
		if (vco && !vco->getParams().empty()) {
			app::ParamWidget* knob = vco->getParams()[0];
			float before = knob->getParamQuantity()->getValue();
			math::Vec p = screenPos(knob->getRelativeOffset(knob->box.size.div(2), rack));
			drag(p, p.plus(math::Vec(0, -40)));
			check(knob->getParamQuantity()->getValue() != before, "tiles input: dragging a knob in a tile turns it");
		}

		// Module: drag the VCF from the back tile to the bottom tile
		app::ModuleWidget* vcf = rack->getModule(2);
		if (vcf) {
			math::Vec grab = vcf->box.pos.plus(math::Vec(3, 190));
			math::Rect bottom = rack3d::Layout::getRackRect(*rack->getLayout()->getPlacement(5));
			drag(screenPos(grab), screenPos(bottom.pos.plus(math::Vec(40 * app::RACK_GRID_WIDTH, 380 + 190))));
			rack3d::Placement* p = rack->getLayout()->placementOf(vcf->getGridBox());
			check(p && p->id == 5, "tiles input: a module can be dragged from one tile to another");
		}

		// Cable: from the LFO's triangle output (right tile) to the VCO's FM input (front tile)
		app::ModuleWidget* lfo = rack->getModule(3);
		if (lfo && vco) {
			size_t n = rack->getCables().size();
			app::PortWidget* out = lfo->getOutput(1);
			app::PortWidget* in = vco->getInput(1);
			drag(screenPos(out->getRelativeOffset(out->box.size.div(2), rack)), screenPos(in->getRelativeOffset(in->box.size.div(2), rack)));
			bool connected = false;
			for (app::CableWidget* cw : rack->getCables())
				connected = connected || (cw->isComplete() && cw->outputPort == out && cw->inputPort == in);
			check(rack->getCables().size() == n + 1 && connected, "tiles input: a cable can be patched from one tile to another");
		}
		check(glGetError() == GL_NO_ERROR, "tiles input: no GL errors");
	}
	setTilesViewShown(false);

	quietWarnings = false;
	resetRack();
	settings::placementsEnabled = oldEnabled;
	INFO("rack3d app self-test: %d failure(s)", failures);
	return failures;
}


} // namespace rack3d
} // namespace rack
