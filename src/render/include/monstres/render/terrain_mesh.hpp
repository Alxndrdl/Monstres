#pragma once

#include "monstres/core/world.hpp"
#include "monstres/render/models.hpp"
#include "monstres/render/renderer.hpp"

#include <cstdint>
#include <vector>

namespace monstres {

namespace terrain {
inline constexpr float kDetailHalfSize = rules::kReliefHalfSize;  // ground grid: the relief's cells
inline constexpr float kPathHeight = 0.03f;       // just above the ground
// The ground is drawn by square tiles of kTile meters, more or less
// detailed: in the relief's own 2 m cells on the player's tile and the
// eight around it, in 4 m cells two or three tiles away, in 8 m cells
// farther (the coarse ground, one mesh for the whole relief, built once).
// The edges of a tile next to a coarser one follow the coarser one's (no
// gap between them).
inline constexpr float kTile = 64.0f;
inline constexpr int kTiles = static_cast<int>(2 * kDetailHalfSize / kTile);
inline constexpr int kLods = 3;
inline constexpr float kLodCells[kLods] = {2.0f, 4.0f, 8.0f};
inline constexpr int kLodReach[kLods - 1] = {1, 3};  // tiles away from the player's (at most) for each detailed level
inline constexpr size_t kTileCache = 120;  // detailed tiles kept once built
inline constexpr size_t kDetailMesh = kMeshCount;  // the drawn tiles: the renderer's mesh after the models
}  // namespace terrain

// Which tile a coordinate is on (clamped to the relief), and where it starts.
int tile_of(float v);
float tile_start(int t);

// A detailed tile: where, how detailed (0: 2 m cells, 1: 4 m), and which of
// its sides follow a coarser neighbor (bits: 1 -x, 2 +x, 4 -z, 8 +z).
struct TileKey {
  int8_t i = 0, j = 0;
  uint8_t lod = 0;
  uint8_t stitch = 0;
  bool operator==(const TileKey&) const = default;
};

// The detailed tiles around a player at (x, z).
std::vector<TileKey> detailed_tiles(float x, float z);

// The ground's colors at the points of the relief's grid, computed when
// first needed (the tiles are built as the player walks).
struct GroundColors {
  std::vector<uint32_t> packed;  // RGBA8 per grid point; alpha 0: not computed yet
};

// The ground of the playable area: grass with color variations and patches
// of bare earth, on the relief of World::ground. Beyond the relief, the
// continent (far_terrain.hpp). Opaque.
// The coarse ground: the whole relief in 8 m cells, tile after tile (row by
// row), each tile a part (MeshData::parts, by tile_part_index).
MeshData build_coarse_ground(const World& world, GroundColors& colors);
inline size_t tile_part_index(int i, int j) { return static_cast<size_t>(i * terrain::kTiles + j); }
// A detailed tile.
MeshData build_ground_tile(const World& world, GroundColors& colors, TileKey key);
// All of it around the player in one mesh (the coarse ground but under the
// detailed tiles, and those): for tests.
MeshData build_ground_mesh(const World& world);

// Which detailed tiles are drawn, built ahead of time as the player walks:
// when the player comes onto another tile, the tiles it needs are built, a
// few each frame, and drawn all at once when they are all ready (meanwhile,
// the ones before stay). Built tiles are kept (kTileCache, the least
// recently drawn ones forgotten first).
//
// Few draw calls: the drawn tiles make one mesh (kDetailMesh), and the
// coarse ground's tiles not drawn in detail are put in order again (its
// indices only): both sorted by sectors around the player, so that those in
// view are mostly next to one another.
class GroundTiles {
public:
  // Starts again with this coarse ground (its parts: one per tile, by tile_part_index).
  void reset(const MeshData& coarse);
  // Builds tiles the player at (x, z) needs, for about budget_ms (at least
  // one if any is missing; all of them when nothing is drawn yet, or
  // nothing near: after a teleporter). True
  // when the drawn tiles changed: then send detail() to the GPU as mesh
  // kDetailMesh, coarse_indices() as the coarse ground's indices, and fill
  // the terrain list again.
  bool update(const World& world, GroundColors& colors, Vec2 player, double budget_ms);
  const MeshData& detail() const { return detail_; }
  const std::vector<uint32_t>& coarse_indices() const { return coarse_sorted_; }
  // The terrain list: the coarse ground but under the drawn tiles, the
  // tiles, the path and the water (with the parts of their meshes, for culling).
  void fill_list(DrawList& list, const std::vector<Part>& path_parts, const std::vector<Part>& water_parts = {}) const;

  int built() const { return built_; }  // tiles built since the start (profile)
  bool ready() const { return !drawn_keys_.empty(); }
  const std::vector<TileKey>& drawn() const { return drawn_keys_; }
  size_t cached() const { return cache_.size(); }

private:
  struct Cached {
    TileKey key;
    MeshData mesh;
    uint64_t last_drawn = 0;
  };
  const Cached* find(TileKey key) const;
  void assemble(Vec2 player);
  std::vector<Cached> cache_;
  std::vector<uint32_t> coarse_indices_;  // as built, tile after tile
  std::vector<Part> coarse_tiles_;
  std::vector<uint32_t> coarse_sorted_;
  std::vector<Part> coarse_sorted_parts_;
  MeshData detail_;
  std::vector<TileKey> drawn_keys_;
  uint64_t frame_ = 0;
  int built_ = 0;
};

// Height of the ground: World::ground (the lake bed dips under the water).
float ground_height(const World& world, float x, float z);

// The lake surface, transparent, slightly darker towards the middle; and
// the sea where it comes into the relief (beyond, the continent's water).
MeshData build_water_mesh(const World& world);

// The dirt path as a ribbon slightly above the ground, with soft
// transparent edges and faded ends.
MeshData build_path_mesh(const World& world);

}  // namespace monstres
