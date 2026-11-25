#pragma once

#include "monstres/core/heightmap.hpp"
#include "monstres/core/math.hpp"
#include "monstres/core/rivers.hpp"
#include "monstres/core/terrain.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace monstres {

// The continent: a big island of about 5 km by 4 km, the playable area's
// relief (heightmap.hpp) on its west side. Derived from the seed, made in
// stages, each from the one before:
//  1. its shapes, coarse: the island's outline (lobed, ragged), its regions
//     (whose edges wind), its places (the volcanoes, the forest, the lake,
//     the marsh, the capital): continent_height, region_at, *_share;
//  2. over the whole of it, its ground on a 10 m grid (make_continent); then,
//     joined to the playable area's relief (its rim: heightmap.hpp), its main
//     features seen from above: the rivers' ways down from the mountains,
//     their waterfalls (rivers.hpp: fit_rivers);
//  3. the ground where the player is, in detail (2 m, heightmap.hpp's tiles:
//     from the grid's points around, smooth between them, its folds, the
//     rivers' beds dug, their waterfalls), and what grows there (outer.hpp);
//  4. the map, drawn from that same ground (continent_map.cpp).
// Not saved: made again from the seed (once per seed: continent_of).
//
// Coordinates as in the game: X towards the east, Z towards the south. The
// starting valley (the playable area) is on the west; the snowy mountains
// in the north; a volcanic peninsula with an active volcano in the east; a
// desert in the south; in the middle the plains, with a forest, a marsh, a
// lake, a lone mountain (an extinct volcano) and the capital.

namespace rules {
inline constexpr float kStartHalfSize = 170.0f; // the starting valley keeps its own relief: the continent rises beyond
inline constexpr float kSnowLine = 230.0f;      // snow above (drawing)
}  // namespace rules

// Named parts of the continent (the map, the colors of the ground).
enum class Region : uint8_t { Ocean, Start, Mountains, Volcano, Desert, Plains, Forest, Marsh, Lake, LoneMountain, Capital, Count };

// A remarkable place: where it is, how big (for the map's labels).
struct Landmark {
  Region region;
  Vec2 at;
  float radius;
};
const std::array<Landmark, 10>& continent_landmarks();

// A body of water on the continent (not the ocean): its surface is flat at `level`.
struct Pond {
  Vec2 center;
  float radius_x, radius_z;
  float level;
};
const std::array<Pond, 2>& continent_ponds();  // the lake, the marsh
// A pond's shore (wavy: never an ellipse): its distance from the middle at
// `angle` (atan2 of the offset in its radii), as a share of its radii.
float pond_outline(const Pond& pond, float angle);
// How far inside a pond (x, z) is: 1 in its middle, 0 on its shore, negative outside.
float pond_shape(const Pond& pond, float x, float z);
// Likewise for the continent's places with an outline: the forest, the
// marsh (its tussocks around the water), the capital (0 for the others).
float place_shape(Region region, float x, float z);

// Where the active volcano's crater is, how wide, and the height of its lava.
struct Crater {
  Vec2 center;
  float radius;
  float lava_level;
};
Crater volcano_crater();

// Height of the continent's ground at (x, z), computed (slow: the grid
// below is what is used). Inside the playable area's square it is 0: the
// relief there is the World's Heightmap, which also comes down to 0 at its
// edges, so both meet without a step.
float continent_height(uint64_t seed, float x, float z);

// The snow's lower edge on the continent at (x, z): wavering, lower and
// lower northwards, down to the foot of the Monts Givrés (where the
// playable north has its snowy slopes: north.hpp, north_snow).
float snow_line(uint64_t seed, float x, float z);

// How much (x, z) is in the mountains (northwards), the desert (southwards),
// the volcano's basalt land (eastwards), 0..1: their edges wind, back and
// forth over hundreds of meters (as made for the playable area by it).
float mountains_share(float x, float z);
float desert_share(float x, float z);
float volcano_share(float x, float z);

// How far inside the island (x, z) is (its first stage's outline): above 0
// on the island, 1 in its middle, below 0 out in the open sea.
float island_inland(uint64_t seed, float x, float z);

// Which region (x, z) belongs to (the strongest one where they meet).
Region region_at(float x, float z);

// The continent's heights on a 10 m grid, lined up with the playable
// area's square (a grid line at x = ±170 and z = ±170).
struct Continent {
  static constexpr float kCell = 10.0f;
  static constexpr float kMinX = -1170.0f, kMinZ = -2470.0f;
  static constexpr int kPointsX = 551, kPointsZ = 501;  // up to x = 4330, z = 2530
  static constexpr float kMaxX = kMinX + kCell * (kPointsX - 1), kMaxZ = kMinZ + kCell * (kPointsZ - 1);
  static constexpr float kDeep = -60.0f;  // the ocean floor beyond the grid
  uint64_t seed = 0;
  std::vector<float> heights;
  std::vector<River> rivers;  // their ways (levels: per world, Heightmap::rivers)  // index = i * kPointsZ + j for (kMinX + i * kCell, kMinZ + j * kCell)

  bool empty() const { return heights.empty(); }
  float at(int i, int j) const { return heights[static_cast<size_t>(i * kPointsZ + j)]; }
  // Height at a grid point given by its coordinates (kDeep outside the grid).
  float at_point(float x, float z) const;
  // Height anywhere, between the grid points (kDeep outside the grid).
  float height(float x, float z) const;
};
Continent make_continent(uint64_t seed);
// The continent of a seed, made once and kept (the last few seeds asked).
std::shared_ptr<const Continent> continent_of(uint64_t seed);

}  // namespace monstres
