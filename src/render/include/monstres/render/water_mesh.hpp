#pragma once

#include "monstres/core/world.hpp"
#include "monstres/render/models.hpp"
#include "monstres/render/renderer.hpp"

#include <cstdint>
#include <vector>

namespace monstres {

// All the waters near the player as one surface (water.hpp: the sea, the
// lakes, the stream and the rivers alike): square tiles of 64 m (as the
// continent's near ground: outer_mesh.hpp), the water's level at each point
// of their grid where it covers the ground (the ground hides it where it
// rises: drawn a cell past the shore). A river going on into the lake or
// the sea at their level meets them in the same surface: never a ribbon
// over them. Clearer where shallow, deep blue farther, white where it falls
// (the rivers' waterfalls). Both faces: seen from under it too.
//
// Fine tiles (2 m cells: the ground's own lattice) around the player; over
// the relief farther away (the far terrain has a hole there), coarse ones
// (8 m). Built as the player goes, a few each frame, kept a while; all drawn
// tiles in one mesh (MeshId::Water), a part per tile.
namespace water_tiles {
inline constexpr float kTile = 64.0f;
inline constexpr float kFine = 2.0f;
inline constexpr float kCoarse = 8.0f;
inline constexpr float kReach = 270.0f;  // fine tiles whose middle is this close (beyond the continent's near ground)
inline constexpr size_t kKept = 160;     // fine tiles kept
}  // namespace water_tiles

// One tile (i, j: from the relief's corner, any sign), its cells `cell` m wide.
MeshData build_water_tile(const World& world, int i, int j, float cell);
// The whole relief's water in coarse tiles (for tests; WaterTiles draws them).
MeshData build_water_mesh(const World& world);

class WaterTiles {
 public:
  void reset();
  // Builds the tiles needed for about budget_ms (all of them with a big
  // budget); true when the drawn mesh changed (send mesh() to the renderer
  // as MeshId::Water, its parts to the terrain list).
  bool update(const World& world, Vec2 player, double budget_ms);
  const MeshData& mesh() const { return mesh_; }
  size_t fine() const { return fine_.size(); }

 private:
  struct Built {
    int i, j;
    MeshData mesh;
    uint64_t used = 0;
  };
  struct Drawn {
    int i, j;
    bool fine;
    bool operator==(const Drawn&) const = default;
  };
  std::vector<Built> fine_, coarse_;
  std::vector<Drawn> drawn_;
  MeshData mesh_;
  uint64_t frame_ = 0;
};

}  // namespace monstres
