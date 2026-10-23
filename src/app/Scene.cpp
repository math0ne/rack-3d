#include <thread>

#include <osdialog.h>

#include <app/Scene.hpp>
#include <app/Browser.hpp>
#include <app/TipWindow.hpp>
#include <app/MenuBar.hpp>
#include <context.hpp>
#include <system.hpp>
#include <network.hpp>
#include <history.hpp>
#include <settings.hpp>
#include <patch.hpp>
#include <asset.hpp>
#include <rack3d/TilesView.hpp>
#include <rack3d/ModelView.hpp>
#include <rack3d/settings.hpp>


namespace rack {
namespace app {


struct ResizeHandle : widget::OpaqueWidget {
	math::Vec size;

	void draw(const DrawArgs& args) override {
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, box.size.x, box.size.y);
		nvgLineTo(args.vg, 0, box.size.y);
		nvgLineTo(args.vg, box.size.x, 0);
		nvgClosePath(args.vg);
		nvgFillColor(args.vg, nvgRGBAf(1, 1, 1, 0.15));
		nvgFill(args.vg);
	}

	void onDragStart(const DragStartEvent& e) override {
		size = APP->window->getSize();
	}

	void onDragMove(const DragMoveEvent& e) override {
		size = size.plus(e.mouseDelta);
		APP->window->setSize(size.round());
	}
};


struct Scene::Internal {
	ResizeHandle* resizeHandle;

	double lastAutosaveTime = 0.0;

	bool heldArrowKeys[4] = {};
};


Scene::Scene() {
	internal = new Internal;

	rackScroll = new RackScrollWidget;
	addChild(rackScroll);

	rack = rackScroll->rackWidget;

	menuBar = createMenuBar();
	addChild(menuBar);

	browser = browserCreate();
	browser->hide();
	addChild(browser);

	if (settings::showTipsOnLaunch) {
		addChild(tipWindowCreate());
	}

	internal->resizeHandle = new ResizeHandle;
	internal->resizeHandle->box.size = math::Vec(15, 15);
	internal->resizeHandle->hide();
	addChild(internal->resizeHandle);
}


Scene::~Scene() {
	delete internal;
}


math::Vec Scene::getMousePos() {
	return mousePos;
}


void Scene::step() {
	if (APP->window->isFullScreen()) {
		// Expand RackScrollWidget to cover entire screen if fullscreen
		rackScroll->box.pos.y = 0;
	}
	else {
		// Always show MenuBar if not fullscreen
		menuBar->show();
		rackScroll->box.pos.y = menuBar->box.size.y;
	}

	internal->resizeHandle->box.pos = box.size.minus(internal->resizeHandle->box.size);

	// Resize owned descendants
	menuBar->box.size.x = box.size.x;
	rackScroll->box.size = box.size.minus(rackScroll->box.pos);

	// Autosave periodically
	if (settings::autosaveInterval > 0.0) {
		double time = system::getTime();
		if (time - internal->lastAutosaveTime >= settings::autosaveInterval) {
			internal->lastAutosaveTime = time;
			APP->patch->saveAutosave();
			settings::save();
		}
	}

	// Scroll RackScrollWidget with arrow keys
	math::Vec arrowDelta;
	if (internal->heldArrowKeys[0]) {
		arrowDelta.x -= 1;
	}
	if (internal->heldArrowKeys[1]) {
		arrowDelta.x += 1;
	}
	if (internal->heldArrowKeys[2]) {
		arrowDelta.y -= 1;
	}
	if (internal->heldArrowKeys[3]) {
		arrowDelta.y += 1;
	}

	if (!arrowDelta.isZero()) {
		int mods = APP->window->getMods();
		float arrowSpeed = 32.f;
		if ((mods & RACK_MOD_MASK) == RACK_MOD_CTRL)
			arrowSpeed /= 4.f;
		if ((mods & RACK_MOD_MASK) == GLFW_MOD_SHIFT)
			arrowSpeed *= 4.f;
		if ((mods & RACK_MOD_MASK) == (RACK_MOD_CTRL | GLFW_MOD_SHIFT))
			arrowSpeed /= 16.f;

		rackScroll->offset += arrowDelta * arrowSpeed;
	}

	Widget::step();
}


void Scene::draw(const DrawArgs& args) {
	Widget::draw(args);
}


void Scene::onHover(const HoverEvent& e) {
	mousePos = e.pos;
	if (mousePos.y < menuBar->box.size.y) {
		menuBar->show();
	}
	OpaqueWidget::onHover(e);
}


void Scene::onDragHover(const DragHoverEvent& e) {
	mousePos = e.pos;
	OpaqueWidget::onDragHover(e);
}


void Scene::onHoverKey(const HoverKeyEvent& e) {
	// Key commands that override children
	if (e.action == GLFW_PRESS || e.action == GLFW_REPEAT) {
		// DEBUG("key %d '%c' scancode %d keyName '%s' mods %02x", e.key, e.key, e.scancode, e.keyName.c_str(), e.mods);
		if (e.isKeyCommand(GLFW_KEY_N, RACK_MOD_CTRL)) {
			APP->patch->loadTemplateDialog();
			e.consume(this);
		}
		if (e.isKeyCommand(GLFW_KEY_Q, RACK_MOD_CTRL)) {
			APP->window->close();
			e.consume(this);
		}
		if (e.isKeyCommand(GLFW_KEY_O, RACK_MOD_CTRL)) {
			APP->patch->loadDialog();
			e.consume(this);
		}
		if (e.isKeyCommand(GLFW_KEY_O, RACK_MOD_CTRL | GLFW_MOD_SHIFT)) {
			APP->patch->revertDialog();
			e.consume(this);
		}
		if (e.isKeyCommand(GLFW_KEY_S, RACK_MOD_CTRL)) {
			APP->patch->saveDialog();
			e.consume(this);
		}
		if (e.isKeyCommand(GLFW_KEY_S, RACK_MOD_CTRL | GLFW_MOD_SHIFT)) {
			APP->patch->saveAsDialog();
			e.consume(this);
		}
		if (e.isKeyCommand(GLFW_KEY_Z, RACK_MOD_CTRL)) {
			APP->history->undo();
			e.consume(this);
		}
		if (e.isKeyCommand(GLFW_KEY_Z, RACK_MOD_CTRL | GLFW_MOD_SHIFT)) {
			APP->history->redo();
			e.consume(this);
		}
		if (e.isKeyCommand(GLFW_KEY_MINUS, RACK_MOD_CTRL) || e.isKeyCommand(GLFW_KEY_KP_SUBTRACT, RACK_MOD_CTRL)) {
			float zoom = std::log2(APP->scene->rackScroll->getZoom());
			zoom *= 2;
			zoom = std::ceil(zoom - 0.01f) - 1;
			zoom /= 2;
			APP->scene->rackScroll->setZoom(std::pow(2.f, zoom));
			e.consume(this);
		}
		if (e.isKeyCommand(GLFW_KEY_EQUAL, RACK_MOD_CTRL)
			// The user might hold shift to access the + character
			|| e.isKeyCommand(GLFW_KEY_EQUAL, RACK_MOD_CTRL | GLFW_MOD_SHIFT)
			// Numpad + key
			|| e.isKeyCommand(GLFW_KEY_KP_ADD, RACK_MOD_CTRL)
			// Some layouts (e.g. QWERTZ) have a + key, but GLFW doesn't have a macro for it
			|| e.isKeyCommand('+', RACK_MOD_CTRL)) {
			float zoom = std::log2(APP->scene->rackScroll->getZoom());
			zoom *= 2;
			zoom = std::floor(zoom + 0.01f) + 1;
			zoom /= 2;
			APP->scene->rackScroll->setZoom(std::pow(2.f, zoom));
			e.consume(this);
		}
		if (e.isKeyCommand(GLFW_KEY_0, RACK_MOD_CTRL) || e.isKeyCommand(GLFW_KEY_KP_0, RACK_MOD_CTRL)) {
			APP->scene->rackScroll->setZoom(1.f);
			e.consume(this);
		}
		if (e.isKeyCommand(GLFW_KEY_F1)) {
			system::openBrowser("https://vcvrack.com/manual/");
			e.consume(this);
		}
		if (e.isKeyCommand(GLFW_KEY_F3)) {
			settings::cpuMeter ^= true;
			e.consume(this);
		}
		if (e.isKeyCommand(GLFW_KEY_F4)) {
			APP->scene->rackScroll->zoomToModules();
			e.consume(this);
		}
		// Rack 3D: debug view of every rack placement's texture
		if (e.isKeyCommand(GLFW_KEY_F6)) {
			rack3d::toggleTilesView();
			e.consume(this);
		}
		// Rack 3D: the 3D view
		if (e.isKeyCommand(GLFW_KEY_F7)) {
			rack3d::toggleModelView();
			e.consume(this);
		}
		// Rack 3D: the 3D view turns by itself
		if (e.isKeyCommand(GLFW_KEY_F8)) {
			rack3d::settings::autoOrbit ^= true;
			e.consume(this);
		}
		if (e.isKeyCommand(GLFW_KEY_F11)) {
			APP->window->setFullScreen(!APP->window->isFullScreen());
			// The MenuBar will be hidden when the mouse moves over the RackScrollWidget.
			// menuBar->hide();
			e.consume(this);
		}

		// Module selections
		if (e.isKeyCommand(GLFW_KEY_A, RACK_MOD_CTRL)) {
			rack->selectAll();
			e.consume(this);
		}
		if (e.isKeyCommand(GLFW_KEY_A, RACK_MOD_CTRL | GLFW_MOD_SHIFT)) {
			rack->deselectAll();
			e.consume(this);
		}
		if (e.isKeyCommand(GLFW_KEY_C, RACK_MOD_CTRL)) {
			if (rack->hasSelection()) {
				rack->copyClipboardSelection();
				e.consume(this);
			}
		}
		if (e.isKeyCommand(GLFW_KEY_I, RACK_MOD_CTRL)) {
			if (rack->hasSelection()) {
				rack->resetSelectionAction();
				e.consume(this);
			}
		}
		if (e.isKeyCommand(GLFW_KEY_R, RACK_MOD_CTRL)) {
			if (rack->hasSelection()) {
				rack->randomizeSelectionAction();
				e.consume(this);
			}
		}
		if (e.isKeyCommand(GLFW_KEY_U, RACK_MOD_CTRL)) {
			if (rack->hasSelection()) {
				rack->disconnectSelectionAction();
				e.consume(this);
			}
		}
		if (e.isKeyCommand(GLFW_KEY_E, RACK_MOD_CTRL)) {
			if (rack->hasSelection()) {
				rack->bypassSelectionAction(!rack->isSelectionBypassed());
				e.consume(this);
			}
		}
		if (e.isKeyCommand(GLFW_KEY_D, RACK_MOD_CTRL)) {
			if (rack->hasSelection()) {
				rack->cloneSelectionAction(false);
				e.consume(this);
			}
		}
		if (e.isKeyCommand(GLFW_KEY_D, RACK_MOD_CTRL | GLFW_MOD_SHIFT)) {
			if (rack->hasSelection()) {
				rack->cloneSelectionAction(true);
				e.consume(this);
			}
		}
		if (e.isKeyCommand(GLFW_KEY_DELETE) || e.isKeyCommand(GLFW_KEY_BACKSPACE)) {
			if (rack->hasSelection()) {
				rack->deleteSelectionAction();
				e.consume(this);
			}
		}
	}

	// Scroll RackScrollWidget with arrow keys
	if (e.action == GLFW_PRESS || e.action == GLFW_RELEASE) {
		if (e.key == GLFW_KEY_LEFT) {
			internal->heldArrowKeys[0] = (e.action == GLFW_PRESS);
			e.consume(this);
		}
		if (e.key == GLFW_KEY_RIGHT) {
			internal->heldArrowKeys[1] = (e.action == GLFW_PRESS);
			e.consume(this);
		}
		if (e.key == GLFW_KEY_UP) {
			internal->heldArrowKeys[2] = (e.action == GLFW_PRESS);
			e.consume(this);
		}
		if (e.key == GLFW_KEY_DOWN) {
			internal->heldArrowKeys[3] = (e.action == GLFW_PRESS);
			e.consume(this);
		}
	}

	if (e.isConsumed())
		return;
	OpaqueWidget::onHoverKey(e);
	if (e.isConsumed())
		return;

	// Key commands that can be overridden by children
	if (e.action == GLFW_PRESS || e.action == GLFW_REPEAT) {
		// Alternative key command for exiting fullscreen, since F11 doesn't work reliably on Mac due to "Show desktop" OS binding.
		if (e.isKeyCommand(GLFW_KEY_ESCAPE, 0)) {
			if (APP->window->isFullScreen()) {
				APP->window->setFullScreen(false);
				e.consume(this);
			}
		}
		if (e.isKeyCommand(GLFW_KEY_V, RACK_MOD_CTRL)) {
			rack->pasteClipboardAction();
			e.consume(this);
		}
		if (e.isKeyCommand(GLFW_KEY_ENTER) || e.isKeyCommand(GLFW_KEY_KP_ENTER)) {
			browser->show();
			e.consume(this);
		}
	}
}


void Scene::onPathDrop(const PathDropEvent& e) {
	if (e.paths.size() >= 1) {
		const std::string& path = e.paths[0];
		std::string extension = system::getExtension(path);

		if (extension == ".vcv") {
			APP->patch->loadPathDialog(path);
			e.consume(this);
			return;
		}
		if (extension == ".vcvs") {
			APP->scene->rack->loadSelection(path);
			e.consume(this);
			return;
		}
	}

	OpaqueWidget::onPathDrop(e);
}


} // namespace app
} // namespace rack
