#pragma once
#include <vector>

#include <render3d/math.hpp>
#include <render3d/gl.hpp>


namespace rack {
namespace rack3d {


struct Layout;
struct PlacementRenderer;


/** Renders the 3D model (for now the cube body) with each placement's live texture on its surface,
into an offscreen multisampled render target.
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
	/** Renders at `width` x `height` pixels into `target`. Leaves the default framebuffer bound. */
	void render(int width, int height, Layout* layout, PlacementRenderer* textures);
	/** Projection * view * model for the last rendered size. */
	render3d::Mat4 getMVP(int width, int height) const;
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

	void updateMeshes(Layout* layout);
};


} // namespace rack3d
} // namespace rack
