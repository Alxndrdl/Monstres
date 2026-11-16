#include "monstres/core/village.hpp"

#include "monstres/core/rng.hpp"

#include <bit>
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

constexpr float kTwoPi = 6.28318530718f;

uint64_t mix(uint64_t z) {
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}

}  // namespace

std::array<float, 4> outline_phases(Vec2 c, uint64_t salt) {
  uint64_t h = mix((static_cast<uint64_t>(std::bit_cast<uint32_t>(c.x)) << 32) ^ std::bit_cast<uint32_t>(c.z) ^
                   (salt * 0x9E3779B97F4A7C15ULL));
  std::array<float, 4> phases{};
  for (float& p : phases) {
    h = mix(h + 0x632BE59BD9B4E019ULL);
    p = kTwoPi * static_cast<float>(h >> 40) / static_cast<float>(1ULL << 24);
  }
  return phases;
}

float outline_scale(Vec2 c, uint64_t salt, float angle) {
  const std::array<float, 4> phases = outline_phases(c, salt);
  float in = 0;
  for (size_t k = 0; k < phases.size(); ++k) in += kOutlineDepths[k] * 0.5f * (1 + std::sin(kOutlineWaves[k] * angle + phases[k]));
  return 1 - in;
}

float outline_distance(Vec2 c, float rx, float rz, uint64_t salt, float x, float z) {
  if (rx <= 0 || rz <= 0) return std::numeric_limits<float>::max();
  const float dx = (x - c.x) / rx, dz = (z - c.z) / rz;
  const float r = std::sqrt(dx * dx + dz * dz);
  if (r < 1e-6f) return 0;
  return r / outline_scale(c, salt, std::atan2(dz, dx));
}

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

ForestLayout make_forest_layout(uint64_t, const Path& path, const RuinsLayout& ruins) {
  ForestLayout f;
  // On the other side of the path than the ruins (whose side is from the seed).
  const float side = ruins.radius > 0 && ruins.center.x > path_x_at(path, ruins.center.z) ? -1.0f : 1.0f;
  f.center = {side * rules::kForestX, rules::kForestZ};
  f.radius_x = rules::kForestRadiusX;
  f.radius_z = rules::kForestRadiusZ;
  return f;
}

float forest_distance(const ForestLayout& f, float x, float z, float margin) {
  if (f.radius_x <= 0) return std::numeric_limits<float>::max();
  return outline_distance(f.center, f.radius_x + margin, f.radius_z + margin, kForestOutline, x, z);
}

bool in_forest(const ForestLayout& f, float x, float z, float margin) { return forest_distance(f, x, z, margin) < 1.0f; }

float clearing_distance(const ForestLayout& f, float x, float z, float margin) {
  if (f.radius_x <= 0) return std::numeric_limits<float>::max();
  const float r = rules::kClearingRadius + margin;
  return outline_distance(f.center, r, r, kClearingOutline, x, z);
}

bool in_clearing(const ForestLayout& f, float x, float z, float margin) {
  return clearing_distance(f, x, z, margin) < 1.0f;
}

CaveLayout make_cave_layout(uint64_t seed, const Path& path) {
  CaveLayout c;
  Rng rng(seed ^ 0x4341564543415645ULL, 23);  // own stream
  const float side = rng.below(2) ? 1.0f : -1.0f;
  c.center = {path_x_at(path, rules::kCaveZ) + side * rules::kCaveX, rules::kCaveZ};
  c.radius = rules::kCaveHillRadius;
  c.facing = {-side, 0};  // towards the path
  c.mouth = {c.center.x + c.facing.x * (c.radius - 6.0f), c.center.z};  // a ravine leads to it
  return c;
}

bool in_cave_hill(const CaveLayout& c, float x, float z, float margin) {
  if (c.radius <= 0) return false;
  const float dx = x - c.center.x, dz = z - c.center.z;
  const float r = c.radius + margin;
  return dx * dx + dz * dz < r * r;
}

CastleLayout make_castle_layout(uint64_t seed, const Path& path) {
  CastleLayout c;
  Rng rng(seed ^ 0x434153544C452121ULL, 29);  // own stream
  const float side = rng.below(2) ? 1.0f : -1.0f;
  c.center = {path_x_at(path, rules::kCastleZ) + side * rules::kCastleX, rules::kCastleZ};
  c.radius = rules::kCastleRadius;
  c.facing = {-side, 0};  // the gate towards the path
  return c;
}

bool in_castle(const CastleLayout& c, float x, float z, float margin) {
  if (c.radius <= 0) return false;
  const float dx = x - c.center.x, dz = z - c.center.z;
  const float r = c.radius + margin;
  return dx * dx + dz * dz < r * r;
}

bool in_ruins(const RuinsLayout& r, float x, float z, float margin) {
  if (r.radius <= 0) return false;
  const float dx = x - r.center.x, dz = z - r.center.z;
  const float radius = r.radius + margin;
  return dx * dx + dz * dz < radius * radius;
}

float lake_distance(const Lake& lake, float x, float z, float margin) {
  if (lake.radius_x <= 0) return std::numeric_limits<float>::max();
  return outline_distance(lake.center, lake.radius_x + margin, lake.radius_z + margin, kLakeOutline, x, z);
}

bool in_lake(const Lake& lake, float x, float z, float margin) { return lake_distance(lake, x, z, margin) < 1.0f; }

bool in_village(const VillageLayout& v, float x, float z, float margin) {
  const float dx = x - v.center.x, dz = z - v.center.z;
  const float r = rules::kVillageRadius + margin;
  return dx * dx + dz * dz < r * r;
}

}  // namespace monstres
