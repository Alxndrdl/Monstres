#pragma once

#include "monstres/core/continent.hpp"
#include "monstres/render/renderer.hpp"

namespace monstres {

// The continent seen from the playable area: its ground, its waters and the
// active volcano's lava, drawn in the far pass (DrawList::far: before
// everything else, with its own depth range, so that nothing flickers at a
// few kilometers).
//
// The ground is one indexed mesh in square rings around the relief's rectangle
// (the World's own ground fills it): 10 m cells first, then 20, 40 and 80 m
// farther away. Where two rings meet, the finer side follows the coarser
// one's edges, so no gap opens between them. The deep ocean has no ground:
// the water there is opaque.
namespace far_terrain {
inline constexpr int kLevels = 4;
inline constexpr float kCells[kLevels] = {10.0f, 20.0f, 40.0f, 80.0f};
// Half size of each ring's outer square, around the origin (meters): each
// one a multiple of the next ring's cells, so that their edges line up.
// The 40 m ring reaches the active volcano (its crater is about 200 m wide).
// The first one holds the whole relief (further north than wide: up to 1280 m).
inline constexpr float kOuter[kLevels] = {1300.0f, 1360.0f, 3520.0f, 5360.0f};
inline constexpr float kOceanReach = 9000.0f;  // the water goes on to the horizon
inline constexpr float kSkirt = 6.0f;          // along the relief's edge, the continent's ground goes down this far
}  // namespace far_terrain

MeshData build_far_ground_mesh(const Continent& continent);
MeshData build_far_water_mesh(const Continent& continent);
MeshData build_lava_mesh(const Continent& continent);

// Height of the far ground at a vertex of ring `level` (what the mesh uses,
// edges between rings included: for tests).
float far_vertex_height(const Continent& continent, int level, float x, float z);

}  // namespace monstres
