#pragma once
#include <vector>
#include <map>
#include <functional>

#include <common.hpp>
#include <math.hpp>
#include <window/Window.hpp>


namespace rack {
namespace app {
struct RackWidget;
} // namespace app

namespace rack3d {


/** Renders each rack placement's block of the RackWidget into its own texture: rails, modules and lights,
but not plugs and cables (those become 3D objects).

Rendering happens between frames, from Window::step() via renderRequested(), and only on frames where something
asked for textures with requestRender(). Whoever shows the textures must not also show the 2D rack that frame,
or module panel caches re-render at two scales every frame.
*/
struct PlacementRenderer {
	struct Target {
		int placementId = -1;
		NVGLUframebuffer* fb = NULL;
		/** Texture size in pixels. */
		math::Vec size;
	};

	/** Texture pixels per rack pixel. */
	float scale = 1.f;
	/** Per-placement scale overriding `scale`, by placement id. A scale of 0 skips that placement this frame and
	keeps its last texture (e.g. it faces away from the camera).
	*/
	std::map<int, float> placementScales;
	std::vector<Target> targets;

	float getScale(int placementId) const {
		auto it = placementScales.find(placementId);
		return it != placementScales.end() ? it->second : scale;
	}

	~PlacementRenderer();
	/** Renders every placement of `rack` into its texture. Must be called with no NanoVG frame open on `vg`. */
	void render(NVGcontext* vg, app::RackWidget* rack);
	/** The target of a placement, or NULL if it hasn't been rendered. */
	const Target* getTarget(int placementId) const;
	/** Deletes every framebuffer. Call before the NanoVG context is deleted. */
	void clear();
};


/** The renderer for the app's window. Created on first use. */
PlacementRenderer* getRenderer();
/** Asks for placement textures to be rendered before this frame is drawn. Call from Widget::step(). */
void requestRender();
/** Runs `f` right after this frame's placement textures are rendered, e.g. to render the 3D scene that uses them.
Implies requestRender(). Call from Widget::step().
*/
void requestAfterRender(std::function<void()> f);
/** Called by Window::step() between stepping and drawing the scene. Renders if requested this frame. */
void renderRequested();
/** Called by Window's destructor before deleting the NanoVG contexts. */
void destroyRenderer();


} // namespace rack3d
} // namespace rack
