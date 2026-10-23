#pragma once
#include <widget/OpaqueWidget.hpp>
#include <rack3d/ModelRenderer.hpp>
#include <rack3d/CableScene.hpp>


namespace rack {
namespace rack3d {


/** The 3D view: the model with every rack placement live on its surface. Toggle with F7.
Like the tiles view, it hides the 2D rack while shown and steps it itself (draw-once rule).
Events over a rack are ray-cast to rack coordinates and handed to the RackWidget, so knobs, ports, menus and module
and cable drags work in 3D. Dragging empty rack space or off the model spins it (it keeps spinning and slows down
after you let go), scrolling zooms unless a module takes it, and double-clicking empty space on a rack turns that rack
to face you.
*/
struct ModelView : widget::OpaqueWidget {
	ModelRenderer renderer;
	/** The 3D cables, simulated in world space as the model turns. */
	CableScene cables;
	/** Spin of the model, in world space: axis * radians per second. */
	render3d::Vec3 angularVelocity;

	static constexpr float DRAG_RADIANS_PER_PX = 0.008f;
	/** Spin decays by e^(-SPIN_DAMPING * seconds). */
	static constexpr float SPIN_DAMPING = 3.f;
	static constexpr float SNAP_SECONDS = 0.4f;
	static constexpr float MIN_DISTANCE = 2.5f;
	static constexpr float MAX_DISTANCE = 20.f;
	/** Rack texture resolution range, in texture pixels per rack pixel. */
	static constexpr float MIN_TEXTURE_SCALE = 1 / 8.f;
	static constexpr float MAX_TEXTURE_SCALE = 2.f;

	~ModelView();
	void step() override;
	void draw(const DrawArgs& args) override;
	void onContextDestroy(const ContextDestroyEvent& e) override;
	void onHover(const HoverEvent& e) override;
	void onButton(const ButtonEvent& e) override;
	void onHoverKey(const HoverKeyEvent& e) override;
	void onHoverText(const HoverTextEvent& e) override;
	void onDragHover(const DragHoverEvent& e) override;
	void onDoubleClick(const DoubleClickEvent& e) override;
	void onDragStart(const DragStartEvent& e) override;
	void onDragMove(const DragMoveEvent& e) override;
	void onDragEnd(const DragEndEvent& e) override;
	void onHoverScroll(const HoverScrollEvent& e) override;

	/** Renders the scene at the view's size. Runs right after the placement textures each frame. */
	void renderModel();
	/** Sets each rack's texture resolution from its on-screen size, rounded up to a power of two, and skips racks
	facing away (they keep their last texture).
	*/
	void updateTextureScales();
	/** The placement under a point of this widget, or -1. Writes where it's hit, in pixels from the placement's
	top-left corner, to `localPx` if given. Ray-casts against every placement's surface; nearest front-facing hit wins.
	*/
	int pickPlacement(math::Vec pos, math::Vec* localPx = NULL);
	/** This widget's coordinates -> rack coordinates of the rack under them. Returns false off every rack. */
	bool toRackPos(math::Vec pos, math::Vec* rackPos);
	/** Rack coordinates -> this widget's coordinates. Returns false if not inside a placement with a surface. */
	bool fromRackPos(math::Vec rackPos, math::Vec* pos);
	/** Where the mouse points in 3D (for the loose ends of cables being dragged). Invalid if it's not over the view
	this frame.
	*/
	Pointer getPointer();
	/** Where a point of this widget points in 3D. */
	Pointer getPointerAt(math::Vec pos);
	/** Turns the model so a placement faces the camera, upright, over SNAP_SECONDS. Stops any spin. */
	void snapToPlacement(int placementId);
	bool isSnapping() const {
		return snapping;
	}

private:
	bool dragging = false;
	double lastDragMoveTime = 0;
	/** Last mouse position over this view, from hover and drag-hover events. Those arrive every frame while the mouse
	is over the view (Window polls the cursor each frame), so `mouseInView` is cleared after each step(). Leave events
	can't be used: forwarding makes rack widgets the hovered ones.
	*/
	math::Vec mousePos;
	bool mouseInView = false;
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
