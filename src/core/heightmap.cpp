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

// knoll_top: height of the flat top of the tower's knoll (see make_heightmap).
float relief(uint64_t seed, const Path& path, const VillageLayout& village, const RuinsLayout& ruins,
             float knoll_top, float x, float z) {
  // Three octaves of hills; the smallest bumps fade out along the path.
  const float near_path = 1 - smooth(1.5f, 7.0f, distance_to_path(path, x, z, 7.0f));
  float h = hills(seed, x, z) + (value_noise(seed + 42, x, z, 6.0f) - 0.5f) * 0.8f * (1 - near_path);

  if (ruins.radius > 0) {
    // A knoll with a flat top where the tower stands, gentle slopes around.
    const float rx = x - ruins.center.x, rz = z - ruins.center.z;
    const float d = std::sqrt(rx * rx + rz * rz);
    const float top = knoll_top;
    const float flat = 1 - smooth(11.0f, ruins.radius + 17, d);  // the enclosure (±9 m) stands level
    const float knoll = kKnollHeight * (1 - smooth(4.0f, ruins.radius + 17, d));
    h = h + knoll + (top - (h + knoll)) * flat;
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
    h *= std::min(smooth(rules::kVillageRadius + 1, rules::kVillageRadius + 12, to_village),
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
  const float fx = std::clamp(x + rules::kReliefHalfSize, 0.0f, static_cast<float>(kCells));
  const float fz = std::clamp(z + rules::kReliefHalfSize, 0.0f, static_cast<float>(kCells));
  const int i = std::min(static_cast<int>(fx), kCells - 1);
  const int j = std::min(static_cast<int>(fz), kCells - 1);
  const float tx = fx - static_cast<float>(i), tz = fz - static_cast<float>(j);
  const float h00 = at(i, j), h10 = at(i + 1, j), h01 = at(i, j + 1), h11 = at(i + 1, j + 1);
  if (tz >= tx) return h00 + (h11 - h01) * tx + (h01 - h00) * tz;  // triangle (x0,z0) (x0,z1) (x1,z1)
  return h00 + (h10 - h00) * tx + (h11 - h10) * tz;                 // triangle (x0,z0) (x1,z1) (x1,z0)
}

Vec3 Heightmap::normal(float x, float z) const {
  const float gx = (height(x + 0.5f, z) - height(x - 0.5f, z));
  const float gz = (height(x, z + 0.5f) - height(x, z - 0.5f));
  const float len = std::sqrt(gx * gx + 1 + gz * gz);
  return {-gx / len, 1 / len, -gz / len};
}

Heightmap make_heightmap(uint64_t seed, const Path& path, const VillageLayout& village, const RuinsLayout& ruins) {
  Heightmap m;
  m.heights.resize(static_cast<size_t>(Heightmap::kPoints * Heightmap::kPoints));
  const float knoll_top = hills(seed, ruins.center.x, ruins.center.z) + kKnollHeight;
  for (int i = 0; i < Heightmap::kPoints; ++i) {
    for (int j = 0; j < Heightmap::kPoints; ++j) {
      const float x = static_cast<float>(i) - rules::kReliefHalfSize;
      const float z = static_cast<float>(j) - rules::kReliefHalfSize;
      m.heights[static_cast<size_t>(i * Heightmap::kPoints + j)] = relief(seed, path, village, ruins, knoll_top, x, z);
    }
  }
  return m;
}

}  // namespace monstres
