// This source file compiles those annoying implementation-in-header libraries

#include <common.hpp> // for fopen_u8

#define GLEW_STATIC
#define GLEW_NO_GLU
#include <GL/glew.h>

#include <nanovg.h>

// #define NANOVG_GL2_IMPLEMENTATION
#define NANOVG_GL3_IMPLEMENTATION
// #define NANOVG_GLES2_IMPLEMENTATION
// #define NANOVG_GLES3_IMPLEMENTATION
#include <nanovg_gl.h>

#include <nanovg_gl_utils.h>

// ABI shims: plugins built against the stock Rack 2 SDK import the GL2-named NanoVG
// entry points from libRack. Rack now renders with the GL3 backend on a core context,
// so forward them, otherwise those plugins would fail to load.
extern "C" {
NVGcontext* nvgCreateGL2(int flags) {
	return nvgCreateGL3(flags);
}
NVGcontext* nvgCreateSharedGL2(NVGcontext* other, int flags) {
	return nvgCreateSharedGL3(other, flags);
}
void nvgDeleteGL2(NVGcontext* ctx) {
	nvgDeleteGL3(ctx);
}
int nvglCreateImageFromHandleGL2(NVGcontext* ctx, GLuint textureId, int w, int h, int flags) {
	return nvglCreateImageFromHandleGL3(ctx, textureId, w, h, flags);
}
GLuint nvglImageHandleGL2(NVGcontext* ctx, int image) {
	return nvglImageHandleGL3(ctx, image);
}
}

#define BLENDISH_IMPLEMENTATION
#include <blendish.h>

#define NANOSVG_IMPLEMENTATION
#define NANOSVG_ALL_COLOR_KEYWORDS
#include <nanosvg.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
