#include <cmath>

#include <rack3d/PlacementRenderer.hpp>
#include <rack3d/Layout.hpp>
#include <app/RackWidget.hpp>
#include <app/Scene.hpp>
#include <context.hpp>


namespace rack {
namespace rack3d {


PlacementRenderer::~PlacementRenderer() {
	clear();
}


void PlacementRenderer::render(NVGcontext* vg, app::RackWidget* rack) {
	Layout* layout = rack->getLayout();

	// Drop targets of placements that no longer exist
	for (auto it = targets.begin(); it != targets.end();) {
		if (!layout->getPlacement(it->placementId)) {
			if (it->fb)
				nvgluDeleteFramebuffer(it->fb);
			it = targets.erase(it);
		}
		else {
			it++;
		}
	}

	for (const Placement& p : layout->placements) {
		math::Rect rect = Layout::getRackRect(p);
		math::Vec size = rect.size.mult(scale).ceil();

		Target* target = NULL;
		for (Target& t : targets) {
			if (t.placementId == p.id)
				target = &t;
		}
		if (!target) {
			targets.push_back(Target());
			target = &targets.back();
			target->placementId = p.id;
		}
		if (!target->fb || !target->size.equals(size)) {
			if (target->fb)
				nvgluDeleteFramebuffer(target->fb);
			target->fb = nvgluCreateFramebuffer(vg, size.x, size.y, 0);
			target->size = size;
			if (!target->fb) {
				WARN("Could not create %gx%g framebuffer for rack placement %d", size.x, size.y, p.id);
				continue;
			}
		}

		// Record the drawing first. Module panel caches (FramebufferWidget) render into their own framebuffers
		// right away and then rebind the default one, so our framebuffer is bound only for nvgEndFrame(), which is
		// when `vg` actually draws. Window::step() does the same for the screen.
		nvgBeginFrame(vg, size.x, size.y, 1.0);
		nvgScale(vg, scale, scale);
		nvgTranslate(vg, -rect.pos.x, -rect.pos.y);

		widget::Widget::DrawArgs args;
		args.vg = vg;
		args.clipBox = rect;
		// Rails and modules, then lights. Not plugs (layer 2) or cables (layer 3).
		rack->widget::Widget::draw(args);
		rack->widget::Widget::drawLayer(args, 1);

		nvgluBindFramebuffer(target->fb);
		glViewport(0, 0, size.x, size.y);
		glClearColor(0.0, 0.0, 0.0, 0.0);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
		nvgEndFrame(vg);
		nvgluBindFramebuffer(NULL);
	}
}


const PlacementRenderer::Target* PlacementRenderer::getTarget(int placementId) const {
	for (const Target& t : targets) {
		if (t.placementId == placementId && t.fb)
			return &t;
	}
	return NULL;
}


void PlacementRenderer::clear() {
	for (Target& t : targets) {
		if (t.fb)
			nvgluDeleteFramebuffer(t.fb);
	}
	targets.clear();
}


static PlacementRenderer* renderer = NULL;
static bool renderRequestedThisFrame = false;
static std::vector<std::function<void()>> afterRender;


PlacementRenderer* getRenderer() {
	if (!renderer)
		renderer = new PlacementRenderer;
	return renderer;
}


void requestRender() {
	renderRequestedThisFrame = true;
}


void requestAfterRender(std::function<void()> f) {
	renderRequestedThisFrame = true;
	afterRender.push_back(f);
}


void renderRequested() {
	if (!renderRequestedThisFrame)
		return;
	renderRequestedThisFrame = false;
	std::vector<std::function<void()>> after;
	after.swap(afterRender);
	if (!APP->window || !APP->scene)
		return;
	getRenderer()->render(APP->window->vg, APP->scene->rack);
	for (auto& f : after)
		f();
}


void destroyRenderer() {
	delete renderer;
	renderer = NULL;
}


} // namespace rack3d
} // namespace rack
