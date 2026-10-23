#include <cmath>

#include <rack3d/TilesView.hpp>
#include <rack3d/PlacementRenderer.hpp>
#include <rack3d/Layout.hpp>
#include <rack3d/Placement.hpp>
#include <rack3d/ModelView.hpp>
#include <app/Scene.hpp>
#include <app/RackScrollWidget.hpp>
#include <app/RackWidget.hpp>
#include <app/MenuBar.hpp>
#include <window/Window.hpp>
#include <context.hpp>
#include <string.hpp>


namespace rack {
namespace rack3d {


static const float MARGIN = 20;
static const float LABEL_HEIGHT = 20;
/** Tiles always fill at most two rows; columns grow with the number of placements. */
static const int ROWS = 2;


static int getRows(int count) {
	return std::min(ROWS, std::max(1, count));
}


static int getColumns(int count) {
	return std::max(1, (count + ROWS - 1) / ROWS);
}


/** Side of the square each tile fits in, in UI px. */
static float getTileSize(math::Vec size, int count) {
	int rows = getRows(count);
	int columns = getColumns(count);
	float w = (size.x - MARGIN * (columns + 1)) / columns;
	float h = (size.y - MARGIN - (MARGIN + LABEL_HEIGHT) * rows) / rows;
	return std::fmax(16.f, std::fmin(w, h));
}


void TilesView::step() {
	app::Scene* scene = APP->scene;
	box = scene->rackScroll->box;

	// The 2D rack is hidden while tiles are shown, so step it here: modules update lights, displays, sizes.
	scene->rack->step();

	// Texture scale so a tile's largest placement fills it at the screen's pixel density.
	// Quantized, so resizing the window doesn't re-render every panel cache on every pixel of change.
	Layout* layout = scene->rack->getLayout();
	float maxSide = 1;
	for (const Placement& p : layout->placements) {
		math::Rect r = Layout::getRackRect(p);
		maxSide = std::fmax(maxSide, std::fmax(r.size.x, r.size.y));
	}
	float pixelRatio = APP->window->pixelRatio;
	float scale = getTileSize(box.size, layout->placements.size()) / maxSide * pixelRatio;
	scale = std::fmax(1 / 16.f, std::round(scale * 16) / 16);
	getRenderer()->scale = scale;
	// Every tile is visible, at one scale (the 3D view sets per-rack scales)
	getRenderer()->placementScales.clear();

	requestRender();
	OpaqueWidget::step();
}


math::Rect TilesView::getTileBox(int index) {
	Layout* layout = APP->scene->rack->getLayout();
	int count = layout->placements.size();
	if (index < 0 || index >= count)
		return math::Rect();
	float tile = getTileSize(box.size, count);
	int columns = getColumns(count);
	float x = MARGIN + (index % columns) * (tile + MARGIN);
	float y = MARGIN + (index / columns) * (tile + MARGIN + LABEL_HEIGHT) + LABEL_HEIGHT;
	// Fitted into the tile keeping the placement's aspect ratio
	math::Rect r = Layout::getRackRect(layout->placements[index]);
	float fit = tile / std::fmax(r.size.x, r.size.y);
	return math::Rect(x, y, r.size.x * fit, r.size.y * fit);
}


bool TilesView::toRackPos(math::Vec pos, math::Vec* rackPos) {
	Layout* layout = APP->scene->rack->getLayout();
	for (size_t i = 0; i < layout->placements.size(); i++) {
		math::Rect tileBox = getTileBox(i);
		if (!tileBox.contains(pos))
			continue;
		math::Rect r = Layout::getRackRect(layout->placements[i]);
		*rackPos = r.pos.plus(pos.minus(tileBox.pos).mult(r.size.div(tileBox.size)));
		return true;
	}
	return false;
}


bool TilesView::fromRackPos(math::Vec rackPos, math::Vec* pos) {
	Layout* layout = APP->scene->rack->getLayout();
	for (size_t i = 0; i < layout->placements.size(); i++) {
		math::Rect r = Layout::getRackRect(layout->placements[i]);
		if (!r.contains(rackPos))
			continue;
		math::Rect tileBox = getTileBox(i);
		*pos = tileBox.pos.plus(rackPos.minus(r.pos).mult(tileBox.size.div(r.size)));
		return true;
	}
	return false;
}


/** Hands a positional event over a tile to the RackWidget, in rack coordinates, the way ZoomWidget forwards events
to its children. Drag start/move/end need no forwarding: EventState sends them straight to the dragged widget.
Nor does drop: it goes to the widget that took the last (forwarded) drag-hover, e.g. a port.
*/
template <class TEvent>
static bool forwardToRack(TilesView* view, const TEvent& e, void (widget::Widget::*handler)(const TEvent&)) {
	math::Vec rackPos;
	if (!view->toRackPos(e.pos, &rackPos))
		return false;
	TEvent e2 = e;
	e2.pos = rackPos;
	(APP->scene->rack->*handler)(e2);
	return true;
}


void TilesView::onHover(const HoverEvent& e) {
	if (!forwardToRack(this, e, &widget::Widget::onHover))
		OpaqueWidget::onHover(e);
}


void TilesView::onButton(const ButtonEvent& e) {
	if (!forwardToRack(this, e, &widget::Widget::onButton))
		OpaqueWidget::onButton(e);
}


void TilesView::onHoverKey(const HoverKeyEvent& e) {
	if (!forwardToRack(this, e, &widget::Widget::onHoverKey))
		OpaqueWidget::onHoverKey(e);
}


void TilesView::onHoverText(const HoverTextEvent& e) {
	if (!forwardToRack(this, e, &widget::Widget::onHoverText))
		OpaqueWidget::onHoverText(e);
}


void TilesView::onHoverScroll(const HoverScrollEvent& e) {
	if (!forwardToRack(this, e, &widget::Widget::onHoverScroll))
		OpaqueWidget::onHoverScroll(e);
}


void TilesView::onDragHover(const DragHoverEvent& e) {
	if (!forwardToRack(this, e, &widget::Widget::onDragHover))
		OpaqueWidget::onDragHover(e);
}


void TilesView::onPathDrop(const PathDropEvent& e) {
	if (!forwardToRack(this, e, &widget::Widget::onPathDrop))
		OpaqueWidget::onPathDrop(e);
}


void TilesView::draw(const DrawArgs& args) {
	nvgBeginPath(args.vg);
	nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
	nvgFillColor(args.vg, nvgRGB(0x14, 0x14, 0x14));
	nvgFill(args.vg);

	Layout* layout = APP->scene->rack->getLayout();
	PlacementRenderer* renderer = getRenderer();

	for (size_t i = 0; i < layout->placements.size(); i++) {
		const Placement& p = layout->placements[i];
		math::Rect tileBox = getTileBox(i);

		// Label
		std::string label = getPlacementName(i) + string::f("  %d HP x %d rows", (int) p.gridRect.size.x, (int) p.gridRect.size.y);
		nvgFontFaceId(args.vg, APP->window->uiFont->handle);
		nvgFontSize(args.vg, 13);
		nvgFillColor(args.vg, nvgRGB(0xc0, 0xc0, 0xc0));
		nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
		nvgText(args.vg, tileBox.pos.x, tileBox.pos.y - LABEL_HEIGHT, label.c_str(), NULL);

		// Texture
		const PlacementRenderer::Target* t = renderer->getTarget(p.id);
		nvgBeginPath(args.vg);
		nvgRect(args.vg, RECT_ARGS(tileBox));
		if (t) {
			nvgFillPaint(args.vg, nvgImagePattern(args.vg, RECT_ARGS(tileBox), 0, t->fb->image, 1.0));
		}
		else {
			nvgFillColor(args.vg, nvgRGB(0x30, 0x30, 0x30));
		}
		nvgFill(args.vg);
	}

	OpaqueWidget::draw(args);
}


static TilesView* tilesView = NULL;


TilesView::~TilesView() {
	// The Scene owns and deletes us (e.g. when Rack restarts in-process)
	if (tilesView == this)
		tilesView = NULL;
}


TilesView* getTilesView() {
	return tilesView;
}


bool isTilesViewShown() {
	return tilesView && tilesView->isVisible();
}


void setTilesViewShown(bool shown) {
	app::Scene* scene = APP->scene;
	// One texture view at a time
	if (shown)
		setModelViewShown(false);
	if (!tilesView) {
		if (!shown)
			return;
		tilesView = new TilesView;
		// Below the menu bar so menus stay usable
		scene->addChildBelow(tilesView, scene->menuBar);
	}
	tilesView->setVisible(shown);
	scene->rackScroll->setVisible(!shown && !isModelViewShown());
	INFO("Tiles view %s", shown ? "shown" : "hidden");
}


void toggleTilesView() {
	setTilesViewShown(!isTilesViewShown());
}


} // namespace rack3d
} // namespace rack
