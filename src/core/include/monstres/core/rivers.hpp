#pragma once

#include "monstres/core/math.hpp"
#include "monstres/core/terrain.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace monstres {

struct Continent;
struct Heightmap;
struct Stream;

// The continent's rivers. Made in the world's stages (continent.hpp), once
// its ground is known over the whole continent (its grid, and the relief's
// rim around the playable area: fit_rivers):
//  - their ways: from a spring up in the mountains down the ground to where
//    they go, the lowest way, meandering over the plains: the first down to
//    the playable area's stream (its spring: west.hpp), the second through
//    the capital and the Lac Miroir to the sea;
//  - their water's level, over the ground as it is: going down, never up,
//    by steps where the ground is steep (its waterfalls: a lip, a pool below
//    in a gorge); flat across the lake; the first ending at the stream's;
//  - their beds, dug into the ground where it is made (the tiles: dig_rivers).
struct River {
  std::vector<Vec2> points;     // every kRiverStep meters, from its spring to its end
  std::vector<float> width;     // its water's half width at each point
  std::vector<float> level;     // its water's surface at each point (fit_rivers; empty before)
  std::vector<uint32_t> falls;  // its waterfalls: the water drops from point i to point i + 1
  std::vector<std::array<float, 4>> boxes;  // every kRiverChunk points: their box (min x, min z, max x, max z)
  std::vector<std::array<float, 4>> groups;  // every kRiverChunk boxes: theirs (looked at first)
  bool operator==(const River&) const = default;
};

namespace rules {
inline constexpr float kRiverStep = 2.0f;
inline constexpr size_t kRiverChunk = 16;
inline constexpr float kRiverReach = 40.0f;  // the ground dug for a river at most this far from its middle
}  // namespace rules

// Where the first river meets the playable area's stream: the stream's spring
// (west.hpp), just beyond the relief's east edge.
Vec2 river_junction(uint64_t seed);

// Their ways (empty levels), down the ground of a world (its continent,
// raised or lowered by the relief's rim: Heightmap::land_height): from
// their springs, the lowest, meandering; the first never lower than
// `join_level` (the stream's, where it ends).
std::vector<River> route_rivers(const Heightmap& ground, float join_level);

// The world's rivers, their levels fitted over its ground (World::ground:
// made, its rim fitted). `stream`: the playable area's (west.hpp), where the
// first river ends: at its level there, going on a few meters along it (its
// water and the river's meeting over the relief's edge).
void fit_rivers(Heightmap& ground, const Stream* stream);

// The river nearest to (x, z), closer than `limit` (false: none).
struct RiverSpot {
  int river = -1;
  size_t segment = 0;
  float along = 0;      // 0..1 along the segment
  float distance = 0;   // from its middle line
  float width = 0;      // its water's half width there
  float level = 0;      // its water's level there (between the points)
  Vec2 direction{};     // downstream
};
bool nearest_river(const std::vector<River>& rivers, float x, float z, float limit, RiverSpot& spot);

// The ground `h` at (x, z) with the rivers' beds dug into it: their water's
// bed under it, the banks rising back to the ground; a low bank kept just
// above the water where the ground is lower (the water held in).
float dig_rivers(const std::vector<River>& rivers, float x, float z, float h);

// Whether (x, z) is in a river's water (with its level), given the ground there.
bool in_river(const std::vector<River>& rivers, float x, float z, float ground, float& level);

}  // namespace monstres
