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
	/** Angle out of the panel, radians: 0 lies flat on the panel, M_PI / 2 stands straight out. */
	float tilt = M_PI / 4;
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


} // namespace rack3d
} // namespace rack
