#pragma once

#include <cstdint>
#include <vector>

namespace monstres {

namespace rules {
inline constexpr float kPathHalfWidth = 1.1f;  // the dirt path is 2.2 m wide
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
  std::vector<Vec2> points;
  bool operator==(const Path&) const = default;
};

Path make_path(uint64_t seed);

// Horizontal distance from (x, z) to the center line of the path.
float distance_to_path(const Path& path, float x, float z);

}  // namespace monstres
