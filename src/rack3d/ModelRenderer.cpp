#include <rack3d/ModelRenderer.hpp>
#include <rack3d/Layout.hpp>
#include <rack3d/PlacementRenderer.hpp>


namespace rack {
namespace rack3d {


using render3d::Vec3;
using render3d::Mat4;
using render3d::Mesh;


static const char* VERT_SRC = R"(#version 450 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
uniform mat4 uMvp;
uniform mat4 uModel;
out vec3 vNormal;
out vec2 vUv;
void main() {
	vNormal = mat3(uModel) * aNormal;
	vUv = aUv;
	gl_Position = uMvp * vec4(aPos, 1.0);
}
)";


static const char* FRAG_SRC = R"(#version 450 core
in vec3 vNormal;
in vec2 vUv;
uniform sampler2D uTex;
uniform bool uUseTex;
uniform vec3 uColor;
out vec4 outColor;
void main() {
	vec3 base = uUseTex ? texture(uTex, vUv).rgb : uColor;
	vec3 n = normalize(vNormal);
	// Key light from the upper left front, plus ambient, so faces facing the camera read clearly
	vec3 light = normalize(vec3(-0.35, 0.55, 1.0));
	float diffuse = max(dot(n, light), 0.0);
	outColor = vec4(base * (0.4 + 0.6 * diffuse), 1.0);
}
)";


ModelRenderer::~ModelRenderer() {
	destroy();
}


/** A quad over a placement's surface, wound counter-clockwise seen from outside. */
static void buildFaceMesh(Mesh& mesh, const Placement& p) {
	const Surface& s = p.surface;
	math::Vec size = Layout::getRackRect(p).size;
	Vec3 n = s.normal();
	// Corners in rack px, with texture coordinates. Rack textures store the rack's bottom row first, so v = 1 - y / height.
	math::Vec corners[4] = {math::Vec(0, 0), math::Vec(size.x, 0), math::Vec(size.x, size.y), math::Vec(0, size.y)};
	std::vector<Mesh::Vertex> vertices;
	for (math::Vec c : corners) {
		Vec3 pos = s.toModel(c);
		vertices.push_back({pos.x, pos.y, pos.z, n.x, n.y, n.z, c.x / size.x, 1 - c.y / size.y});
	}
	// Top-left, top-right, bottom-right is clockwise seen from outside, so go the other way
	mesh.upload(vertices, {0, 3, 2, 0, 2, 1});
}


/** An axis-aligned box, faces wound counter-clockwise seen from outside. */
static void buildBoxMesh(Mesh& mesh, Vec3 min, Vec3 max) {
	std::vector<Mesh::Vertex> vertices;
	std::vector<uint32_t> indices;
	// Each face: normal and the two in-plane axes (u x v = normal)
	const Vec3 axes[6][3] = {
		{Vec3(0, 0, 1), Vec3(1, 0, 0), Vec3(0, 1, 0)},
		{Vec3(0, 0, -1), Vec3(-1, 0, 0), Vec3(0, 1, 0)},
		{Vec3(1, 0, 0), Vec3(0, 0, -1), Vec3(0, 1, 0)},
		{Vec3(-1, 0, 0), Vec3(0, 0, 1), Vec3(0, 1, 0)},
		{Vec3(0, 1, 0), Vec3(1, 0, 0), Vec3(0, 0, -1)},
		{Vec3(0, -1, 0), Vec3(1, 0, 0), Vec3(0, 0, 1)},
	};
	Vec3 center = (min + max) * 0.5f;
	Vec3 half = (max - min) * 0.5f;
	auto scale = [&](Vec3 v) {
		return Vec3(v.x * half.x, v.y * half.y, v.z * half.z);
	};
	for (const auto& a : axes) {
		Vec3 n = a[0], u = a[1], v = a[2];
		uint32_t base = vertices.size();
		for (Vec3 c : {Vec3() - u - v, u - v, u + v, Vec3() - u + v}) {
			Vec3 p = center + scale(n + c);
			vertices.push_back({p.x, p.y, p.z, n.x, n.y, n.z, 0, 0});
		}
		for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u})
			indices.push_back(base + i);
	}
	mesh.upload(vertices, indices);
}


void ModelRenderer::updateMeshes(Layout* layout) {
	std::vector<float> key;
	for (const Placement& p : layout->placements) {
		if (!p.hasSurface)
			continue;
		const Surface& s = p.surface;
		math::Rect r = Layout::getRackRect(p);
		key.insert(key.end(), {(float) p.id, s.origin.x, s.origin.y, s.origin.z, s.right.x, s.right.y, s.right.z, s.down.x, s.down.y, s.down.z, s.unitsPerPx, r.size.x, r.size.y});
	}
	if (key == meshKey)
		return;
	meshKey = key;

	for (Face& f : faces)
		f.mesh.destroy();
	faces.clear();
	bool any = false;
	Vec3 min(INFINITY, INFINITY, INFINITY), max(-INFINITY, -INFINITY, -INFINITY);
	for (const Placement& p : layout->placements) {
		if (!p.hasSurface)
			continue;
		faces.push_back(Face());
		faces.back().placementId = p.id;
		buildFaceMesh(faces.back().mesh, p);
		math::Vec size = Layout::getRackRect(p).size;
		for (math::Vec c : {math::Vec(0, 0), math::Vec(size.x, size.y), math::Vec(size.x, 0), math::Vec(0, size.y)}) {
			Vec3 pos = p.surface.toModel(c);
			min = Vec3(std::fmin(min.x, pos.x), std::fmin(min.y, pos.y), std::fmin(min.z, pos.z));
			max = Vec3(std::fmax(max.x, pos.x), std::fmax(max.y, pos.y), std::fmax(max.z, pos.z));
			any = true;
		}
	}
	// Until models are loaded from files, the body is the placements' bounding box
	body.destroy();
	if (any)
		buildBoxMesh(body, min, max);
}


Mat4 ModelRenderer::getMVP(int width, int height) const {
	float aspect = height > 0 ? (float) width / height : 1.f;
	Mat4 proj = Mat4::perspective(fovY, aspect, 0.1f, 100.f);
	Mat4 view = Mat4::lookAt(Vec3(0, 0, distance), Vec3(0, 0, 0), Vec3(0, 1, 0));
	return proj * view * Mat4::rotate(rotation);
}


void ModelRenderer::render(int width, int height, Layout* layout, PlacementRenderer* textures) {
	if (!program) {
		program = render3d::compileProgram(VERT_SRC, FRAG_SRC);
		if (!program)
			return;
	}
	if (!target.resize(width, height, samples))
		return;
	updateMeshes(layout);

	target.bind();
	// Premultiplied clear color
	float a = background[3];
	glClearColor(background[0] * a, background[1] * a, background[2] * a, a);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	glFrontFace(GL_CCW);
	glDisable(GL_BLEND);

	glUseProgram(program);
	Mat4 mvp = getMVP(width, height);
	Mat4 model = Mat4::rotate(rotation);
	glUniformMatrix4fv(glGetUniformLocation(program, "uMvp"), 1, GL_FALSE, mvp.m);
	glUniformMatrix4fv(glGetUniformLocation(program, "uModel"), 1, GL_FALSE, model.m);
	glUniform1i(glGetUniformLocation(program, "uTex"), 0);
	GLint useTexLoc = glGetUniformLocation(program, "uUseTex");
	GLint colorLoc = glGetUniformLocation(program, "uColor");

	// Body
	glUniform1i(useTexLoc, 0);
	glUniform3f(colorLoc, 0.05f, 0.05f, 0.055f);
	body.draw();

	// Faces, pulled toward the camera so they win against the body they lie on
	glEnable(GL_POLYGON_OFFSET_FILL);
	glPolygonOffset(-1.f, -1.f);
	glActiveTexture(GL_TEXTURE0);
	for (Face& f : faces) {
		const PlacementRenderer::Target* t = textures ? textures->getTarget(f.placementId) : NULL;
		if (t) {
			glBindTexture(GL_TEXTURE_2D, t->fb->texture);
			glUniform1i(useTexLoc, 1);
		}
		else {
			glUniform1i(useTexLoc, 0);
			glUniform3f(colorLoc, 0.2f, 0.2f, 0.2f);
		}
		f.mesh.draw();
	}
	glDisable(GL_POLYGON_OFFSET_FILL);

	// Leave state as NanoVG expects it
	glBindTexture(GL_TEXTURE_2D, 0);
	glUseProgram(0);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	target.resolve();
}


void ModelRenderer::destroy() {
	for (Face& f : faces)
		f.mesh.destroy();
	faces.clear();
	meshKey.clear();
	body.destroy();
	if (program)
		glDeleteProgram(program);
	program = 0;
	target.destroy();
}


} // namespace rack3d
} // namespace rack
