#include "monstres/core/heightmap.hpp"

#include "monstres/core/world.hpp"

#include <algorithm>
#include <cmath>

namespace monstres {

namespace {

uint64_t mix64(uint64_t z) {
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}

float lattice(uint64_t seed, int64_t x, int64_t z) {
  const uint64_t h = mix64(seed * 0x9E3779B97F4A7C15ULL + static_cast<uint64_t>(x) * 0xC2B2AE3D27D4EB4FULL +
                           static_cast<uint64_t>(z) * 0x165667B19E3779F9ULL);
  return static_cast<float>(h >> 40) / static_cast<float>(1ULL << 24);
}

float smooth(float e0, float e1, float x) {
  const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
  return t * t * (3 - 2 * t);
}

constexpr float kLakeDepth = 1.4f;

// The big hills only (no small bumps).
float hills(uint64_t seed, float x, float z) {
  return (value_noise(seed + 40, x, z, 34.0f) - 0.5f) * 8.0f + (value_noise(seed + 41, x, z, 15.0f) - 0.5f) * 3.0f;
}

constexpr float kKnollHeight = 3.0f;  // the tower's knoll, above the hills around it

constexpr float kCastleKnollHeight = 4.5f;

// A knoll with a flat top of radius `flat_radius` at height `top`, gentle slopes out to `outer`.
float add_knoll(float h, Vec2 center, float x, float z, float top, float height, float flat_radius, float outer) {
  const float rx = x - center.x, rz = z - center.z;
  const float d = std::sqrt(rx * rx + rz * rz);
  const float flat = 1 - smooth(flat_radius, outer, d);
  const float knoll = height * (1 - smooth(4.0f, outer, d));
  return h + knoll + (top - (h + knoll)) * flat;
}

// knoll_top, castle_top: heights of the flat tops of the knolls (see make_heightmap).
float relief(uint64_t seed, const Path& path, const Places& places, float knoll_top, float castle_top, float x,
             float z) {
  const VillageLayout& village = places.village;
  const RuinsLayout& ruins = places.ruins;
  // Three octaves of hills; the smallest bumps fade out along the path.
  const float near_path = 1 - smooth(1.5f, 7.0f, distance_to_path(path, x, z, 7.0f));
  float h = hills(seed, x, z) + (value_noise(seed + 42, x, z, 6.0f) - 0.5f) * 0.8f * (1 - near_path);

  if (ruins.radius > 0) {
    // A knoll with a flat top where the tower stands (the enclosure, ±9 m, stands level).
    h = add_knoll(h, ruins.center, x, z, knoll_top, kKnollHeight, 11.0f, ruins.radius + 17);
  }
  if (places.castle.radius > 0) {
    // A higher one for the castle: its walls (±14 m) stand level.
    h = add_knoll(h, places.castle.center, x, z, castle_top, kCastleKnollHeight, rules::kCastleHalf + 5.0f,
                  places.castle.radius + 32);
  }
  if (const CaveLayout& cave = places.cave; cave.radius > 0) {
    // The rocky hill: cliffs all around, a lumpy top; in front of the mouth,
    // a passage stays at the level of the ground outside.
    const float cx = x - cave.center.x, cz = z - cave.center.z;
    const float d = std::sqrt(cx * cx + cz * cz);
    const float rise = (rules::kCaveHillHeight + 2.5f * value_noise(seed + 43, x, z, 7.0f)) *
                       (1 - smooth(cave.radius - 8.0f, cave.radius, d));
    const float mx = x - cave.mouth.x, mz = z - cave.mouth.z;
    const float along = mx * cave.facing.x + mz * cave.facing.z;  // outwards
    const float across = std::fabs(-mx * cave.facing.z + mz * cave.facing.x);
    const float passage = (1 - smooth(2.6f, 3.6f, across)) * (1 - smooth(5.5f, 6.5f, -along));
    h += rise * (1 - passage);
  }

  // Hills rise around the playable area, then the relief fades out far
  // beyond it, where the flat horizon ground takes over.
  const float edge = std::max(std::fabs(x), std::fabs(z));
  h += smooth(rules::kWorldHalfSize - 9, rules::kWorldHalfSize + 11, edge) * 5.0f;
  h *= 1 - smooth(rules::kReliefHalfSize - 10, rules::kReliefHalfSize - 1, edge);

  if (village.lake.radius_x > 0) {
    // Flat village and shore, at y = 0.
    const float vx = x - village.center.x, vz = z - village.center.z;
    const float to_village = std::sqrt(vx * vx + vz * vz);
    const Lake& lake = village.lake;
    const float lx = (x - lake.center.x) / lake.radius_x, lz = (z - lake.center.z) / lake.radius_z;
    const float lake_depth = 1 - std::sqrt(lx * lx + lz * lz);  // 0 at the shore, 1 in the middle
    h *= std::min(smooth(rules::kVillageRadius + 3, rules::kVillageRadius + 15, to_village),
                  smooth(0.1f, 1.3f, -lake_depth));
    if (lake_depth > 0) h = -kLakeDepth * std::min(1.0f, lake_depth * 2.5f);
  }
  return h;
}

}  // namespace

float value_noise(uint64_t seed, float x, float z, float cell) {
  const float fx = x / cell, fz = z / cell;
  const float x0 = std::floor(fx), z0 = std::floor(fz);
  const float tx = fx - x0, tz = fz - z0;
  const float sx = tx * tx * (3 - 2 * tx), sz = tz * tz * (3 - 2 * tz);
  const auto ix = static_cast<int64_t>(x0), iz = static_cast<int64_t>(z0);
  const float a = lattice(seed, ix, iz), b = lattice(seed, ix + 1, iz);
  const float c = lattice(seed, ix, iz + 1), d = lattice(seed, ix + 1, iz + 1);
  return (a + (b - a) * sx) + ((c + (d - c) * sx) - (a + (b - a) * sx)) * sz;
}

float Heightmap::height(float x, float z) const {
  if (heights.empty()) return 0;
  const float fx = std::clamp((x + rules::kReliefHalfSize) / kCellSize, 0.0f, static_cast<float>(kCells));
  const float fz = std::clamp((z + rules::kReliefHalfSize) / kCellSize, 0.0f, static_cast<float>(kCells));
  const int i = std::min(static_cast<int>(fx), kCells - 1);
  const int j = std::min(static_cast<int>(fz), kCells - 1);
  const float tx = fx - static_cast<float>(i), tz = fz - static_cast<float>(j);
  const float h00 = at(i, j), h10 = at(i + 1, j), h01 = at(i, j + 1), h11 = at(i + 1, j + 1);
  if (tz >= tx) return h00 + (h11 - h01) * tx + (h01 - h00) * tz;  // triangle (x0,z0) (x0,z1) (x1,z1)
  return h00 + (h10 - h00) * tx + (h11 - h10) * tz;                 // triangle (x0,z0) (x1,z1) (x1,z0)
}

Vec3 Heightmap::normal(float x, float z) const {
  constexpr float k = kCellSize / 2;
  const float gx = (height(x + k, z) - height(x - k, z)) / kCellSize;
  const float gz = (height(x, z + k) - height(x, z - k)) / kCellSize;
  const float len = std::sqrt(gx * gx + 1 + gz * gz);
  return {-gx / len, 1 / len, -gz / len};
}

Heightmap make_heightmap(uint64_t seed, const Path& path, const Places& places) {
  Heightmap m;
  m.heights.resize(static_cast<size_t>(Heightmap::kPoints * Heightmap::kPoints));
  const float knoll_top = hills(seed, places.ruins.center.x, places.ruins.center.z) + kKnollHeight;
  const float castle_top = hills(seed, places.castle.center.x, places.castle.center.z) + kCastleKnollHeight;
  for (int i = 0; i < Heightmap::kPoints; ++i) {
    for (int j = 0; j < Heightmap::kPoints; ++j) {
      const float x = static_cast<float>(i) * Heightmap::kCellSize - rules::kReliefHalfSize;
      const float z = static_cast<float>(j) * Heightmap::kCellSize - rules::kReliefHalfSize;
      m.heights[static_cast<size_t>(i * Heightmap::kPoints + j)] =
          relief(seed, path, places, knoll_top, castle_top, x, z);
    }
  }
  return m;
}

}  // namespace monstres
