#include <map>
#include <algorithm>
#include <queue>
#include <functional>

#include <osdialog.h>

#include <app/RackWidget.hpp>
#include <widget/TransparentWidget.hpp>
#include <app/RailWidget.hpp>
#include <app/Scene.hpp>
#include <settings.hpp>
#include <plugin.hpp>
#include <engine/Engine.hpp>
#include <context.hpp>
#include <system.hpp>
#include <asset.hpp>
#include <patch.hpp>
#include <helpers.hpp>
#include <rack3d/Layout.hpp>
#include <rack3d/settings.hpp>
#include <rack3d/Placement.hpp>


namespace rack {
namespace app {


struct RackWidget::Internal {
	RailWidget* rail = NULL;
	widget::Widget* moduleContainer = NULL;
	widget::Widget* plugContainer = NULL;
	widget::Widget* cableContainer = NULL;
	int nextCableColorId = 0;
	/** The last mouse position in the RackWidget */
	math::Vec mousePos;

	bool selecting = false;
	math::Vec selectionStart;
	math::Vec selectionEnd;
	std::set<ModuleWidget*> selectedModules;
	std::map<widget::Widget*, math::Vec> moduleOldPositions;

	/** Rack 3D placements. Only enforced while rack3d::settings::placementsEnabled is on. */
	rack3d::Layout layout;
	/** Rack 3D: stray modules that couldn't be moved into any rack, by module ID, with the position they were at.
	moveStrayModules() doesn't retry them until they move, so a full layout doesn't cost a full search every frame.
	*/
	std::map<int64_t, math::Vec> strayGivenUp;
};


/** Creates a new Module and ModuleWidget */
static ModuleWidget* moduleWidgetFromJson(json_t* moduleJ) {
	plugin::Model* model = plugin::modelFromJson(moduleJ);
	assert(model);
	INFO("Creating module %s", model->getFullName().c_str());
	engine::Module* module = model->createModule();
	assert(module);
	module->fromJson(moduleJ);

	// Create ModuleWidget
	INFO("Creating module widget %s", model->getFullName().c_str());
	ModuleWidget* moduleWidget = module->model->createModuleWidget(module);
	assert(moduleWidget);
	return moduleWidget;
}


struct ModuleContainer : widget::Widget {
	void draw(const DrawArgs& args) override {
		// Draw ModuleWidget shadows
		Widget::drawLayer(args, -1);

		Widget::draw(args);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		// Draw lights after translucent rectangle
		if (layer == 1) {
			Widget::drawLayer(args, 1);
		}
	}
};


/** Children PlugWidgets are owned by CableWidgets. */
struct PlugContainer : widget::TransparentWidget {
	void draw(const DrawArgs& args) override {
		// Don't draw on layer 0
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 2) {
			// Draw Plugs
			Widget::draw(args);

			// Draw plug lights
			nvgSave(args.vg);
			nvgGlobalTint(args.vg, color::WHITE);
			Widget::drawLayer(args, 1);
			nvgRestore(args.vg);
		}
	}
};


struct CableContainer : widget::TransparentWidget {
	void draw(const DrawArgs& args) override {
		// Don't draw on layer 0
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 3) {
			// Draw cable shadows
			Widget::drawLayer(args, -1);

			// Draw cables
			Widget::draw(args);
		}
	}
};


RackWidget::RackWidget() {
	internal = new Internal;

	internal->rail = new RailWidget;
	addChild(internal->rail);

	internal->moduleContainer = new ModuleContainer;
	addChild(internal->moduleContainer);

	internal->plugContainer = new PlugContainer;
	addChild(internal->plugContainer);

	internal->cableContainer = new CableContainer;
	addChild(internal->cableContainer);

	// Until patches store their own placements, every rack is a cube
	internal->layout.addCube();
}

RackWidget::~RackWidget() {
	clear();
	delete internal;
}

rack3d::Layout* RackWidget::getLayout() {
	return &internal->layout;
}

/** Rack 3D: moves modules that are outside every placement into the nearest rack. They get there when a plugin sets
box.pos directly, or from a patch made without placements. Skips the module being dragged.
*/
static void moveStrayModules(RackWidget* rack) {
	RackWidget::Internal* internal = rack->internal;
	std::vector<ModuleWidget*> strays;
	for (widget::Widget* w : internal->moduleContainer->children) {
		ModuleWidget* mw = static_cast<ModuleWidget*>(w);
		if (w == APP->event->draggedWidget)
			continue;
		if (!rack3d::isStray(mw)) {
			internal->strayGivenUp.erase(mw->module->id);
			continue;
		}
		auto it = internal->strayGivenUp.find(mw->module->id);
		if (it != internal->strayGivenUp.end() && it->second.equals(mw->box.pos))
			continue;
		strays.push_back(mw);
	}

	for (ModuleWidget* mw : strays) {
		math::Vec oldPos = mw->box.pos;
		rack->setModulePosNearest(mw, oldPos);
		if (rack3d::isStray(mw)) {
			WARN("No room in any rack for %s (module %lld), leaving it outside", mw->model->getFullName().c_str(), (long long) mw->module->id);
			internal->strayGivenUp[mw->module->id] = mw->box.pos;
		}
		else {
			INFO("Moved %s (module %lld) into a rack", mw->model->getFullName().c_str(), (long long) mw->module->id);
		}
	}
}

void RackWidget::step() {
	if (rack3d::settings::placementsEnabled)
		moveStrayModules(this);
	Widget::step();
}

void RackWidget::draw(const DrawArgs& args) {
	float b = settings::rackBrightness;

	// Draw rack rails and modules
	Widget::draw(args);

	// Draw translucent dark rectangle
	if (b < 1.f) {
		// Get zoom level
		float t[6];
		nvgCurrentTransform(args.vg, t);
		float zoom = t[3];
		// Draw mouse spotlight
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0.0, 0.0, VEC_ARGS(box.size));
		nvgFillPaint(args.vg, nvgRadialGradient(args.vg,
			VEC_ARGS(internal->mousePos), 0.0,
			settings::spotlightRadius / zoom,
			nvgRGBAf(0, 0, 0, 1.f - b - settings::spotlightBrightness),
			nvgRGBAf(0, 0, 0, 1.f - b)));
		nvgFill(args.vg);
	}

	// Draw lights and halos
	Widget::drawLayer(args, 1);

	// Tint all draws after this point
	nvgGlobalTint(args.vg, nvgRGBAf(b, b, b, 1));

	// Draw plugs
	Widget::drawLayer(args, 2);

	// Draw cables
	Widget::drawLayer(args, 3);

	// Draw selection rectangle
	if (internal->selecting) {
		nvgBeginPath(args.vg);
		math::Rect selectionBox = math::Rect::fromCorners(internal->selectionStart, internal->selectionEnd);
		nvgRect(args.vg, RECT_ARGS(selectionBox));
		nvgFillColor(args.vg, nvgRGBAf(1, 0, 0, 0.25));
		nvgFill(args.vg);
		nvgStrokeWidth(args.vg, 2.0);
		nvgStrokeColor(args.vg, nvgRGBAf(1, 0, 0, 0.5));
		nvgStroke(args.vg);
	}
}

void RackWidget::onHover(const HoverEvent& e) {
	// Set before calling children's onHover()
	internal->mousePos = e.pos;

	OpaqueWidget::onHover(e);
}

void RackWidget::onHoverKey(const HoverKeyEvent& e) {
	OpaqueWidget::onHoverKey(e);
}

void RackWidget::onButton(const ButtonEvent& e) {
	OpaqueWidget::onButton(e);
	if (e.isConsumed())
		return;

	if (e.button == GLFW_MOUSE_BUTTON_RIGHT) {
		if (e.action == GLFW_PRESS) {
			APP->scene->browser->show();
		}
		e.consume(this);
	}
}

void RackWidget::onDragStart(const DragStartEvent& e) {
	if (e.button == GLFW_MOUSE_BUTTON_LEFT) {
		// Deselect all modules
		updateSelectionFromRect();
		internal->selecting = true;
		internal->selectionStart = internal->mousePos;
		internal->selectionEnd = internal->mousePos;
	}
}

void RackWidget::onDragEnd(const DragEndEvent& e) {
	if (e.button == GLFW_MOUSE_BUTTON_LEFT) {
		internal->selecting = false;
	}
}

void RackWidget::onDragHover(const DragHoverEvent& e) {
	// Set before calling children's onDragHover()
	internal->mousePos = e.pos;

	if (internal->selecting) {
		internal->selectionEnd = internal->mousePos;
		updateSelectionFromRect();
	}

	OpaqueWidget::onDragHover(e);
}

widget::Widget* RackWidget::getModuleContainer() {
	return internal->moduleContainer;
}

widget::Widget* RackWidget::getPlugContainer() {
	return internal->plugContainer;
}

widget::Widget* RackWidget::getCableContainer() {
	return internal->cableContainer;
}

math::Vec RackWidget::getMousePos() {
	return internal->mousePos;
}

void RackWidget::clear() {
	// This isn't required because removing all ModuleWidgets should remove all cables, but do it just in case.
	clearCables();
	// Remove ModuleWidgets
	for (ModuleWidget* mw : getModules()) {
		removeModule(mw);
		delete mw;
	}
}

/** Rack 3D: after loading a patch, fits its modules into the placements (see rack3d::Layout::fitBoxes()).
Modules that don't fit go on the overflow shelf; they're marked so moveStrayModules() doesn't search for them.
*/
static void fitModulesIntoPlacements(RackWidget* rack) {
	RackWidget::Internal* internal = rack->internal;
	std::vector<ModuleWidget*> mws = rack->getModules();
	std::vector<math::Rect> boxes;
	for (ModuleWidget* mw : mws)
		boxes.push_back(mw->getGridBox());

	std::vector<math::Vec> positions;
	int overflow = 0;
	rack3d::Layout::FitResult result = internal->layout.fitBoxes(boxes, positions, &overflow);
	if (result == rack3d::Layout::FIT_KEPT)
		return;

	for (size_t i = 0; i < mws.size(); i++) {
		mws[i]->setGridPosition(positions[i]);
		if (rack3d::isStray(mws[i]))
			internal->strayGivenUp[mws[i]->module->id] = mws[i]->box.pos;
	}
	INFO("Fitted %d modules into racks (%s)", (int) mws.size(), result == rack3d::Layout::FIT_MOVED_BLOCK ? "moved as one block" : "packed by rows");
	if (overflow > 0) {
		WARN("%d modules didn't fit in any rack", overflow);
		rack3d::warnOverflow(overflow);
	}
}

void RackWidget::mergeJson(json_t* rootJ) {
	// Rack 3D: placements. Stock Rack ignores this key.
	json_object_set_new(rootJ, "rack3d", internal->layout.toJson());

	// modules
	json_t* modulesJ = json_object_get(rootJ, "modules");
	if (!modulesJ)
		return;
	size_t moduleIndex;
	json_t* moduleJ;
	json_array_foreach(modulesJ, moduleIndex, moduleJ) {
		// module
		json_t* idJ = json_object_get(moduleJ, "id");
		if (!idJ)
			continue;
		int64_t id = json_integer_value(idJ);
		// TODO Legacy v0.6?
		ModuleWidget* mw = getModule(id);
		if (!mw) {
			WARN("Cannot find ModuleWidget %lld", (long long) id);
			continue;
		}

		// pos
		math::Vec pos = mw->box.pos.minus(RACK_OFFSET);
		pos = pos.div(RACK_GRID_SIZE).round();
		json_t* posJ = json_pack("[i, i]", (int) pos.x, (int) pos.y);
		json_object_set_new(moduleJ, "pos", posJ);
	}

	// Calculate plug orders
	std::map<Widget*, int> plugOrders;
	int plugOrder = 1;
	for (Widget* w : internal->plugContainer->children) {
		plugOrders[w] = plugOrder++;
	}

	// cables
	json_t* cablesJ = json_object_get(rootJ, "cables");
	if (!cablesJ)
		return;
	size_t cableIndex;
	json_t* cableJ;
	json_array_foreach(cablesJ, cableIndex, cableJ) {
		// cable
		json_t* idJ = json_object_get(cableJ, "id");
		if (!idJ)
			continue;
		int64_t id = json_integer_value(idJ);
		CableWidget* cw = getCable(id);
		if (!cw) {
			WARN("Cannot find CableWidget %lld", (long long) id);
			continue;
		}

		cw->mergeJson(cableJ);

		// inputPlugOrder
		auto plugOrderIt = plugOrders.find(cw->inputPlug);
		if (plugOrderIt != plugOrders.end()) {
			int inputPlugOrder = plugOrderIt->second;
			json_object_set_new(cableJ, "inputPlugOrder", json_integer(inputPlugOrder));
		}

		// outputPlugOrder
		plugOrderIt = plugOrders.find(cw->outputPlug);
		if (plugOrderIt != plugOrders.end()) {
			int outputPlugOrder = plugOrderIt->second;
			json_object_set_new(cableJ, "outputPlugOrder", json_integer(outputPlugOrder));
		}
	}
}

void RackWidget::fromJson(json_t* rootJ) {
	// version
	std::string version;
	json_t* versionJ = json_object_get(rootJ, "version");
	if (versionJ)
		version = json_string_value(versionJ);

	bool legacyV05 = false;
	if (string::startsWith(version, "0.3.") || string::startsWith(version, "0.4.") || string::startsWith(version, "0.5.") || version == "dev") {
		legacyV05 = true;
	}

	// Rack 3D: placements. Patches without them (stock patches, or ours resaved by stock Rack) get the default cube.
	if (!internal->layout.fromJson(json_object_get(rootJ, "rack3d"))) {
		internal->layout.clear();
		internal->layout.addCube();
	}
	internal->strayGivenUp.clear();

	// modules
	json_t* modulesJ = json_object_get(rootJ, "modules");
	if (!modulesJ)
		return;

	// Rack 3D: put modules exactly where the patch says, like stock. With placements on they're fitted into racks
	// afterwards, all at once, instead of each one being pushed to its nearest free spot as it's created.
	bool placementsEnabled = rack3d::settings::placementsEnabled;
	rack3d::settings::placementsEnabled = false;
	DEFER({rack3d::settings::placementsEnabled = placementsEnabled;});

	size_t moduleIndex;
	json_t* moduleJ;
	json_array_foreach(modulesJ, moduleIndex, moduleJ) {
		// Get module ID
		json_t* idJ = json_object_get(moduleJ, "id");
		int64_t id;
		if (idJ)
			id = json_integer_value(idJ);
		else
			id = moduleIndex;

		// Get Module
		engine::Module* module = APP->engine->getModule(id);
		if (!module) {
			WARN("Cannot find Module %lld", (long long) id);
			continue;
		}

		// Create ModuleWidget
		INFO("Creating module widget %s", module->model->getFullName().c_str());
		ModuleWidget* mw = module->model->createModuleWidget(module);

		// pos
		json_t* posJ = json_object_get(moduleJ, "pos");
		double x = 0.0, y = 0.0;
		json_unpack(posJ, "[F, F]", &x, &y);
		math::Vec pos = math::Vec(x, y);
		if (legacyV05) {
			// In <=v0.5, positions were in pixel units
		}
		else {
			pos = pos.mult(RACK_GRID_SIZE);
		}
		pos = pos.plus(RACK_OFFSET);
		setModulePosForce(mw, pos);

		internal->moduleContainer->addChild(mw);
	}

	rack3d::settings::placementsEnabled = placementsEnabled;
	if (placementsEnabled)
		fitModulesIntoPlacements(this);

	updateExpanders();

	std::map<Widget*, int> plugOrders;

	// cables
	json_t* cablesJ = json_object_get(rootJ, "cables");
	// In <=v0.6, cables were called wires
	if (!cablesJ)
		cablesJ = json_object_get(rootJ, "wires");
	if (!cablesJ)
		return;
	size_t cableIndex;
	json_t* cableJ;
	json_array_foreach(cablesJ, cableIndex, cableJ) {
		// Get cable ID
		json_t* idJ = json_object_get(cableJ, "id");
		int64_t id;
		// In <=v0.6, the cable ID was the index in the array.
		if (idJ)
			id = json_integer_value(idJ);
		else
			id = cableIndex;

		// Get Cable
		engine::Cable* cable = APP->engine->getCable(id);
		if (!cable) {
			WARN("Cannot find Cable %lld", (long long) id);
			continue;
		}

		// Create CableWidget
		CableWidget* cw = new CableWidget;
		try {
			cw->setCable(cable);
			cw->fromJson(cableJ);
		}
		catch (Exception& e) {
			delete cw;
			// If creating CableWidget fails, remove Cable from Engine.
			APP->engine->removeCable(cable);
			delete cable;
			continue;
		}
		addCable(cw);

		// inputPlugOrder
		json_t* inputPlugOrderJ = json_object_get(cableJ, "inputPlugOrder");
		if (inputPlugOrderJ) {
			plugOrders[cw->inputPlug] = json_integer_value(inputPlugOrderJ);
		}

		// outputPlugOrder
		json_t* outputPlugOrderJ = json_object_get(cableJ, "outputPlugOrder");
		if (outputPlugOrderJ) {
			plugOrders[cw->outputPlug] = json_integer_value(outputPlugOrderJ);
		}
	}

	// Reorder plugs, approximately O(n log(n) log(n))
	internal->plugContainer->children.sort([&](Widget* w1, Widget* w2) {
		return get(plugOrders, w1, 0) < get(plugOrders, w2, 0);
	});
}

struct PasteJsonResult {
	/** Old module ID -> new module */
	std::map<int64_t, ModuleWidget*> newModules;
};
static PasteJsonResult RackWidget_pasteJson(RackWidget* that, json_t* rootJ, history::ComplexAction* complexAction) {
	that->deselectAll();

	std::map<int64_t, ModuleWidget*> newModules;
	math::Vec minPos(INFINITY, INFINITY);
	math::Vec maxPos(-INFINITY, -INFINITY);

	// modules
	json_t* modulesJ = json_object_get(rootJ, "modules");
	if (!modulesJ)
		return {};

	size_t moduleIndex;
	json_t* moduleJ;
	json_array_foreach(modulesJ, moduleIndex, moduleJ) {
		json_t* idJ = json_object_get(moduleJ, "id");
		if (!idJ)
			continue;
		int64_t id = json_integer_value(idJ);

		engine::Module::jsonStripIds(moduleJ);

		ModuleWidget* mw;
		try {
			mw = moduleWidgetFromJson(moduleJ);
		}
		catch (Exception& e) {
			WARN("%s", e.what());
			continue;
		}
		assert(mw);
		assert(mw->module);

		APP->engine->addModule(mw->module);

		// pos
		json_t* posJ = json_object_get(moduleJ, "pos");
		double x = 0.0, y = 0.0;
		json_unpack(posJ, "[F, F]", &x, &y);
		math::Vec pos = math::Vec(x, y);
		mw->box.pos = pos * RACK_GRID_SIZE + RACK_OFFSET;
		minPos = minPos.min(mw->box.getTopLeft());
		maxPos = maxPos.max(mw->box.getBottomRight());

		that->internal->moduleContainer->addChild(mw);
		that->select(mw);

		newModules[id] = mw;
	}

	// Adjust center of selection to appear at center of rack view
	math::Vec selectionCenter = (minPos + maxPos) / 2;
	math::Vec mousePos = that->internal->mousePos;
	math::Vec deltaPos = ((mousePos - selectionCenter) / RACK_GRID_SIZE).round() * RACK_GRID_SIZE;

	for (auto pair : newModules) {
		ModuleWidget* mw = pair.second;
		mw->box.pos += deltaPos;
	}

	// This calls updateExpanders()
	that->setSelectionPosNearest(math::Vec(0, 0));

	// Rack 3D: if the selection doesn't fit in the racks, paste none of it. Clearing newModules also skips its cables.
	bool stray = false;
	for (auto pair : newModules)
		stray = stray || rack3d::isStray(pair.second);
	if (stray) {
		INFO("No room in any rack for the pasted selection, removing it");
		for (auto pair : newModules) {
			that->removeModule(pair.second);
			delete pair.second;
		}
		newModules.clear();
		rack3d::warnNoRoom();
		return {newModules};
	}

	// Add positioned selected modules to history
	for (ModuleWidget* mw : that->getSelected()) {
		// history::ModuleAdd
		history::ModuleAdd* h = new history::ModuleAdd;
		h->setModule(mw);
		complexAction->push(h);
	}

	// cables
	json_t* cablesJ = json_object_get(rootJ, "cables");
	if (cablesJ) {
		size_t cableIndex;
		json_t* cableJ;
		json_array_foreach(cablesJ, cableIndex, cableJ) {
			engine::Cable::jsonStripIds(cableJ);

			// Overwrite old module IDs with new module IDs
			json_t* inputModuleIdJ = json_object_get(cableJ, "inputModuleId");
			if (!inputModuleIdJ)
				continue;
			int64_t inputModuleId = json_integer_value(inputModuleIdJ);
			auto inputModuleIdIt = newModules.find(inputModuleId);
			if (inputModuleIdIt == newModules.end())
				continue;
			inputModuleId = inputModuleIdIt->second->module->id;
			json_object_set_new(cableJ, "inputModuleId", json_integer(inputModuleId));

			json_t* outputModuleIdJ = json_object_get(cableJ, "outputModuleId");
			if (!outputModuleIdJ)
				continue;
			int64_t outputModuleId = json_integer_value(outputModuleIdJ);
			auto outputModuleIdIt = newModules.find(outputModuleId);
			if (outputModuleIdIt == newModules.end())
				continue;
			outputModuleId = outputModuleIdIt->second->module->id;
			json_object_set_new(cableJ, "outputModuleId", json_integer(outputModuleId));

			// Create Cable
			engine::Cable* cable = new engine::Cable;
			try {
				cable->fromJson(cableJ);
				APP->engine->addCable(cable);
			}
			catch (Exception& e) {
				WARN("Cannot paste cable: %s", e.what());
				delete cable;
				continue;
			}

			// Create CableWidget
			app::CableWidget* cw = new app::CableWidget;
			cw->setCable(cable);
			cw->fromJson(cableJ);
			that->addCable(cw);

			// history::CableAdd
			history::CableAdd* h = new history::CableAdd;
			h->setCable(cw);
			complexAction->push(h);
		}
	}

	return {newModules};
}

void RackWidget::pasteJsonAction(json_t* rootJ) {
	history::ComplexAction* complexAction = new history::ComplexAction;
	complexAction->name = string::translate("RackWidget.history.pasteModules");
	DEFER({
		if (!complexAction->isEmpty())
			APP->history->push(complexAction);
		else
			delete complexAction;
	});

	RackWidget_pasteJson(this, rootJ, complexAction);
}

void RackWidget::pasteModuleJsonAction(json_t* moduleJ) {
	engine::Module::jsonStripIds(moduleJ);

	ModuleWidget* mw;
	try {
		mw = moduleWidgetFromJson(moduleJ);
	}
	catch (Exception& e) {
		WARN("%s", e.what());
		return;
	}
	assert(mw);
	assert(mw->module);

	history::ComplexAction* h = new history::ComplexAction;
	h->name = string::translate("RackWidget.history.pasteModule");

	APP->engine->addModule(mw->module);

	updateModuleOldPositions();
	addModuleAtMouse(mw);
	// Rack 3D: no room in any rack
	if (rack3d::rejectIfStray(mw)) {
		delete h;
		return;
	}
	h->push(getModuleDragAction());

	// history::ModuleAdd
	history::ModuleAdd* ha = new history::ModuleAdd;
	ha->setModule(mw);
	h->push(ha);

	APP->history->push(h);
}

void RackWidget::pasteClipboardAction() {
	const char* json = glfwGetClipboardString(APP->window->win);
	if (!json) {
		WARN("Could not get text from clipboard.");
		return;
	}

	json_error_t error;
	json_t* rootJ = json_loads(json, 0, &error);
	if (!rootJ) {
		WARN("JSON parsing error at %s %d:%d %s", error.source, error.line, error.column, error.text);
		return;
	}
	DEFER({json_decref(rootJ);});

	json_t* modulesJ = json_object_get(rootJ, "modules");
	if (modulesJ) {
		// JSON is a selection of modules
		pasteJsonAction(rootJ);
	}
	else {
		// JSON is a single module
		pasteModuleJsonAction(rootJ);
	}
}

void RackWidget::addModule(ModuleWidget* m) {
	assert(m);

	// Module must be 3U high and at least 1HP wide
	if (m->box.size.x < RACK_GRID_WIDTH / 2)
		throw Exception("Module %s width is %g px, must be at least %g px", m->model->getFullName().c_str(), m->box.size.x, RACK_GRID_WIDTH);

	if (m->box.size.y != RACK_GRID_HEIGHT)
		throw Exception("Module %s height is %g px, must be %g px", m->model->getFullName().c_str(), m->box.size.y, RACK_GRID_HEIGHT);

	internal->moduleContainer->addChild(m);

	updateExpanders();
}

void RackWidget::addModuleAtMouse(ModuleWidget* mw) {
	assert(mw);
	// Move module nearest to the mouse position
	math::Vec pos = internal->mousePos.minus(mw->box.size.div(2));

	if (settings::squeezeModules)
		setModulePosSqueeze(mw, pos);
	else
		setModulePosNearest(mw, pos);

	addModule(mw);
}

void RackWidget::removeModule(ModuleWidget* m) {
	// Unset touchedParamWidget
	if (touchedParam) {
		ModuleWidget* touchedModule = touchedParam->getAncestorOfType<ModuleWidget>();
		if (touchedModule == m)
			touchedParam = NULL;
	}

	// Disconnect cables
	m->disconnect();

	// Deselect module if selected
	internal->selectedModules.erase(m);

	// Remove module from ModuleContainer
	internal->moduleContainer->removeChild(m);

	updateExpanders();
}

ModuleWidget* RackWidget::getModule(int64_t moduleId) {
	for (widget::Widget* w : internal->moduleContainer->children) {
		ModuleWidget* mw = dynamic_cast<ModuleWidget*>(w);
		assert(mw);
		if (mw->module->id == moduleId)
			return mw;
	}
	return NULL;
}

std::vector<ModuleWidget*> RackWidget::getModules() {
	std::vector<ModuleWidget*> mws;
	mws.reserve(internal->moduleContainer->children.size());
	for (widget::Widget* w : internal->moduleContainer->children) {
		ModuleWidget* mw = dynamic_cast<ModuleWidget*>(w);
		assert(mw);
		mws.push_back(mw);
	}
	return mws;
}

bool RackWidget::hasModules() {
	return internal->moduleContainer->children.empty();
}

/** Rack 3D: whether a module box (rack px) may sit here, i.e. lies entirely inside one placement.
Always true while placements are off.
*/
static bool isPlaceable(rack3d::Layout& layout, math::Rect box) {
	if (!rack3d::settings::placementsEnabled)
		return true;
	math::Rect gridBox(((box.pos - RACK_OFFSET) / RACK_GRID_SIZE).round(), (box.size / RACK_GRID_SIZE).round());
	return layout.placementOf(gridBox) != NULL;
}

/** Rack 3D: snapshot of module positions taken before a move that shoves neighbours (force, squeeze),
so the move can be undone if it pushed any module out of its placement. Does nothing while placements are off.
*/
struct ShoveGuard {
	RackWidget* rack;
	bool active;
	std::vector<std::pair<widget::Widget*, math::Vec>> positions;

	ShoveGuard(RackWidget* rack) : rack(rack), active(rack3d::settings::placementsEnabled) {
		if (!active)
			return;
		for (widget::Widget* w : rack->internal->moduleContainer->children)
			positions.push_back(std::make_pair(w, w->box.pos));
	}

	/** Whether `mw` and every module that moved are inside a placement. */
	bool isValid(ModuleWidget* mw) {
		if (!active)
			return true;
		rack3d::Layout& layout = *rack->getLayout();
		if (!isPlaceable(layout, mw->box))
			return false;
		for (const auto& pair : positions) {
			widget::Widget* w = pair.first;
			if (!w->box.pos.equals(pair.second) && !isPlaceable(layout, w->box))
				return false;
		}
		return true;
	}

	void restore() {
		for (const auto& pair : positions)
			pair.first->box.pos = pair.second;
	}
};

bool RackWidget::requestModulePos(ModuleWidget* mw, math::Vec pos) {
	math::Rect mwBox = math::Rect(pos, mw->box.size);
	if (!isPlaceable(internal->layout, mwBox))
		return false;

	// Check intersection with other modules
	for (widget::Widget* w2 : internal->moduleContainer->children) {
		// Don't intersect with self
		if (mw == w2)
			continue;
		// Check intersection
		math::Rect w2Box = w2->box;
		if (mwBox.intersects(w2Box))
			return false;
	}

	// Accept requested position
	mw->setPosition(mwBox.pos);
	updateExpanders();
	return true;
}

static math::Vec eachNearestGridPos(math::Vec pos, std::function<bool(math::Vec pos)> f) {
	math::Vec leftPos = (pos / RACK_GRID_SIZE).round();
	math::Vec rightPos = leftPos + math::Vec(1, 0);

	while (true) {
		if (f(leftPos * RACK_GRID_SIZE))
			return leftPos * RACK_GRID_SIZE;
		leftPos.x -= 1;

		if (f(rightPos * RACK_GRID_SIZE))
			return rightPos * RACK_GRID_SIZE;
		rightPos.x += 1;
	}

	assert(false);
	return math::Vec();
}

/** Rack 3D: like eachNearestGridPos(), but for placements mode, where most of the grid is off-limits.
Calls f on grid-aligned rack positions, first the one nearest `pos`, then every cell of the layout's bounding box
ordered by distance in rack px. Unlike eachNearestGridPos() it always ends: returns the first position f accepts,
or a non-finite Vec if f rejects them all.
*/
static math::Vec eachNearestLayoutPos(rack3d::Layout& layout, math::Vec pos, std::function<bool(math::Vec pos)> f) {
	math::Vec start = ((pos - RACK_OFFSET) / RACK_GRID_SIZE).round() * RACK_GRID_SIZE + RACK_OFFSET;
	if (f(start))
		return start;

	if (layout.placements.empty())
		return math::Vec(NAN, NAN);
	math::Rect bounds = layout.placements[0].gridRect;
	for (const rack3d::Placement& p : layout.placements)
		bounds = bounds.expand(p.gridRect);

	std::vector<math::Vec> candidates;
	for (int y = (int) bounds.getTop(); y < (int) bounds.getBottom(); y++) {
		for (int x = (int) bounds.getLeft(); x < (int) bounds.getRight(); x++) {
			candidates.push_back(math::Vec(x, y) * RACK_GRID_SIZE + RACK_OFFSET);
		}
	}
	std::stable_sort(candidates.begin(), candidates.end(), [&](math::Vec a, math::Vec b) {
		return (a - pos).square() < (b - pos).square();
	});
	for (math::Vec c : candidates) {
		if (f(c))
			return c;
	}
	return math::Vec(NAN, NAN);
}

void RackWidget::setModulePosNearest(ModuleWidget* mw, math::Vec pos) {
	if (rack3d::settings::placementsEnabled) {
		// If nothing fits, the module stays where it is
		eachNearestLayoutPos(internal->layout, pos, [&](math::Vec pos) -> bool {
			return requestModulePos(mw, pos);
		});
		return;
	}

	eachNearestGridPos(pos, [&](math::Vec pos) -> bool {
		return requestModulePos(mw, pos);
	});
}

static bool compareModuleLeft(ModuleWidget* a, ModuleWidget* b) {
	return a->getGridBox().getLeft() < b->getGridBox().getLeft();
}

void RackWidget::setModulePosForce(ModuleWidget* mw, math::Vec pos) {
	ShoveGuard guard(this);

	math::Rect mwBox;
	mwBox.pos = ((pos - RACK_OFFSET) / RACK_GRID_SIZE).round();
	mwBox.size = mw->getGridSize();

	// Collect modules to the left and right of new pos
	std::set<ModuleWidget*, decltype(compareModuleLeft)*> leftModules(compareModuleLeft);
	std::set<ModuleWidget*, decltype(compareModuleLeft)*> rightModules(compareModuleLeft);
	for (widget::Widget* w2 : internal->moduleContainer->children) {
		ModuleWidget* mw2 = (ModuleWidget*) w2;
		// Skip this module
		if (mw2 == mw)
			continue;
		// Modules must be on the same row as pos
		if (mw2->getGridBox().getTop() != mwBox.getTop())
			continue;
		// Insert into leftModules or rightModules
		if (mw2->getGridBox().getLeft() >= mwBox.getLeft())
			rightModules.insert(mw2);
		else
			leftModules.insert(mw2);
	}

	// Set module position
	mw->setGridPosition(mwBox.pos);

	// Shove left modules
	math::Vec cursor = mwBox.getTopLeft();
	for (auto it = leftModules.rbegin(); it != leftModules.rend(); it++) {
		ModuleWidget* mw2 = (ModuleWidget*) *it;
		math::Rect mw2Box = mw2->getGridBox();

		if (mw2Box.getRight() <= cursor.x)
			break;

		mw2Box.pos.x = cursor.x - mw2Box.size.x;
		mw2->setGridPosition(mw2Box.pos);
		cursor.x = mw2Box.getLeft();
	}

	// Shove right modules
	cursor = mwBox.getTopRight();
	for (auto it = rightModules.begin(); it != rightModules.end(); it++) {
		ModuleWidget* mw2 = (ModuleWidget*) *it;
		math::Rect mw2Box = mw2->getGridBox();

		if (mw2Box.getLeft() >= cursor.x)
			break;

		mw2Box.pos.x = cursor.x;
		mw2->setGridPosition(mw2Box.pos);
		cursor.x = mw2Box.getRight();
	}

	// Rack 3D: never shove a module out of its placement
	if (!guard.isValid(mw)) {
		guard.restore();
		setModulePosNearest(mw, pos);
	}

	updateExpanders();
}

void RackWidget::setModulePosSqueeze(ModuleWidget* mw, math::Vec pos) {
	// Reset modules to their old positions, including this module
	for (auto& pair : internal->moduleOldPositions) {
		widget::Widget* w2 = pair.first;
		w2->box.pos = pair.second;
	}

	unsqueezeModulePos(mw);
	squeezeModulePos(mw, pos);

	updateExpanders();
}

void RackWidget::squeezeModulePos(ModuleWidget* mw, math::Vec pos) {
	ShoveGuard guard(this);

	math::Rect mwBox;
	mwBox.pos = ((pos - RACK_OFFSET) / RACK_GRID_SIZE).round();
	mwBox.size = mw->getGridSize();

	// Collect modules to the left and right of new pos
	std::set<ModuleWidget*, decltype(compareModuleLeft)*> leftModules(compareModuleLeft);
	std::set<ModuleWidget*, decltype(compareModuleLeft)*> rightModules(compareModuleLeft);
	for (widget::Widget* w2 : internal->moduleContainer->children) {
		ModuleWidget* mw2 = static_cast<ModuleWidget*>(w2);
		// Skip this module
		if (mw2 == mw)
			continue;
		// Modules must be on the same row as pos
		if (mw2->getGridBox().getTop() != mwBox.getTop())
			continue;
		// Insert into leftModules or rightModules
		if (mw2->getGridBox().getLeft() >= mwBox.getLeft())
			rightModules.insert(mw2);
		else
			leftModules.insert(mw2);
	}

	ModuleWidget* leftModule = leftModules.empty() ? NULL : *leftModules.rbegin();
	ModuleWidget* rightModule = rightModules.empty() ? NULL : *rightModules.begin();

	// If there isn't enough space between the last leftModule and first rightModule, place module to the right of the leftModule and shove right modules.
	if (leftModule && rightModule && leftModule->getGridBox().getRight() + mwBox.getWidth() > rightModule->getGridBox().getLeft()) {
		mwBox.pos.x = leftModule->getGridBox().getRight();

		// Shove right modules
		float xRight = mwBox.getRight();
		for (auto it = rightModules.begin(); it != rightModules.end(); it++) {
			widget::Widget* w2 = *it;
			ModuleWidget* mw2 = static_cast<ModuleWidget*>(w2);
			math::Rect mw2Box = mw2->getGridBox();
			// Break when module no longer needs to be shoved
			if (mw2Box.getLeft() >= xRight)
				break;
			// Shove module to the right of the last module
			math::Rect newBox = mw2Box;
			newBox.pos.x = xRight;
			mw2->setGridPosition(newBox.pos);
			xRight = newBox.getRight();
		}
	}
	// Place right of leftModule
	else if (leftModule && leftModule->getGridBox().getRight() > mwBox.getLeft()) {
		mwBox.pos.x = leftModule->getGridBox().getRight();
	}
	// Place left of rightModule
	else if (rightModule && rightModule->getGridBox().getLeft() < mwBox.getRight()) {
		mwBox.pos.x = rightModule->getGridBox().getLeft() - mwBox.getWidth();
	}

	// Commit new pos
	mw->setGridPosition(mwBox.pos);

	// Rack 3D: never shove a module out of its placement
	if (!guard.isValid(mw)) {
		guard.restore();
		setModulePosNearest(mw, pos);
	}
}

void RackWidget::unsqueezeModulePos(ModuleWidget* mw) {
	math::Rect mwBox = mw->getGridBox();

	// Collect modules to the left and right of old pos, including this module.
	std::set<ModuleWidget*, decltype(compareModuleLeft)*> leftModules(compareModuleLeft);
	std::set<ModuleWidget*, decltype(compareModuleLeft)*> rightModules(compareModuleLeft);
	for (widget::Widget* w2 : internal->moduleContainer->children) {
		ModuleWidget* mw2 = static_cast<ModuleWidget*>(w2);
		// Skip this module
		if (mw2 == mw)
			continue;
		// Modules must be on the same row as pos
		if (mw2->getGridBox().getTop() != mwBox.getTop())
			continue;
		// Insert into leftModules or rightModules
		if (mw2->getGridBox().getLeft() >= mwBox.getLeft())
			rightModules.insert(mw2);
		else
			leftModules.insert(mw2);
	}

	// Immediate right/left modules
	ModuleWidget* leftModule = leftModules.empty() ? NULL : *leftModules.rbegin();
	ModuleWidget* rightModule = rightModules.empty() ? NULL : *rightModules.begin();

	// Shove right modules back to empty space left by module.
	if (leftModule && rightModule && (leftModule != mw) && (rightModule != mw) && leftModule->getGridBox().getRight() >= mwBox.getLeft() && rightModule->getGridBox().getLeft() <= mwBox.getRight()) {
		float xLeft = mwBox.getLeft();
		float xRight = mwBox.getRight();
		for (auto it = rightModules.begin(); it != rightModules.end(); it++) {
			widget::Widget* w2 = *it;
			ModuleWidget* mw2 = static_cast<ModuleWidget*>(w2);
			math::Rect mw2Box = mw2->getGridBox();
			// Break when module is no longer touching
			if (xRight < mw2Box.getLeft())
				break;
			// Shove module to the left
			math::Rect newBox = mw2Box;
			newBox.pos.x = xLeft;
			mw2->setGridPosition(newBox.pos);
			xLeft = newBox.getRight();
			xRight = mw2Box.getRight();
		}
	}
}

void RackWidget::updateModuleOldPositions() {
	internal->moduleOldPositions.clear();
	for (ModuleWidget* mw : getModules()) {
		internal->moduleOldPositions[mw] = mw->getPosition();
	}
}

history::ComplexAction* RackWidget::getModuleDragAction() {
	history::ComplexAction* h = new history::ComplexAction;
	h->name = string::translate("RackWidget.history.moveModules");

	for (ModuleWidget* mw : getModules()) {
		// Create ModuleMove action if the module was moved.
		auto it = internal->moduleOldPositions.find(mw);
		if (it == internal->moduleOldPositions.end())
			continue;
		math::Vec oldPos = it->second;
		if (!oldPos.equals(mw->box.pos)) {
			history::ModuleMove* mmh = new history::ModuleMove;
			mmh->moduleId = mw->module->id;
			mmh->oldPos = oldPos;
			mmh->newPos = mw->box.pos;
			h->push(mmh);
		}
	}

	return h;
}

void RackWidget::updateSelectionFromRect() {
	math::Rect selectionBox = math::Rect::fromCorners(internal->selectionStart, internal->selectionEnd);
	deselectAll();
	for (ModuleWidget* mw : getModules()) {
		bool selected = internal->selecting && selectionBox.intersects(mw->box);
		if (selected)
			select(mw);
	}
}

void RackWidget::selectAll() {
	internal->selectedModules.clear();
	for (widget::Widget* w : internal->moduleContainer->children) {
		ModuleWidget* mw = dynamic_cast<ModuleWidget*>(w);
		assert(mw);
		internal->selectedModules.insert(mw);
	}
}

void RackWidget::deselectAll() {
	internal->selectedModules.clear();
}

void RackWidget::select(ModuleWidget* mw, bool selected) {
	if (selected) {
		internal->selectedModules.insert(mw);
	}
	else {
		auto it = internal->selectedModules.find(mw);
		if (it != internal->selectedModules.end())
			internal->selectedModules.erase(it);
	}
}

bool RackWidget::hasSelection() {
	return !internal->selectedModules.empty();
}

const std::set<ModuleWidget*>& RackWidget::getSelected() {
	return internal->selectedModules;
}

bool RackWidget::isSelected(ModuleWidget* mw) {
	auto it = internal->selectedModules.find(mw);
	return (it != internal->selectedModules.end());
}

json_t* RackWidget::selectionToJson(bool cables) {
	json_t* rootJ = json_object();

	std::set<engine::Module*> modules;

	// modules
	json_t* modulesJ = json_array();
	for (ModuleWidget* mw : getSelected()) {
		json_t* moduleJ = mw->toJson();

		// pos
		math::Vec pos = mw->box.pos.minus(RACK_OFFSET);
		pos = pos.div(RACK_GRID_SIZE).round();
		json_t* posJ = json_pack("[i, i]", (int) pos.x, (int) pos.y);
		json_object_set_new(moduleJ, "pos", posJ);

		json_array_append_new(modulesJ, moduleJ);
		modules.insert(mw->getModule());
	}
	json_object_set_new(rootJ, "modules", modulesJ);

	if (cables) {
		// cables
		json_t* cablesJ = json_array();
		// Only add complete cables to JSON
		for (CableWidget* cw : getCompleteCables()) {
			engine::Cable* cable = cw->getCable();
			if (!cable || !cable->inputModule || !cable->outputModule)
				continue;
			const auto inputIt = modules.find(cable->inputModule);
			if (inputIt == modules.end())
				continue;
			const auto outputIt = modules.find(cable->outputModule);
			if (outputIt == modules.end())
				continue;

			json_t* cableJ = cable->toJson();
			cw->mergeJson(cableJ);

			json_array_append_new(cablesJ, cableJ);
		}
		json_object_set_new(rootJ, "cables", cablesJ);
	}

	return rootJ;
}

static const char SELECTION_FILTERS[] = "VCV Rack module selection (.vcvs):vcvs";

void RackWidget::loadSelection(std::string path) {
	FILE* file = std::fopen(path.c_str(), "r");
	if (!file)
		throw Exception("Could not load selection file %s", path.c_str());
	DEFER({std::fclose(file);});

	INFO("Loading selection %s", path.c_str());

	json_error_t error;
	json_t* rootJ = json_loadf(file, 0, &error);
	if (!rootJ)
		throw Exception("File is not a valid selection file. JSON parsing error at %s %d:%d %s", error.source, error.line, error.column, error.text);
	DEFER({json_decref(rootJ);});

	// Set mouse position to center of rack viewport, so selection is placed in view.
	internal->mousePos = getViewport().getCenter();

	pasteJsonAction(rootJ);
}

void RackWidget::loadSelectionDialog() {
	std::string dir = settings::lastSelectionDirectory;

	// Use fallback <Rack user dir>/selections
	if (dir == "" || !system::isDirectory(dir)) {
		dir = asset::user("selections");
		system::createDirectory(dir);
	}

	osdialog_filters* filters = osdialog_filters_parse(SELECTION_FILTERS);
	DEFER({osdialog_filters_free(filters);});

	char* pathC = osdialog_file(OSDIALOG_OPEN, dir.c_str(), NULL, filters);
	if (!pathC) {
		// Cancel silently
		return;
	}
	std::string path = pathC;
	std::free(pathC);

	try {
		loadSelection(path);
	}
	catch (Exception& e) {
		osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, e.what());
	}

	settings::lastSelectionDirectory = system::getDirectory(path);
}

void RackWidget::saveSelection(std::string path) {
	INFO("Saving selection %s", path.c_str());

	json_t* rootJ = selectionToJson();
	assert(rootJ);
	DEFER({json_decref(rootJ);});

	engine::Module::jsonStripIds(rootJ);

	FILE* file = std::fopen(path.c_str(), "w");
	if (!file) {
		std::string message = string::f(string::translate("RackWidget.saveSelectionFailed"), path);
		osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, message.c_str());
		return;
	}
	DEFER({std::fclose(file);});

	json_dumpf(rootJ, file, JSON_INDENT(2));
}

void RackWidget::saveSelectionDialog() {
	std::string dir = settings::lastSelectionDirectory;

	// Use fallback <Rack user dir>/selections
	if (dir == "" || !system::isDirectory(dir)) {
		dir = asset::user("selections");
		system::createDirectory(dir);
	}

	osdialog_filters* filters = osdialog_filters_parse(SELECTION_FILTERS);
	DEFER({osdialog_filters_free(filters);});

	char* pathC = osdialog_file(OSDIALOG_SAVE, dir.c_str(), "Untitled.vcvs", filters);
	if (!pathC) {
		// No path selected
		return;
	}
	std::string path = pathC;
	std::free(pathC);

	// Automatically append .vcvs extension
	if (system::getExtension(path) != ".vcvs")
		path += ".vcvs";

	saveSelection(path);

	settings::lastSelectionDirectory = system::getDirectory(path);
}

void RackWidget::copyClipboardSelection() {
	json_t* rootJ = selectionToJson();
	DEFER({json_decref(rootJ);});
	char* moduleJson = json_dumps(rootJ, JSON_INDENT(2));
	DEFER({std::free(moduleJson);});
	glfwSetClipboardString(APP->window->win, moduleJson);
}

void RackWidget::resetSelectionAction() {
	history::ComplexAction* complexAction = new history::ComplexAction;
	complexAction->name = string::translate("RackWidget.history.resetModules");

	for (ModuleWidget* mw : getSelected()) {
		assert(mw->module);

		// history::ModuleChange
		history::ModuleChange* h = new history::ModuleChange;
		h->moduleId = mw->module->id;
		h->oldModuleJ = mw->toJson();

		APP->engine->resetModule(mw->module);

		h->newModuleJ = mw->toJson();
		complexAction->push(h);
	}

	APP->history->push(complexAction);
}

void RackWidget::randomizeSelectionAction() {
	history::ComplexAction* complexAction = new history::ComplexAction;
	complexAction->name = string::translate("RackWidget.history.randomizeModules");

	for (ModuleWidget* mw : getSelected()) {
		assert(mw->module);

		// history::ModuleChange
		history::ModuleChange* h = new history::ModuleChange;
		h->moduleId = mw->module->id;
		h->oldModuleJ = mw->toJson();

		APP->engine->randomizeModule(mw->module);

		h->newModuleJ = mw->toJson();
		complexAction->push(h);
	}

	APP->history->push(complexAction);
}

void RackWidget::disconnectSelectionAction() {
	history::ComplexAction* complexAction = new history::ComplexAction;
	complexAction->name = string::translate("RackWidget.history.disconnectCables");

	for (ModuleWidget* mw : getSelected()) {
		mw->appendDisconnectActions(complexAction);
	}

	if (!complexAction->isEmpty())
		APP->history->push(complexAction);
	else
		delete complexAction;
}

void RackWidget::cloneSelectionAction(bool cloneCables) {
	json_t* rootJ = selectionToJson(cloneCables);
	DEFER({json_decref(rootJ);});

	history::ComplexAction* complexAction = new history::ComplexAction;
	complexAction->name = string::translate("RackWidget.history.duplicateModules");
	DEFER({
		if (!complexAction->isEmpty())
			APP->history->push(complexAction);
		else
			delete complexAction;
	});

	auto p = RackWidget_pasteJson(this, rootJ, complexAction);

	// Clone cables attached to inputs of selected modules but outputs of non-selected modules
	if (cloneCables) {
		for (CableWidget* cw : getCompleteCables()) {
			auto inputIt = p.newModules.find(cw->getCable()->inputModule->id);
			if (inputIt == p.newModules.end())
				continue;

			auto outputIt = p.newModules.find(cw->getCable()->outputModule->id);
			if (outputIt != p.newModules.end())
				continue;

			engine::Module* clonedInputModule = inputIt->second->module;

			// Create cable attached to cloned ModuleWidget's input
			engine::Cable* clonedCable = new engine::Cable;
			clonedCable->inputModule = clonedInputModule;
			clonedCable->inputId = cw->cable->inputId;
			clonedCable->outputModule = cw->cable->outputModule;
			clonedCable->outputId = cw->cable->outputId;
			APP->engine->addCable(clonedCable);

			app::CableWidget* clonedCw = new app::CableWidget;
			clonedCw->setCable(clonedCable);
			clonedCw->color = cw->color;
			APP->scene->rack->addCable(clonedCw);

			// history::CableAdd
			history::CableAdd* hca = new history::CableAdd;
			hca->setCable(clonedCw);
			complexAction->push(hca);
		}
	}
}

void RackWidget::bypassSelectionAction(bool bypassed) {
	history::ComplexAction* complexAction = new history::ComplexAction;
	complexAction->name = bypassed ? string::translate("RackWidget.history.bypassModules") : string::translate("RackWidget.history.unbypassModules");

	for (ModuleWidget* mw : getSelected()) {
		assert(mw->module);
		if (mw->module->isBypassed() == bypassed)
			continue;

		// history::ModuleBypass
		history::ModuleBypass* h = new history::ModuleBypass;
		h->moduleId = mw->module->id;
		h->bypassed = bypassed;
		complexAction->push(h);

		APP->engine->bypassModule(mw->module, bypassed);
	}

	if (!complexAction->isEmpty())
		APP->history->push(complexAction);
	else
		delete complexAction;
}

bool RackWidget::isSelectionBypassed() {
	for (ModuleWidget* mw : getSelected()) {
		if (!mw->getModule()->isBypassed())
			return false;
	}
	return true;
}

void RackWidget::deleteSelectionAction() {
	history::ComplexAction* complexAction = new history::ComplexAction;
	complexAction->name = string::translate("RackWidget.history.deleteModules");

	// Copy selected set since removing ModuleWidgets modifies it.
	std::set<ModuleWidget*> selectedModules = getSelected();
	for (ModuleWidget* mw : selectedModules) {
		mw->appendDisconnectActions(complexAction);

		// history::ModuleRemove
		history::ModuleRemove* moduleRemove = new history::ModuleRemove;
		moduleRemove->setModule(mw);
		complexAction->push(moduleRemove);

		removeModule(mw);
		delete mw;
	}

	APP->history->push(complexAction);
}

bool RackWidget::requestSelectionPos(math::Vec delta) {
	// Calculate new positions
	std::map<widget::Widget*, math::Rect> mwBoxes;
	for (ModuleWidget* mw : getSelected()) {
		math::Rect mwBox = mw->box;
		mwBox.pos += delta;
		if (!isPlaceable(internal->layout, mwBox))
			return false;
		mwBoxes[mw] = mwBox;
	}

	// Check intersection with other modules
	for (widget::Widget* w2 : internal->moduleContainer->children) {
		// Don't intersect with selected modules
		auto it = mwBoxes.find(w2);
		if (it != mwBoxes.end())
			continue;
		math::Rect w2Box = w2->box;
		// Check intersection with all selected modules
		for (const auto& pair : mwBoxes) {
			if (pair.second.intersects(w2Box))
				return false;
		}
	}

	// Accept requested position
	for (const auto& pair : mwBoxes) {
		pair.first->setPosition(pair.second.pos);
	}
	updateExpanders();
	return true;
}

void RackWidget::setSelectionPosNearest(math::Vec delta) {
	std::set<ModuleWidget*> selected = getSelected();
	if (rack3d::settings::placementsEnabled && !selected.empty()) {
		// Search over where the selection's top-left corner lands, so the search covers the layout's cells.
		// If nothing fits, the selection stays where it is.
		math::Vec anchor(INFINITY, INFINITY);
		for (ModuleWidget* mw : selected)
			anchor = anchor.min(mw->box.pos);
		eachNearestLayoutPos(internal->layout, anchor + delta, [&](math::Vec pos) -> bool {
			return requestSelectionPos(pos - anchor);
		});
		return;
	}

	eachNearestGridPos(delta, [&](math::Vec delta) -> bool {
		return requestSelectionPos(delta);
	});
}

void RackWidget::appendSelectionContextMenu(ui::Menu* menu) {
	int n = getSelected().size();
	menu->addChild(createMenuLabel(n == 1 ? string::translate("RackWidget.selectedOne") : string::f(string::translate("RackWidget.selectedMany"), n)));

	// Enable alwaysConsume of menu items if the number of selected modules changes

	// Select all
	menu->addChild(createMenuItem(string::translate("RackWidget.selectAll"), widget::getKeyCommandName(GLFW_KEY_A, RACK_MOD_CTRL), [=]() {
		selectAll();
	}, false, true));

	// Deselect
	menu->addChild(createMenuItem(string::translate("RackWidget.deselect"), widget::getKeyCommandName(GLFW_KEY_A, RACK_MOD_CTRL | GLFW_MOD_SHIFT), [=]() {
		deselectAll();
	}, n == 0, true));

	// Copy
	menu->addChild(createMenuItem(string::translate("RackWidget.copy"), widget::getKeyCommandName(GLFW_KEY_C, RACK_MOD_CTRL), [=]() {
		copyClipboardSelection();
	}, n == 0));

	// Paste
	menu->addChild(createMenuItem(string::translate("RackWidget.paste"), widget::getKeyCommandName(GLFW_KEY_V, RACK_MOD_CTRL), [=]() {
		pasteClipboardAction();
	}, false, true));

	// Save
	menu->addChild(createMenuItem(string::translate("RackWidget.saveAs"), "", [=]() {
		saveSelectionDialog();
	}, n == 0));

	// Initialize
	menu->addChild(createMenuItem(string::translate("RackWidget.initialize"), widget::getKeyCommandName(GLFW_KEY_I, RACK_MOD_CTRL), [=]() {
		resetSelectionAction();
	}, n == 0));

	// Randomize
	menu->addChild(createMenuItem(string::translate("RackWidget.randomize"), widget::getKeyCommandName(GLFW_KEY_R, RACK_MOD_CTRL), [=]() {
		randomizeSelectionAction();
	}, n == 0));

	// Disconnect cables
	menu->addChild(createMenuItem(string::translate("RackWidget.disconnectCables"), widget::getKeyCommandName(GLFW_KEY_U, RACK_MOD_CTRL), [=]() {
		disconnectSelectionAction();
	}, n == 0));

	// Bypass
	std::string bypassText = widget::getKeyCommandName(GLFW_KEY_E, RACK_MOD_CTRL);
	bool bypassed = (n > 0) && isSelectionBypassed();
	if (bypassed)
		bypassText += " " CHECKMARK_STRING;
	menu->addChild(createMenuItem(string::translate("RackWidget.bypass"), bypassText, [=]() {
		bypassSelectionAction(!bypassed);
	}, n == 0, true));

	// Duplicate
	menu->addChild(createMenuItem(string::translate("RackWidget.duplicate"), widget::getKeyCommandName(GLFW_KEY_D, RACK_MOD_CTRL), [=]() {
		cloneSelectionAction(false);
	}, n == 0));

	// Duplicate with cables
	menu->addChild(createMenuItem("└ " + string::translate("RackWidget.duplicateWithCables"), widget::getKeyCommandName(GLFW_KEY_D, RACK_MOD_CTRL | GLFW_MOD_SHIFT), [=]() {
		cloneSelectionAction(true);
	}, n == 0));

	// Delete
	menu->addChild(createMenuItem(string::translate("RackWidget.delete"), widget::getKeyCommandName(GLFW_KEY_BACKSPACE, 0) + "/" + widget::getKeyCommandName(GLFW_KEY_DELETE, 0), [=]() {
		deleteSelectionAction();
	}, n == 0, true));
}

void RackWidget::clearCables() {
	// Since cables manage plugs, all plugs will be removed from plugContainer
	internal->cableContainer->clearChildren();
}

void RackWidget::clearCablesAction() {
	// Add CableRemove for every cable to a ComplexAction
	history::ComplexAction* complexAction = new history::ComplexAction;
	complexAction->name = string::translate("RackWidget.history.clearCables");

	for (CableWidget* cw : getCompleteCables()) {
		// history::CableRemove
		history::CableRemove* h = new history::CableRemove;
		h->setCable(cw);
		complexAction->push(h);
	}

	if (!complexAction->isEmpty())
		APP->history->push(complexAction);
	else
		delete complexAction;

	clearCables();
}

void RackWidget::clearCablesOnPort(PortWidget* port) {
	for (CableWidget* cw : getCablesOnPort(port)) {
		removeCable(cw);
		delete cw;
	}
}

void RackWidget::addCable(CableWidget* cw) {
	internal->cableContainer->addChild(cw);
}

void RackWidget::removeCable(CableWidget* cw) {
	internal->cableContainer->removeChild(cw);
}

CableWidget* RackWidget::getIncompleteCable() {
	for (auto it = internal->cableContainer->children.rbegin(); it != internal->cableContainer->children.rend(); it++) {
		CableWidget* cw = dynamic_cast<CableWidget*>(*it);
		assert(cw);
		if (!cw->isComplete())
			return cw;
	}
	return NULL;
}

PlugWidget* RackWidget::getTopPlug(PortWidget* port) {
	assert(port);
	for (auto it = internal->plugContainer->children.rbegin(); it != internal->plugContainer->children.rend(); it++) {
		PlugWidget* plug = dynamic_cast<PlugWidget*>(*it);
		assert(plug);
		CableWidget* cw = plug->getCable();
		PortWidget* port2 = cw->getPort(plug->getType());
		if (port2 == port)
			return plug;
	}
	return NULL;
}

CableWidget* RackWidget::getTopCable(PortWidget* port) {
	PlugWidget* plug = getTopPlug(port);
	if (plug)
		return plug->getCable();
	return NULL;
}

CableWidget* RackWidget::getCable(int64_t cableId) {
	for (widget::Widget* w : internal->cableContainer->children) {
		CableWidget* cw = dynamic_cast<CableWidget*>(w);
		assert(cw);
		if (!cw->cable)
			continue;
		if (cw->cable->id == cableId)
			return cw;
	}
	return NULL;
}

CableWidget* RackWidget::getCable(PortWidget* outputPort, PortWidget* inputPort) {
	for (widget::Widget* w : internal->cableContainer->children) {
		CableWidget* cw = dynamic_cast<CableWidget*>(w);
		assert(cw);
		if (cw->outputPort == outputPort && cw->inputPort == inputPort)
			return cw;
	}
	return NULL;
}

std::vector<CableWidget*> RackWidget::getCables() {
	std::vector<CableWidget*> cws;
	cws.reserve(internal->cableContainer->children.size());
	for (widget::Widget* w : internal->cableContainer->children) {
		CableWidget* cw = dynamic_cast<CableWidget*>(w);
		assert(cw);
		cws.push_back(cw);
	}
	return cws;
}

std::vector<CableWidget*> RackWidget::getCompleteCables() {
	std::vector<CableWidget*> cws;
	// Assume that most cables are complete, so pre-allocate and shrink vector.
	cws.reserve(internal->cableContainer->children.size());
	for (widget::Widget* w : internal->cableContainer->children) {
		CableWidget* cw = dynamic_cast<CableWidget*>(w);
		assert(cw);
		if (cw->isComplete())
			cws.push_back(cw);
	}
	cws.shrink_to_fit();
	return cws;
}

std::vector<CableWidget*> RackWidget::getIncompleteCables() {
	std::vector<CableWidget*> cws;
	for (widget::Widget* w : internal->cableContainer->children) {
		CableWidget* cw = dynamic_cast<CableWidget*>(w);
		assert(cw);
		if (!cw->isComplete())
			cws.push_back(cw);
	}
	return cws;
}

std::vector<CableWidget*> RackWidget::getCablesOnPort(PortWidget* port) {
	assert(port);
	std::vector<CableWidget*> cws;
	for (widget::Widget* w : internal->plugContainer->children) {
		PlugWidget* plug = dynamic_cast<PlugWidget*>(w);
		assert(plug);
		CableWidget* cw = plug->getCable();
		PortWidget* port2 = cw->getPort(plug->getType());
		if (port2 == port)
			cws.push_back(cw);
	}
	return cws;
}

std::vector<CableWidget*> RackWidget::getCompleteCablesOnPort(PortWidget* port) {
	assert(port);
	std::vector<CableWidget*> cws;
	for (widget::Widget* w : internal->plugContainer->children) {
		PlugWidget* plug = dynamic_cast<PlugWidget*>(w);
		assert(plug);
		CableWidget* cw = plug->getCable();
		if (!cw->isComplete())
			continue;
		PortWidget* port2 = cw->getPort(plug->getType());
		if (port2 == port)
			cws.push_back(cw);
	}
	return cws;
}


int RackWidget::getNextCableColorId() {
	return internal->nextCableColorId;
}


void RackWidget::setNextCableColorId(int id) {
	internal->nextCableColorId = id;
}


NVGcolor RackWidget::getNextCableColor() {
	if (settings::cableColors.empty())
		return color::WHITE;

	int id = internal->nextCableColorId;
	if (settings::cableAutoRotate) {
		internal->nextCableColorId++;
	}
	if (id >= (int) settings::cableColors.size())
		id = 0;
	if (internal->nextCableColorId >= (int) settings::cableColors.size())
		internal->nextCableColorId = 0;
	return settings::cableColors[id];
}


ParamWidget* RackWidget::getTouchedParam() {
	return touchedParam;
}


void RackWidget::setTouchedParam(ParamWidget* pw) {
	touchedParam = pw;
}


void RackWidget::updateExpanders() {
	for (widget::Widget* w : internal->moduleContainer->children) {
		ModuleWidget* mw = (ModuleWidget*) w;

		math::Vec pLeft = mw->getGridBox().getTopLeft();
		math::Vec pRight = mw->getGridBox().getTopRight();
		ModuleWidget* mwLeft = NULL;
		ModuleWidget* mwRight = NULL;

		// Find adjacent modules
		for (widget::Widget* w2 : internal->moduleContainer->children) {
			ModuleWidget* mw2 = (ModuleWidget*) w2;
			if (mw2 == mw)
				continue;

			math::Vec p2Left = mw2->getGridBox().getTopLeft();
			math::Vec p2Right = mw2->getGridBox().getTopRight();

			// Check if this is a left module
			if (p2Right.equals(pLeft))
				mwLeft = mw2;

			// Check if this is a right module
			if (p2Left.equals(pRight))
				mwRight = mw2;
		}

		mw->module->leftExpander.moduleId = mwLeft ? mwLeft->module->id : -1;
		mw->module->rightExpander.moduleId = mwRight ? mwRight->module->id : -1;
	}
}


} // namespace app
} // namespace rack
