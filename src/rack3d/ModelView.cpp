#include <cmath>
#include <algorithm>

#include <rack3d/ModelView.hpp>
#include <rack3d/TilesView.hpp>
#include <rack3d/PlacementRenderer.hpp>
#include <rack3d/Layout.hpp>
#include <rack3d/settings.hpp>
#include <app/Knob.hpp>
#include <app/SliderKnob.hpp>
#include <app/SvgSlider.hpp>
#include <app/Switch.hpp>
#include <app/LightWidget.hpp>
#include <engine/ParamQuantity.hpp>
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

	updateTextureScales();

	// Camera motion
	float dt = math::clamp((float) APP->window->getLastFrameDuration(), 0.f, 0.1f);
	// Turning auto-orbit on (F8 or the menu) starts it now, without the pause after using the view
	if (settings::autoOrbit && !autoOrbitWasOn)
		idleTime = ORBIT_RESUME_SECONDS;
	autoOrbitWasOn = settings::autoOrbit;
	if (snapping) {
		snapTime += dt;
		float u = math::clamp(snapTime / SNAP_SECONDS, 0.f, 1.f);
		float ease = 1 - std::pow(1 - u, 3.f);
		renderer.rotation = render3d::Quat::slerp(snapFrom, snapTo, ease);
		if (u >= 1)
			snapping = false;
	}
	else if (settings::autoOrbit && !dragging && idleTime >= ORBIT_RESUME_SECONDS) {
		// Turn about the vertical axis while tipping toward and away from the camera, starting from wherever the model
		// was left, easing in so it never jumps
		if (!orbiting) {
			orbiting = true;
			orbitFrom = renderer.rotation;
			orbitAngle = 0;
			angularVelocity = render3d::Vec3();
		}
		float ease = math::clamp((idleTime - ORBIT_RESUME_SECONDS) / ORBIT_EASE_SECONDS, 0.f, 1.f);
		orbitAngle += 2 * M_PI / settings::ORBIT_TURN_SECONDS * settings::orbitSpeed * ease * ease * dt;
		float tilt = ORBIT_TILT * std::sin(orbitAngle / ORBIT_TILT_TURNS);
		renderer.rotation = (render3d::Quat::fromAxisAngle(render3d::Vec3(1, 0, 0), tilt) * render3d::Quat::fromAxisAngle(render3d::Vec3(0, 1, 0), orbitAngle) * orbitFrom).normalize();
	}
	else if (!dragging) {
		orbiting = false;
		float speed = angularVelocity.norm();
		if (speed > 1e-4f) {
			render3d::Quat dq = render3d::Quat::fromAxisAngle(angularVelocity, speed * dt);
			renderer.rotation = (dq * renderer.rotation).normalize();
			angularVelocity = angularVelocity * std::exp(-settings::spinDamping * dt);
		}
		else {
			angularVelocity = render3d::Vec3();
		}
	}

	idleTime += dt;
	if (!settings::autoOrbit)
		orbiting = false;

	// Cables follow the model's motion this frame, and loose ends follow the mouse
	cables.update(dt, renderer.rotation, scene->rack, scene->rack->getLayout(), getPointer());
	mouseInView = false;
	updateControls();

	requestAfterRender([this]() {
		renderModel();
	});
	OpaqueWidget::step();
}


bool ModelView::toRackPos(math::Vec pos, math::Vec* rackPos) {
	math::Vec local;
	int id = pickPlacement(pos, &local);
	if (id < 0)
		return false;
	*rackPos = Layout::getRackRect(*APP->scene->rack->getLayout()->getPlacement(id)).pos.plus(local);
	return true;
}


bool ModelView::fromRackPos(math::Vec rackPos, math::Vec* pos) {
	for (const Placement& p : APP->scene->rack->getLayout()->placements) {
		math::Rect r = Layout::getRackRect(p);
		if (!p.hasSurface || !r.contains(rackPos))
			continue;
		render3d::Vec3 ndc = renderer.getMVP(box.size.x, box.size.y).transformPoint(p.surface.toModel(rackPos.minus(r.pos)));
		*pos = math::Vec((ndc.x + 1) / 2 * box.size.x, (1 - ndc.y) / 2 * box.size.y);
		return true;
	}
	return false;
}


/** Hands a positional event over a rack to the RackWidget, at the ray-cast hit in rack coordinates (as TilesView
does with tiles). Drag start/move/end go straight to the dragged widget, and drop to the widget that took the last
drag-hover, so they need no forwarding.
*/
template <class TEvent>
static bool forwardToRack(ModelView* view, const TEvent& e, void (widget::Widget::*handler)(const TEvent&)) {
	math::Vec rackPos;
	if (!view->toRackPos(e.pos, &rackPos))
		return false;
	TEvent e2 = e;
	e2.pos = rackPos;
	(APP->scene->rack->*handler)(e2);
	return true;
}


Pointer ModelView::getPointer() {
	if (!mouseInView)
		return Pointer();
	return getPointerAt(mousePos);
}


Pointer ModelView::getPointerAt(math::Vec pos) {
	using render3d::Vec3;
	Pointer p;
	if (box.size.x <= 0 || box.size.y <= 0)
		return p;
	p.valid = true;
	float x = 2 * pos.x / box.size.x - 1;
	float y = 1 - 2 * pos.y / box.size.y;
	render3d::Mat4 inv = renderer.getViewProjection(box.size.x, box.size.y).inverse();
	Vec3 near = inv.transformPoint(Vec3(x, y, -1));
	Vec3 far = inv.transformPoint(Vec3(x, y, 1));
	p.rayOrigin = near;
	p.rayDir = (far - near).normalize();
	math::Vec local;
	int id = pickPlacement(pos, &local);
	if (id >= 0) {
		const Placement* placement = APP->scene->rack->getLayout()->getPlacement(id);
		p.onRack = true;
		p.hit = placement->surface.toModel(local);
		p.hitNormal = placement->surface.normal();
	}
	return p;
}


void ModelView::onHover(const HoverEvent& e) {
	mousePos = e.pos;
	mouseInView = true;
	if (!forwardToRack(this, e, &widget::Widget::onHover))
		OpaqueWidget::onHover(e);
}




void ModelView::noteInteraction() {
	idleTime = 0;
	orbiting = false;
}


void ModelView::onButton(const ButtonEvent& e) {
	noteInteraction();
	if (e.action == GLFW_PRESS)
		lastButtonPos = e.pos;
	if (forwardToRack(this, e, &widget::Widget::onButton)) {
		// A left press on empty rack space spins the model, like one off the model, instead of starting a selection
		bool emptyRack = e.getTarget() == APP->scene->rack;
		if (!(e.button == GLFW_MOUSE_BUTTON_LEFT && emptyRack) && e.isConsumed())
			return;
	}
	// Take it, so drags (spin) and double-clicks (snap) come to us
	e.consume(this);
}


void ModelView::onHoverKey(const HoverKeyEvent& e) {
	noteInteraction();
	if (!forwardToRack(this, e, &widget::Widget::onHoverKey))
		OpaqueWidget::onHoverKey(e);
}


void ModelView::onHoverText(const HoverTextEvent& e) {
	if (!forwardToRack(this, e, &widget::Widget::onHoverText))
		OpaqueWidget::onHoverText(e);
}


void ModelView::onDragHover(const DragHoverEvent& e) {
	mousePos = e.pos;
	mouseInView = true;
	if (!forwardToRack(this, e, &widget::Widget::onDragHover))
		OpaqueWidget::onDragHover(e);
}


void ModelView::onDoubleClick(const DoubleClickEvent& e) {
	int id = pickPlacement(lastButtonPos);
	if (id >= 0)
		snapToPlacement(id);
}


void ModelView::onDragStart(const DragStartEvent& e) {
	noteInteraction();
	dragging = true;
	snapping = false;
	angularVelocity = render3d::Vec3();
	lastDragMoveTime = system::getTime();
}


void ModelView::onDragMove(const DragMoveEvent& e) {
	noteInteraction();
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
	noteInteraction();
	// A module under the mouse may take it (e.g. knob scrolling); otherwise zoom
	if (forwardToRack(this, e, &widget::Widget::onHoverScroll) && e.isConsumed())
		return;
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


void ModelView::updateTextureScales() {
	using render3d::Vec3;
	PlacementRenderer* textures = getRenderer();
	textures->placementScales.clear();
	float pixelRatio = APP->window->pixelRatio;
	math::Vec viewPx = box.size.mult(pixelRatio);
	render3d::Mat4 mvp = renderer.getMVP(box.size.x, box.size.y);
	auto toScreen = [&](Vec3 p) {
		Vec3 ndc = mvp.transformPoint(p);
		return math::Vec((ndc.x + 1) / 2 * viewPx.x, (1 - ndc.y) / 2 * viewPx.y);
	};

	for (const Placement& p : APP->scene->rack->getLayout()->placements) {
		if (!p.hasSurface)
			continue;
		const Surface& s = p.surface;
		math::Vec size = Layout::getRackRect(p).size;
		// Facing away from the camera: skip it and keep its last texture
		Vec3 center = s.toModel(size.div(2));
		Vec3 toCamera = renderer.rotation.conjugate().rotate(Vec3(0, 0, renderer.distance)) - center;
		if (s.normal().dot(toCamera) <= 0) {
			textures->placementScales[p.id] = 0;
			continue;
		}
		// Screen pixels per rack pixel, along the rack's edges
		math::Vec topLeft = toScreen(s.toModel(math::Vec(0, 0)));
		float alongX = toScreen(s.toModel(math::Vec(size.x, 0))).minus(topLeft).norm() / size.x;
		float alongY = toScreen(s.toModel(math::Vec(0, size.y))).minus(topLeft).norm() / size.y;
		float density = std::fmax(alongX, alongY);
		// Round up to a power of two, so panel caches re-render only when a rack crosses a level
		float scale = std::pow(2.f, std::ceil(std::log2(std::fmax(density, 1e-3f))));
		textures->placementScales[p.id] = math::clamp(scale, MIN_TEXTURE_SCALE, MAX_TEXTURE_SCALE);
	}
}


/** Whether a widget has a light in it (e.g. a light button). */
static bool hasLight(widget::Widget* w) {
	for (widget::Widget* child : w->children) {
		if (dynamic_cast<app::LightWidget*>(child) || hasLight(child))
			return true;
	}
	return false;
}


/** Finds the 3D elements among a widget's descendants. */
static void findElements(widget::Widget* w, std::vector<Element3D*>& out) {
	for (widget::Widget* child : w->children) {
		if (Element3D* e = dynamic_cast<Element3D*>(child)) {
			if (e->isVisible())
				out.push_back(e);
			continue;
		}
		findElements(child, out);
	}
}


void ModelView::updateControls() {
	knobs.clear();
	parts.clear();
	elements.clear();
	elementWidgets.clear();
	app::RackWidget* rack = APP->scene->rack;
	Layout* layout = rack->getLayout();
	for (app::ModuleWidget* mw : rack->getModules()) {
		const Placement* p = layout->placementOf(mw->getGridBox());
		if (!p || !p->hasSurface)
			continue;
		const Surface& s = p->surface;
		math::Vec origin = Layout::getRackRect(*p).pos;

		// 3D elements: the box's corners on the rack, swung out around the hinge edge by the element's tilt
		std::vector<Element3D*> moduleElements;
		findElements(mw, moduleElements);
		for (Element3D* el : moduleElements) {
			math::Vec size = el->box.size;
			if (size.x <= 0 || size.y <= 0)
				continue;
			auto flat = [&](math::Vec local) {
				return s.toModel(el->getRelativeOffset(local, rack).minus(origin));
			};
			math::Vec hingeA, hingeB;
			switch (el->hinge) {
				case Element3D::HINGE_BOTTOM: hingeA = math::Vec(0, size.y); hingeB = size; break;
				case Element3D::HINGE_TOP: hingeA = math::Vec(0, 0); hingeB = math::Vec(size.x, 0); break;
				case Element3D::HINGE_LEFT: hingeA = math::Vec(0, 0); hingeB = math::Vec(0, size.y); break;
				case Element3D::HINGE_RIGHT: hingeA = math::Vec(size.x, 0); hingeB = size; break;
			}
			render3d::Vec3 h0 = flat(hingeA);
			render3d::Vec3 axis = (flat(hingeB) - h0).normalize();
			render3d::Vec3 n = s.normal();
			render3d::Vec3 lift = n * (el->lift * s.unitsPerPx);
			float c = std::cos(el->tilt), sn = std::sin(el->tilt);
			ElementDraw d;
			const math::Vec corners[4] = {math::Vec(0, 0), math::Vec(size.x, 0), size, math::Vec(0, size.y)};
			for (int k = 0; k < 4; k++) {
				// Split the corner's offset from the hinge into along-hinge and away-from-hinge parts, and turn the
				// second out of the panel
				render3d::Vec3 v = flat(corners[k]) - h0;
				render3d::Vec3 along = axis * v.dot(axis);
				render3d::Vec3 away = v - along;
				d.corners[k] = h0 + along + away * c + n * (away.norm() * sn) + lift;
			}
			elements.push_back(d);
			elementWidgets.push_back(el);
		}

		for (app::ParamWidget* pw : mw->getParams()) {
			engine::ParamQuantity* pq = pw->getParamQuantity();
			if (!pq || !pw->isVisible())
				continue;
			auto partAt = [&](widget::Widget* w, PartDraw::Kind kind) {
				PartDraw part;
				part.kind = kind;
				part.center = s.toModel(w->getRelativeOffset(w->box.size.div(2), rack).minus(origin));
				part.right = s.right;
				part.up = -s.down;
				part.normal = s.normal();
				part.width = w->box.size.x * s.unitsPerPx;
				part.height = w->box.size.y * s.unitsPerPx;
				return part;
			};

			// Sliders: a cap over the handle, which the slider keeps at the current value
			if (app::SvgSlider* slider = dynamic_cast<app::SvgSlider*>(pw)) {
				if (slider->handle && slider->handle->isVisible())
					parts.push_back(partAt(slider->handle, PartDraw::SLIDER_HANDLE));
				continue;
			}
			// Switches: buttons sink when pressed or latched; toggles tilt their lever
			if (app::Switch* sw = dynamic_cast<app::Switch*>(pw)) {
				PartDraw part = partAt(sw, PartDraw::kindForSwitch(sw->box.size.x, sw->box.size.y, hasLight(sw)));
				part.pressed = pq->getValue() > pq->getMinValue();
				part.position = pq->getScaledValue();
				parts.push_back(part);
				continue;
			}

			app::Knob* knob = dynamic_cast<app::Knob*>(pw);
			// Other slider-like knobs keep their 2D look
			if (!knob || dynamic_cast<app::SliderKnob*>(pw))
				continue;
			KnobDraw k;
			math::Vec center = pw->getRelativeOffset(pw->box.size.div(2), rack).minus(origin);
			k.center = s.toModel(center);
			k.right = s.right;
			k.up = -s.down;
			k.normal = s.normal();
			float diameter = std::fmin(pw->box.size.x, pw->box.size.y);
			k.radius = diameter / 2 * s.unitsPerPx;
			k.style = KnobDraw::styleForDiameter(diameter);
			k.angle = math::rescale(pq->getScaledValue(), 0.f, 1.f, knob->minAngle, knob->maxAngle);
			knobs.push_back(k);
		}
	}
}


void ModelView::renderModel() {
	float pixelRatio = APP->window->pixelRatio;
	int width = std::max(1, (int) std::round(box.size.x * pixelRatio));
	int height = std::max(1, (int) std::round(box.size.y * pixelRatio));
	renderer.environment = settings::environment;
	renderer.fog = settings::fog;
	renderer.cableGlow = settings::cableGlow;
	renderer.contrast = settings::contrast;
	renderElements(APP->window->vg);
	renderer.render(width, height, APP->scene->rack->getLayout(), getRenderer(), &cables, &knobs, &parts, &elements);
}


void ModelView::renderElements(NVGcontext* vg) {
	// Free the framebuffers of elements that are gone
	for (auto it = elementTargets.begin(); it != elementTargets.end();) {
		if (std::find(elementWidgets.begin(), elementWidgets.end(), it->first) == elementWidgets.end()) {
			if (it->second.fb)
				nvgluDeleteFramebuffer(it->second.fb);
			it = elementTargets.erase(it);
		}
		else {
			it++;
		}
	}

	for (size_t i = 0; i < elementWidgets.size(); i++) {
		Element3D* el = elementWidgets[i];
		float scale = math::clamp(el->resolution, 0.25f, 8.f);
		math::Vec size = el->box.size.mult(scale).ceil();
		ElementTarget& t = elementTargets[el];
		if (!t.fb || !t.size.equals(size)) {
			if (t.fb)
				nvgluDeleteFramebuffer(t.fb);
			t.fb = nvgluCreateFramebuffer(vg, size.x, size.y, 0);
			t.size = size;
			if (!t.fb)
				continue;
		}
		// Record first, bind only for nvgEndFrame (see PlacementRenderer::render)
		nvgBeginFrame(vg, size.x, size.y, 1.0);
		nvgScale(vg, scale, scale);
		widget::Widget::DrawArgs args;
		args.vg = vg;
		args.clipBox = el->box.zeroPos();
		el->drawContent(args);
		el->widget::Widget::drawLayer(args, 1);
		nvgluBindFramebuffer(t.fb);
		glViewport(0, 0, size.x, size.y);
		glClearColor(0.0, 0.0, 0.0, 0.0);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
		nvgEndFrame(vg);
		nvgluBindFramebuffer(NULL);
		elements[i].texture = t.fb->texture;
	}
}


void ModelView::clearElementTargets() {
	for (auto& pair : elementTargets) {
		if (pair.second.fb)
			nvgluDeleteFramebuffer(pair.second.fb);
	}
	elementTargets.clear();
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
	std::string hint = "3D view (F7) · drag to spin · scroll to zoom · double-click a rack to face it · F8: auto-orbit ";
	hint += settings::autoOrbit ? "(on)" : "(off)";
	hint += " · F6: tiles";
	nvgText(args.vg, 12, box.size.y - 10, hint.c_str(), NULL);

	OpaqueWidget::draw(args);
}


void ModelView::onContextDestroy(const ContextDestroyEvent& e) {
	deleteImage(e.vg);
	clearElementTargets();
	renderer.destroy();
	OpaqueWidget::onContextDestroy(e);
}


ModelView* getModelView() {
	return modelView;
}


bool getScreenOffset(widget::Widget* w, math::Vec v, math::Vec* out) {
	app::RackWidget* rack = APP->scene->rack;
	bool onRack = false;
	for (widget::Widget* p = w; p; p = p->parent) {
		if (p == rack) {
			onRack = true;
			break;
		}
	}
	if (!onRack || !isModelViewShown()) {
		*out = w->getAbsoluteOffset(v);
		return true;
	}
	math::Vec viewPos;
	if (!modelView->fromRackPos(w->getRelativeOffset(v, rack), &viewPos))
		return false;
	*out = modelView->getAbsoluteOffset(viewPos);
	return true;
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
