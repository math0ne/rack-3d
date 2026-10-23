#include <cmath>

#include <rack3d/CableScene.hpp>
#include <rack3d/Layout.hpp>
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


void CableScene::update(float dt, Quat rotation, app::RackWidget* rack, Layout* layout) {
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

	// Sync with the rack's cables: add new ones, drop removed ones. Incomplete cables (being dragged) aren't
	// simulated yet.
	for (auto& pair : entries)
		pair.second.seen = false;
	struct Live {
		Entry* entry;
		PortPose out, in;
	};
	std::vector<Live> live;
	for (app::CableWidget* cw : rack->getCables()) {
		if (!cw->isComplete() || !cw->cable)
			continue;
		PortPose out, in;
		if (!getPortPose(cw->outputPort, rack, layout, &out) || !getPortPose(cw->inputPort, rack, layout, &in))
			continue;
		Entry& e = entries[cw];
		bool fresh = e.cableId != cw->cable->id;
		e.cableId = cw->cable->id;
		e.color = cw->color;
		e.seen = true;
		if (fresh) {
			box.rotation = rotation;
			e.rope.reset(plugEnd(out, rotation), plugEnd(in, rotation), box, params);
		}
		live.push_back({&e, out, in});
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
				l.entry->rope.ends[0] = plugEnd(l.out, box.rotation);
				l.entry->rope.ends[1] = plugEnd(l.in, box.rotation);
				if (s == 1)
					l.entry->rope.updateTargetLength(box, params);
				l.entry->rope.step(stepSeconds, box, params);
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
