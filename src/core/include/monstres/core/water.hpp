#pragma once

#include "monstres/core/world.hpp"

#include <cstdint>

namespace monstres {

// The waters of the world, all alike: the sea, the lakes (the village's, the
// continent's lake and marsh: continent_ponds), the west's stream and the
// continent's rivers (rivers.hpp). One answer for all of them, wherever it
// is asked from: walking, diving, swimming, what grows, the monsters, the
// map, the water drawn (one surface: a river going on into the lake or the
// sea at its level, never a ribbon of it over them).
//
// Under water (the sea, the lakes, a river deep enough), the magician goes
// on in a pocket of air around it: it sinks, and rises while the jump is
// held (as a flying monster climbs), slower than on land; up at the surface,
// its head out. Only aquatic monsters (Bloup, Ploufi) come along: another
// one out goes back into the crystal as the magician goes in, and comes out
// again once it is back on dry land. Aquatic monsters live down there too,
// among rocks and seaweed; a battle under water is fought swimming (rising
// while the jump is held, sinking otherwise). In winter the lakes and rivers
// are frozen (walked on); never the sea.

namespace rules {
inline constexpr float kSwimFrom = 0.75f;      // water this high over the feet: swimming
inline constexpr float kSwimSpeed = 3.2f;      // m/s, across
inline constexpr float kSwimRise = 2.6f;       // m/s, up (the jump held)
inline constexpr float kSwimSink = 1.4f;       // m/s, down (let go)
inline constexpr float kSwimAccel = 9.0f;      // m/s²: towards those speeds
inline constexpr float kFloatDepth = 0.8f;     // up at the surface: the feet this far under it
inline constexpr float kSwimClimb = 1.3f;      // a swimmer gets out onto a bank this high over its feet
inline constexpr float kWadeSlow = 0.8f;       // wading (water over the ankles): this much slower
inline constexpr uint16_t kAshoreTicks = 40;   // back on dry land this long: the monster left behind comes out
}  // namespace rules

enum class WaterKind : uint8_t { None, Sea, Lake, River };

struct WaterSpot {
  WaterKind kind = WaterKind::None;
  float level = 0;   // its surface
  float ground = 0;  // the ground under it
  float depth() const { return level - ground; }
};

// Whether water covers (x, z) (outside; inside, none): its surface above the
// ground there by more than -margin (margin > 0: or nearly). Where two meet
// (a river into the lake, into the sea), the higher one.
bool water_at(const World& world, float x, float z, WaterSpot& spot, float margin = 0);
// The same, the ground there already known (`ground`: World::ground's height at (x, z)).
bool water_at_ground(const World& world, float x, float z, float ground, WaterSpot& spot, float margin = 0);
// Frozen: the lakes and rivers in winter (the sea never). Walked on.
bool frozen(const World& world, const WaterSpot& spot);
// The ground, or the ice over the water there (frozen).
float floor_height(const World& world, float x, float z);

// Bloup and Ploufi: they go under water (and the magician with them).
inline bool aquatic(Species species) { return species == Species::Fish || species == Species::Axolotl; }

// A body of that shape in the water (not frozen) over half its height, the
// water there deeper than its height: swimming. (`spot`: that water.)
bool submerged(const World& world, const Body& body, const BodyShape& shape, WaterSpot* spot = nullptr);
// The magician swimming (rules::kSwimFrom of water over its feet, or more).
bool swimming(const World& world);
// The magician all under water (its pocket of air around it).
bool diving(const World& world);
// Whether team[slot] may come out where the magician is (under water: only an aquatic one).
bool may_come_out(const World& world, size_t slot);

}  // namespace monstres
