#pragma once
#include <vector>

#include <render3d/math.hpp>
#include <render3d/gl.hpp>


namespace rack {
namespace rack3d {


/** The scene around the model: a sunken concrete plaza among giant brutalist structures, in world space
(the camera looks down -z from +z, the model sits at the origin and turns in place).
*/
namespace environment {


/** Height of the plaza floor. The cube's bottom is at y = -1, so cables hanging off it stay clear. */
static const float FLOOR_Y = -3.2f;
/** Half the side of the sky box. Inside the camera's far plane even at its corners. */
static const float SKY_HALF = 150.f;

/** Appends the concrete structures (all axis-aligned boxes, faces wound counter-clockwise from outside) to the mesh
data. The layout is fixed: the distant blocks are placed by a seeded generator.
*/
void buildConcrete(std::vector<render3d::Mesh::Vertex>& vertices, std::vector<uint32_t>& indices);
/** Appends an axis-aligned box. */
void appendBox(std::vector<render3d::Mesh::Vertex>& vertices, std::vector<uint32_t>& indices, render3d::Vec3 min, render3d::Vec3 max);


} // namespace environment
} // namespace rack3d
} // namespace rack
