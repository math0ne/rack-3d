#include <cmath>

#include <rack3d/CableScene.hpp>
#include <rack3d/Layout.hpp>
#include <rack3d/settings.hpp>
#include <app/RackWidget.hpp>
#include <app/CableWidget.hpp>
#include <app/PortWidget.hpp>
#include <engine/Cable.hpp>


namespace rack {
namespace rack3d {


using render3d::Vec3;
using render3d::Quat;


bool getPortPose(app::PortWidget* port, app::RackWidget* rack, Layout* layout, PortPose* pose) {
	math::Vec rackPos = port->getRelativeOffset(port->box.size.div(2), rack);
	for (const Placement& p : layout->placements) {
		math::Rect r = Layout::getRackRect(p);
		if (!p.hasSurface || !r.contains(rackPos))
			continue;
		pose->position = p.surface.toModel(rackPos.minus(r.pos));
		pose->normal = p.surface.normal();
		return true;
	}
	return false;
}


/** A plugged rope end at `pose`, in world space for a model turned by `rotation`. */
static RopeEnd plugEnd(const PortPose& pose, Quat rotation) {
	RopeEnd e;
	e.tip = rotation.rotate(pose.position);
	e.dir = rotation.rotate(pose.normal);
	return e;
}


/** The loose end of a cable being dragged, in world space. `dangling` ends hang off the rope. */
static RopeEnd heldEnd(app::CableWidget* cw, const Pointer& pointer, Quat rotation, app::RackWidget* rack, Layout* layout, const BoxCollider& box, float hoverHeight, float snapHeight, const RopeEnd* previous) {
	RopeEnd e;
	// Over a port it would plug into: hover just off that jack, pointing in
	app::PortWidget* snap = cw->outputPort ? cw->hoveredInputPort : cw->hoveredOutputPort;
	PortPose pose;
	if (snap && getPortPose(snap, rack, layout, &pose)) {
		e.tip = rotation.rotate(pose.position + pose.normal * snapHeight);
		e.dir = rotation.rotate(pose.normal);
		return e;
	}
	// Over a rack: float off it, pointing in
	if (pointer.valid && pointer.onRack) {
		e.tip = rotation.rotate(pointer.hit + pointer.hitNormal * hoverHeight);
		e.dir = rotation.rotate(pointer.hitNormal);
		return e;
	}
	// In space: where the mouse ray crosses the plane through the model's centre facing the camera, outside it
	if (pointer.valid && std::fabs(pointer.rayDir.z) > 1e-6f) {
		float t = -pointer.rayOrigin.z / pointer.rayDir.z;
		e.tip = pointer.rayOrigin + pointer.rayDir * t;
		box.pushOut(e.tip, hoverHeight);
		e.dangling = true;
		return e;
	}
	// Mouse elsewhere: stay put
	if (previous)
		return *previous;
	e.dangling = true;
	return e;
}


CableScene::CableScene() {
	params = settings::cableParams;
}


void CableScene::update(float dt, Quat rotation, app::RackWidget* rack, Layout* layout, const Pointer& pointer) {
	// Settings act live. A new segment count changes every rope's point count, so they're laid out again.
	if (settings::cableParams.segments != params.segments)
		entries.clear();
	params = settings::cableParams;

	if (!hasLastRotation) {
		lastRotation = rotation;
		hasLastRotation = true;
	}

	// The collision box: the placements' bounding box, like the rendered body
	BoxCollider box;
	{
		bool any = false;
		Vec3 lo(INFINITY, INFINITY, INFINITY), hi(-INFINITY, -INFINITY, -INFINITY);
		for (const Placement& p : layout->placements) {
			if (!p.hasSurface)
				continue;
			math::Vec size = Layout::getRackRect(p).size;
			for (math::Vec c : {math::Vec(0, 0), math::Vec(size.x, 0), math::Vec(0, size.y), math::Vec(size.x, size.y)}) {
				Vec3 q = p.surface.toModel(c);
				lo = Vec3(std::fmin(lo.x, q.x), std::fmin(lo.y, q.y), std::fmin(lo.z, q.z));
				hi = Vec3(std::fmax(hi.x, q.x), std::fmax(hi.y, q.y), std::fmax(hi.z, q.z));
				any = true;
			}
		}
		// The cube is centred on the origin; the box is too
		if (any)
			box.halfSize = Vec3(std::fmax(-lo.x, hi.x), std::fmax(-lo.y, hi.y), std::fmax(-lo.z, hi.z));
	}

	// Sync with the rack's cables, including ones being dragged: add new ones, drop removed ones.
	// Grabbing a plugged cable and dropping one on a port keep the same CableWidget, so the rope carries on.
	for (auto& pair : entries)
		pair.second.seen = false;
	struct Live {
		Entry* entry;
		app::CableWidget* cw;
		/** Which ends are in ports; the other is held. */
		bool hasOut, hasIn;
		PortPose out, in;
	};
	std::vector<Live> live;
	box.rotation = rotation;
	for (app::CableWidget* cw : rack->getCables()) {
		Live l;
		l.cw = cw;
		l.hasOut = cw->outputPort && getPortPose(cw->outputPort, rack, layout, &l.out);
		l.hasIn = cw->inputPort && getPortPose(cw->inputPort, rack, layout, &l.in);
		// Needs at least one end on a rack
		if (!l.hasOut && !l.hasIn)
			continue;
		if (cw->isComplete() && !(l.hasOut && l.hasIn))
			continue;

		auto it = entries.find(cw);
		bool fresh = it == entries.end();
		Entry& e = entries[cw];
		// A new widget at a freed one's address: neither plugged port matches
		if (!fresh && !(cw->outputPort && cw->outputPort == e.outputPort) && !(cw->inputPort && cw->inputPort == e.inputPort))
			fresh = true;
		e.cableId = cw->cable ? cw->cable->id : -1;
		e.outputPort = cw->outputPort;
		e.inputPort = cw->inputPort;
		e.color = cw->color;
		e.seen = true;
		l.entry = &e;
		if (fresh) {
			RopeEnd a = l.hasOut ? plugEnd(l.out, rotation) : heldEnd(cw, pointer, rotation, rack, layout, box, hoverHeight, snapHeight, NULL);
			RopeEnd b = l.hasIn ? plugEnd(l.in, rotation) : heldEnd(cw, pointer, rotation, rack, layout, box, hoverHeight, snapHeight, NULL);
			e.rope.reset(a, b, box, params);
		}
		live.push_back(l);
	}
	for (auto it = entries.begin(); it != entries.end();) {
		if (!it->second.seen)
			it = entries.erase(it);
		else
			it++;
	}

	// Simulate at a fixed step, turning the model from its last rotation to this one across the frame
	accumulator = std::fmin(accumulator + dt, 0.1f);
	int steps = (int) (accumulator / stepSeconds);
	accumulator -= steps * stepSeconds;
	if (steps > 0) {
		// Angular velocity of the turn this frame, for contact friction
		Quat delta = (rotation * lastRotation.conjugate()).normalize();
		if (delta.w < 0)
			delta = Quat(-delta.w, -delta.x, -delta.y, -delta.z);
		float angle = 2 * std::acos(std::fmin(1.f, delta.w));
		Vec3 axis(delta.x, delta.y, delta.z);
		box.angularVelocity = (axis.norm() > 1e-6f && dt > 0) ? axis.normalize() * (angle / (steps * stepSeconds)) : Vec3();
		for (int s = 1; s <= steps; s++) {
			box.rotation = Quat::slerp(lastRotation, rotation, (float) s / steps);
			for (Live& l : live) {
				Rope& rope = l.entry->rope;
				rope.ends[0] = l.hasOut ? plugEnd(l.out, box.rotation) : heldEnd(l.cw, pointer, box.rotation, rack, layout, box, hoverHeight, snapHeight, &rope.ends[0]);
				rope.ends[1] = l.hasIn ? plugEnd(l.in, box.rotation) : heldEnd(l.cw, pointer, box.rotation, rack, layout, box, hoverHeight, snapHeight, &rope.ends[1]);
				if (s == 1)
					rope.updateTargetLength(box, params);
				rope.step(stepSeconds, box, params);
			}
		}
	}
	lastRotation = rotation;
}


void CableScene::clear() {
	entries.clear();
	hasLastRotation = false;
	accumulator = 0;
}


} // namespace rack3d
} // namespace rack
