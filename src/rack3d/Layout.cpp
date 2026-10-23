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


json_t* Layout::toJson() const {
	json_t* rack3dJ = json_object();
	json_object_set_new(rack3dJ, "version", json_integer(1));
	json_t* placementsJ = json_array();
	for (const Placement& p : placements) {
		json_t* pJ = json_object();
		json_object_set_new(pJ, "id", json_integer(p.id));
		math::Rect r = p.gridRect;
		json_object_set_new(pJ, "grid", json_pack("[i, i, i, i]", (int) r.pos.x, (int) r.pos.y, (int) r.size.x, (int) r.size.y));
		json_array_append_new(placementsJ, pJ);
	}
	json_object_set_new(rack3dJ, "placements", placementsJ);
	return rack3dJ;
}


bool Layout::fromJson(json_t* rack3dJ) {
	if (!rack3dJ)
		return false;
	json_t* placementsJ = json_object_get(rack3dJ, "placements");
	if (!json_is_array(placementsJ) || json_array_size(placementsJ) == 0)
		return false;

	Layout l;
	size_t i;
	json_t* pJ;
	json_array_foreach(placementsJ, i, pJ) {
		Placement p;
		int x, y, w, h;
		if (json_unpack(pJ, "{s:i, s:[i, i, i, i]}", "id", &p.id, "grid", &x, &y, &w, &h) != 0) {
			WARN("rack3d: invalid placement %d in patch", (int) i);
			return false;
		}
		p.gridRect = math::Rect(x, y, w, h);
		if (!l.insertPlacement(p)) {
			WARN("rack3d: placement %d in patch overlaps another or is empty", p.id);
			return false;
		}
	}
	*this = l;
	return true;
}


Layout::FitResult Layout::fitBoxes(const std::vector<math::Rect>& boxes, std::vector<math::Vec>& positions, int* overflow) const {
	positions.resize(boxes.size());
	if (overflow)
		*overflow = 0;
	for (size_t i = 0; i < boxes.size(); i++)
		positions[i] = boxes[i].pos;
	if (boxes.empty())
		return FIT_KEPT;

	// 1. Everything already fits
	auto isPlaced = [&](const math::Rect& b) {
		for (const Placement& p : placements) {
			if (p.gridRect.contains(b))
				return true;
		}
		return false;
	};
	if (std::all_of(boxes.begin(), boxes.end(), isPlaced))
		return FIT_KEPT;

	// 2. The whole patch fits in one placement as a block
	math::Rect bounds = boxes[0];
	for (const math::Rect& b : boxes)
		bounds = bounds.expand(b);
	for (const Placement& p : placements) {
		if (bounds.size.x <= p.gridRect.size.x && bounds.size.y <= p.gridRect.size.y) {
			math::Vec delta = p.gridRect.pos.minus(bounds.pos);
			for (size_t i = 0; i < boxes.size(); i++)
				positions[i] = boxes[i].pos.plus(delta);
			return FIT_MOVED_BLOCK;
		}
	}

	// 3. Pack runs of touching boxes into placement rows
	std::vector<size_t> order(boxes.size());
	for (size_t i = 0; i < order.size(); i++)
		order[i] = i;
	std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
		if (boxes[a].pos.y != boxes[b].pos.y)
			return boxes[a].pos.y < boxes[b].pos.y;
		return boxes[a].pos.x < boxes[b].pos.x;
	});
	// A run is a chain of boxes in one row where each box's left edge touches the previous box's right edge
	std::vector<std::vector<size_t>> runs;
	for (size_t i : order) {
		if (!runs.empty()) {
			const math::Rect& last = boxes[runs.back().back()];
			if (last.pos.y == boxes[i].pos.y && last.getRight() == boxes[i].getLeft()) {
				runs.back().push_back(i);
				continue;
			}
		}
		runs.push_back({i});
	}

	// Free cursor for each placement row, in placement then row order
	struct Row {
		float y, right, cursor;
	};
	std::vector<Row> rows;
	for (const Placement& p : placements) {
		for (int r = 0; r < (int) p.gridRect.size.y; r++)
			rows.push_back({p.gridRect.pos.y + r, p.gridRect.getRight(), p.gridRect.getLeft()});
	}
	// Overflow shelf: one unbounded row below every block ever allocated
	float shelfY = nextRow;
	float shelfCursor = 0;

	for (const std::vector<size_t>& run : runs) {
		float width = 0;
		for (size_t i : run)
			width += boxes[i].size.x;
		Row* row = NULL;
		for (Row& r : rows) {
			if (r.cursor + width <= r.right) {
				row = &r;
				break;
			}
		}
		float x, y;
		if (row) {
			x = row->cursor;
			y = row->y;
			row->cursor += width;
		}
		else {
			x = shelfCursor;
			y = shelfY;
			shelfCursor += width;
			if (overflow)
				*overflow += run.size();
		}
		for (size_t i : run) {
			positions[i] = math::Vec(x, y);
			x += boxes[i].size.x;
		}
	}
	return FIT_PACKED;
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

	// JSON round-trip
	{
		Layout a;
		a.addCube();
		a.removePlacement(2);
		json_t* j = a.toJson();
		Layout b;
		check(b.fromJson(j), "fromJson accepts toJson's output");
		json_decref(j);
		bool same = a.placements.size() == b.placements.size();
		for (size_t i = 0; same && i < a.placements.size(); i++)
			same = a.placements[i].id == b.placements[i].id && a.placements[i].gridRect.equals(b.placements[i].gridRect);
		check(same, "JSON round-trip keeps ids and blocks");
		check(b.addPlacement(1, 1) == 6 && b.getPlacement(6)->gridRect.pos.y == 24, "after loading, new ids and blocks continue after the loaded ones");
		Layout c;
		c.addCube();
		check(!c.fromJson(NULL), "fromJson(NULL) fails");
		json_t* bad = json_pack("{s:[{s:i, s:[i,i,i,i]}, {s:i, s:[i,i,i,i]}]}", "placements", "id", 0, "grid", 0, 0, 10, 2, "id", 1, "grid", 0, 2, 10, 2);
		check(!c.fromJson(bad), "fromJson rejects blocks without a gutter");
		json_decref(bad);
		check(c.placements.size() == 6, "a rejected fromJson leaves the layout unchanged");
	}

	// Fitting stock patches
	{
		Layout cube;
		cube.addCube();
		std::vector<math::Vec> pos;
		auto noOverlap = [&](const std::vector<math::Rect>& boxes) {
			for (size_t i = 0; i < boxes.size(); i++)
				for (size_t j = i + 1; j < boxes.size(); j++)
					if (math::Rect(pos[i], boxes[i].size).intersects(math::Rect(pos[j], boxes[j].size)))
						return false;
			return true;
		};
		auto allPlaced = [&](const std::vector<math::Rect>& boxes) {
			for (size_t i = 0; i < boxes.size(); i++)
				if (!cube.placementOf(math::Rect(pos[i], boxes[i].size)))
					return false;
			return true;
		};

		std::vector<math::Rect> inside = {math::Rect(0, 0, 10, 1), math::Rect(20, 5, 10, 1)};
		check(cube.fitBoxes(inside, pos) == Layout::FIT_KEPT && pos[1].equals(math::Vec(20, 5)), "fit: boxes already in placements are kept");

		std::vector<math::Rect> small = {math::Rect(-30, -2, 10, 1), math::Rect(-20, -2, 6, 1), math::Rect(-30, -1, 8, 1)};
		check(cube.fitBoxes(small, pos) == Layout::FIT_MOVED_BLOCK, "fit: a small patch is moved as a block");
		check(pos[0].equals(math::Vec(0, 0)) && pos[1].equals(math::Vec(10, 0)) && pos[2].equals(math::Vec(0, 1)), "fit: ...keeping its shape");

		// A wide patch: row 0 has runs [0..40) and [50..130) (80 HP, too wide for a block), row 1 has one module
		std::vector<math::Rect> wide = {
			math::Rect(0, 0, 20, 1), math::Rect(20, 0, 20, 1),
			math::Rect(50, 0, 40, 1), math::Rect(90, 0, 40, 1),
			math::Rect(10, 1, 10, 1),
		};
		int overflow = -1;
		check(cube.fitBoxes(wide, pos, &overflow) == Layout::FIT_PACKED, "fit: a wide patch is packed");
		check(pos[1].x - pos[0].x == 20 && pos[1].y == pos[0].y, "fit: a run of touching modules stays together");
		check(overflow == 2 && pos[2].y == 24 && pos[3].y == 24 && pos[3].x - pos[2].x == 40, "fit: a run wider than any rack goes to the overflow shelf, intact");
		check(cube.placementOf(math::Rect(pos[4], wide[4].size)) && cube.placementOf(math::Rect(pos[0], wide[0].size)), "fit: the rest is placed");
		check(noOverlap(wide), "fit: no overlaps after packing");

		// Many runs: they fill rows in order
		std::vector<math::Rect> many;
		for (int i = 0; i < 40; i++)
			many.push_back(math::Rect(i * 25, i % 3, 20, 1));
		check(cube.fitBoxes(many, pos, &overflow) == Layout::FIT_PACKED && overflow == 0, "fit: 40 runs of 20 HP all fit");
		check(allPlaced(many) && noOverlap(many), "fit: ...placed, no overlaps");
	}

	if (failures == 0)
		INFO("rack3d::Layout self-test passed");
	return failures;
}


} // namespace rack3d
} // namespace rack
