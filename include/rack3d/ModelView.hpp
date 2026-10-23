#pragma once
#include <widget/OpaqueWidget.hpp>
#include <rack3d/ModelRenderer.hpp>


namespace rack {
namespace rack3d {


/** The 3D view: the model with every rack placement live on its surface. Toggle with F7.
Like the tiles view, it hides the 2D rack while shown and steps it itself (draw-once rule).
Drag to spin the model (it keeps spinning and slows down after you let go), scroll to zoom, double-click a rack to
turn it to face you.
*/
struct ModelView : widget::OpaqueWidget {
	ModelRenderer renderer;
	/** Spin of the model, in world space: axis * radians per second. */
	render3d::Vec3 angularVelocity;

	static constexpr float DRAG_RADIANS_PER_PX = 0.008f;
	/** Spin decays by e^(-SPIN_DAMPING * seconds). */
	static constexpr float SPIN_DAMPING = 3.f;
	static constexpr float SNAP_SECONDS = 0.4f;
	static constexpr float MIN_DISTANCE = 2.5f;
	static constexpr float MAX_DISTANCE = 20.f;

	~ModelView();
	void step() override;
	void draw(const DrawArgs& args) override;
	void onContextDestroy(const ContextDestroyEvent& e) override;
	void onButton(const ButtonEvent& e) override;
	void onDoubleClick(const DoubleClickEvent& e) override;
	void onDragStart(const DragStartEvent& e) override;
	void onDragMove(const DragMoveEvent& e) override;
	void onDragEnd(const DragEndEvent& e) override;
	void onHoverScroll(const HoverScrollEvent& e) override;

	/** Renders the scene at the view's size. Runs right after the placement textures each frame. */
	void renderModel();
	/** The placement under a point of this widget, or -1. Writes where it's hit, in pixels from the placement's
	top-left corner, to `localPx` if given. Ray-casts against every placement's surface; nearest front-facing hit wins.
	*/
	int pickPlacement(math::Vec pos, math::Vec* localPx = NULL);
	/** Turns the model so a placement faces the camera, upright, over SNAP_SECONDS. Stops any spin. */
	void snapToPlacement(int placementId);
	bool isSnapping() const {
		return snapping;
	}

private:
	bool dragging = false;
	double lastDragMoveTime = 0;
	math::Vec lastButtonPos;
	bool snapping = false;
	render3d::Quat snapFrom;
	render3d::Quat snapTo;
	float snapTime = 0;

	/** NanoVG image wrapping renderer.target.texture. */
	int image = -1;
	GLuint imageTexture = 0;
	int imageWidth = 0;
	int imageHeight = 0;

	void deleteImage(NVGcontext* vg);
};


/** The 3D view, or NULL if it was never shown. */
ModelView* getModelView();
bool isModelViewShown();
/** Showing the 3D view hides the tiles view. */
void setModelViewShown(bool shown);
void toggleModelView();


} // namespace rack3d
} // namespace rack
