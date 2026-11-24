#pragma once

#include "monstres/core/world.hpp"

namespace monstres {

// The land's last touches, so that it makes sense:
//
// - the water left in hollows inland (where the continent dips below the sea
//   inside the land: a lagoon with no way out) is joined to the open sea by
//   a winding channel dug through the lowest ground, round the ways and the
//   places; when none can go (a way, a place, a hill in between, too far),
//   the hollow is filled up into a low meadow (join_waters, build_layouts);
// - the buildings of the wilds (the inn, the farms, the cabins, the barns,
//   the mine's entrance, the grotto...) stand on flat ground, their doors at
//   the level of the ground before them: the ground under each and before its
//   door is levelled, blending back into the slope around with an uneven
//   edge (seat_buildings, generate_wilds, before the wilds' trees grow).

namespace rules {
inline constexpr float kChannelLongest = 320.0f;  // a channel to the sea: this long at most...
inline constexpr float kChannelDeepest = 6.0f;    // ...and dug through ground at most this high above the sea
inline constexpr float kPadMargin = 1.0f;          // a building's flat ground: past its walls
inline constexpr float kPadApron = 3.5f;           // and before its door
inline constexpr float kPadBlend = 6.0f;           // blending back into the ground over this much at least
inline constexpr float kPadReach = 22.0f;          // and at most
}  // namespace rules

// What join_waters did (for the tests): channels dug, hollows filled.
struct WaterFix {
  int channels = 0;
  int filled = 0;
};
WaterFix join_waters(World& world);

// Whether the kind of a building whose ground is levelled (seat_buildings).
bool seated_kind(ObjectKind kind);
void seat_buildings(World& world, size_t first);

}  // namespace monstres
