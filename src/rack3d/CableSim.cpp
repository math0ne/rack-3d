#include <algorithm>
#include <cmath>

#include <rack3d/CableSim.hpp>


namespace rack {
namespace rack3d {


using render3d::Vec3;
using render3d::Quat;


/** Samples along each segment for collision, so chords can't slice through box edges. */
static const float SEG_SAMPLES[] = {0.2f, 0.4f, 0.6f, 0.8f};
/** Collision pad as a multiple of the rope radius: the tube mesh is a polygon and must clear edges visually. */
static const float COLLIDE_PAD = 1.25f;
/** Contact sliding slower than this (units/s) sticks: static friction. */
static const float STATIC_SPEED = 0.15f;


bool BoxCollider::pushOut(Vec3& p, float pad, Vec3* normal) const {
	Vec3 l = rotation.conjugate().rotate(p);
	Vec3 e = halfSize + Vec3(pad, pad, pad);
	float ax = std::fabs(l.x), ay = std::fabs(l.y), az = std::fabs(l.z);
	if (ax >= e.x || ay >= e.y || az >= e.z)
		return false;
	float dx = e.x - ax, dy = e.y - ay, dz = e.z - az;
	Vec3 n;
	if (dx <= dy && dx <= dz) {
		float s = l.x < 0 ? -1.f : 1.f;
		l.x = s * e.x;
		n = Vec3(s, 0, 0);
	}
	else if (dy <= dz) {
		float s = l.y < 0 ? -1.f : 1.f;
		l.y = s * e.y;
		n = Vec3(0, s, 0);
	}
	else {
		float s = l.z < 0 ? -1.f : 1.f;
		l.z = s * e.z;
		n = Vec3(0, 0, s);
	}
	p = rotation.rotate(l);
	if (normal)
		*normal = rotation.rotate(n);
	return true;
}


/** Fills `out` with points on an arc around the box's centre from `a` to `b`, pushed onto the box's surface. */
static void arcPoints(Vec3 a, Vec3 b, const BoxCollider& box, float pad, std::vector<Vec3>& out) {
	int m = out.size();
	float ra = a.norm(), rb = b.norm();
	Vec3 da = a.normalize(), db = b.normalize();
	float angle = std::acos(std::max(-1.f, std::min(1.f, da.dot(db))));
	Vec3 axis = da.cross(db);
	if (axis.norm() < 1e-4f) {
		// Opposite (or equal) directions: any axis perpendicular to `da`
		axis = Vec3(da.y, -da.x, 0.3f).cross(da);
	}
	axis = axis.normalize();
	for (int i = 0; i < m; i++) {
		float t = (m > 1) ? (float) i / (m - 1) : 0.f;
		Quat q = Quat::fromAxisAngle(axis, angle * t);
		out[i] = q.rotate(da) * (ra + (rb - ra) * t);
		box.pushOut(out[i], pad);
	}
}


float estimatePathLength(Vec3 a, Vec3 b, const BoxCollider& box, float pad, int samples) {
	std::vector<Vec3> pts(samples);
	arcPoints(a, b, box, pad, pts);
	float length = 0;
	for (int i = 0; i + 1 < samples; i++)
		length += (pts[i + 1] - pts[i]).norm();
	return length;
}


void Rope::pinEnd(const RopeEnd& e, int iTip, int iBack, int iNext, const RopeParams& params) {
	Vec3 dir = e.dir;
	if (e.dangling) {
		// The plug hangs off the rope: it continues the rope's direction
		dir = P[iBack] - P[iNext];
		dir = dir.norm() > 1e-6f ? dir.normalize() : Vec3(0, -1, 0);
		// A held plug's back is where it's held; its tip sticks out past it, continuing the rope
		P[iBack] = e.tip;
		P[iTip] = e.tip + dir * params.plugLen;
		return;
	}
	P[iTip] = e.tip;
	P[iBack] = e.tip + dir * params.plugLen;
}


void Rope::pin(const RopeParams& params) {
	int n = count();
	pinEnd(ends[0], 0, 1, 2, params);
	pinEnd(ends[1], n - 1, n - 2, n - 3, params);
}


void Rope::reset(const RopeEnd& a, const RopeEnd& b, const BoxCollider& box, const RopeParams& params) {
	int n = params.segments + 4;
	P.assign(n, Vec3());
	Q.assign(n, Vec3());
	V.assign(n, Vec3());
	ends[0] = a;
	ends[1] = b;
	// Dangling ends need neighbours to point along; seed them first
	pin(params);
	pin(params);
	std::vector<Vec3> arc(n - 2);
	arcPoints(P[1], P[n - 2], box, params.radius * COLLIDE_PAD * 1.2f, arc);
	for (int i = 1; i < n - 1; i++)
		P[i] = arc[i - 1];
	pin(params);
	Q = P;
	updateTargetLength(box, params);
	length = targetLength;
}


/** Where the rope leaves a plug: the plug's back, from the end itself (rope points are only re-pinned by step()). */
static Vec3 plugBack(const RopeEnd& e, const RopeParams& params) {
	return e.dangling ? e.tip : e.tip + e.dir * params.plugLen;
}


void Rope::updateTargetLength(const BoxCollider& box, const RopeParams& params) {
	float path = estimatePathLength(plugBack(ends[0], params), plugBack(ends[1], params), box, params.radius * COLLIDE_PAD);
	targetLength = std::fmax(0.25f, path * params.slack);
}


void Rope::step(float h, const BoxCollider& box, const RopeParams& params) {
	int n = count();
	if (n < 5)
		return;
	float g = -params.gravity * h * h;
	length += (targetLength - length) * (1 - std::exp(-8 * h));

	// Integrate free points, with internal damping: each point's velocity is pulled toward its neighbours' average,
	// which kills waves travelling along the rope but leaves whole-rope swing alone.
	for (int i = 0; i < n; i++)
		V[i] = P[i] - Q[i];
	for (int i = 2; i < n - 2; i++) {
		Vec3 v = V[i] + ((V[i - 1] + V[i + 1]) * 0.5f - V[i]) * params.internalDamping;
		v = v * params.damping;
		Q[i] = P[i];
		P[i] = P[i] + v;
		P[i].y += g;
	}
	for (int i : {0, 1, n - 2, n - 1})
		Q[i] = P[i];
	pin(params);

	float rest = length / (n - 3);
	float k = params.stiffness;
	float kb = params.bend * params.bend;
	float pad = params.radius * COLLIDE_PAD;
	// Pinned plug points have no mass
	auto W = [&](int i) {
		return (i >= 2 && i <= n - 3) ? 1.f : 0.f;
	};

	for (int it = 0; it < params.iterations; it++) {
		// Stretch, sweeping alternately forward and backward to avoid one-sided sag
		for (int j = 1; j < n - 2; j++) {
			int i = (it & 1) ? n - 2 - j : j;
			float wa = W(i), wb = W(i + 1);
			Vec3 d = P[i + 1] - P[i];
			float len = d.norm();
			if (len < 1e-9f || wa + wb == 0)
				continue;
			float c = (len - rest) / len * k / (wa + wb);
			P[i] = P[i] + d * (wa * c);
			P[i + 1] = P[i + 1] - d * (wb * c);
		}
		// Bend: points two apart can't get closer than a straight run (including the plugs, so cables leave
		// jacks straight)
		if (kb > 0) {
			for (int i = 0; i < n - 2; i++) {
				float wa = W(i), wb = W(i + 2);
				if (wa + wb == 0)
					continue;
				float target = (i == 0 || i == n - 3) ? rest + params.plugLen : 2 * rest;
				Vec3 d = P[i + 2] - P[i];
				float len = d.norm();
				if (len >= target || len < 1e-9f)
					continue;
				float c = (len - target) / len * kb / (wa + wb);
				P[i] = P[i] + d * (wa * c);
				P[i + 2] = P[i + 2] - d * (wb * c);
			}
		}
		// Collide points, then samples along each segment: a sample at t pushed out by d moves the ends by
		// (1-t, t), scaled so the sample point itself moves exactly d
		for (int i = 2; i < n - 2; i++)
			box.pushOut(P[i], pad);
		for (int i = 1; i < n - 2; i++) {
			float wa = W(i), wb = W(i + 1);
			for (float t : SEG_SAMPLES) {
				Vec3 a = P[i] + (P[i + 1] - P[i]) * t;
				Vec3 b = a;
				if (!box.pushOut(b, pad))
					continue;
				Vec3 d = b - a;
				float ca = wa * (1 - t), cb = wb * t;
				float s = 1 / (ca * (1 - t) + cb * t);
				if (wa > 0)
					P[i] = P[i] + d * (ca * s);
				if (wb > 0)
					P[i + 1] = P[i + 1] + d * (cb * s);
			}
		}
	}

	// Contacts: velocity relative to the box surface (omega x p) loses its inward part and its outward part
	// (scaled by bounce), and its tangential part to friction; slow sliding sticks
	for (int i = 2; i < n - 2; i++) {
		Vec3 a = P[i];
		Vec3 normal;
		if (!box.pushOut(a, pad + 0.004f, &normal))
			continue;
		Vec3 surface = box.angularVelocity.cross(P[i]) * h;
		Vec3 v = (P[i] - Q[i]) - surface;
		float vn = v.dot(normal);
		v = v - normal * (vn < 0 ? vn : vn * (1 - params.bounce));
		float vnn = v.dot(normal);
		Vec3 tangential = v - normal * vnn;
		if (tangential.norm() < STATIC_SPEED * h)
			v = v - tangential;
		else
			v = v - tangential * params.friction;
		Q[i] = P[i] - (v + surface);
	}
}


float Rope::meanSpeed(float h, const BoxCollider& box) const {
	int n = count();
	if (n < 5)
		return 0;
	float sum = 0;
	for (int i = 2; i < n - 2; i++) {
		Vec3 v = (P[i] - Q[i]) - box.angularVelocity.cross(P[i]) * h;
		sum += v.norm() / h;
	}
	return sum / (n - 4);
}


int cableSimSelfTest() {
	int failures = 0;
	auto check = [&](bool ok, const char* what) {
		if (!ok) {
			WARN("rack3d cable sim self-test failed: %s", what);
			failures++;
		}
	};
	const float h = 1 / 240.f;
	RopeParams params;
	BoxCollider box;

	auto anyInside = [&](const Rope& r) {
		for (int i = 2; i < r.count() - 2; i++) {
			Vec3 p = r.P[i];
			if (box.pushOut(p, params.radius * 0.5f))
				return true;
		}
		return false;
	};
	auto anyNaN = [&](const Rope& r) {
		for (const Vec3& p : r.P) {
			if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
				return true;
		}
		return false;
	};
	auto run = [&](Rope& r, float seconds) {
		for (int i = 0; i < seconds / h; i++)
			r.step(h, box, params);
	};

	// A cable between two jacks on the front face settles, hangs outside the cube and stays still
	{
		RopeEnd a, b;
		a.tip = Vec3(-0.5f, 0.3f, 1);
		a.dir = Vec3(0, 0, 1);
		b.tip = Vec3(0.5f, -0.2f, 1);
		b.dir = Vec3(0, 0, 1);
		Rope r;
		r.reset(a, b, box, params);
		check(r.count() == params.segments + 4, "rope has segments + 4 points");
		run(r, 3);
		check(!anyNaN(r), "settled rope has no NaNs");
		check(!anyInside(r), "settled rope is outside the cube");
		float s3 = r.meanSpeed(h, box);
		run(r, 3);
		float s6 = r.meanSpeed(h, box);
		run(r, 4);
		float s10 = r.meanSpeed(h, box);
		INFO("rack3d cable sim: front-face rope mean speed after 3/6/10 s: %.4f / %.4f / %.4f units/s", s3, s6, s10);
		check(r.meanSpeed(h, box) < 0.02f, "settled rope is still");
		check(r.P[0].isNear(a.tip) && r.P[1].isNear(a.tip + a.dir * params.plugLen), "plug A stays in its jack");
		float total = 0;
		for (int i = 1; i < r.count() - 2; i++)
			total += (r.P[i + 1] - r.P[i]).norm();
		check(std::fabs(total - r.length) / r.length < 0.05f, "rope keeps its rest length (within 5%)");
		// Hanging: the middle sags below the plugs
		check(r.P[r.count() / 2].y < std::fmin(a.tip.y, b.tip.y), "the rope sags under gravity");
	}

	// A cable from the front face to the back face wraps over the top without cutting through edges
	{
		RopeEnd a, b;
		a.tip = Vec3(0, 0.6f, 1);
		a.dir = Vec3(0, 0, 1);
		b.tip = Vec3(0, 0.6f, -1);
		b.dir = Vec3(0, 0, -1);
		Rope r;
		r.reset(a, b, box, params);
		run(r, 3);
		bool chordsOutside = true;
		for (int i = 1; i < r.count() - 2; i++) {
			for (float t : {0.25f, 0.5f, 0.75f}) {
				Vec3 m = r.P[i] + (r.P[i + 1] - r.P[i]) * t;
				chordsOutside = chordsOutside && !box.pushOut(m, 0);
			}
		}
		check(!anyNaN(r) && chordsOutside, "a front-to-back cable wraps around the cube without cutting through it");
	}

	// Spinning the cube: plugs follow it, the rope stays outside and finite, and settles once it stops
	{
		RopeEnd a0, b0;
		a0.tip = Vec3(-0.5f, 0, 1);
		a0.dir = Vec3(0, 0, 1);
		b0.tip = Vec3(1, 0.3f, 0.2f);
		b0.dir = Vec3(1, 0, 0);
		Rope r;
		r.reset(a0, b0, box, params);
		box.angularVelocity = Vec3(0.5f, 6, 0.3f);
		for (int i = 0; i < 240; i++) {
			Quat dq = Quat::fromAxisAngle(box.angularVelocity, box.angularVelocity.norm() * h);
			box.rotation = (dq * box.rotation).normalize();
			RopeEnd a = a0, b = b0;
			a.tip = box.rotation.rotate(a0.tip);
			a.dir = box.rotation.rotate(a0.dir);
			b.tip = box.rotation.rotate(b0.tip);
			b.dir = box.rotation.rotate(b0.dir);
			r.ends[0] = a;
			r.ends[1] = b;
			r.step(h, box, params);
		}
		check(!anyNaN(r), "spinning the cube hard keeps the rope finite");
		check(r.P[0].isNear(box.rotation.rotate(a0.tip)), "plugs follow the spinning cube");
		box.angularVelocity = Vec3();
		run(r, 3);
		check(!anyInside(r) && r.meanSpeed(h, box) < 0.02f, "after spinning, the rope settles outside the cube");
		box = BoxCollider();
	}

	// A held, dangling end: the plug follows the rope's direction and the rope pays out as it's pulled away
	{
		RopeEnd a, b;
		a.tip = Vec3(0, 0, 1);
		a.dir = Vec3(0, 0, 1);
		b.tip = Vec3(0, 0, 1.5f);
		b.dangling = true;
		Rope r;
		r.reset(a, b, box, params);
		float shortLength = r.targetLength;
		r.ends[1].tip = Vec3(1.5f, 1.5f, 2.5f);
		r.updateTargetLength(box, params);
		run(r, 2);
		check(r.targetLength > shortLength && std::fabs(r.length - r.targetLength) < 0.01f, "pulling a held end away pays out rope");
		int n = r.count();
		Vec3 plug = r.P[n - 2] - r.P[n - 1];
		Vec3 along = r.P[n - 3] - r.P[n - 2];
		check(!anyNaN(r) && plug.normalize().dot(along.normalize()) > 0.9f, "a dangling plug follows the rope's direction");
	}

	if (failures == 0)
		INFO("rack3d cable sim self-test passed");
	return failures;
}


} // namespace rack3d
} // namespace rack
