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


/** The 3D cables: one physics rope per Rack cable, kept in sync with the RackWidget's cables every frame. */
struct CableScene {
	struct Entry {
		int64_t cableId = -1;
		Rope rope;
		NVGcolor color;
		bool seen = false;
	};

	RopeParams params;

	CableScene() {
		// Rack scale: a ~4 mm cable is ~12 rack px, and the cube's 760 px faces are 2 units
		params.radius = 0.016f;
		params.plugLen = 0.1f;
	}

	/** Physics step, seconds. */
	float stepSeconds = 1 / 240.f;
	/** Keyed by CableWidget; the cable id guards against a new widget at a freed address. */
	std::map<app::CableWidget*, Entry> entries;

	/** Syncs ropes with the rack's complete cables and simulates `dt` seconds while the model turned from its last
	rotation to `rotation` (in world space; the camera looks along -z).
	*/
	void update(float dt, render3d::Quat rotation, app::RackWidget* rack, Layout* layout);
	void clear();

private:
	render3d::Quat lastRotation;
	bool hasLastRotation = false;
	float accumulator = 0;
};


} // namespace rack3d
} // namespace rack
