#pragma once

#include <cstdint>
#include <vector>

namespace monstres {

namespace rules {
inline constexpr float kPathHalfWidth = 1.1f;  // the dirt path is 2.2 m wide
inline constexpr float kPathExtent = 80.0f;    // it runs from z = -84 to z = +84 (past the edges)
inline constexpr float kPathMaxX = 16.0f;      // and winds at most this far from x = 0
}

struct Vec2 {
  float x = 0, z = 0;
  bool operator==(const Vec2&) const = default;
};

// A winding dirt path crossing the world from south to north, passing near
// the spawn point. A polyline sampled every ~0.75 m, extending past the world
// edges. Built from the world seed with integer randomness and plain
// arithmetic only, so it is identical on every platform.
struct Path {
  std::vector<Vec2> points;  // by increasing z
  bool operator==(const Path&) const = default;
};

Path make_path(uint64_t seed);

// Horizontal distance from (x, z) to the center line of the path, or
// `limit` if it is farther than that (much faster when far from the path).
float distance_to_path(const Path& path, float x, float z, float limit = 1e15f);

}  // namespace monstres
