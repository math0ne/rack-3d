#include <cmath>
#include <random>

#include <rack3d/Environment.hpp>


namespace rack {
namespace rack3d {
namespace environment {


using render3d::Vec3;
using render3d::Mesh;


void appendBox(std::vector<Mesh::Vertex>& vertices, std::vector<uint32_t>& indices, Vec3 min, Vec3 max) {
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
}


void buildConcrete(std::vector<Mesh::Vertex>& vertices, std::vector<uint32_t>& indices) {
	const float F = FLOOR_Y;
	auto box = [&](float x0, float y0, float z0, float x1, float y1, float z1) {
		appendBox(vertices, indices, Vec3(std::fmin(x0, x1), std::fmin(y0, y1), std::fmin(z0, z1)), Vec3(std::fmax(x0, x1), std::fmax(y0, y1), std::fmax(z0, z1)));
	};

	// Plaza floor, and a stepped plinth under the cube
	box(-200, F - 1, -200, 200, F, 200);
	box(-2.6f, F, -2.6f, 2.6f, F + 0.45f, 2.6f);
	box(-1.8f, F + 0.45f, -1.8f, 1.8f, F + 0.8f, 1.8f);

	// Grand stair rising to a terrace and a monumental gate: two pylons under a massive lintel, framing the sky
	for (int i = 0; i < 12; i++) {
		float z = -22 - i * 0.9f;
		box(-9, F, z, 9, F + (i + 1) * 0.32f, z - 0.9f);
	}
	const float T = F + 12 * 0.32f;
	box(-30, F, -32.8f, 30, T, -60);
	box(-17, T, -46, -9, 30, -38);
	box(9, T, -46, 17, 30, -38);
	box(-19, 30, -47, 19, 37, -37);
	// Low parapet walls along the terrace
	box(-30, T, -33.4f, -9, T + 1.1f, -32.8f);
	box(9, T, -33.4f, 30, T + 1.1f, -32.8f);

	// Left: colonnade carrying a beam and a deep cantilevered slab, closed by a blank wall
	for (float z = -6; z >= -30; z -= 6)
		box(-8.6f, F, z - 0.6f, -7.4f, 10, z + 0.6f);
	box(-8.9f, 10, -31, -7.1f, 11.5f, -4);
	box(-27, 11.5f, -31, -5.5f, 13.2f, -2);
	box(-29, F, -31, -27, 13.2f, -2);
	// Left tower behind, with heavy floor plates sticking out every few metres
	box(-25, F, -62, -13, 46, -48);
	for (float y = 4; y < 44; y += 5)
		box(-26, y, -63, -12, y + 0.8f, -47);

	// Left foreground pier framing the view
	box(-6.6f, F, -3.2f, -4.6f, 16, -1.2f);

	// Right: a finned block under a heavy overhanging crown
	box(10, F, -28, 26, 16, -10);
	for (float z = -27.4f; z <= -10.4f; z += 1.6f)
		box(9.3f, F, z - 0.25f, 10, 16, z + 0.25f);
	box(9, 16, -29, 27, 19.5f, -9);
	// Low retaining wall in the right foreground
	box(4.5f, F, -5.5f, 15, F + 1.8f, -4.5f);

	// High bridge across the back on two piers
	box(-90, 9, -78, 90, 11.5f, -74);
	for (float x = -60; x <= 60; x += 30)
		box(x - 2, F, -78, x + 2, 9, -74);

	// Distant megastructures fading into the haze, seeded so the skyline is always the same
	std::mt19937 rng(1970);
	std::uniform_real_distribution<float> uni(0.f, 1.f);
	for (int i = 0; i < 46; i++) {
		// Behind and to the sides of the plaza; nothing in front of the camera
		float angle = (-160 + 320 * uni(rng)) * M_PI / 180;
		float r = 70 + 60 * uni(rng);
		float x = r * std::sin(angle);
		float z = -r * std::cos(angle);
		if (z > -20)
			continue;
		float w = 8 + 18 * uni(rng);
		float d = 8 + 18 * uni(rng);
		float h = 18 + 55 * uni(rng) * uni(rng) + 10 * uni(rng);
		box(x - w / 2, F, z - d / 2, x + w / 2, h, z + d / 2);
		// Some carry a wider slab on top, the brutalist hat
		if (uni(rng) < 0.45f) {
			float o = 2 + 3 * uni(rng);
			box(x - w / 2 - o, h, z - d / 2 - o, x + w / 2 + o, h + 2 + 2 * uni(rng), z + d / 2 + o);
		}
	}
}


} // namespace environment
} // namespace rack3d
} // namespace rack
