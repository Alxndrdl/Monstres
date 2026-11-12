#pragma once

#include "monstres/core/math.hpp"
#include "monstres/core/terrain.hpp"
#include "monstres/core/village.hpp"

#include <cstdint>
#include <vector>

namespace monstres {

namespace rules {
inline constexpr float kReliefHalfSize = 170.0f;  // the relief spans [-170, 170]; flat (y = 0) beyond
inline constexpr float kMaxWalkSlope = 1.0f;     // 45°: steeper ground cannot be walked up (jump instead)
}  // namespace rules

// Smooth pseudo-random field in [0, 1] (value noise), stable for a seed.
// Plain arithmetic only: identical on every platform.
float value_noise(uint64_t seed, float x, float z, float cell_size);

// The relief of the ground: heights on a 2 m grid. Between grid points the
// ground is made of two triangles per cell, split along the diagonal from
// (x0, z0) to (x1, z1): exactly the triangles the renderer draws, so what
// bodies walk on is what the player sees. Derived from the seed (not saved).
//
// An empty heightmap is flat ground at y = 0 (hand-made test worlds).
struct Heightmap {
  static constexpr float kCellSize = 2.0f;  // meters
  static constexpr int kCells = static_cast<int>(2 * rules::kReliefHalfSize / kCellSize);  // per side
  static constexpr int kPoints = kCells + 1;
  // kPoints * kPoints, index = i * kPoints + j for (x = i * kCellSize - 170, z = j * kCellSize - 170)
  std::vector<float> heights;

  bool empty() const { return heights.empty(); }
  float at(int i, int j) const { return heights[static_cast<size_t>(i * kPoints + j)]; }

  // Height of the ground at (x, z), anywhere (0 outside the relief).
  float height(float x, float z) const;

  // Unit normal of the ground around (x, z), smoothed over about one cell.
  Vec3 normal(float x, float z) const;

  bool operator==(const Heightmap&) const = default;
};

// The places of the world, which shape the relief.
struct Places {
  const VillageLayout& village;
  const RuinsLayout& ruins;
  const CaveLayout& cave;
  const CastleLayout& castle;
};

// Rolling hills, higher towards the edges of the world; flat around the
// village, a sunken bed under the lake, gentle along the path, a knoll
// with a flat top for the tower of the ruins, a higher one for the castle,
// and the rocky hill of the cave, with steep sides.
Heightmap make_heightmap(uint64_t seed, const Path& path, const Places& places);

}  // namespace monstres
