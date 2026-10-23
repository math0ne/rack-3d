#include <algorithm>
#include <cmath>

#include <rack3d/Layout.hpp>
#include <app/common.hpp>


namespace rack {
namespace rack3d {


int Layout::addPlacement(int rows, int hp) {
	Placement p;
	p.id = nextId;
	p.gridRect = math::Rect(math::Vec(0, nextRow), math::Vec(hp, rows));
	placements.push_back(p);
	nextId++;
	nextRow += rows + 1;
	return p.id;
}


bool Layout::insertPlacement(const Placement& p) {
	if (p.id < 0 || getPlacement(p.id))
		return false;
	if (!(p.gridRect.size.x >= 1 && p.gridRect.size.y >= 1))
		return false;
	// Blocks must not share any row, even far apart horizontally, because Rack's row algorithms (force, squeeze,
	// expanders) work along whole rows. They also need an empty gutter row between them.
	float top = p.gridRect.getTop() - 1;
	float bottom = p.gridRect.getBottom() + 1;
	for (const Placement& p2 : placements) {
		if (top < p2.gridRect.getBottom() && p2.gridRect.getTop() < bottom)
			return false;
	}
	placements.push_back(p);
	nextId = std::max(nextId, p.id + 1);
	nextRow = std::max(nextRow, (int) std::lround(p.gridRect.getBottom()) + 1);
	return true;
}


void Layout::removePlacement(int id) {
	placements.erase(std::remove_if(placements.begin(), placements.end(), [&](const Placement& p) {
		return p.id == id;
	}), placements.end());
}


void Layout::clear() {
	placements.clear();
	nextId = 0;
	nextRow = 0;
}


std::vector<int> Layout::addCube() {
	std::vector<int> ids;
	for (int i = 0; i < 6; i++)
		ids.push_back(addPlacement(CUBE_ROWS, CUBE_HP));
	return ids;
}


Placement* Layout::getPlacement(int id) {
	for (Placement& p : placements) {
		if (p.id == id)
			return &p;
	}
	return NULL;
}


Placement* Layout::placementOf(math::Rect gridBox) {
	for (Placement& p : placements) {
		if (p.gridRect.contains(gridBox))
			return &p;
	}
	return NULL;
}


Placement* Layout::placementAt(math::Vec gridPos) {
	for (Placement& p : placements) {
		if (p.gridRect.contains(gridPos))
			return &p;
	}
	return NULL;
}


Placement* Layout::nearestPlacement(math::Vec gridPos) {
	Placement* best = NULL;
	float bestDist = INFINITY;
	for (Placement& p : placements) {
		math::Vec closest = gridPos.clamp(p.gridRect);
		float dist = closest.minus(gridPos).square();
		if (dist < bestDist) {
			bestDist = dist;
			best = &p;
		}
	}
	return best;
}


math::Rect Layout::clampInto(const Placement& p, math::Rect gridBox) {
	return gridBox.nudge(p.gridRect);
}


math::Rect Layout::getRackRect(const Placement& p) {
	return math::Rect(p.gridRect.pos.mult(app::RACK_GRID_SIZE).plus(app::RACK_OFFSET), p.gridRect.size.mult(app::RACK_GRID_SIZE));
}


math::Vec Layout::toLocal(const Placement& p, math::Vec rackPos) {
	return rackPos.minus(getRackRect(p).pos);
}


math::Vec Layout::fromLocal(const Placement& p, math::Vec localPos) {
	return localPos.plus(getRackRect(p).pos);
}


int layoutSelfTest() {
	int failures = 0;
	auto check = [&](bool ok, const char* what) {
		if (!ok) {
			WARN("rack3d::Layout self-test failed: %s", what);
			failures++;
		}
	};

	Layout l;
	check(l.nearestPlacement(math::Vec(0, 0)) == NULL, "nearest with no placements is NULL");

	// Cube: six square blocks stacked with gutter rows
	std::vector<int> ids = l.addCube();
	check(ids.size() == 6, "addCube adds 6");
	for (int i = 0; i < 6; i++) {
		Placement* p = l.getPlacement(ids[i]);
		check(p != NULL, "cube face exists");
		if (!p)
			continue;
		check(p->gridRect.pos.equals(math::Vec(0, i * 4)), "cube face i starts at row 4i");
		math::Rect r = Layout::getRackRect(*p);
		check(r.size.equals(math::Vec(1140, 1140)), "cube face is 1140 px square");
	}

	// Membership
	check(l.placementOf(math::Rect(0, 0, 10, 1)) == l.getPlacement(ids[0]), "box at origin is on face 0");
	check(l.placementOf(math::Rect(66, 2, 10, 1)) == l.getPlacement(ids[0]), "box touching face 0's right edge is on face 0");
	check(l.placementOf(math::Rect(67, 2, 10, 1)) == NULL, "box past face 0's right edge is on no face");
	check(l.placementOf(math::Rect(0, 3, 10, 1)) == NULL, "box in gutter row 3 is on no face");
	check(l.placementOf(math::Rect(0, 4, 10, 1)) == l.getPlacement(ids[1]), "box at row 4 is on face 1");
	check(l.placementOf(math::Rect(-1, 0, 10, 1)) == NULL, "box left of HP 0 is on no face");
	check(l.placementAt(math::Vec(75.5, 2.5)) == l.getPlacement(ids[0]), "point near face 0's far corner is on face 0");
	check(l.placementAt(math::Vec(10, 3.5)) == NULL, "point in a gutter is on no face");

	// Nearest
	check(l.nearestPlacement(math::Vec(10, 3.2)) == l.getPlacement(ids[0]), "gutter point nearer face 0 picks face 0");
	check(l.nearestPlacement(math::Vec(10, 3.8)) == l.getPlacement(ids[1]), "gutter point nearer face 1 picks face 1");
	check(l.nearestPlacement(math::Vec(500, 9)) == l.getPlacement(ids[2]), "point far right of face 2 picks face 2");

	// Clamp
	Placement* f0 = l.getPlacement(ids[0]);
	if (f0) {
		check(Layout::clampInto(*f0, math::Rect(70, 5, 10, 1)).equals(math::Rect(66, 2, 10, 1)), "clamp pulls box back inside face 0");
		check(Layout::clampInto(*f0, math::Rect(5, 1, 10, 1)).equals(math::Rect(5, 1, 10, 1)), "clamp leaves an inside box alone");
	}

	// Local <-> rack round-trip
	Placement* f2 = l.getPlacement(ids[2]);
	if (f2) {
		math::Vec local(123, 456);
		check(Layout::toLocal(*f2, Layout::fromLocal(*f2, local)).equals(local), "local/rack round-trip");
		check(Layout::fromLocal(*f2, math::Vec(0, 0)).equals(app::RACK_OFFSET.plus(math::Vec(0, 8 * app::RACK_GRID_HEIGHT))), "face 2 origin in rack px");
	}

	// Append-only: removing a block doesn't let the next one reuse its rows
	l.removePlacement(ids[5]);
	check(l.getPlacement(ids[5]) == NULL, "removed placement is gone");
	int id = l.addPlacement(2, 40);
	Placement* p = l.getPlacement(id);
	check(p && p->gridRect.pos.y == 24, "new block goes below the removed one, not into its rows");
	check(id == 6, "ids are never reused");

	// Insert (patch loading)
	Layout l2;
	Placement a;
	a.id = 7;
	a.gridRect = math::Rect(0, 0, 20, 2);
	check(l2.insertPlacement(a), "insert into empty layout");
	Placement b;
	b.id = 7;
	b.gridRect = math::Rect(0, 10, 20, 2);
	check(!l2.insertPlacement(b), "duplicate id rejected");
	b.id = 8;
	b.gridRect = math::Rect(0, 2, 20, 2);
	check(!l2.insertPlacement(b), "block touching another (no gutter) rejected");
	b.gridRect = math::Rect(30, 0, 20, 2);
	check(!l2.insertPlacement(b), "block sharing rows with another rejected");
	b.gridRect = math::Rect(0, 3, 20, 2);
	check(l2.insertPlacement(b), "block after a gutter accepted");
	check(l2.addPlacement(1, 10) == 9, "next id follows the largest inserted id");
	Placement* c = l2.placementOf(math::Rect(0, 6, 1, 1));
	check(c && c->id == 9, "next block goes below inserted blocks");

	if (failures == 0)
		INFO("rack3d::Layout self-test passed");
	return failures;
}


} // namespace rack3d
} // namespace rack
