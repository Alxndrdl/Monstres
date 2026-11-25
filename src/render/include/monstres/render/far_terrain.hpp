#pragma once

#include "monstres/core/continent.hpp"
#include "monstres/core/heightmap.hpp"
#include "monstres/render/renderer.hpp"

namespace monstres {

// The continent seen from the playable area: its ground, its waters and the
// active volcano's lava, drawn in the far pass (DrawList::far: before
// everything else, with its own depth range, so that nothing flickers at a
// few kilometers).
//
// The ground is one indexed mesh in square rings around the origin (the
// World's own ground fills the relief's rectangle; cells wholly on it are left out): 10 m cells first, then 20, 40 and 80 m
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
// Away from the start (the player gone far over the continent), the rings
// follow it: around a center on a lattice of kCenterStep (the rings' grids
// stay the same: a multiple of the coarsest cell), moved when the player is
// kRecenter from it; the first ring then narrower (the relief is not in it).
inline constexpr float kOuterAway[kLevels] = {720.0f, 1360.0f, 3520.0f, 6400.0f};
inline constexpr float kCenterStep = 160.0f;
inline constexpr float kRecenter = 400.0f;
inline constexpr float kOceanReach = 9000.0f;  // the water goes on to the horizon
inline constexpr float kSkirt = 6.0f;          // along the relief's edge, the continent's ground goes down this far
}  // namespace far_terrain

// With `ground` (the World's: its tiles beyond the relief, heightmap.hpp),
// the continent's ground with its folds, as the player will walk on it
// there (the folds too fine for a ring left out); without, the continent's
// alone.
// Around `center` (the origin, or far_center_for's).
MeshData build_far_ground_mesh(const Continent& continent, const Heightmap* ground = nullptr, Vec2 center = {0, 0});
// The center the far ground should have for a player there, its center now
// `now` (`now` itself while the player is not far from it).
Vec2 far_center_for(Vec2 player, Vec2 now);
// (With `ground`: the sea where the continent's ground raised by the relief's rim is below it.)
MeshData build_far_water_mesh(const Continent& continent, const Heightmap* ground = nullptr);
MeshData build_lava_mesh(const Continent& continent);

// Height of the far ground at a vertex of ring `level` (what the mesh uses,
// edges between rings included: for tests).
float far_vertex_height(const Continent& continent, int level, float x, float z, const Heightmap* ground = nullptr,
                        Vec2 center = {0, 0});
// Its color there (by region, height and steepness): the continent's tiles drawn near use it too.
// (With the world's ground: its snow going on from the playable north's;
// its rivers' water.)
Color far_color(const Continent& continent, float x, float z, float h, Vec3 normal, const Heightmap* ground = nullptr);

}  // namespace monstres
