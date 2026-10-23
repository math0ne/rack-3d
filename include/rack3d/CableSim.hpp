#pragma once
#include <vector>

#include <render3d/math.hpp>


namespace rack {
namespace rack3d {


/** The model's collision shape for cables: an oriented box, in world space. */
struct BoxCollider {
	render3d::Quat rotation;
	render3d::Vec3 halfSize = render3d::Vec3(1, 1, 1);
	/** World angular velocity of the box (rad/s), so cables resting on it are carried along by friction. */
	render3d::Vec3 angularVelocity;

	/** If `p` is inside the box grown by `pad`, moves it to the nearest face and writes that face's world normal to
	`normal` (if given). Returns whether it moved.
	*/
	bool pushOut(render3d::Vec3& p, float pad, render3d::Vec3* normal = NULL) const;
};


/** Tuning for cable ropes. Units are model units (the cube is 2 wide). Defaults come from the cable sandbox. */
struct RopeParams {
	/** Rope points between the two plugs. */
	int segments = 32;
	/** Rest length / estimated length of the path around the model. */
	float slack = 1.15f;
	/** Stretch constraint strength per iteration (< 1 is stretchy). */
	float stiffness = 1.f;
	/** Bending stiffness (0 = string, 1 = stiff cable). */
	float bend = 0.35f;
	int iterations = 20;
	/** Velocity kept per step (air drag). */
	float damping = 0.996f;
	/** Smooths velocity along the rope (the cable's own friction). */
	float internalDamping = 0.5f;
	float gravity = 9.8f;
	/** Tangential velocity removed on contact with the model. */
	float friction = 0.5f;
	/** Restitution against the model. */
	float bounce = 0.f;
	float radius = 0.022f;
	/** Rigid plug length sticking out of the jack. */
	float plugLen = 0.12f;
};


/** One end of a rope. */
struct RopeEnd {
	/** Where the plug's tip is (in the jack, or held). */
	render3d::Vec3 tip;
	/** Unit direction from the tip back along the plug (out of the jack). */
	render3d::Vec3 dir = render3d::Vec3(0, 0, 1);
	/** A held plug whose direction follows the rope instead of `dir`. */
	bool dangling = false;
};


/** A cable as a Verlet rope. Points 0, 1 and n-2, n-1 are the two plugs (tip, back), set from the ends every step. */
struct Rope {
	/** Positions, previous positions (Verlet), and scratch velocities. */
	std::vector<render3d::Vec3> P, Q, V;
	/** Current and target rest length. The current one eases toward the target, so cables pay out and reel in. */
	float length = 0;
	float targetLength = 0;
	RopeEnd ends[2];

	int count() const {
		return P.size();
	}
	/** Lays the rope out along an arc around the model between the plugs, at its estimated rest length. */
	void reset(const RopeEnd& a, const RopeEnd& b, const BoxCollider& box, const RopeParams& params);
	/** Re-estimates the target rest length from the plug positions. */
	void updateTargetLength(const BoxCollider& box, const RopeParams& params);
	/** Advances by `h` seconds. Call at a fixed step (e.g. 1/240 s). */
	void step(float h, const BoxCollider& box, const RopeParams& params);
	/** Mean speed of the free points relative to the box's surface, units/s. For tests and sleeping. */
	float meanSpeed(float h, const BoxCollider& box) const;

private:
	void pin(const RopeParams& params);
	void pinEnd(const RopeEnd& e, int iTip, int iBack, int iNext, const RopeParams& params);
};


/** Estimated length of a path from `a` to `b` hugging the box: an arc around its centre pushed onto its surface. */
float estimatePathLength(render3d::Vec3 a, render3d::Vec3 b, const BoxCollider& box, float pad, int samples = 17);

/** Checks the rope physics. Returns the number of failed checks and logs each failure. */
int cableSimSelfTest();


} // namespace rack3d
} // namespace rack
