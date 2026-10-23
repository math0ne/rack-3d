#pragma once
#include <cmath>

#include <common.hpp>


namespace rack {
/** Rack 3D: minimal 3D math and OpenGL 4.5 helpers for the 3D view. */
namespace render3d {


struct Vec3 {
	float x = 0.f;
	float y = 0.f;
	float z = 0.f;

	Vec3() {}
	Vec3(float x, float y, float z) : x(x), y(y), z(z) {}

	Vec3 operator+(Vec3 b) const {
		return Vec3(x + b.x, y + b.y, z + b.z);
	}
	Vec3 operator-(Vec3 b) const {
		return Vec3(x - b.x, y - b.y, z - b.z);
	}
	Vec3 operator-() const {
		return Vec3(-x, -y, -z);
	}
	Vec3 operator*(float s) const {
		return Vec3(x * s, y * s, z * s);
	}
	Vec3 operator/(float s) const {
		return Vec3(x / s, y / s, z / s);
	}
	float dot(Vec3 b) const {
		return x * b.x + y * b.y + z * b.z;
	}
	Vec3 cross(Vec3 b) const {
		return Vec3(y * b.z - z * b.y, z * b.x - x * b.z, x * b.y - y * b.x);
	}
	float norm() const {
		return std::sqrt(dot(*this));
	}
	Vec3 normalize() const {
		float n = norm();
		return n > 0.f ? *this / n : *this;
	}
	bool isNear(Vec3 b, float eps = 1e-4f) const {
		return std::fabs(x - b.x) <= eps && std::fabs(y - b.y) <= eps && std::fabs(z - b.z) <= eps;
	}
};


/** Unit quaternion representing a rotation. */
struct Quat {
	float w = 1.f;
	float x = 0.f;
	float y = 0.f;
	float z = 0.f;

	Quat() {}
	Quat(float w, float x, float y, float z) : w(w), x(x), y(y), z(z) {}

	/** Rotation by `angle` radians around `axis` (needn't be normalized). */
	static Quat fromAxisAngle(Vec3 axis, float angle);
	/** Rotation whose matrix has columns `c0`, `c1`, `c2` (an orthonormal, right-handed basis). */
	static Quat fromBasis(Vec3 c0, Vec3 c1, Vec3 c2);
	/** Shortest-path spherical interpolation. */
	static Quat slerp(Quat a, Quat b, float t);

	/** The rotation `b` followed by this one. */
	Quat operator*(Quat b) const;
	Quat conjugate() const {
		return Quat(w, -x, -y, -z);
	}
	Quat normalize() const;
	Vec3 rotate(Vec3 v) const;
	/** Angle between two rotations, in radians. */
	float angleTo(Quat b) const;
};


/** 4x4 matrix, column-major like OpenGL: m[column * 4 + row]. */
struct Mat4 {
	float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

	static Mat4 identity() {
		return Mat4();
	}
	/** OpenGL perspective projection. `fovY` in radians. */
	static Mat4 perspective(float fovY, float aspect, float near, float far);
	/** View matrix looking from `eye` at `center`. */
	static Mat4 lookAt(Vec3 eye, Vec3 center, Vec3 up);
	static Mat4 translate(Vec3 v);
	static Mat4 rotate(Quat q);
	static Mat4 scale(Vec3 s);

	Mat4 operator*(const Mat4& b) const;
	/** Transforms a point, with perspective divide. */
	Vec3 transformPoint(Vec3 p) const;
	/** Transforms a direction (ignores translation). */
	Vec3 transformDir(Vec3 d) const;
	/** General inverse. Returns identity if singular. */
	Mat4 inverse() const;
	bool isNear(const Mat4& b, float eps = 1e-4f) const;
};


/** Checks the math. Returns the number of failed checks and logs each failure. */
int mathSelfTest();


} // namespace render3d
} // namespace rack
