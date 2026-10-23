#pragma once
#include <map>
#include <vector>

#include <nanovg.h>
#include <rack3d/CableSim.hpp>


namespace rack {
namespace app {
struct RackWidget;
struct CableWidget;
struct PortWidget;
} // namespace app

namespace rack3d {


struct Layout;


/** A port's place on the model: the jack's centre (model space) and the outward direction of its rack's face. */
struct PortPose {
	render3d::Vec3 position;
	render3d::Vec3 normal;
};

/** Where `port` is on the model. False if it's not on a rack with a surface. */
bool getPortPose(app::PortWidget* port, app::RackWidget* rack, Layout* layout, PortPose* pose);


/** Where the mouse points in 3D, for the loose end of a cable being dragged. */
struct Pointer {
	/** The mouse is over the 3D view. */
	bool valid = false;
	/** The mouse ray hits a rack: where (model space) and that rack's outward normal. */
	bool onRack = false;
	render3d::Vec3 hit;
	render3d::Vec3 hitNormal;
	/** The mouse ray in world space. */
	render3d::Vec3 rayOrigin;
	render3d::Vec3 rayDir;
};


/** The 3D cables: one physics rope per Rack cable, kept in sync with the RackWidget's cables every frame.
A cable being dragged (incomplete) has one end in its port and the other held at the mouse: hovering just off a port
it would plug into, floating over the rack under the mouse and pointing into it, or dangling in space.
*/
struct CableScene {
	struct Entry {
		int64_t cableId = -1;
		app::PortWidget* outputPort = NULL;
		app::PortWidget* inputPort = NULL;
		Rope rope;
		NVGcolor color;
		bool seen = false;
	};
	/** How far a held plug floats off the rack under the mouse, and off a port it would plug into. */
	float hoverHeight = 0.15f;
	float snapHeight = 0.03f;

	/** Copied from rack3d::settings::cableParams at every update(), so the View menu's sliders act live. */
	RopeParams params;

	CableScene();

	/** Physics step, seconds. */
	float stepSeconds = 1 / 240.f;
	/** Keyed by CableWidget; the cable id guards against a new widget at a freed address. */
	std::map<app::CableWidget*, Entry> entries;

	/** Syncs ropes with the rack's cables and simulates `dt` seconds while the model turned from its last rotation
	to `rotation` (in world space; the camera looks along -z). `pointer` places the loose ends of cables being dragged.
	*/
	void update(float dt, render3d::Quat rotation, app::RackWidget* rack, Layout* layout, const Pointer& pointer = Pointer());
	void clear();

private:
	render3d::Quat lastRotation;
	bool hasLastRotation = false;
	float accumulator = 0;
};


} // namespace rack3d
} // namespace rack
