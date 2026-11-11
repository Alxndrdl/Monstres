#include "monstres/core/village.hpp"

#include "monstres/core/rng.hpp"

#include <cmath>
#include <limits>

namespace monstres {

namespace {

// x of the path where it crosses the line z (closest sample).
float path_x_at(const Path& path, float z) {
  float best_x = 0, best_d = std::numeric_limits<float>::max();
  for (const Vec2& p : path.points) {
    const float d = std::fabs(p.z - z);
    if (d < best_d) {
      best_d = d;
      best_x = p.x;
    }
  }
  return best_x;
}

}  // namespace

VillageLayout make_village_layout(uint64_t, const Path& path) {
  VillageLayout v;
  v.center = {path_x_at(path, rules::kVillageZ), rules::kVillageZ};
  // The lake is beside the village, on the side of the middle of the world.
  const float side = v.center.x > 0 ? -1.0f : 1.0f;
  v.lake.center = {v.center.x + side * 24.0f, v.center.z + 3.0f};
  v.lake.radius_x = rules::kLakeRadiusX;
  v.lake.radius_z = rules::kLakeRadiusZ;
  return v;
}

RuinsLayout make_ruins_layout(uint64_t seed, const Path& path) {
  RuinsLayout r;
  // Beside the path, on the side chosen by the seed.
  Rng rng(seed ^ 0x544F574552544F57ULL, 19);  // own stream
  const float side = rng.below(2) ? 1.0f : -1.0f;
  r.center = {path_x_at(path, rules::kRuinsZ) + side * rules::kRuinsFromPath, rules::kRuinsZ};
  r.radius = rules::kRuinsRadius;
  return r;
}

bool in_ruins(const RuinsLayout& r, float x, float z, float margin) {
  if (r.radius <= 0) return false;
  const float dx = x - r.center.x, dz = z - r.center.z;
  const float radius = r.radius + margin;
  return dx * dx + dz * dz < radius * radius;
}

bool in_lake(const Lake& lake, float x, float z, float margin) {
  if (lake.radius_x <= 0) return false;
  const float rx = lake.radius_x + margin, rz = lake.radius_z + margin;
  if (rx <= 0 || rz <= 0) return false;
  const float dx = (x - lake.center.x) / rx, dz = (z - lake.center.z) / rz;
  return dx * dx + dz * dz < 1.0f;
}

bool in_village(const VillageLayout& v, float x, float z, float margin) {
  const float dx = x - v.center.x, dz = z - v.center.z;
  const float r = rules::kVillageRadius + margin;
  return dx * dx + dz * dz < r * r;
}

}  // namespace monstres
