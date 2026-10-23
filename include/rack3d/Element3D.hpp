#pragma once
#include <widget/Widget.hpp>


namespace rack {
namespace rack3d {


/** A part of a module's panel that stands out of the panel in Rack 3D's 3D view: a display fin, a raised screen, a
flap. Add it to a ModuleWidget like any other widget and draw its content in drawContent() (or give it children).

- In the 2D rack it draws flat in its box, like any widget, so the module works everywhere.
- In the 3D view it is left out of the rack's texture and drawn on its own quad of `box.size` panel pixels, hinged to
  the panel along one edge of its box (`hinge`), tilted out by `tilt` and lifted `lift` pixels off the panel. The quad
  is lit and shadowed like the rest of the scene and seen from both sides.

Change `tilt` and `lift` in step(), for instance from a knob, to move it; the content is redrawn every frame, so it can
show live state. Clicks in the 3D view go to the panel behind it.

	struct Fin : rack3d::Element3D {
		MyModule* module;
		void step() override {
			if (module)
				tilt = module->params[MyModule::ANGLE_PARAM].getValue() * M_PI / 2;
			Element3D::step();
		}
		void drawContent(const DrawArgs& args) override {
			// NanoVG drawing in box coordinates
		}
	};
*/
struct Element3D : widget::Widget {
	enum Hinge {
		/** Swings out around its bottom edge, its top edge rising off the panel (like a laptop screen opening). */
		HINGE_BOTTOM,
		HINGE_TOP,
		HINGE_LEFT,
		HINGE_RIGHT,
	};
	Hinge hinge = HINGE_BOTTOM;
	/** Angle out of the panel, radians: 0 lies flat on the panel, M_PI / 2 stands straight out. Past M_PI / 2 it
	leans back over the panel on the other side of the hinge.
	*/
	float tilt = M_PI / 4;
	/** Turns the element in the panel's plane around the middle of its hinge edge, radians, clockwise as seen on the
	panel. With HINGE_BOTTOM and the hinge's middle on a circle, angle 2 * M_PI * k / n points n elements outward
	like petals. Only the 3D view uses it: in 2D the box is drawn unturned (see `flatIn2D`).
	*/
	float angle = 0;
	/** Draw the content flat in the box in the 2D rack. Turn off for elements that only make sense in 3D (e.g. turned
	ones), and draw a 2D version some other way.
	*/
	bool flatIn2D = true;
	/** Bends the element along its length, from the hinge to the opposite edge: the total angle it turns through,
	radians. Positive curls toward its front (the side its content faces), negative away. 0 is flat.
	*/
	float curl = 0;
	/** Bends the element across its width, along the hinge: the angle of the arc, radians. Positive raises the side
	edges toward the front, like a scoop. 0 is flat.
	*/
	float cup = 0;
	/** How far the hinge stands in front of the panel, in panel pixels. */
	float lift = 0;
	/** Texture pixels per panel pixel in the 3D view. Raise it for crisp small text on large elements. */
	float resolution = 2;

	/** Draws the element's content, in box coordinates. Called for the 2D rack and for the element's own texture in
	the 3D view. The default draws the children.
	*/
	virtual void drawContent(const DrawArgs& args);

	/** Draws the content, except while Rack 3D draws the rack's textures (the element gets its own quad there). */
	void draw(const DrawArgs& args) override;
	void drawLayer(const DrawArgs& args, int layer) override;
};


/** True while Rack 3D draws the rack's textures for the 3D view (Element3Ds are left out of them). A widget can use
it to draw something in the 2D rack only, e.g. a flat stand-in for its module's 3D elements.
*/
bool isDrawingRackTextures();


} // namespace rack3d
} // namespace rack
