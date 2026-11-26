#pragma once

#include "monstres/core/continent.hpp"
#include "monstres/core/world.hpp"
#include "monstres/render/models.hpp"
#include "monstres/render/renderer.hpp"

#include <cstdint>
#include <vector>

namespace monstres {

// The continent's ground near the player, beyond the relief (its tiles:
// heightmap.hpp), in detail: square tiles of 64 m in 4 m cells around the
// player (the far terrain, drawn first, has the same ground in 10 m cells
// further on: these are drawn over it). Colored as the far terrain is; along
// the relief's edge a band going down (no gap where the relief's coarser
// ground meets them). Built as the player walks, a few each frame, kept a
// while; all drawn tiles in one mesh (kOuterMesh), a part per tile. (Their
// water: water_mesh.hpp, all the waters alike.)
namespace near_outer {
inline constexpr float kTile = 64.0f;
inline constexpr float kCell = 2.0f;  // (the ground's own lattice: its rivers' beds, its waterfalls)
inline constexpr float kReach = 260.0f;    // tiles whose middle is this close to the player
inline constexpr size_t kKept = 96;        // built tiles kept
inline constexpr float kSkirt = 3.0f;      // the band along the relief's edge goes down this far
inline constexpr size_t kOuterMesh = kMeshCount + 1;  // the renderer's mesh after the detailed ground's
}  // namespace near_outer

// One tile (i, j: from the relief's corner, any sign), built.
MeshData build_outer_tile(const World& world, const Continent& continent, int i, int j);
// The tiles a player at (x, z) needs (outside the relief's rectangle).
std::vector<std::pair<int, int>> outer_tiles_around(float x, float z);

class OuterGround {
 public:
  void reset();
  // Builds the tiles needed for about budget_ms (all of them with a big
  // budget); true when the drawn mesh changed (send mesh() to the renderer
  // as kOuterMesh, and add() it to the terrain list again).
  bool update(const World& world, const Continent& continent, Vec2 player, double budget_ms);
  const MeshData& mesh() const { return mesh_; }
  // Into the terrain list (DrawList::batches sized for kOuterMesh).
  void add(DrawList& list) const;
  size_t cached() const { return cache_.size(); }
  size_t drawn() const { return drawn_.size(); }

 private:
  struct Built {
    int i, j;
    MeshData mesh;
    uint64_t used = 0;
  };
  std::vector<Built> cache_;
  std::vector<std::pair<int, int>> drawn_;
  MeshData mesh_;
  uint64_t frame_ = 0;
};

}  // namespace monstres
