#include <cmath>

#include <rack3d/TilesView.hpp>
#include <rack3d/PlacementRenderer.hpp>
#include <rack3d/Layout.hpp>
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
static const int COLUMNS = 3;
static const char* const CUBE_FACE_NAMES[] = {"front", "right", "back", "left", "top", "bottom"};


/** Side of the square each tile fits in, in UI px. */
static float getTileSize(math::Vec size, int count) {
	int rows = std::max(1, (count + COLUMNS - 1) / COLUMNS);
	float w = (size.x - MARGIN * (COLUMNS + 1)) / COLUMNS;
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

	requestRender();
	OpaqueWidget::step();
}


void TilesView::draw(const DrawArgs& args) {
	nvgBeginPath(args.vg);
	nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
	nvgFillColor(args.vg, nvgRGB(0x14, 0x14, 0x14));
	nvgFill(args.vg);

	Layout* layout = APP->scene->rack->getLayout();
	PlacementRenderer* renderer = getRenderer();
	float tile = getTileSize(box.size, layout->placements.size());
	bool isCube = layout->placements.size() == 6;

	for (size_t i = 0; i < layout->placements.size(); i++) {
		const Placement& p = layout->placements[i];
		float x = MARGIN + (i % COLUMNS) * (tile + MARGIN);
		float y = MARGIN + (i / COLUMNS) * (tile + MARGIN + LABEL_HEIGHT);

		// Label
		std::string label = string::f("Rack %d", p.id);
		if (isCube)
			label += string::f(" (%s)", CUBE_FACE_NAMES[i]);
		label += string::f("  %d HP x %d rows", (int) p.gridRect.size.x, (int) p.gridRect.size.y);
		nvgFontFaceId(args.vg, APP->window->uiFont->handle);
		nvgFontSize(args.vg, 13);
		nvgFillColor(args.vg, nvgRGB(0xc0, 0xc0, 0xc0));
		nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
		nvgText(args.vg, x, y, label.c_str(), NULL);

		// Texture, fitted into the tile keeping its aspect ratio
		math::Rect r = Layout::getRackRect(p);
		float fit = tile / std::fmax(r.size.x, r.size.y);
		math::Rect tileBox(x, y + LABEL_HEIGHT, r.size.x * fit, r.size.y * fit);
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


bool isTilesViewShown() {
	return tilesView && tilesView->isVisible();
}


void setTilesViewShown(bool shown) {
	app::Scene* scene = APP->scene;
	if (!tilesView) {
		if (!shown)
			return;
		tilesView = new TilesView;
		// Below the menu bar so menus stay usable
		scene->addChildBelow(tilesView, scene->menuBar);
	}
	tilesView->setVisible(shown);
	scene->rackScroll->setVisible(!shown);
	INFO("Tiles view %s", shown ? "shown" : "hidden");
}


void toggleTilesView() {
	setTilesViewShown(!isTilesViewShown());
}


} // namespace rack3d
} // namespace rack
