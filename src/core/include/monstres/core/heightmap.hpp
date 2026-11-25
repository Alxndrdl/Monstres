#pragma once

#include "monstres/core/math.hpp"
#include "monstres/core/rivers.hpp"
#include "monstres/core/terrain.hpp"
#include "monstres/core/village.hpp"

#include <cstdint>
#include <memory>
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

// Inside (interior.hpp), a floor going up and down (the mine's galleries
// going down): pieces of floor, capsules from a to b (a disc where a == b),
// the floor's height at a and at b (in between, along the line). Where they
// meet, their heights blend (each weighs more the further inside it is).
struct FloorPiece {
  Vec2 a, b;
  float radius = 0;
  float ya = 0, yb = 0;
  bool operator==(const FloorPiece&) const = default;
};
float floor_height(const std::vector<FloorPiece>& floor, float x, float z);

// The continent's ground beyond the relief, before it blends into the
// relief's edge: over a given ground `base` (the continent's, raised by the
// rim: Heightmap::rise), its detail, rougher up high, by patches, gentle
// under the sea; as seen with points
// `spacing` apart (the far terrain's rings: the folds too fine for them
// left out; 0: all of them, as the tiles have it).
float outer_ground(uint64_t seed, float x, float z, float base, float spacing);

// The ground beyond the relief's rectangle (the rest of the continent), made
// in tiles around where it is looked at, kept a while, then forgotten. A tile
// depends only on the seed, the positions in it and the relief's edge: the
// same whatever was asked before, tiles meeting exactly. Not part of the
// state (not compared, not copied: a copy starts with none).
struct GroundTile;
class TileCache {
 public:
  TileCache();
  TileCache(const TileCache&);
  TileCache& operator=(const TileCache&);
  TileCache(TileCache&&) noexcept;
  TileCache& operator=(TileCache&&) noexcept;
  ~TileCache();
  bool operator==(const TileCache&) const { return true; }
  struct Store;
  Store& store() const;
  size_t size() const;  // tiles made and kept now

 private:
  mutable std::unique_ptr<Store> tiles_;
};

namespace rules {
inline constexpr int kTileCells = 64;            // a tile of the ground beyond the relief: 64 cells of 2 m (128 m)
inline constexpr size_t kTilesKept = 320;        // kept at most (about 5 MB), the longest unused forgotten
inline constexpr float kOuterBlend = 80.0f;      // the continent's own detail comes in over this much past the relief's edge
}  // namespace rules

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
  // Inside (no heights): the floor's slopes, if any (empty: flat at y = 0).
  std::vector<FloorPiece> floor;
  // The buildings of the wilds seated on it (landfix.hpp: seat_buildings, once).
  bool seated = false;
  uint64_t seed = 0;     // (the ground beyond the relief: its tiles)
  TileCache tiles;
  // Along the relief's edge, every kRimStep meters round it (from its
  // north-west corner, eastwards; fit_rim): how much higher the relief's
  // edge is than the continent there (smoothed along it), and how thick the
  // world's nature is just inside it (things per square of 5 m; empty: the
  // continent's own). Beyond the relief, the continent's ground is raised (or
  // lowered) by the first, fading out over a few hundred meters (rise): the
  // relief's shapes (the north's valley up to the col, the terrace) go on
  // into the continent instead of stopping at a straight line; the map, the
  // sea and the far terrain have it too.
  static constexpr float kRimStep = 10.0f;
  std::vector<float> rim, rim_density;
  // The continent's rivers, their levels fitted over this ground (rivers.hpp:
  // fit_rivers): their beds dug into the tiles.
  std::vector<River> rivers;
  // The playable north's valley floor where it starts (north.hpp: its snow
  // lies above it; none: no north), for the snow beyond the relief.
  float north_floor = -1e9f;
  // How much snow lies, all year long, on ground at height h at (x, z)
  // beyond the relief (0..1, before steep faces shed it): the playable
  // north's snowy slopes going on (as north_snow), fading out into the
  // continent's own, above its snow line (continent.hpp: snow_line).
  float snow(float x, float z, float h) const;
  float rise(float x, float z) const;
  // How thick the world's nature is near (x, z) beyond the relief, as it
  // fades out (-1: no rim, the continent's own).
  float edge_density(float x, float z) const;

  bool empty() const { return heights.empty(); }
  float at(int i, int j) const { return heights[index(i, j)]; }

  // Height of the ground at (x, z), anywhere: on the relief's grid inside
  // its rectangle, on the tiles of the continent beyond (made when asked).
  float height(float x, float z) const;
  // Beyond the relief: the height at a grid point of the same 2 m lattice
  // (i, j counted from kMinX, kMinZ, any sign), from its tile.
  float outer_at(int i, int j) const;
  // The same, worked out (no tile made or kept: what the tiles hold there;
  // the map's picture reads it).
  float lattice_point(int i, int j) const;

  // Unit normal of the ground around (x, z), smoothed over about one cell.
  Vec3 normal(float x, float z) const;

  // The continent's ground at (x, z), without the hills (0 in the valley):
  // on its 10 m grid (the relief's, then the continent's beyond).
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

// Sets Heightmap::rim from the relief as it is (after all its shaping), and
// forgets the tiles made before.
void fit_rim(Heightmap& ground);

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
  const RoadsLayout* roads = nullptr;  // the side roads (roads.hpp): gentle along them, as along the path
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
