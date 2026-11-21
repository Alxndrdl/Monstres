#pragma once

#include "monstres/core/math.hpp"
#include "monstres/core/terrain.hpp"
#include "monstres/core/village.hpp"

#include <cstdint>
#include <vector>

namespace monstres {

namespace rules {
// The relief spans x in [-640, 640], z in [-1280, 640]: further north, up to
// the foot of the mountains; the continent beyond (continent.hpp).
inline constexpr float kReliefHalfSize = 640.0f;  // west, east and south
inline constexpr float kReliefNorth = 1280.0f;    // north (z down to -1280)
// How far (x, z) is from the relief's edge, inside (negative outside).
inline float relief_inset(float x, float z) {
  const float ax = x < 0 ? -x : x;
  const float a = kReliefHalfSize - ax, b = kReliefHalfSize - z, c = z + kReliefNorth;
  return a < b ? (a < c ? a : c) : (b < c ? b : c);
}
// kReliefHalfSize less that: what max(|x|, |z|) was when the relief was a
// square (the edge at kReliefHalfSize all around).
inline float relief_edge(float x, float z) { return kReliefHalfSize - relief_inset(x, z); }
inline constexpr float kMaxWalkSlope = 1.0f;     // 45°: steeper ground cannot be walked up (jump instead)
inline constexpr float kSeaLevel = -2.0f;        // the ocean: walkers do not go below it
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
  static constexpr float kMinX = -rules::kReliefHalfSize, kMinZ = -rules::kReliefNorth;
  static constexpr int kCellsX = static_cast<int>(2 * rules::kReliefHalfSize / kCellSize);
  static constexpr int kCellsZ = static_cast<int>((rules::kReliefHalfSize + rules::kReliefNorth) / kCellSize);
  static constexpr int kPointsX = kCellsX + 1, kPointsZ = kCellsZ + 1;
  // kPointsX * kPointsZ, index(i, j) = i * kPointsZ + j for (x = kMinX + i * kCellSize, z = kMinZ + j * kCellSize)
  std::vector<float> heights;
  static constexpr size_t index(int i, int j) { return static_cast<size_t>(i * kPointsZ + j); }
  static constexpr float x_of(int i) { return kMinX + static_cast<float>(i) * kCellSize; }
  static constexpr float z_of(int j) { return kMinZ + static_cast<float>(j) * kCellSize; }
  // The grid point at or before x (z), within the grid.
  static int i_of(float x) { return clamp_index(static_cast<int>(floor_of((x - kMinX) / kCellSize)), kPointsX); }
  static int j_of(float z) { return clamp_index(static_cast<int>(floor_of((z - kMinZ) / kCellSize)), kPointsZ); }
  // The continent's own ground (no hills) on its 10 m grid: where the sea is.
  static constexpr float kLandCell = 10.0f;
  static constexpr int kLandPointsX = static_cast<int>(2 * rules::kReliefHalfSize / kLandCell) + 1;
  static constexpr int kLandPointsZ = static_cast<int>((rules::kReliefHalfSize + rules::kReliefNorth) / kLandCell) + 1;
  std::vector<float> land;  // index i * kLandPointsZ + j

  bool empty() const { return heights.empty(); }
  float at(int i, int j) const { return heights[index(i, j)]; }

  // Height of the ground at (x, z), anywhere (0 outside the relief).
  float height(float x, float z) const;

  // Unit normal of the ground around (x, z), smoothed over about one cell.
  Vec3 normal(float x, float z) const;

  // The continent's ground at (x, z), without the hills (0 in the valley).
  float land_height(float x, float z) const;
  // Whether (x, z) is in the sea: the continent's ground there is below it.
  // (A hollow of the hills going below the sea's level is not the sea.)
  bool sea(float x, float z) const { return !land.empty() && land_height(x, z) < rules::kSeaLevel; }

  bool operator==(const Heightmap&) const = default;

 private:
  static int clamp_index(int i, int n) { return i < 0 ? 0 : i > n - 1 ? n - 1 : i; }
  static float floor_of(float v) {
    const float t = static_cast<float>(static_cast<int>(v));
    return t > v ? t - 1 : t;
  }
};

// The places of the world, which shape the relief.
struct Places {
  const VillageLayout& village;
  const RuinsLayout& ruins;
  const CaveLayout& cave;
  const CastleLayout& castle;
  // The west's places (west.hpp): flat ground for the farm, knolls for the
  // mill and the lighthouse, the crater's bowl, the stream's bed. Its stream
  // gets its water's level here (going down along it, under the ground).
  WestLayout* west = nullptr;
};

// The terrace inland (west.hpp): how much higher the ground is there (0 in
// the valley), and how wide its edge is around (x, z): a few meters where it
// is a cliff, tens where it is a slope (and where the path goes up).
float terrace_rise(uint64_t seed, const Path& path, const WestLayout& west, const CaveLayout& cave, float x, float z);
float cliff_width(uint64_t seed, const Path& path, const WestLayout& west, const CaveLayout& cave, float x, float z);

// The ground beyond the valley before any place shapes it: the hills and the
// continent's (as make_heightmap draws it, near enough to place things).
float natural_height(uint64_t seed, float x, float z);

// Rolling hills, higher towards the edges of the world; flat around the
// village, a sunken bed under the lake, gentle along the path, a knoll
// with a flat top for the tower of the ruins, a higher one for the castle,
// and the rocky hill of the cave, with steep sides.
Heightmap make_heightmap(uint64_t seed, const Path& path, const Places& places);

}  // namespace monstres
