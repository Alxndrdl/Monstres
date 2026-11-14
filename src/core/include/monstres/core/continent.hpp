#pragma once

#include "monstres/core/heightmap.hpp"
#include "monstres/core/math.hpp"
#include "monstres/core/terrain.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace monstres {

// The continent: a big island of about 4 km by 4 km around the playable
// area (World: [-140, 140] m, on its west side). Only its relief exists for
// now: the regions are empty and cannot be reached, but they are seen from
// afar, and drawn on the map. Derived from the seed, like the relief of the
// playable area (not saved, not part of World: building it takes a moment,
// and the rules do not need it yet).
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
  std::vector<float> heights;  // index = i * kPointsZ + j for (kMinX + i * kCell, kMinZ + j * kCell)

  bool empty() const { return heights.empty(); }
  float at(int i, int j) const { return heights[static_cast<size_t>(i * kPointsZ + j)]; }
  // Height at a grid point given by its coordinates (kDeep outside the grid).
  float at_point(float x, float z) const;
  // Height anywhere, between the grid points (kDeep outside the grid).
  float height(float x, float z) const;
};
Continent make_continent(uint64_t seed);

}  // namespace monstres
