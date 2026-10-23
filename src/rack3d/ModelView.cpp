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
#include <system.hpp>


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

	// Camera motion
	float dt = math::clamp((float) APP->window->getLastFrameDuration(), 0.f, 0.1f);
	if (snapping) {
		snapTime += dt;
		float u = math::clamp(snapTime / SNAP_SECONDS, 0.f, 1.f);
		float ease = 1 - std::pow(1 - u, 3.f);
		renderer.rotation = render3d::Quat::slerp(snapFrom, snapTo, ease);
		if (u >= 1)
			snapping = false;
	}
	else if (!dragging) {
		float speed = angularVelocity.norm();
		if (speed > 1e-4f) {
			render3d::Quat dq = render3d::Quat::fromAxisAngle(angularVelocity, speed * dt);
			renderer.rotation = (dq * renderer.rotation).normalize();
			angularVelocity = angularVelocity * std::exp(-SPIN_DAMPING * dt);
		}
		else {
			angularVelocity = render3d::Vec3();
		}
	}

	requestAfterRender([this]() {
		renderModel();
	});
	OpaqueWidget::step();
}


void ModelView::onButton(const ButtonEvent& e) {
	if (e.action == GLFW_PRESS)
		lastButtonPos = e.pos;
	// Consume, so drags and double-clicks come to us
	OpaqueWidget::onButton(e);
}


void ModelView::onDoubleClick(const DoubleClickEvent& e) {
	int id = pickPlacement(lastButtonPos);
	if (id >= 0)
		snapToPlacement(id);
}


void ModelView::onDragStart(const DragStartEvent& e) {
	dragging = true;
	snapping = false;
	angularVelocity = render3d::Vec3();
	lastDragMoveTime = system::getTime();
}


void ModelView::onDragMove(const DragMoveEvent& e) {
	// Screen x turns around the vertical axis, screen y tilts around the horizontal one (world axes)
	math::Vec d = e.mouseDelta;
	render3d::Vec3 axisAngle(d.y * DRAG_RADIANS_PER_PX, d.x * DRAG_RADIANS_PER_PX, 0);
	float angle = axisAngle.norm();
	if (angle <= 0)
		return;
	render3d::Quat dq = render3d::Quat::fromAxisAngle(axisAngle, angle);
	renderer.rotation = (dq * renderer.rotation).normalize();

	// Remember the spin, smoothed over a few moves, for when the drag ends
	double now = system::getTime();
	float dt = std::fmax(1e-3, now - lastDragMoveTime);
	lastDragMoveTime = now;
	angularVelocity = angularVelocity * 0.5f + axisAngle * (0.5f / dt);
}


void ModelView::onDragEnd(const DragEndEvent& e) {
	dragging = false;
	// Let go after stopping: no spin
	if (system::getTime() - lastDragMoveTime > 0.1)
		angularVelocity = render3d::Vec3();
}


void ModelView::onHoverScroll(const HoverScrollEvent& e) {
	renderer.distance = math::clamp(renderer.distance * std::exp(-e.scrollDelta.y * 0.002f), MIN_DISTANCE, MAX_DISTANCE);
	e.consume(this);
}


int ModelView::pickPlacement(math::Vec pos, math::Vec* localPx) {
	using render3d::Vec3;
	if (box.size.x <= 0 || box.size.y <= 0)
		return -1;
	// Mouse -> normalized device coordinates -> a ray in model space
	float x = 2 * pos.x / box.size.x - 1;
	float y = 1 - 2 * pos.y / box.size.y;
	render3d::Mat4 inv = renderer.getMVP(box.size.x, box.size.y).inverse();
	Vec3 near = inv.transformPoint(Vec3(x, y, -1));
	Vec3 far = inv.transformPoint(Vec3(x, y, 1));
	Vec3 dir = far - near;

	int best = -1;
	float bestT = INFINITY;
	for (const Placement& p : APP->scene->rack->getLayout()->placements) {
		if (!p.hasSurface)
			continue;
		const Surface& s = p.surface;
		Vec3 n = s.normal();
		float facing = n.dot(dir);
		// Only faces turned toward the camera
		if (facing >= 0)
			continue;
		float t = n.dot(s.origin - near) / facing;
		if (t < 0 || t >= bestT)
			continue;
		Vec3 hit = near + dir * t;
		math::Vec local((hit - s.origin).dot(s.right) / s.unitsPerPx, (hit - s.origin).dot(s.down) / s.unitsPerPx);
		math::Vec size = Layout::getRackRect(p).size;
		if (local.x < 0 || local.y < 0 || local.x > size.x || local.y > size.y)
			continue;
		best = p.id;
		bestT = t;
		if (localPx)
			*localPx = local;
	}
	return best;
}


void ModelView::snapToPlacement(int placementId) {
	Placement* p = APP->scene->rack->getLayout()->getPlacement(placementId);
	if (!p || !p->hasSurface)
		return;
	const Surface& s = p->surface;
	// The rotation taking the rack's right to +x, its up (-down) to +y and its normal to +z, toward the camera
	render3d::Quat toFace = render3d::Quat::fromBasis(s.right, -s.down, s.normal()).conjugate();
	snapFrom = renderer.rotation;
	snapTo = toFace;
	snapTime = 0;
	snapping = true;
	angularVelocity = render3d::Vec3();
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
	nvgText(args.vg, 12, box.size.y - 10, "3D view (F7) · drag to spin · scroll to zoom · double-click a rack to face it · F6: tiles", NULL);

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
