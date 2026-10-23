#include <rack3d/ModelRenderer.hpp>
#include <rack3d/Layout.hpp>
#include <rack3d/PlacementRenderer.hpp>
#include <rack3d/CableScene.hpp>
#include <rack3d/Environment.hpp>


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
// Pushes vertices out along their normals (the cable glow halo is the tube, inflated)
uniform float uInflate;
out vec3 vNormal;
out vec2 vUv;
out vec3 vWorld;
void main() {
	vec3 pos = aPos + aNormal * uInflate;
	vNormal = mat3(uModel) * aNormal;
	vUv = aUv;
	vWorld = (uModel * vec4(pos, 1.0)).xyz;
	gl_Position = uMvp * vec4(pos, 1.0);
}
)";


/** Lighting is done in linear color and written back gamma encoded. Materials: 0 flat color, 1 rack texture,
2 concrete (procedural, from world position), 3 sky, 4 glow halo (added on top, brightest where the tube faces you).
*/
static const char* FRAG_SRC = R"(#version 450 core
in vec3 vNormal;
in vec2 vUv;
in vec3 vWorld;
uniform sampler2D uTex;
uniform int uMaterial;
uniform vec3 uColor;
uniform float uSpecular;
// Light the surface gives off itself, as a fraction of its color
uniform float uEmissive;
uniform vec3 uCamPos;
uniform bool uEnvironment;
uniform vec3 uSunDir;
uniform float uFog;
uniform float uFloorY;
uniform mat4 uLightNear;
uniform mat4 uLightFar;
uniform sampler2DShadow uShadowNear;
uniform sampler2DShadow uShadowFar;
uniform float uShadowTexel;
uniform float uFogStart;
// Overall brightness, applied to everything before gamma
uniform float uExposure;
// Contrast: uFill scales the soft light (ambient, camera-side key, the sky and haze), uSunBoost the sun (direct light,
// its glow, shafts, highlights). Emissive light (cable glow, LEDs) is left alone.
uniform float uFill;
uniform float uSunBoost;
out vec4 outColor;

// Hazy late sun, a soft key from the camera side so panels stay readable, and sky / ground ambient
const vec3 SUN_COLOR = vec3(1.0, 0.82, 0.6) * 2.1;
const vec3 KEY_DIR = normalize(vec3(0.35, 0.45, 1.0));
const vec3 KEY_COLOR = vec3(0.8, 0.86, 1.0) * 0.42;
const vec3 SKY_AMBIENT = vec3(0.3, 0.35, 0.45);
const vec3 GROUND_AMBIENT = vec3(0.11, 0.1, 0.09);

vec3 toLinear(vec3 c) { return pow(max(c, 0.0), vec3(2.2)); }
// Soft shoulder above 0.8 so sunlit white panels roll off instead of clipping, then gamma
vec3 toGamma(vec3 c) {
	c = max(c * uExposure, 0.0);
	vec3 over = max(c - 0.8, 0.0);
	c = min(c, 0.8) + 0.2 * (1.0 - exp(-over / 0.2));
	return pow(c, vec3(1.0 / 2.2));
}

float hash(vec3 p) {
	p = fract(p * 0.3183099 + 0.1);
	p *= 17.0;
	return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float noise(vec3 x) {
	vec3 i = floor(x);
	vec3 f = fract(x);
	f = f * f * (3.0 - 2.0 * f);
	return mix(mix(mix(hash(i), hash(i + vec3(1, 0, 0)), f.x), mix(hash(i + vec3(0, 1, 0)), hash(i + vec3(1, 1, 0)), f.x), f.y),
		mix(mix(hash(i + vec3(0, 0, 1)), hash(i + vec3(1, 0, 1)), f.x), mix(hash(i + vec3(0, 1, 1)), hash(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}

float fbm(vec3 p) {
	float v = 0.0, a = 0.5;
	for (int i = 0; i < 4; i++) {
		v += a * noise(p);
		p *= 2.03;
		a *= 0.5;
	}
	return v;
}

// Board-formed concrete: plank seams, form panel joints, tie holes, rain streaks, darker toward the ground
vec3 concrete(vec3 p, vec3 n) {
	vec3 an = abs(n);
	float stain = fbm(p * vec3(0.35, 0.09, 0.35));
	float grain = noise(p * 11.0);
	float c = 0.2 + 0.12 * (stain - 0.5) + 0.04 * (grain - 0.5);
	if (an.y < 0.5) {
		// Wall coordinates: along the wall, up
		vec2 uv = an.x > an.z ? p.zy : p.xy;
		float board = abs(fract(uv.y / 0.22) - 0.5);
		c -= 0.02 * smoothstep(0.42, 0.5, board);
		vec2 panel = abs(fract(uv / vec2(2.4, 1.32)) - 0.5);
		c -= 0.05 * max(smoothstep(0.488, 0.5, panel.x), smoothstep(0.48, 0.5, panel.y));
		vec2 tie = (fract(uv / vec2(0.6, 0.66)) - 0.5) * vec2(0.6, 0.66);
		c -= 0.12 * (1.0 - smoothstep(0.022, 0.034, length(tie)));
		c *= mix(0.72, 1.0, clamp((p.y - uFloorY) / 2.0, 0.0, 1.0));
	}
	else {
		// Paving slabs
		vec2 panel = abs(fract(p.xz / 3.0) - 0.5);
		c -= 0.04 * max(smoothstep(0.492, 0.5, panel.x), smoothstep(0.492, 0.5, panel.y));
	}
	return vec3(c) * vec3(1.0, 0.98, 0.94);
}

// The haze: horizon to zenith, warmer and brighter toward the sun
vec3 skyColor(vec3 dir) {
	float sun = max(dot(dir, uSunDir), 0.0);
	vec3 horizon = vec3(0.24, 0.24, 0.25);
	vec3 zenith = vec3(0.05, 0.07, 0.1);
	vec3 c = mix(horizon, zenith, clamp(dir.y * 1.5, 0.0, 1.0));
	c = mix(c, vec3(0.22, 0.21, 0.2), clamp(-dir.y * 3.0, 0.0, 1.0));
	c *= uFill;
	c += vec3(1.0, 0.72, 0.42) * (0.3 * pow(sun, 5.0) + 0.9 * pow(sun, 60.0)) * uSunBoost;
	return c;
}

float shadowIn(sampler2DShadow map, mat4 light, vec3 p, out bool inside) {
	vec3 q = (light * vec4(p, 1.0)).xyz * 0.5 + 0.5;
	inside = all(greaterThan(q, vec3(0.01))) && all(lessThan(q, vec3(0.99)));
	if (!inside)
		return 1.0;
	float sum = 0.0;
	for (int x = -1; x <= 1; x++)
		for (int y = -1; y <= 1; y++)
			sum += texture(map, vec3(q.xy + vec2(x, y) * uShadowTexel, q.z));
	return sum / 9.0;
}

float sunShadow(vec3 p, vec3 n) {
	if (!uEnvironment)
		return 1.0;
	bool inside;
	float s = shadowIn(uShadowNear, uLightNear, p + n * 0.01, inside);
	if (inside)
		return s;
	return shadowIn(uShadowFar, uLightFar, p + n * 0.12, inside);
}

// Height fog: thick near the floor, thinning upward, starting behind the model so it stays crisp at any zoom
float fogAmount(vec3 p) {
	vec3 d = p - uCamPos;
	float dist = length(d);
	float t = max(dist - uFogStart, 0.0);
	float k = d.y / max(dist, 1e-4) * 0.16;
	float h0 = exp(-(uCamPos.y - uFloorY) * 0.16);
	float integral = abs(k) < 1e-4 ? t * h0 : h0 * (1.0 - exp(-t * k)) / k;
	return 1.0 - exp(-0.022 * uFog * integral);
}

// Light shafts: sunlit haze along the view ray, marched through the wide shadow map
vec3 shafts(vec3 p) {
	vec3 d = p - uCamPos;
	float dist = min(length(d), 70.0);
	vec3 dir = normalize(d);
	const int STEPS = 24;
	float stepLen = dist / float(STEPS);
	float jitter = hash(vec3(gl_FragCoord.xy, 0.0));
	float lit = 0.0;
	for (int i = 0; i < STEPS; i++) {
		vec3 q = uCamPos + dir * (float(i) + jitter) * stepLen;
		vec3 s = (uLightFar * vec4(q, 1.0)).xyz * 0.5 + 0.5;
		float sun = texture(uShadowFar, s);
		lit += sun * exp(-max(q.y - uFloorY, 0.0) * 0.12) * stepLen;
	}
	// Mostly forward scattering: shafts show when looking toward the sun
	float phase = 0.06 + 1.4 * pow(max(dot(dir, uSunDir), 0.0), 8.0);
	return SUN_COLOR * vec3(1.0, 0.88, 0.72) * lit * phase * 0.003 * uFog * uSunBoost;
}

void main() {
	vec3 viewDir = normalize(vWorld - uCamPos);
	if (uMaterial == 3) {
		vec3 c = skyColor(viewDir);
		if (uEnvironment)
			c += shafts(vWorld);
		outColor = vec4(toGamma(c), 1.0);
		return;
	}
	if (uMaterial == 4) {
		float facing = abs(dot(normalize(vNormal), viewDir));
		float glow = pow(facing, 2.0) * uEmissive;
		outColor = vec4(toLinear(uColor) * glow * uExposure, 0.0);
		return;
	}
	vec3 base;
	// Light the surface gives off: cable glow, and LEDs and displays on panels (bright, strongly colored pixels)
	vec3 emitted = vec3(0.0);
	if (uMaterial == 1) {
		vec3 t = texture(uTex, vUv).rgb;
		base = toLinear(t);
		float hi = max(t.r, max(t.g, t.b)), lo = min(t.r, min(t.g, t.b));
		float saturation = hi > 0.0 ? (hi - lo) / hi : 0.0;
		emitted = base * smoothstep(0.55, 0.8, saturation) * smoothstep(0.45, 0.75, hi) * 0.9;
	}
	else if (uMaterial == 2)
		base = concrete(vWorld, normalize(vNormal));
	else
		base = toLinear(uColor);
	vec3 n = normalize(vNormal);
	// Light the side facing the camera, so thin parts drawn from both sides don't go black
	if (dot(n, viewDir) > 0.0 && uMaterial != 2)
		n = -n;
	vec3 toCam = -viewDir;
	float sunDiffuse = max(dot(n, uSunDir), 0.0) * sunShadow(vWorld, n);
	float keyDiffuse = max(dot(n, KEY_DIR), 0.0);
	vec3 ambient = mix(GROUND_AMBIENT, SKY_AMBIENT, n.y * 0.5 + 0.5);
	vec3 sun = SUN_COLOR * uSunBoost;
	vec3 c = base * ((ambient + KEY_COLOR * keyDiffuse) * uFill + sun * sunDiffuse);
	// Blinn-Phong highlights for plastic and metal parts
	if (uSpecular > 0.0) {
		vec3 hk = normalize(KEY_DIR + toCam);
		vec3 hs = normalize(uSunDir + toCam);
		c += uSpecular * (KEY_COLOR * uFill * pow(max(dot(n, hk), 0.0), 40.0) + sun * sunDiffuse * pow(max(dot(n, hs), 0.0), 60.0));
	}
	c += base * uEmissive + emitted;
	if (uEnvironment) {
		c = mix(c, skyColor(viewDir), fogAmount(vWorld));
		c += shafts(vWorld);
	}
	outColor = vec4(toGamma(c), 1.0);
}
)";


static const char* SHADOW_VERT_SRC = R"(#version 450 core
layout(location = 0) in vec3 aPos;
uniform mat4 uMvp;
void main() {
	gl_Position = uMvp * vec4(aPos, 1.0);
}
)";


static const char* SHADOW_FRAG_SRC = R"(#version 450 core
void main() {
}
)";


/** Shadow map resolution, and the half-sizes of the two cascades' light-space boxes. */
static const int SHADOW_SIZE = 2048;
static const float SHADOW_NEAR_HALF = 5.f;
static const float SHADOW_FAR_HALF = 75.f;
/** Materials, as in FRAG_SRC. */
enum {
	MATERIAL_COLOR,
	MATERIAL_TEXTURE,
	MATERIAL_CONCRETE,
	MATERIAL_SKY,
	MATERIAL_GLOW,
};


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
	// Far enough for the sky box's corners
	Mat4 proj = Mat4::perspective(fovY, aspect, 0.1f, 400.f);
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
	GLint mvpLoc = glGetUniformLocation(active, "uMvp");
	GLint modelLoc = glGetUniformLocation(active, "uModel");
	glDisable(GL_CULL_FACE);
	Mat4 modelRotation = Mat4::rotate(rotation);

	auto draw = [&](Mesh& mesh, const Mat4& model, Vec3 color) {
		Mat4 mvp = viewProj * model;
		glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, mvp.m);
		glUniformMatrix4fv(modelLoc, 1, GL_FALSE, model.m);
		// Glossy plastic
		setMaterial(MATERIAL_COLOR, color, 0.45f);
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


/** The frame of a control on its rack: x = rack right, y = rack up, z = out of the face, origin at `center`. */
static Mat4 controlFrame(Vec3 center, Vec3 right, Vec3 up, Vec3 normal) {
	Mat4 f;
	f.m[0] = right.x;
	f.m[1] = right.y;
	f.m[2] = right.z;
	f.m[4] = up.x;
	f.m[5] = up.y;
	f.m[6] = up.z;
	f.m[8] = normal.x;
	f.m[9] = normal.y;
	f.m[10] = normal.z;
	f.m[12] = center.x;
	f.m[13] = center.y;
	f.m[14] = center.z;
	return f;
}


void ModelRenderer::drawParts(const std::vector<PartDraw>& parts, const Mat4& viewProj) {
	if (parts.empty())
		return;
	if (!buttonMesh.vao) {
		using P = math::Vec;
		buildRevolved(buttonMesh, {P(1, 0), P(1, 0.85f), P(0.88f, 1), P(0, 1)});
		buildRevolved(bezelMesh, {P(1.18f, 0), P(1.18f, 0.22f), P(1.06f, 0.3f), P(1.0f, 0.3f), P(1.0f, 0)});
		buildRevolved(nutMesh, {P(1, 0), P(1, 0.25f), P(0.8f, 0.32f), P(0, 0.32f)});
		buildRevolved(leverMesh, {P(1, 0), P(1, 0.82f), P(1.5f, 0.88f), P(1.5f, 0.97f), P(1.2f, 1), P(0, 1)});
		if (!boxMesh.vao)
			buildUnitBox(boxMesh);
	}
	GLint mvpLoc = glGetUniformLocation(active, "uMvp");
	GLint modelLoc = glGetUniformLocation(active, "uModel");
	glDisable(GL_CULL_FACE);
	Mat4 modelRotation = Mat4::rotate(rotation);
	auto draw = [&](Mesh& mesh, const Mat4& model, Vec3 color) {
		Mat4 mvp = viewProj * model;
		glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, mvp.m);
		glUniformMatrix4fv(modelLoc, 1, GL_FALSE, model.m);
		setMaterial(MATERIAL_COLOR, color, 0.6f);
		mesh.draw();
	};
	const Vec3 dark(0.1f, 0.1f, 0.11f), metal(0.72f, 0.73f, 0.75f), cap(0.82f, 0.82f, 0.84f);

	for (const PartDraw& p : parts) {
		Mat4 frame = modelRotation * controlFrame(p.center, p.right, p.up, p.normal);
		float shortSide = std::fmin(p.width, p.height);
		switch (p.kind) {
			case PartDraw::SLIDER_HANDLE: {
				// A cap the size of the 2D handle, raised off the face, with a line across its middle
				float h = shortSide * 0.6f;
				draw(boxMesh, frame * Mat4::translate(Vec3(0, -p.height / 2, 0)) * Mat4::scale(Vec3(p.width, p.height, h)), cap);
				bool vertical = p.height < p.width;
				Vec3 lineSize = vertical ? Vec3(p.width * 0.9f, p.height * 0.12f, 0.002f) : Vec3(p.width * 0.12f, p.height * 0.9f, 0.002f);
				draw(boxMesh, frame * Mat4::translate(Vec3(0, -lineSize.y / 2, h)) * Mat4::scale(lineSize), dark);
			} break;
			case PartDraw::BUTTON: {
				float r = shortSide / 2 * 0.8f;
				// Sinks when pressed or latched
				float h = r * (p.pressed ? 0.25f : 0.6f);
				draw(bezelMesh, frame * Mat4::scale(Vec3(r, r, r)), metal);
				draw(buttonMesh, frame * Mat4::scale(Vec3(r, r, h)), p.pressed ? Vec3(0.2f, 0.2f, 0.22f) : Vec3(0.13f, 0.13f, 0.14f));
			} break;
			case PartDraw::LIGHT_BUTTON: {
				float r = shortSide / 2 * 0.85f;
				draw(bezelMesh, frame * Mat4::scale(Vec3(r, r, r * (p.pressed ? 0.5f : 1.f))), metal);
			} break;
			case PartDraw::TOGGLE: {
				// Nut on the face, lever tilted toward the switch's position along its long side
				float nutR = shortSide / 2 * 0.75f;
				draw(nutMesh, frame * Mat4::scale(Vec3(nutR, nutR, nutR)), metal);
				bool vertical = p.height >= p.width;
				float tilt = (p.position * 2 - 1) * 0.6f;
				// Tilting toward +y (up) is negative around x; toward +x (right) is positive around y
				render3d::Quat q = vertical ? render3d::Quat::fromAxisAngle(Vec3(1, 0, 0), -tilt) : render3d::Quat::fromAxisAngle(Vec3(0, 1, 0), tilt);
				float leverR = nutR * 0.28f;
				float leverLen = std::fmax(p.width, p.height) * 0.45f;
				draw(leverMesh, frame * Mat4::translate(Vec3(0, 0, nutR * 0.3f)) * Mat4::rotate(q) * Mat4::scale(Vec3(leverR, leverR, leverLen)), metal);
			} break;
		}
	}
}


void ModelRenderer::updateCableMeshes(const CableScene* cables) {
	if (cableMeshes.size() < cables->entries.size())
		cableMeshes.resize(cables->entries.size());
	size_t i = 0;
	for (const auto& pair : cables->entries) {
		const Rope& rope = pair.second.rope;
		if (rope.count() < 4)
			continue;
		// Tube from the back of one plug to the back of the other
		std::vector<Vec3> points(rope.P.begin() + 1, rope.P.end() - 1);
		buildTube(cableMeshes[i], points, cables->params.radius);
		i++;
	}
}


void ModelRenderer::drawCables(const CableScene* cables, const Mat4& viewProj) {
	GLint mvpLoc = glGetUniformLocation(active, "uMvp");
	GLint modelLoc = glGetUniformLocation(active, "uModel");
	// Tubes and plugs are drawn from both sides
	glDisable(GL_CULL_FACE);
	if (!plugMesh.vao)
		buildCylinder(plugMesh);
	const RopeParams& params = cables->params;

	size_t i = 0;
	for (const auto& pair : cables->entries) {
		const CableScene::Entry& e = pair.second;
		const Rope& rope = e.rope;
		if (rope.count() < 4)
			continue;
		Mat4 identity;
		glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, viewProj.m);
		glUniformMatrix4fv(modelLoc, 1, GL_FALSE, identity.m);
		setMaterial(MATERIAL_COLOR, Vec3(e.color.r, e.color.g, e.color.b), 0.35f, 0.45f * cableGlow);
		cableMeshes[i].draw();
		i++;

		// Plugs: dark cylinders from the tip (in the jack) to the back
		setMaterial(MATERIAL_COLOR, Vec3(0.12f, 0.12f, 0.13f), 0.5f);
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


void ModelRenderer::setMaterial(int material, Vec3 color, float specular, float emissive) {
	glUniform1i(glGetUniformLocation(active, "uMaterial"), material);
	glUniform3f(glGetUniformLocation(active, "uColor"), color.x, color.y, color.z);
	glUniform1f(glGetUniformLocation(active, "uSpecular"), specular);
	glUniform1f(glGetUniformLocation(active, "uEmissive"), emissive);
	glUniform1f(glGetUniformLocation(active, "uInflate"), 0.f);
}


void ModelRenderer::drawCableGlow(const CableScene* cables, const Mat4& viewProj) {
	if (cableGlow <= 0.f)
		return;
	// Additive halos over the scene: depth tested so the model hides them, but not written
	glEnable(GL_BLEND);
	glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ZERO, GL_ONE);
	glDepthMask(GL_FALSE);
	glDisable(GL_CULL_FACE);
	Mat4 identity;
	glUniformMatrix4fv(glGetUniformLocation(active, "uMvp"), 1, GL_FALSE, viewProj.m);
	glUniformMatrix4fv(glGetUniformLocation(active, "uModel"), 1, GL_FALSE, identity.m);
	size_t i = 0;
	for (const auto& pair : cables->entries) {
		const CableScene::Entry& e = pair.second;
		if (e.rope.count() < 4)
			continue;
		setMaterial(MATERIAL_GLOW, Vec3(e.color.r, e.color.g, e.color.b), 0.f, 0.5f * cableGlow);
		glUniform1f(glGetUniformLocation(active, "uInflate"), cables->params.radius * 2.2f);
		cableMeshes[i].draw();
		i++;
	}
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
}


void ModelRenderer::drawScene(const Mat4& viewProj, bool shadowPass, PlacementRenderer* textures, const CableScene* cables, const std::vector<KnobDraw>* knobs, const std::vector<PartDraw>* parts) {
	GLint mvpLoc = glGetUniformLocation(active, "uMvp");
	GLint modelLoc = glGetUniformLocation(active, "uModel");
	Mat4 identity;

	// Structures
	if (environment) {
		glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, viewProj.m);
		glUniformMatrix4fv(modelLoc, 1, GL_FALSE, identity.m);
		setMaterial(MATERIAL_CONCRETE, Vec3(), 0.f);
		glEnable(GL_CULL_FACE);
		concrete.draw();
	}

	// Body
	Mat4 model = Mat4::rotate(rotation);
	Mat4 mvp = viewProj * model;
	glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, mvp.m);
	glUniformMatrix4fv(modelLoc, 1, GL_FALSE, model.m);
	setMaterial(MATERIAL_COLOR, Vec3(0.05f, 0.05f, 0.055f), 0.1f);
	glEnable(GL_CULL_FACE);
	body.draw();

	// Faces, pulled toward the camera so they win against the body they lie on. The body casts their shadows.
	if (!shadowPass) {
		glEnable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(-1.f, -1.f);
		glActiveTexture(GL_TEXTURE0);
		for (Face& f : faces) {
			const PlacementRenderer::Target* t = textures ? textures->getTarget(f.placementId) : NULL;
			if (t) {
				glBindTexture(GL_TEXTURE_2D, t->fb->texture);
				setMaterial(MATERIAL_TEXTURE, Vec3(), 0.04f);
			}
			else {
				setMaterial(MATERIAL_COLOR, Vec3(0.2f, 0.2f, 0.2f), 0.04f);
			}
			f.mesh.draw();
		}
		glDisable(GL_POLYGON_OFFSET_FILL);
	}

	if (knobs)
		drawKnobs(*knobs, viewProj);
	if (parts)
		drawParts(*parts, viewProj);
	if (cables)
		drawCables(cables, viewProj);
}


void ModelRenderer::render(int width, int height, Layout* layout, PlacementRenderer* textures, const CableScene* cables, const std::vector<KnobDraw>* knobs, const std::vector<PartDraw>* parts) {
	if (!program) {
		program = render3d::compileProgram(VERT_SRC, FRAG_SRC);
		if (!program)
			return;
	}
	if (!target.resize(width, height, samples))
		return;
	updateMeshes(layout);
	if (cables)
		updateCableMeshes(cables);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	glFrontFace(GL_CCW);
	glCullFace(GL_BACK);
	glDisable(GL_BLEND);

	bool shadows = false;
	if (environment) {
		if (!concrete.vao) {
			std::vector<Mesh::Vertex> vertices;
			std::vector<uint32_t> indices;
			environment::buildConcrete(vertices, indices);
			concrete.upload(vertices, indices);
			vertices.clear();
			indices.clear();
			float h = environment::SKY_HALF;
			environment::appendBox(vertices, indices, Vec3(-h, -h, -h), Vec3(h, h, h));
			sky.upload(vertices, indices);
		}
		if (!shadowProgram)
			shadowProgram = render3d::compileProgram(SHADOW_VERT_SRC, SHADOW_FRAG_SRC);
		shadows = shadowProgram && shadowNear.resize(SHADOW_SIZE) && shadowFar.resize(SHADOW_SIZE);
	}

	// Sun shadow maps: orthographic views along the sun, a tight one around the model and a wide one over the plaza
	if (shadows) {
		auto lightMatrix = [&](float half) {
			Mat4 view = Mat4::lookAt(sunDir * (half * 1.5f), Vec3(), Vec3(0, 1, 0));
			return Mat4::ortho(-half, half, -half, half, 0.f, half * 3.f) * view;
		};
		lightNear = lightMatrix(SHADOW_NEAR_HALF);
		lightFar = lightMatrix(SHADOW_FAR_HALF);
		glUseProgram(shadowProgram);
		active = shadowProgram;
		// Slope-scaled bias against acne
		glEnable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(2.f, 4.f);
		shadowNear.bind();
		drawScene(lightNear, true, textures, cables, knobs, parts);
		shadowFar.bind();
		drawScene(lightFar, true, textures, cables, knobs, parts);
		glDisable(GL_POLYGON_OFFSET_FILL);
	}

	target.bind();
	// Premultiplied clear color
	float a = background[3];
	glClearColor(background[0] * a, background[1] * a, background[2] * a, a);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glUseProgram(program);
	active = program;
	Mat4 viewProj = getViewProjection(width, height);
	glUniform1i(glGetUniformLocation(program, "uTex"), 0);
	glUniform1i(glGetUniformLocation(program, "uShadowNear"), 1);
	glUniform1i(glGetUniformLocation(program, "uShadowFar"), 2);
	glUniform1i(glGetUniformLocation(program, "uEnvironment"), shadows);
	glUniform3f(glGetUniformLocation(program, "uCamPos"), 0, 0, distance);
	glUniform3f(glGetUniformLocation(program, "uSunDir"), sunDir.x, sunDir.y, sunDir.z);
	glUniform1f(glGetUniformLocation(program, "uFog"), fog);
	glUniform1f(glGetUniformLocation(program, "uFloorY"), environment::FLOOR_Y);
	glUniform1f(glGetUniformLocation(program, "uShadowTexel"), 1.f / SHADOW_SIZE);
	glUniform1f(glGetUniformLocation(program, "uFogStart"), distance + 2.f);
	glUniform1f(glGetUniformLocation(program, "uExposure"), exposure);
	// Contrast 0 is the plain lighting; 1 drops the soft light to a fifth and lifts the sun by half
	glUniform1f(glGetUniformLocation(program, "uFill"), 1.f - 0.8f * contrast);
	glUniform1f(glGetUniformLocation(program, "uSunBoost"), 1.f + 0.5f * contrast);
	glUniformMatrix4fv(glGetUniformLocation(program, "uLightNear"), 1, GL_FALSE, lightNear.m);
	glUniformMatrix4fv(glGetUniformLocation(program, "uLightFar"), 1, GL_FALSE, lightFar.m);
	// Shadow samplers must point at depth textures with comparison even when unused
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, shadows ? shadowNear.texture : 0);
	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, shadows ? shadowFar.texture : 0);
	glActiveTexture(GL_TEXTURE0);

	drawScene(viewProj, false, textures, cables, knobs, parts);

	// Sky last, behind everything; seen from inside, so no culling
	if (environment) {
		Mat4 identity;
		glUniformMatrix4fv(glGetUniformLocation(program, "uMvp"), 1, GL_FALSE, viewProj.m);
		glUniformMatrix4fv(glGetUniformLocation(program, "uModel"), 1, GL_FALSE, identity.m);
		setMaterial(MATERIAL_SKY, Vec3(), 0.f);
		glDisable(GL_CULL_FACE);
		sky.draw();
	}
	if (cables)
		drawCableGlow(cables, viewProj);

	// Leave state as NanoVG expects it
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);
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
	buttonMesh.destroy();
	bezelMesh.destroy();
	nutMesh.destroy();
	leverMesh.destroy();
	for (Face& f : faces)
		f.mesh.destroy();
	faces.clear();
	meshKey.clear();
	body.destroy();
	concrete.destroy();
	sky.destroy();
	shadowNear.destroy();
	shadowFar.destroy();
	if (program)
		glDeleteProgram(program);
	program = 0;
	if (shadowProgram)
		glDeleteProgram(shadowProgram);
	shadowProgram = 0;
	active = 0;
	target.destroy();
}


} // namespace rack3d
} // namespace rack
