#pragma once

#include "monstres/core/terrain.hpp"

#include <cstdint>

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
inline constexpr float kForestX = 88.0f;          // the forest: on the other side of the path than the ruins
inline constexpr float kForestZ = -5.0f;
inline constexpr float kForestRadiusX = 45.0f;
inline constexpr float kForestRadiusZ = 58.0f;
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
struct ForestLayout {
  Vec2 center;
  float radius_x = 0;  // 0: no forest
  float radius_z = 0;
  bool operator==(const ForestLayout&) const = default;
};
ForestLayout make_forest_layout(uint64_t seed, const Path& path, const RuinsLayout& ruins);
bool in_forest(const ForestLayout& forest, float x, float z, float margin = 0);

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

}  // namespace monstres
