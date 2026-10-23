#pragma once
#include <widget/OpaqueWidget.hpp>
#include <rack3d/ModelRenderer.hpp>


namespace rack {
namespace rack3d {


/** The 3D view: the model with every rack placement live on its surface. Toggle with F7.
Like the tiles view, it hides the 2D rack while shown and steps it itself (draw-once rule).
*/
struct ModelView : widget::OpaqueWidget {
	ModelRenderer renderer;

	~ModelView();
	void step() override;
	void draw(const DrawArgs& args) override;
	void onContextDestroy(const ContextDestroyEvent& e) override;

	/** Renders the scene at the view's size. Runs right after the placement textures each frame. */
	void renderModel();

private:
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
