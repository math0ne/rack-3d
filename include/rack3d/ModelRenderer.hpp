#pragma once
#include <vector>

#include <render3d/math.hpp>
#include <render3d/gl.hpp>


namespace rack {
namespace rack3d {


struct Layout;
struct PlacementRenderer;
struct CableScene;


/** A knob to draw in 3D, standing on its rack's face over the 2D knob. Model space. */
struct KnobDraw {
	render3d::Vec3 center;
	/** The rack's right, up and outward normal at the knob. */
	render3d::Vec3 right;
	render3d::Vec3 up;
	render3d::Vec3 normal;
	/** Half the 2D knob's width, in model units. */
	float radius = 0;
	/** Rack's knob angle in radians: 0 points up, positive turns clockwise as seen on the panel. */
	float angle = 0;
	enum Style {
		TRIMPOT,
		SMALL,
		MEDIUM,
		LARGE,
		NUM_STYLES
	};
	Style style = MEDIUM;

	/** The style for a 2D knob `diameterPx` rack pixels wide (Rack's Trimpot ~18, RoundSmall ~23, RoundBlack ~28,
	RoundLarge ~36, RoundBig 45, RoundHuge ~54).
	*/
	static Style styleForDiameter(float diameterPx) {
		if (diameterPx < 20)
			return TRIMPOT;
		if (diameterPx < 32)
			return SMALL;
		if (diameterPx < 48)
			return MEDIUM;
		return LARGE;
	}
};


/** A slider handle or switch to draw in 3D over its 2D widget. Model space. */
struct PartDraw {
	enum Kind {
		/** A slider's handle cap, at the handle's current position. */
		SLIDER_HANDLE,
		/** A push button (momentary) or latching button: a cap that sinks when pressed or latched. */
		BUTTON,
		/** A button with a light in it: just a raised bezel, so the light stays visible. */
		LIGHT_BUTTON,
		/** A toggle switch: a nut and a lever tilted along the switch's long side. */
		TOGGLE,
	};
	Kind kind = BUTTON;
	render3d::Vec3 center;
	render3d::Vec3 right;
	render3d::Vec3 up;
	render3d::Vec3 normal;
	/** The 2D widget's (or handle's) size, in model units. */
	float width = 0;
	float height = 0;
	/** Buttons: pressed or latched on. */
	bool pressed = false;
	/** Toggles: position 0..1 (0 = down / left, 1 = up / right). */
	float position = 0;

	/** How to draw a Switch widget `width` x `height` rack px: noticeably elongated ones are toggles. */
	static Kind kindForSwitch(float width, float height, bool hasLight) {
		float aspect = std::fmax(width, height) / std::fmax(1e-3f, std::fmin(width, height));
		if (aspect >= 1.4f)
			return TOGGLE;
		return hasLight ? LIGHT_BUTTON : BUTTON;
	}
};


/** Renders the 3D model (for now the cube body) with each placement's live texture on its surface,
3D knobs, slider handles and switches over the 2D ones, and the cables, into an offscreen multisampled render target.
*/
struct ModelRenderer {
	/** Model orientation. */
	render3d::Quat rotation;
	/** Camera distance from the model's centre, looking at it along -z. */
	float distance = 6.f;
	/** Vertical field of view, radians. */
	float fovY = 35.f * M_PI / 180.f;
	/** Background color (straight, not premultiplied). */
	float background[4] = {0.08f, 0.08f, 0.08f, 1.f};
	int samples = 4;

	render3d::RenderTarget target;

	~ModelRenderer();
	/** Renders at `width` x `height` pixels into `target`, with `cables` (world space) if given.
	Leaves the default framebuffer bound.
	*/
	void render(int width, int height, Layout* layout, PlacementRenderer* textures, const CableScene* cables = NULL, const std::vector<KnobDraw>* knobs = NULL, const std::vector<PartDraw>* parts = NULL);
	/** Projection * view * model. */
	render3d::Mat4 getMVP(int width, int height) const;
	/** Projection * view, for things in world space (cables). */
	render3d::Mat4 getViewProjection(int width, int height) const;
	/** Frees GL objects. Call before the GL context goes away. */
	void destroy();

private:
	GLuint program = 0;
	render3d::Mesh body;
	struct Face {
		int placementId;
		render3d::Mesh mesh;
	};
	std::vector<Face> faces;
	/** Surfaces the face meshes were built from, to rebuild them when the layout changes. */
	std::vector<float> meshKey;
	/** One tube mesh per cable, rebuilt every frame; one unit cylinder for every plug. */
	std::vector<render3d::Mesh> cableMeshes;
	render3d::Mesh plugMesh;

	/** One mesh per knob style (unit radius, standing on z = 0), plus a grey cap insert, and a unit box for pointers. */
	render3d::Mesh knobMeshes[KnobDraw::NUM_STYLES];
	render3d::Mesh knobCapMesh;
	render3d::Mesh boxMesh;
	/** Switch parts: button cap, bezel ring, toggle nut, toggle lever (unit sizes). */
	render3d::Mesh buttonMesh;
	render3d::Mesh bezelMesh;
	render3d::Mesh nutMesh;
	render3d::Mesh leverMesh;

	void updateMeshes(Layout* layout);
	void drawCables(const CableScene* cables, const render3d::Mat4& viewProj);
	void drawKnobs(const std::vector<KnobDraw>& knobs, const render3d::Mat4& viewProj);
	void drawParts(const std::vector<PartDraw>& parts, const render3d::Mat4& viewProj);
};


} // namespace rack3d
} // namespace rack
