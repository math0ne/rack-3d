#include <algorithm>

#include <render3d/math.hpp>


namespace rack {
namespace render3d {


Quat Quat::fromAxisAngle(Vec3 axis, float angle) {
	Vec3 a = axis.normalize();
	float s = std::sin(angle / 2);
	return Quat(std::cos(angle / 2), a.x * s, a.y * s, a.z * s);
}


Quat Quat::fromBasis(Vec3 c0, Vec3 c1, Vec3 c2) {
	// Matrix element m<row><col>
	float m00 = c0.x, m10 = c0.y, m20 = c0.z;
	float m01 = c1.x, m11 = c1.y, m21 = c1.z;
	float m02 = c2.x, m12 = c2.y, m22 = c2.z;
	float trace = m00 + m11 + m22;
	Quat q;
	if (trace > 0) {
		float s = std::sqrt(trace + 1) * 2;
		q = Quat(s / 4, (m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s);
	}
	else if (m00 > m11 && m00 > m22) {
		float s = std::sqrt(1 + m00 - m11 - m22) * 2;
		q = Quat((m21 - m12) / s, s / 4, (m01 + m10) / s, (m02 + m20) / s);
	}
	else if (m11 > m22) {
		float s = std::sqrt(1 + m11 - m00 - m22) * 2;
		q = Quat((m02 - m20) / s, (m01 + m10) / s, s / 4, (m12 + m21) / s);
	}
	else {
		float s = std::sqrt(1 + m22 - m00 - m11) * 2;
		q = Quat((m10 - m01) / s, (m02 + m20) / s, (m12 + m21) / s, s / 4);
	}
	return q.normalize();
}


Quat Quat::slerp(Quat a, Quat b, float t) {
	float d = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
	// Take the short way around
	if (d < 0) {
		b = Quat(-b.w, -b.x, -b.y, -b.z);
		d = -d;
	}
	float ka, kb;
	if (d > 0.9995f) {
		// Nearly identical: linear is accurate and avoids dividing by ~0
		ka = 1 - t;
		kb = t;
	}
	else {
		float theta = std::acos(d);
		float s = std::sin(theta);
		ka = std::sin((1 - t) * theta) / s;
		kb = std::sin(t * theta) / s;
	}
	return Quat(a.w * ka + b.w * kb, a.x * ka + b.x * kb, a.y * ka + b.y * kb, a.z * ka + b.z * kb).normalize();
}


Quat Quat::fromTo(Vec3 a, Vec3 b) {
	a = a.normalize();
	b = b.normalize();
	float d = a.dot(b);
	if (d > 0.999999f)
		return Quat();
	if (d < -0.999999f) {
		// Opposite: half a turn around any axis perpendicular to `a`
		Vec3 axis = Vec3(1, 0, 0).cross(a);
		if (axis.norm() < 1e-4f)
			axis = Vec3(0, 1, 0).cross(a);
		return fromAxisAngle(axis, M_PI);
	}
	return fromAxisAngle(a.cross(b), std::acos(d));
}


Quat Quat::operator*(Quat b) const {
	return Quat(
		w * b.w - x * b.x - y * b.y - z * b.z,
		w * b.x + x * b.w + y * b.z - z * b.y,
		w * b.y - x * b.z + y * b.w + z * b.x,
		w * b.z + x * b.y - y * b.x + z * b.w);
}


Quat Quat::normalize() const {
	float n = std::sqrt(w * w + x * x + y * y + z * z);
	if (n <= 0)
		return Quat();
	return Quat(w / n, x / n, y / n, z / n);
}


Vec3 Quat::rotate(Vec3 v) const {
	Vec3 u(x, y, z);
	Vec3 t = u.cross(v) * 2;
	return v + t * w + u.cross(t);
}


float Quat::angleTo(Quat b) const {
	float d = std::fabs(w * b.w + x * b.x + y * b.y + z * b.z);
	return 2 * std::acos(std::min(1.f, d));
}


Mat4 Mat4::perspective(float fovY, float aspect, float near, float far) {
	Mat4 r;
	float f = 1 / std::tan(fovY / 2);
	std::fill(r.m, r.m + 16, 0.f);
	r.m[0] = f / aspect;
	r.m[5] = f;
	r.m[10] = (far + near) / (near - far);
	r.m[11] = -1;
	r.m[14] = 2 * far * near / (near - far);
	return r;
}


Mat4 Mat4::lookAt(Vec3 eye, Vec3 center, Vec3 up) {
	Vec3 f = (center - eye).normalize();
	Vec3 s = f.cross(up).normalize();
	Vec3 u = s.cross(f);
	Mat4 r;
	r.m[0] = s.x;
	r.m[4] = s.y;
	r.m[8] = s.z;
	r.m[1] = u.x;
	r.m[5] = u.y;
	r.m[9] = u.z;
	r.m[2] = -f.x;
	r.m[6] = -f.y;
	r.m[10] = -f.z;
	r.m[12] = -s.dot(eye);
	r.m[13] = -u.dot(eye);
	r.m[14] = f.dot(eye);
	return r;
}


Mat4 Mat4::translate(Vec3 v) {
	Mat4 r;
	r.m[12] = v.x;
	r.m[13] = v.y;
	r.m[14] = v.z;
	return r;
}


Mat4 Mat4::rotate(Quat q) {
	Vec3 c0 = q.rotate(Vec3(1, 0, 0));
	Vec3 c1 = q.rotate(Vec3(0, 1, 0));
	Vec3 c2 = q.rotate(Vec3(0, 0, 1));
	Mat4 r;
	r.m[0] = c0.x;
	r.m[1] = c0.y;
	r.m[2] = c0.z;
	r.m[4] = c1.x;
	r.m[5] = c1.y;
	r.m[6] = c1.z;
	r.m[8] = c2.x;
	r.m[9] = c2.y;
	r.m[10] = c2.z;
	return r;
}


Mat4 Mat4::scale(Vec3 s) {
	Mat4 r;
	r.m[0] = s.x;
	r.m[5] = s.y;
	r.m[10] = s.z;
	return r;
}


Mat4 Mat4::operator*(const Mat4& b) const {
	Mat4 r;
	for (int c = 0; c < 4; c++) {
		for (int row = 0; row < 4; row++) {
			float sum = 0;
			for (int k = 0; k < 4; k++)
				sum += m[k * 4 + row] * b.m[c * 4 + k];
			r.m[c * 4 + row] = sum;
		}
	}
	return r;
}


Vec3 Mat4::transformPoint(Vec3 p) const {
	float x = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12];
	float y = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
	float z = m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14];
	float w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
	if (w != 0 && w != 1)
		return Vec3(x / w, y / w, z / w);
	return Vec3(x, y, z);
}


Vec3 Mat4::transformDir(Vec3 d) const {
	return Vec3(
		m[0] * d.x + m[4] * d.y + m[8] * d.z,
		m[1] * d.x + m[5] * d.y + m[9] * d.z,
		m[2] * d.x + m[6] * d.y + m[10] * d.z);
}


Mat4 Mat4::inverse() const {
	// Cofactor expansion (as in MESA's gluInvertMatrix)
	const float* a = m;
	float inv[16];
	inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
	inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
	inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
	inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
	inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
	inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
	inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
	inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
	inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
	inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
	inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
	inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
	inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
	inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
	inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
	inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];
	float det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
	Mat4 r;
	if (std::fabs(det) < 1e-12f)
		return r;
	for (int i = 0; i < 16; i++)
		r.m[i] = inv[i] / det;
	return r;
}


bool Mat4::isNear(const Mat4& b, float eps) const {
	for (int i = 0; i < 16; i++) {
		if (std::fabs(m[i] - b.m[i]) > eps)
			return false;
	}
	return true;
}


int mathSelfTest() {
	int failures = 0;
	auto check = [&](bool ok, const char* what) {
		if (!ok) {
			WARN("render3d math self-test failed: %s", what);
			failures++;
		}
	};
	const float PI = M_PI;

	// Quaternions
	Quat qz = Quat::fromAxisAngle(Vec3(0, 0, 1), PI / 2);
	check(qz.rotate(Vec3(1, 0, 0)).isNear(Vec3(0, 1, 0)), "90 deg about z takes x to y");
	Quat qx = Quat::fromAxisAngle(Vec3(1, 0, 0), PI / 2);
	check((qx * qz).rotate(Vec3(1, 0, 0)).isNear(qx.rotate(qz.rotate(Vec3(1, 0, 0)))), "a * b rotates by b then a");
	check((qz * qz.conjugate()).rotate(Vec3(0.3f, -2, 5)).isNear(Vec3(0.3f, -2, 5)), "q * conjugate(q) is identity");
	check(std::fabs(Quat().angleTo(qz) - PI / 2) < 1e-4f, "angleTo measures the rotation angle");
	Quat half = Quat::slerp(Quat(), qz, 0.5f);
	check(half.rotate(Vec3(1, 0, 0)).isNear(Vec3(std::sqrt(0.5f), std::sqrt(0.5f), 0)), "slerp halfway is 45 deg");
	check(Quat::slerp(Quat(), qz, 0).angleTo(Quat()) < 1e-3f && Quat::slerp(Quat(), qz, 1).angleTo(qz) < 1e-3f, "slerp ends at its ends");
	Quat negZ(-qz.w, -qz.x, -qz.y, -qz.z);
	check(Quat::slerp(Quat(), negZ, 0.5f).angleTo(half) < 1e-3f, "slerp takes the short way when quats have opposite signs");

	// fromTo, including opposite directions
	bool fromToOk = true;
	for (Vec3 a : {Vec3(1, 0, 0), Vec3(0, 0, 1), Vec3(1, 2, 3)}) {
		for (Vec3 b : {Vec3(0, 1, 0), Vec3(0, 0, -1), -a, a, Vec3(-2, 0.5f, 1)}) {
			fromToOk = fromToOk && Quat::fromTo(a, b).rotate(a.normalize()).isNear(b.normalize(), 1e-3f);
		}
	}
	check(fromToOk, "fromTo takes a to b (including a to -a)");

	// Quaternion from basis, including the trace <= 0 branches
	Quat samples[] = {
		Quat::fromAxisAngle(Vec3(1, 2, 3), 0.7f),
		Quat::fromAxisAngle(Vec3(1, 0, 0), PI),
		Quat::fromAxisAngle(Vec3(0, 1, 0), PI * 0.9f),
		Quat::fromAxisAngle(Vec3(0, 0, 1), PI * 0.95f),
		Quat::fromAxisAngle(Vec3(-1, 1, 0.2f), 2.5f),
	};
	bool basisOk = true;
	for (Quat q : samples) {
		Quat b = Quat::fromBasis(q.rotate(Vec3(1, 0, 0)), q.rotate(Vec3(0, 1, 0)), q.rotate(Vec3(0, 0, 1)));
		basisOk = basisOk && b.angleTo(q) < 1e-3f;
	}
	check(basisOk, "fromBasis recovers the rotation (all 4 branches)");

	// Matrices
	Quat q = Quat::fromAxisAngle(Vec3(1, -2, 0.5f), 1.1f);
	Vec3 v(0.4f, 1.5f, -2);
	check(Mat4::rotate(q).transformPoint(v).isNear(q.rotate(v)), "rotation matrix matches the quaternion");
	Mat4 trs = Mat4::translate(Vec3(1, 2, 3)) * Mat4::rotate(q) * Mat4::scale(Vec3(2, 2, 2));
	check(trs.transformPoint(v).isNear(q.rotate(v * 2) + Vec3(1, 2, 3)), "T * R * S applies S, then R, then T");
	check((trs * trs.inverse()).isNear(Mat4()) && (trs.inverse() * trs).isNear(Mat4()), "inverse");
	check(trs.transformDir(Vec3(1, 0, 0)).isNear(q.rotate(Vec3(2, 0, 0))), "transformDir ignores translation");

	Mat4 view = Mat4::lookAt(Vec3(0, 0, 5), Vec3(0, 0, 0), Vec3(0, 1, 0));
	check(view.transformPoint(Vec3(0, 0, 0)).isNear(Vec3(0, 0, -5)), "lookAt puts the target in front (-z)");
	check(view.transformPoint(Vec3(1, 0, 0)).isNear(Vec3(1, 0, -5)), "lookAt keeps +x to the right");
	Mat4 view2 = Mat4::lookAt(Vec3(3, 4, 0), Vec3(0, 0, 0), Vec3(0, 1, 0));
	check(view2.transformPoint(Vec3(0, 0, 0)).isNear(Vec3(0, 0, -5)), "lookAt from an angle");

	Mat4 proj = Mat4::perspective(PI / 2, 2, 1, 10);
	check(std::fabs(proj.transformPoint(Vec3(0, 0, -1)).z + 1) < 1e-4f, "near plane maps to z = -1");
	check(std::fabs(proj.transformPoint(Vec3(0, 0, -10)).z - 1) < 1e-4f, "far plane maps to z = +1");
	check(proj.transformPoint(Vec3(2, 1, -1)).isNear(Vec3(1, 1, -1)), "fov 90 and aspect 2 put (2, 1, -1) at the corner");

	if (failures == 0)
		INFO("render3d math self-test passed");
	return failures;
}


} // namespace render3d
} // namespace rack
