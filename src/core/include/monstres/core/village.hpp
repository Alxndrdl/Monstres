#pragma once

#include "monstres/core/terrain.hpp"

#include <cstdint>

namespace monstres {

// The village and its lake. Their place depends on the world seed (and the
// path, which crosses the village); like the path, they are derived data,
// rebuilt from the seed, not saved.

namespace rules {
inline constexpr float kVillageZ = -28.0f;        // the village sits on the path, north of the start
inline constexpr float kVillageRadius = 14.0f;    // no trees or rocks inside
inline constexpr float kLakeRadiusX = 9.0f;
inline constexpr float kLakeRadiusZ = 6.5f;
inline constexpr float kWaterLevel = -0.12f;      // the lake surface, just below the ground
inline constexpr float kSwimLevel = -0.3f;        // where fish bodies float
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

// Whether (x, z) is in the lake, with its shore moved out by `margin` meters
// (negative: only well inside the water).
bool in_lake(const Lake& lake, float x, float z, float margin = 0);

bool in_village(const VillageLayout& village, float x, float z, float margin = 0);

}  // namespace monstres
