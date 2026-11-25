#pragma once

#include "monstres/core/weather.hpp"
#include "monstres/core/world.hpp"
#include "monstres/render/models.hpp"
#include "monstres/render/renderer.hpp"

#include <span>
#include <vector>

namespace monstres {

// The landscape: everything that never moves but the ground (the objects
// and their shadows, grass and flowers), detailed around the player only.
// Within kDetailRadius of its center every object is drawn with its shadow
// (trees and bushes in full within kFullRadius, simpler models beyond);
// grass and flowers grow within kGrassRadius, thinning out with the
// distance; farther away only the landmarks (houses, towers, walls, big
// rocks) and simplified trees, up to kObjectsRadius. Small things, grass
// and trees shrink away before their edge (View::fade_*), so the landscape
// can be rebuilt around a new center (every kStep meters walked) without
// anything popping up.
namespace landscape {
inline constexpr float kStep = 16.0f;
inline constexpr float kDetailRadius = 64.0f;
inline constexpr float kFullRadius = 26.0f;
inline constexpr float kGrassRadius = 56.0f;
// From the player: the center is at most kStep * 0.71 away.
inline constexpr float kGrassFadeStart = 33.0f, kGrassFadeEnd = 43.0f;
inline constexpr float kDetailFadeStart = 42.0f, kDetailFadeEnd = 52.0f;
// Trees and rocks (the world has tens of thousands): only within this far
// of the center, shrinking away before (from the player).
inline constexpr float kObjectsRadius = 215.0f;
inline constexpr float kTreeFadeStart = 160.0f, kTreeFadeEnd = 198.0f;
// Grass thins out (from the center): all of it within kGrassThinStart, then
// fewer and wider tufts, down to a share of kGrassThinLeast at kGrassRadius.
inline constexpr float kGrassThinStart = 24.0f, kGrassThinLeast = 0.6f;
// Beyond this (from the center), simpler tufts and flowers (one triangle a blade).
inline constexpr float kGrassFullRadius = 22.0f;

// The world by chunks of kChunk meters (lined up with the objects' index):
// what the landscape is made of is computed chunk by chunk, when first
// needed, and kept (LandscapeCache).
// Beyond the objects' index (the relief and a little more), more on every
// side, out to where one may go (rules::kReach*): the continent's nature
// there (outer.hpp). Only those around the player are computed; the
// continent's are forgotten again when it is far away.
inline constexpr float kChunk = 32.0f;
inline constexpr int kOuterWest = static_cast<int>((-rules::kReachMinX - ObjectIndex::kHalf) / kChunk) + 8;
inline constexpr int kOuterEast = static_cast<int>((rules::kReachMaxX - ObjectIndex::kHalf) / kChunk) + 8;
inline constexpr int kOuterNorth = static_cast<int>((-rules::kReachMinZ - ObjectIndex::kNorth) / kChunk) + 8;
inline constexpr int kOuterSouth = static_cast<int>((rules::kReachMaxZ - ObjectIndex::kHalf) / kChunk) + 8;
inline constexpr float kChunkOriginX = -ObjectIndex::kHalf - kOuterWest * kChunk, kChunkOriginZ = -ObjectIndex::kNorth - kOuterNorth * kChunk;
inline constexpr int kChunksX = ObjectIndex::kCellsX / 4 + kOuterWest + kOuterEast, kChunksZ = ObjectIndex::kCellsZ / 4 + kOuterNorth + kOuterSouth;
static_assert(ObjectIndex::kCell * 4 == kChunk && (ObjectIndex::kCellsX / 4) * 4 == ObjectIndex::kCellsX && (ObjectIndex::kCellsZ / 4) * 4 == ObjectIndex::kCellsZ);

// The built landscape is sorted for culling: by sectors around its center,
// those within kInnerBand first (each sector a part of every batch).
inline constexpr int kSectors = 32;
inline constexpr float kInnerBand = 40.0f;
inline constexpr int kBins = 2 * kSectors;
}  // namespace landscape

// Center of the detailed landscape for a player at (x, z).
Vec2 landscape_center(float x, float z);

// When the player has come to a new center but its chunks are not all
// computed yet: whether the landscape around `built` may stay a little
// longer. Only while the player is still well within it (its grass and
// small things reach farther than they show: kGrassRadius against
// kGrassFadeEnd); beyond, the landscape must be built right away, or grass
// would be missing ahead.
inline constexpr float kWaitReach = landscape::kGrassRadius - landscape::kGrassFadeEnd - 1.5f;
bool landscape_can_wait(Vec2 player, Vec2 built);

// The models' bounds, for culling: set once the models are loaded (until
// then, and for meshes built at runtime, generous bounds).
void set_mesh_bounds(std::span<const MeshData> meshes);
Aabb mesh_bounds(MeshId id);
// Where an instance of a mesh may draw (turned around the vertical axis).
Aabb instance_bounds(MeshId id, const Instance& in);

// What the landscape is made of, by chunks: each object's instances (its
// model up close, at a middle distance and from afar, its shadow), and the
// grass and flowers. Computed when first needed, ahead of time when the
// player comes closer (prepare), and kept: building the landscape around a
// new center only copies from here. For one world (cleared when another
// one comes).
class LandscapeCache {
public:
  // One object, ready to draw.
  struct Item {
    float x = 0, z = 0;
    bool nature = false;  // trees, rocks...: not drawn beyond kObjectsRadius
    bool small = false;   // not drawn beyond kDetailRadius
    MeshId full = MeshId::Count, mid = MeshId::Count, far = MeshId::Count;
    Instance instance;
    bool has_shadow = false;
    Instance shadow;
  };
  struct Grass {
    MeshId mesh;
    Instance instance;
    float keep;  // in [0, 1): thinning out, the tufts with the lowest values stay
  };
  struct Chunk {
    bool objects_done = false, grass_done = false;
    bool listed = false;  // in filled_
    std::vector<Item> items;
    std::vector<Grass> grass;
  };

  // Computes, nearest first, the chunks that the landscape around a player
  // at `at` will soon need, for about `budget_ms` milliseconds (at least one
  // chunk if any is missing). Returns how many were computed.
  int prepare(const World& world, Vec2 at, double budget_ms);
  // Whether everything the landscape around `center` needs is computed.
  bool ready(const World& world, Vec2 center);
  void clear();
  size_t chunks_with_grass() const;
  // Whether the chunk with (x, z) has its objects computed (tests).
  bool objects_ready(float x, float z) const;

  // The chunk (cx, cz), computed as far as asked.
  const Chunk& chunk(const World& world, int cx, int cz, bool grass);

private:
  friend void build_landscape(const World& world, DrawList& list, Vec2 center, LandscapeCache& cache, Season season);
  void check_world(const World& world);
  uint64_t seed_ = ~0ULL;
  size_t object_count_ = 0;
  const void* objects_ = nullptr;
  std::vector<Chunk> chunks_;  // kChunksX * kChunksZ (index cx * kChunksZ + cz), filled when needed
  std::vector<uint32_t> filled_;  // the chunks with something computed (forgotten when far)
  // Scratch space for building (kept to reuse its memory).
  std::vector<std::vector<Instance>> bins_;
  std::vector<Aabb> bin_bounds_;
};

// The landscape around `center` (the player's landscape_center by default):
// the trees and rocks within landscape::kObjectsRadius only, the buildings
// everywhere; sorted by sectors around the center, each one a part of
// every batch (DrawList::parts), for culling. Rebuild it when the world
// changes or the center moves. Without a cache, everything is computed.
// In its season: the trees bare in winter (no flowers, less grass), fallen
// leaves in autumn (under the trees, a few in the meadows; fewer flowers),
// more flowers in spring (the colors themselves: SeasonLook, in the shaders).
void build_landscape(const World& world, DrawList& list, Vec2 center, LandscapeCache& cache,
                     Season season = Season::Summer);
void build_landscape(const World& world, DrawList& list, Vec2 center, Season season = Season::Summer);
void build_landscape(const World& world, DrawList& list, Season season = Season::Summer);

// One frame of the landscape following the player: rebuilt around its new
// center when the chunks are ready (or right away, see landscape_can_wait),
// chunks computed ahead meanwhile, for about budget_ms. `built`: the center
// of the landscape in `list` ({1e9, 1e9} at first). `all_now`: no waiting
// (a new world, pictures). True when rebuilt; `prepared` counts the chunks computed.
bool follow_player(const World& world, DrawList& list, LandscapeCache& cache, Vec2& built, double budget_ms,
                   bool all_now = false, int* prepared = nullptr, Season season = Season::Summer);

// One object of an interior (interior.hpp) into `list`, with its shadow.
void add_interior_object(const World& world, const Object& object, DrawList& list);

}  // namespace monstres
