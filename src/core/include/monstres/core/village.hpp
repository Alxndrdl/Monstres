#pragma once

#include <array>

#include "monstres/core/terrain.hpp"

#include <cstdint>
#include <vector>

namespace monstres {

// Places of the world: the village and its lake, the ruins. Their place depends on the world seed (and the
// path, which crosses the village); like the path, they are derived data,
// rebuilt from the seed, not saved.

namespace rules {
inline constexpr float kVillageZ = -28.0f;        // the village sits on the path, north of the start
inline constexpr float kVillageRadius = 14.0f;    // no trees or rocks inside
inline constexpr float kLakeRadiusX = 9.0f;
inline constexpr float kLakeRadiusZ = 6.5f;
inline constexpr float kWaterLevel = -0.12f;      // the lake surface, just below the ground
inline constexpr float kSwimLevel = -0.3f;        // where fish bodies float
inline constexpr float kRuinsZ = 50.0f;           // the ruins are south of the start, by the path
inline constexpr float kRuinsRadius = 15.0f;      // no trees or rocks inside
inline constexpr float kRuinsFromPath = 14.0f;    // the tower stands this far beside the path
inline constexpr float kForestX = 100.0f;         // the forest: on the other side of the path than the ruins
inline constexpr float kForestZ = -5.0f;
inline constexpr float kForestRadiusX = 73.0f;
inline constexpr float kForestRadiusZ = 94.0f;
inline constexpr float kForestTreeScale = 1.35f;  // its trees grow taller than elsewhere
inline constexpr float kClearingRadius = 22.0f;   // the heart of the forest: no trees (the giant; the teleporter at its edge)
inline constexpr float kClearingFringe = 0.35f;   // beyond it (in its radii), the trees thin out
inline constexpr float kCaveX = 62.0f;            // the cave: in a rocky hill, far north
inline constexpr float kCaveZ = -110.0f;
inline constexpr float kCaveHillRadius = 21.0f;   // the foot of its cliffs
inline constexpr float kCaveHillHeight = 9.0f;
inline constexpr float kCastleX = 60.0f;          // the castle: on a hill, far south
inline constexpr float kCastleZ = 106.0f;
inline constexpr float kCastleHalf = 14.0f;       // its walls: a square of 28 m
inline constexpr float kCastleRadius = 22.0f;     // no trees or rocks inside
}  // namespace rules

struct Lake {
  Vec2 center;
  float radius_x = 0;  // 0: no lake (e.g. hand-made test worlds)
  float radius_z = 0;
  bool operator==(const Lake&) const = default;
};

struct VillageLayout {
  Vec2 center;
  Lake lake;
  bool operator==(const VillageLayout&) const = default;
};

VillageLayout make_village_layout(uint64_t seed, const Path& path);

// The ruins of the tower: an abandoned magician's tower on a knoll beside the
// path, surrounded by broken walls and columns.
struct RuinsLayout {
  Vec2 center;      // the tower; radius 0: no ruins (hand-made test worlds)
  float radius = 0;
  bool operator==(const RuinsLayout&) const = default;
};

RuinsLayout make_ruins_layout(uint64_t seed, const Path& path);

bool in_ruins(const RuinsLayout& ruins, float x, float z, float margin = 0);

// Whether (x, z) is in the lake, with its shore moved out by `margin` meters
// (negative: only well inside the water).
bool in_lake(const Lake& lake, float x, float z, float margin = 0);

bool in_village(const VillageLayout& village, float x, float z, float margin = 0);

// The forest: an ellipse of dense woods.
// Natural outlines: the lake, the forest and its clearing are no ellipses.
// At each angle around its center, a place reaches its ellipse's radius times
// outline_scale (between kOutlineLeast and 1): a few waves, whose phases come
// from the place's center and a number of its own.
inline constexpr float kOutlineLeast = 0.6f;
// Its waves: how many around, and how deep (their sum: 1 - kOutlineLeast).
inline constexpr std::array<float, 4> kOutlineWaves = {2, 3, 5, 7};
inline constexpr std::array<float, 4> kOutlineDepths = {0.16f, 0.11f, 0.08f, 0.05f};
float outline_scale(Vec2 center, uint64_t salt, float angle);
// Distance from a center to (x, z), in its ellipse's radii, stretched by the
// outline: 1 on the outline, 0 at the center.
float outline_distance(Vec2 center, float radius_x, float radius_z, uint64_t salt, float x, float z);
// The phases of outline_scale's waves (for the map, which draws the same outlines).
std::array<float, 4> outline_phases(Vec2 center, uint64_t salt);
inline constexpr uint64_t kLakeOutline = 1, kForestOutline = 2, kClearingOutline = 3;

// 0 in the middle of the lake, 1 on its shore (moved out by `margin` meters).
float lake_distance(const Lake& lake, float x, float z, float margin = 0);

struct ForestLayout {
  Vec2 center;
  float radius_x = 0;  // 0: no forest
  float radius_z = 0;
  bool operator==(const ForestLayout&) const = default;
};
ForestLayout make_forest_layout(uint64_t seed, const Path& path, const RuinsLayout& ruins);
bool in_forest(const ForestLayout& forest, float x, float z, float margin = 0);
float forest_distance(const ForestLayout& forest, float x, float z, float margin = 0);  // 1 at its edge
// The clearing in the heart of the forest, around its teleporter: no trees.
bool in_clearing(const ForestLayout& forest, float x, float z, float margin = 0);
float clearing_distance(const ForestLayout& forest, float x, float z, float margin = 0);  // 1 at its edge

// The cave: a rocky hill with steep sides; its mouth, at the foot of the
// cliff, faces the path.
struct CaveLayout {
  Vec2 center;          // of the hill; radius 0: no cave
  float radius = 0;     // the foot of the cliffs
  Vec2 mouth;           // the entrance, at the end of a ravine cut into the cliff
  Vec2 facing;          // unit vector from the hill towards the outside, through the mouth
  bool operator==(const CaveLayout&) const = default;
};
CaveLayout make_cave_layout(uint64_t seed, const Path& path);
bool in_cave_hill(const CaveLayout& cave, float x, float z, float margin = 0);

// The castle: square walls on a hill, the gate facing the path.
struct CastleLayout {
  Vec2 center;          // radius 0: no castle
  float radius = 0;
  Vec2 facing;          // unit vector from the center through the gate (±X)
  bool operator==(const CastleLayout&) const = default;
};
CastleLayout make_castle_layout(uint64_t seed, const Path& path);
bool in_castle(const CastleLayout& castle, float x, float z, float margin = 0);

// The west, between the valley and the sea (west.hpp): a road from the path
// to the lighthouse, a stream down to the sea, and the places along them.
// Derived from the seed (not saved); none in hand-made worlds.
// Boxes around every kChunk points (min x, min z, max x, max z), to skip
// the far ones when measuring distances.
struct Chunks {
  static constexpr size_t kChunk = 16;
  std::vector<std::array<float, 4>> boxes;
  bool operator==(const Chunks&) const = default;
};
struct Road {
  std::vector<Vec2> points;  // every ~1 m, from the path westwards
  Chunks chunks;
  bool operator==(const Road&) const = default;
};
struct Stream {
  std::vector<Vec2> points;  // every ~1 m, from its spring (north) down to the sea
  std::vector<float> level;  // its water's surface at each point (going down, never up)
  Chunks chunks;
  bool operator==(const Stream&) const = default;
};
struct WestLayout {
  Road road;
  Stream stream;
  Vec2 cart, cart_along;        // the merchant's cart by the road, the road's way there
  Vec2 bridge, bridge_along;    // where the road crosses the stream
  Vec2 farm, farm_front;        // the farm's middle; its front (towards the road, along X or Z)
  Vec2 mill;                    // the mill's knoll
  Vec2 pasture;                 // the shepherd's pen
  Vec2 burrow, burrow_front;    // the burrow's mound; where its hole looks
  Vec2 woods;                   // the woods' middle (an ellipse)
  float woods_rx = 0, woods_rz = 0;
  Vec2 cabin, camp, hollow_tree;  // in the woods
  Vec2 lighthouse, sea;         // the lighthouse on the coast; the way to the sea from it
  Vec2 menhirs;                 // the circle of standing stones (a teleporter in its middle)
  Vec2 crater;                  // where the meteorite fell
  bool valid() const { return !road.points.empty(); }
  bool operator==(const WestLayout&) const = default;
};

}  // namespace monstres
