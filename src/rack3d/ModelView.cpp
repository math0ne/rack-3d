#include <cmath>

#include <rack3d/ModelView.hpp>
#include <rack3d/TilesView.hpp>
#include <rack3d/PlacementRenderer.hpp>
#include <rack3d/Layout.hpp>
#include <app/Scene.hpp>
#include <app/RackScrollWidget.hpp>
#include <app/RackWidget.hpp>
#include <app/MenuBar.hpp>
#include <window/Window.hpp>
#include <context.hpp>


namespace rack {
namespace rack3d {


static ModelView* modelView = NULL;


ModelView::~ModelView() {
	// GL objects were freed in onContextDestroy(), which Window sends before the GL context goes away.
	// The Scene (and so this widget) is deleted after the Window.
	if (modelView == this)
		modelView = NULL;
}


void ModelView::step() {
	app::Scene* scene = APP->scene;
	box = scene->rackScroll->box;

	// The 2D rack is hidden while the 3D view is shown, so step it here
	scene->rack->step();

	// Placement texture scale: a face filling about the view's height, at the screen's pixel density.
	// Quantized, so resizing doesn't re-render every panel cache on every pixel of change.
	float pixelRatio = APP->window->pixelRatio;
	float scale = box.size.y * pixelRatio * 0.6f / 1140.f;
	scale = math::clamp(std::round(scale * 16) / 16, 1 / 16.f, 2.f);
	getRenderer()->scale = scale;

	requestAfterRender([this]() {
		renderModel();
	});
	OpaqueWidget::step();
}


void ModelView::renderModel() {
	float pixelRatio = APP->window->pixelRatio;
	int width = std::max(1, (int) std::round(box.size.x * pixelRatio));
	int height = std::max(1, (int) std::round(box.size.y * pixelRatio));
	renderer.render(width, height, APP->scene->rack->getLayout(), getRenderer());
}


void ModelView::deleteImage(NVGcontext* vg) {
	if (image >= 0)
		nvgDeleteImage(vg, image);
	image = -1;
	imageTexture = 0;
	imageWidth = imageHeight = 0;
}


void ModelView::draw(const DrawArgs& args) {
	const render3d::RenderTarget& t = renderer.target;
	if (!t.texture) {
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
		nvgFillColor(args.vg, nvgRGB(0x14, 0x14, 0x14));
		nvgFill(args.vg);
		return;
	}

	// Wrap the render target's texture as a NanoVG image (re-wrap when it's recreated)
	if (image < 0 || imageTexture != t.texture || imageWidth != t.width || imageHeight != t.height) {
		deleteImage(args.vg);
		// GL renders bottom-up; the texture is ours, so NanoVG mustn't delete it
		image = nvglCreateImageFromHandleGL3(args.vg, t.texture, t.width, t.height, NVG_IMAGE_FLIPY | NVG_IMAGE_PREMULTIPLIED | NVG_IMAGE_NODELETE);
		imageTexture = t.texture;
		imageWidth = t.width;
		imageHeight = t.height;
	}

	nvgBeginPath(args.vg);
	nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
	nvgFillPaint(args.vg, nvgImagePattern(args.vg, 0, 0, box.size.x, box.size.y, 0, image, 1.0));
	nvgFill(args.vg);

	nvgFontFaceId(args.vg, APP->window->uiFont->handle);
	nvgFontSize(args.vg, 13);
	nvgFillColor(args.vg, nvgRGBA(0xc0, 0xc0, 0xc0, 0xa0));
	nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);
	nvgText(args.vg, 12, box.size.y - 10, "3D view (F7) · F6: tiles", NULL);

	OpaqueWidget::draw(args);
}


void ModelView::onContextDestroy(const ContextDestroyEvent& e) {
	deleteImage(e.vg);
	renderer.destroy();
	OpaqueWidget::onContextDestroy(e);
}


ModelView* getModelView() {
	return modelView;
}


bool isModelViewShown() {
	return modelView && modelView->isVisible();
}


void setModelViewShown(bool shown) {
	app::Scene* scene = APP->scene;
	if (shown)
		setTilesViewShown(false);
	if (!modelView) {
		if (!shown)
			return;
		modelView = new ModelView;
		// Below the menu bar so menus stay usable
		scene->addChildBelow(modelView, scene->menuBar);
	}
	modelView->setVisible(shown);
	scene->rackScroll->setVisible(!shown && !isTilesViewShown());
	INFO("3D view %s", shown ? "shown" : "hidden");
}


void toggleModelView() {
	setModelViewShown(!isModelViewShown());
}


} // namespace rack3d
} // namespace rack
