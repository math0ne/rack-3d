#pragma once
#include <vector>

#include <jansson.h>

#include <common.hpp>
#include <math.hpp>
#include <render3d/math.hpp>


namespace rack {
/** Rack 3D: racks placed on flat surfaces of a 3D model. */
namespace rack3d {


/** Where a placement sits on the 3D model: a flat rectangle, in model units.
Rack pixel (x, y) of the block (relative to its top-left corner) lands at `origin + right * x * unitsPerPx + down * y * unitsPerPx`.
*/
struct Surface {
	/** Model position of the block's top-left corner. */
	render3d::Vec3 origin;
	/** Unit vectors along the block's +x and +y (rack y points down). */
	render3d::Vec3 right = render3d::Vec3(1, 0, 0);
	render3d::Vec3 down = render3d::Vec3(0, -1, 0);
	/** Model units per rack pixel. */
	float unitsPerPx = 1.f;

	/** Model position of a point given in pixels from the block's top-left corner. */
	render3d::Vec3 toModel(math::Vec localPx) const {
		return origin + right * (localPx.x * unitsPerPx) + down * (localPx.y * unitsPerPx);
	}
	/** Outward unit normal (the side the rack faces). */
	render3d::Vec3 normal() const {
		return down.cross(right).normalize();
	}
};


/** A rack placed on a flat region of the 3D model.
It owns a fixed block of the single shared rack grid. Modules inside that block are "on" this rack.
*/
struct Placement {
	int id = -1;
	/** Block of the rack grid in grid units (x = HP, y = rows), relative to RACK_OFFSET like ModuleWidget::getGridBox().
	Never moves once allocated, so positions recorded in undo history stay valid.
	*/
	math::Rect gridRect;
	/** Where it sits on the model. Placements without one aren't shown in 3D. */
	bool hasSurface = false;
	Surface surface;
};


/** Allocates placements' grid blocks and maps between rack coordinates and placements.
Pure math: owns no widgets and knows nothing about modules.
*/
struct Layout {
	/** The default rack on a cube face: 2 rows × 50 HP (750 × 760 px), centred on the square face with a 5 px
	margin left and right. (3 rows × 76 HP would fill a face exactly; that's what Phase 2 patches use.)
	*/
	static constexpr int CUBE_ROWS = 2;
	static constexpr int CUBE_HP = 50;
	/** The cube is 2 model units wide, centred on the origin. */
	static constexpr float CUBE_SIZE = 2.f;

	std::vector<Placement> placements;

	/** Allocates a `rows` × `hp` block below every block ever allocated, with one empty gutter row between blocks.
	Returns the new placement's id.
	*/
	int addPlacement(int rows, int hp);
	/** Adds a placement with a known id and block, e.g. when loading a patch.
	Returns false if the id is taken, the block is empty, or it overlaps or touches another block (gutter rows must stay empty).
	*/
	bool insertPlacement(const Placement& p);
	/** Removes a placement. Its block is not reused until clear(): the caller must move or remove its modules first. */
	void removePlacement(int id);
	void clear();
	/** Adds 6 cube-face placements, with surfaces, in the order front, right, back, left, top, bottom.
	Model axes: +x right, +y up, +z toward the viewer. Side faces read upright; the top face's upper edge is at
	the back and the bottom face's at the front, so both read upright when the cube tips toward them.
	Each rack is `rows` × `hp`; its longer side spans the face and it's centred along the shorter one.
	Returns their ids in face order.
	*/
	std::vector<int> addCube(int rows = CUBE_ROWS, int hp = CUBE_HP);

	Placement* getPlacement(int id);
	/** The placement whose block fully contains `gridBox`, or NULL if none does (e.g. it's in a gutter or straddles two blocks). */
	Placement* placementOf(math::Rect gridBox);
	/** The placement whose block contains the grid point, or NULL. */
	Placement* placementAt(math::Vec gridPos);
	/** The placement whose block is closest to the grid point (0 distance if inside). NULL if there are no placements. */
	Placement* nearestPlacement(math::Vec gridPos);

	/** Saves the placements to the patch's root "rack3d" object. */
	json_t* toJson() const;
	/** Replaces the placements with those from a patch's root "rack3d" object.
	Returns false (and leaves the layout unchanged) if it's missing or invalid.
	*/
	bool fromJson(json_t* rack3dJ);

	enum FitResult {
		/** Every box was already inside a placement. */
		FIT_KEPT,
		/** The whole patch was moved as one block into a placement. */
		FIT_MOVED_BLOCK,
		/** Rows were split into runs of touching boxes and packed into placement rows. */
		FIT_PACKED,
	};
	/** Fits module boxes (grid units, e.g. from a stock patch) into the placements. Writes each box's new grid
	position to `positions`. Tries, in order: keep every box where it is; move the whole patch as one block into a
	placement; pack runs of touching boxes (possible expander chains) into placement rows, left to right, closing gaps.
	A run that fits in a row stays together. One too wide for every row is split at module boundaries (the number of
	broken neighbour pairs is added to `splits`: expanders across them stop working). Only a box wider than every row
	goes on the overflow shelf below every placement (outside all of them), counted in `overflow`.
	Boxes must not overlap.
	*/
	FitResult fitBoxes(const std::vector<math::Rect>& boxes, std::vector<math::Vec>& positions, int* overflow = NULL, int* splits = NULL) const;

	/** Moves `gridBox` the least distance needed to lie inside `p`'s block. The box must fit (size ≤ block size). */
	static math::Rect clampInto(const Placement& p, math::Rect gridBox);
	/** The placement's block in rack pixels. */
	static math::Rect getRackRect(const Placement& p);
	/** Rack pixels → pixels relative to the block's top-left corner. */
	static math::Vec toLocal(const Placement& p, math::Vec rackPos);
	/** Pixels relative to the block's top-left corner → rack pixels. */
	static math::Vec fromLocal(const Placement& p, math::Vec localPos);

private:
	int nextId = 0;
	/** First grid row free for the next block. Only grows, until clear(). */
	int nextRow = 0;
};


/** Checks Layout's invariants. Returns the number of failed checks and logs each failure. */
int layoutSelfTest();


} // namespace rack3d
} // namespace rack
