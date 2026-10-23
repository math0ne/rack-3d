#include <rack3d/ModelRenderer.hpp>
#include <rack3d/Layout.hpp>
#include <rack3d/PlacementRenderer.hpp>
#include <rack3d/CableScene.hpp>


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


Mat4 ModelRenderer::getViewProjection(int width, int height) const {
	float aspect = height > 0 ? (float) width / height : 1.f;
	Mat4 proj = Mat4::perspective(fovY, aspect, 0.1f, 100.f);
	Mat4 view = Mat4::lookAt(Vec3(0, 0, distance), Vec3(0, 0, 0), Vec3(0, 1, 0));
	return proj * view;
}


Mat4 ModelRenderer::getMVP(int width, int height) const {
	return getViewProjection(width, height) * Mat4::rotate(rotation);
}


/** Sides around a cable tube and a plug. */
static const int TUBE_SIDES = 10;
static const int PLUG_SIDES = 16;


/** A tube of radius `r` along `points`, using parallel-transport frames so it doesn't twist. */
static void buildTube(Mesh& mesh, const std::vector<Vec3>& points, float r) {
	int n = points.size();
	if (n < 2)
		return;
	std::vector<Mesh::Vertex> vertices;
	vertices.reserve(n * TUBE_SIDES);
	Vec3 t = (points[1] - points[0]).normalize();
	Vec3 normal = Vec3(t.y, -t.x, t.z).cross(t);
	if (normal.norm() < 1e-4f)
		normal = Vec3(0, t.z, -t.y);
	normal = normal.normalize();
	for (int i = 0; i < n; i++) {
		t = (points[std::min(i + 1, n - 1)] - points[std::max(i - 1, 0)]).normalize();
		normal = (normal - t * t.dot(normal)).normalize();
		Vec3 binormal = t.cross(normal);
		for (int j = 0; j < TUBE_SIDES; j++) {
			float a = 2 * M_PI * j / TUBE_SIDES;
			Vec3 d = normal * std::cos(a) + binormal * std::sin(a);
			Vec3 p = points[i] + d * r;
			vertices.push_back({p.x, p.y, p.z, d.x, d.y, d.z, 0, 0});
		}
	}
	std::vector<uint32_t> indices;
	indices.reserve((n - 1) * TUBE_SIDES * 6);
	for (int i = 0; i < n - 1; i++) {
		for (int j = 0; j < TUBE_SIDES; j++) {
			uint32_t a = i * TUBE_SIDES + j;
			uint32_t b = i * TUBE_SIDES + (j + 1) % TUBE_SIDES;
			uint32_t c = a + TUBE_SIDES;
			uint32_t d = b + TUBE_SIDES;
			indices.insert(indices.end(), {a, c, b, b, c, d});
		}
	}
	mesh.upload(vertices, indices);
}


/** A closed cylinder of radius 1 from z = 0 to z = 1. */
static void buildCylinder(Mesh& mesh) {
	std::vector<Mesh::Vertex> vertices;
	std::vector<uint32_t> indices;
	for (int j = 0; j < PLUG_SIDES; j++) {
		float a = 2 * M_PI * j / PLUG_SIDES;
		float c = std::cos(a), s = std::sin(a);
		vertices.push_back({c, s, 0, c, s, 0, 0, 0});
		vertices.push_back({c, s, 1, c, s, 0, 0, 0});
	}
	for (int j = 0; j < PLUG_SIDES; j++) {
		uint32_t a = 2 * j, b = 2 * ((j + 1) % PLUG_SIDES);
		indices.insert(indices.end(), {a, b, a + 1, b, b + 1, a + 1});
	}
	// Caps
	for (float z : {0.f, 1.f}) {
		uint32_t center = vertices.size();
		float nz = z > 0 ? 1.f : -1.f;
		vertices.push_back({0, 0, z, 0, 0, nz, 0, 0});
		for (int j = 0; j < PLUG_SIDES; j++) {
			float a = 2 * M_PI * j / PLUG_SIDES;
			vertices.push_back({std::cos(a), std::sin(a), z, 0, 0, nz, 0, 0});
		}
		for (int j = 0; j < PLUG_SIDES; j++)
			indices.insert(indices.end(), {center, center + 1 + j, center + 1 + (j + 1) % PLUG_SIDES});
	}
	mesh.upload(vertices, indices);
}


/** A solid of revolution around +z from a profile of (radius, z) points running from the base's outer edge up to
the top's centre. Over `ridgeZ0`..`ridgeZ1` the radius is grooved by `ridges` grip ridges `ridgeDepth` deep.
*/
static void buildRevolved(Mesh& mesh, const std::vector<math::Vec>& profile, int ridges = 0, float ridgeDepth = 0, float ridgeZ0 = 0, float ridgeZ1 = 0) {
	const int sides = ridges > 0 ? ridges * 4 : 32;
	std::vector<Mesh::Vertex> vertices;
	std::vector<uint32_t> indices;
	auto radiusAt = [&](float r, float z, float theta, float* dr) {
		*dr = 0;
		if (ridges <= 0 || z < ridgeZ0 || z > ridgeZ1)
			return r;
		*dr = -r * ridgeDepth * ridges * std::sin(ridges * theta);
		return r * (1 + ridgeDepth * std::cos(ridges * theta));
	};
	for (size_t k = 0; k + 1 < profile.size(); k++) {
		math::Vec a = profile[k], b = profile[k + 1];
		// The profile segment's outward normal in (radius, z): perpendicular to (dr, dz)
		math::Vec d = b.minus(a);
		math::Vec n2 = math::Vec(d.y, -d.x).normalize();
		uint32_t base = vertices.size();
		for (int j = 0; j <= sides; j++) {
			float theta = 2 * M_PI * j / sides;
			float c = std::cos(theta), s = std::sin(theta);
			for (math::Vec p : {a, b}) {
				float dr;
				float r = radiusAt(p.x, p.y, theta, &dr);
				// Ridges tilt the normal around the axis
				Vec3 radial(c, s, 0), tangent(-s, c, 0);
				Vec3 n = radial * n2.x + Vec3(0, 0, n2.y) - tangent * (r > 0 ? dr / r * n2.x : 0);
				n = n.normalize();
				vertices.push_back({r * c, r * s, p.y, n.x, n.y, n.z, 0, 0});
			}
		}
		for (int j = 0; j < sides; j++) {
			uint32_t i0 = base + 2 * j, i1 = i0 + 1, i2 = i0 + 2, i3 = i0 + 3;
			indices.insert(indices.end(), {i0, i2, i1, i1, i2, i3});
		}
	}
	mesh.upload(vertices, indices);
}


/** A unit box: x in [-0.5, 0.5], y in [0, 1], z in [0, 1]. */
static void buildUnitBox(Mesh& mesh) {
	buildBoxMesh(mesh, Vec3(-0.5f, 0, 0), Vec3(0.5f, 1, 1));
}


struct KnobStyleInfo {
	/** Body color, pointer color. */
	Vec3 body, pointer;
	/** Pointer: from radius y0 to y1, `width` wide, on top at `top` (all in knob radii). */
	float y0, y1, width, top;
};


static const KnobStyleInfo KNOB_STYLES[KnobDraw::NUM_STYLES] = {
	// Trimpot: low silver cap with a dark slot across it
	{Vec3(0.72f, 0.73f, 0.75f), Vec3(0.15f, 0.15f, 0.16f), -0.75f, 0.75f, 0.14f, 0.32f},
	// Small: black, slim skirt, white pointer
	{Vec3(0.08f, 0.08f, 0.09f), Vec3(0.92f, 0.92f, 0.92f), 0.2f, 0.72f, 0.09f, 0.6f},
	// Medium: taller black with a wider skirt
	{Vec3(0.08f, 0.08f, 0.09f), Vec3(0.92f, 0.92f, 0.92f), 0.15f, 0.74f, 0.08f, 0.76f},
	// Large: gripped black with a grey cap insert
	{Vec3(0.07f, 0.07f, 0.08f), Vec3(0.95f, 0.95f, 0.95f), 0.1f, 0.52f, 0.07f, 0.67f},
};


static void buildKnobMeshes(Mesh meshes[KnobDraw::NUM_STYLES], Mesh& cap) {
	using P = math::Vec;
	buildRevolved(meshes[KnobDraw::TRIMPOT], {P(0.95f, 0), P(0.95f, 0.25f), P(0.82f, 0.32f), P(0, 0.32f)});
	buildRevolved(meshes[KnobDraw::SMALL], {P(1, 0), P(1, 0.12f), P(0.82f, 0.16f), P(0.78f, 0.55f), P(0.7f, 0.6f), P(0, 0.6f)});
	buildRevolved(meshes[KnobDraw::MEDIUM], {P(1, 0), P(1, 0.14f), P(0.85f, 0.18f), P(0.8f, 0.7f), P(0.72f, 0.76f), P(0, 0.76f)}, 24, 0.015f, 0.18f, 0.7f);
	buildRevolved(meshes[KnobDraw::LARGE], {P(1, 0), P(1, 0.1f), P(0.9f, 0.14f), P(0.88f, 0.6f), P(0.8f, 0.66f), P(0, 0.66f)}, 20, 0.045f, 0.14f, 0.6f);
	buildRevolved(cap, {P(0.6f, 0), P(0.6f, 0.012f), P(0, 0.012f)});
}


void ModelRenderer::drawKnobs(const std::vector<KnobDraw>& knobs, const Mat4& viewProj) {
	if (knobs.empty())
		return;
	if (!knobMeshes[0].vao) {
		buildKnobMeshes(knobMeshes, knobCapMesh);
		buildUnitBox(boxMesh);
	}
	GLint mvpLoc = glGetUniformLocation(program, "uMvp");
	GLint modelLoc = glGetUniformLocation(program, "uModel");
	GLint colorLoc = glGetUniformLocation(program, "uColor");
	glUniform1i(glGetUniformLocation(program, "uUseTex"), 0);
	glDisable(GL_CULL_FACE);
	Mat4 modelRotation = Mat4::rotate(rotation);

	auto draw = [&](Mesh& mesh, const Mat4& model, Vec3 color) {
		Mat4 mvp = viewProj * model;
		glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, mvp.m);
		glUniformMatrix4fv(modelLoc, 1, GL_FALSE, model.m);
		glUniform3f(colorLoc, color.x, color.y, color.z);
		mesh.draw();
	};

	for (const KnobDraw& k : knobs) {
		// The knob's frame: x = rack right, y = rack up, z = out of the face
		Mat4 frame;
		frame.m[0] = k.right.x;
		frame.m[1] = k.right.y;
		frame.m[2] = k.right.z;
		frame.m[4] = k.up.x;
		frame.m[5] = k.up.y;
		frame.m[6] = k.up.z;
		frame.m[8] = k.normal.x;
		frame.m[9] = k.normal.y;
		frame.m[10] = k.normal.z;
		frame.m[12] = k.center.x;
		frame.m[13] = k.center.y;
		frame.m[14] = k.center.z;
		// Rack angles turn clockwise as seen on the panel; around the outward normal that's negative
		Mat4 knob = modelRotation * frame * Mat4::rotate(render3d::Quat::fromAxisAngle(Vec3(0, 0, 1), -k.angle)) * Mat4::scale(Vec3(k.radius, k.radius, k.radius));
		const KnobStyleInfo& style = KNOB_STYLES[k.style];
		draw(knobMeshes[k.style], knob, style.body);
		if (k.style == KnobDraw::LARGE)
			draw(knobCapMesh, knob * Mat4::translate(Vec3(0, 0, style.top)), Vec3(0.32f, 0.32f, 0.34f));
		Mat4 pointer = knob * Mat4::translate(Vec3(0, style.y0, style.top)) * Mat4::scale(Vec3(style.width, style.y1 - style.y0, 0.02f));
		draw(boxMesh, pointer, style.pointer);
	}
}


void ModelRenderer::drawCables(const CableScene* cables, const Mat4& viewProj) {
	GLint mvpLoc = glGetUniformLocation(program, "uMvp");
	GLint modelLoc = glGetUniformLocation(program, "uModel");
	GLint colorLoc = glGetUniformLocation(program, "uColor");
	glUniform1i(glGetUniformLocation(program, "uUseTex"), 0);
	// Tubes and plugs are drawn from both sides
	glDisable(GL_CULL_FACE);

	if (cableMeshes.size() < cables->entries.size())
		cableMeshes.resize(cables->entries.size());
	if (!plugMesh.vao)
		buildCylinder(plugMesh);
	const RopeParams& params = cables->params;

	size_t i = 0;
	for (const auto& pair : cables->entries) {
		const CableScene::Entry& e = pair.second;
		const Rope& rope = e.rope;
		if (rope.count() < 4)
			continue;
		// Tube from the back of one plug to the back of the other
		std::vector<Vec3> points(rope.P.begin() + 1, rope.P.end() - 1);
		buildTube(cableMeshes[i], points, params.radius);
		Mat4 identity;
		glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, viewProj.m);
		glUniformMatrix4fv(modelLoc, 1, GL_FALSE, identity.m);
		glUniform3f(colorLoc, e.color.r, e.color.g, e.color.b);
		cableMeshes[i].draw();
		i++;

		// Plugs: dark cylinders from the tip (in the jack) to the back
		glUniform3f(colorLoc, 0.12f, 0.12f, 0.13f);
		int n = rope.count();
		for (int end = 0; end < 2; end++) {
			Vec3 tip = rope.P[end == 0 ? 0 : n - 1];
			Vec3 back = rope.P[end == 0 ? 1 : n - 2];
			Vec3 axis = back - tip;
			float length = axis.norm();
			if (length < 1e-6f)
				continue;
			float plugRadius = params.radius * 2.2f;
			Mat4 model = Mat4::translate(tip) * Mat4::rotate(render3d::Quat::fromTo(Vec3(0, 0, 1), axis)) * Mat4::scale(Vec3(plugRadius, plugRadius, length));
			Mat4 mvp = viewProj * model;
			glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, mvp.m);
			glUniformMatrix4fv(modelLoc, 1, GL_FALSE, model.m);
			plugMesh.draw();
		}
	}
}


void ModelRenderer::render(int width, int height, Layout* layout, PlacementRenderer* textures, const CableScene* cables, const std::vector<KnobDraw>* knobs) {
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

	if (knobs)
		drawKnobs(*knobs, getViewProjection(width, height));
	if (cables)
		drawCables(cables, getViewProjection(width, height));

	// Leave state as NanoVG expects it
	glBindTexture(GL_TEXTURE_2D, 0);
	glUseProgram(0);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	target.resolve();
}


void ModelRenderer::destroy() {
	for (render3d::Mesh& m : cableMeshes)
		m.destroy();
	cableMeshes.clear();
	plugMesh.destroy();
	for (render3d::Mesh& m : knobMeshes)
		m.destroy();
	knobCapMesh.destroy();
	boxMesh.destroy();
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
