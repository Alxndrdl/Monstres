#pragma once

#include "monstres/core/world.hpp"
#include "monstres/render/renderer.hpp"

namespace monstres {

namespace terrain {
inline constexpr float kDetailHalfSize = rules::kReliefHalfSize;  // ground grid: the relief's cells
inline constexpr float kPathHeight = 0.03f;       // just above the ground
// The ground is drawn in square rings around a center that follows the
// player (snapped to kRecenter): 2 m cells close, then 4 m, then 8 m up to
// the edge of the relief. Half sizes of the rings (multiples of the next
// ring's cells, so that their edges line up).
inline constexpr float kRecenter = 64.0f;
inline constexpr int kRings = 3;
inline constexpr float kRingCells[kRings] = {2.0f, 4.0f, 8.0f};
inline constexpr float kRingHalf[kRings] = {96.0f, 224.0f, 1e9f};
inline constexpr float kSkirt = 4.0f;  // along the relief's edge, the ground goes down this far (no gap with the continent)
}  // namespace terrain

// Center of the ground's rings for a player at (x, z).
Vec2 ground_center(float x, float z);

// The ground's colors at the points of the relief's grid, computed when
// first needed (the ground is built again as the player walks).
struct GroundColors {
  std::vector<uint32_t> packed;  // RGBA8 per grid point; alpha 0: not computed yet
};

// The ground of the playable area: grass with color variations and patches
// of bare earth, on the relief of World::ground, in rings around `center`
// (see terrain::kRings). Beyond the relief, the continent (far_terrain.hpp).
// Opaque.
MeshData build_ground_mesh(const World& world, GroundColors& colors, Vec2 center);
MeshData build_ground_mesh(const World& world);  // around the player, without keeping the colors

// Height of the ground: World::ground (the lake bed dips under the water).
float ground_height(const World& world, float x, float z);

// The lake surface, transparent, slightly darker towards the middle; and
// the sea where it comes into the relief (beyond, the continent's water).
MeshData build_water_mesh(const World& world);

// The dirt path as a ribbon slightly above the ground, with soft
// transparent edges and faded ends.
MeshData build_path_mesh(const World& world);

}  // namespace monstres
