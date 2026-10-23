#pragma once
#include <vector>
#include <cstdint>

#include <common.hpp>
#include <math.hpp>
#include <window/Window.hpp>


namespace rack {
namespace render3d {


/** Compiles and links a GLSL program. Logs errors and returns 0 on failure. */
GLuint compileProgram(const char* vertSrc, const char* fragSrc);


/** Indexed triangle mesh. Vertex layout: position (vec3, location 0), normal (vec3, 1), uv (vec2, 2). */
struct Mesh {
	GLuint vao = 0;
	GLuint vbo = 0;
	GLuint ibo = 0;
	int indexCount = 0;

	struct Vertex {
		float px, py, pz;
		float nx, ny, nz;
		float u, v;
	};

	void upload(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
	void draw();
	void destroy();
};


/** Offscreen render target: multisampled color + depth, resolved into a plain texture.
Bind(), draw, then resolve() and use `texture`.
*/
struct RenderTarget {
	int width = 0;
	int height = 0;
	int samples = 0;
	/** Multisampled framebuffer, drawn into. */
	GLuint msFbo = 0;
	GLuint msColor = 0;
	GLuint msDepth = 0;
	/** Resolved framebuffer and its color texture (RGBA8, premultiplied alpha). */
	GLuint fbo = 0;
	GLuint texture = 0;

	/** (Re)creates the buffers if the size or sample count changed. Returns false on failure. */
	bool resize(int width, int height, int samples);
	/** Binds the multisampled framebuffer and sets the viewport. */
	void bind();
	/** Resolves into `texture` and binds the default framebuffer. */
	void resolve();
	void destroy();
};


/** Square depth-only render target whose depth texture is sampled with comparison (sampler2DShadow), for shadow maps.
Bind(), draw, then bind the default framebuffer and sample `texture`.
*/
struct DepthTarget {
	int size = 0;
	GLuint fbo = 0;
	/** DEPTH_COMPONENT32F with linear comparison filtering (2x2 PCF in hardware); outside the map reads as lit. */
	GLuint texture = 0;

	/** (Re)creates the target if the size changed. Returns false on failure. */
	bool resize(int size);
	/** Binds the framebuffer, sets the viewport and clears depth. */
	void bind();
	void destroy();
};


} // namespace render3d
} // namespace rack
