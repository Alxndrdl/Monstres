#pragma once

#include "monstres/core/world.hpp"

namespace monstres {

// Along the roads: side roads (lanes) leave the path, the west's road or
// another lane for the places people go to (RoadsLayout, derived from the
// seed, see make_roads): the castle's gate, the windmill, the shepherd's
// pen, the water mill, the circle of standing stones, the woodcutter's
// cabin, the mine, the refuge, the hot springs, Rochebrune's arena and
// breeding farm, the grotto; and the places along the path:
//
// - the inn (Aubin: a good meal heals the team), its tables outside;
// - the farmstead (Rosalie), its fields, its haystacks, its orchard;
// - the sawmill in the larches, its logs and stumps;
// - wayside shrines at some forks; signposts at every one; benches,
//   woodpiles, haystacks... by the path here and there.
//
// Each lane winds its cheapest way over the land (round the water, the
// cliffs, the places, up gentle slopes), then is smoothed.

namespace rules {
inline constexpr float kLaneHalfWidth = 0.95f;   // the lanes: narrower than the path
inline constexpr float kInnRadius = 12.0f;       // the inn's ground (no trees nor rocks)
inline constexpr float kFarmsteadRadius = 22.0f;
inline constexpr float kSawmillRadius = 14.0f;
}  // namespace rules

RoadsLayout make_roads(uint64_t seed, const Path& path, const VillageLayout& village, const RuinsLayout& ruins,
                       const ForestLayout& forest, const CaveLayout& cave, const CastleLayout& castle, const WestLayout& west,
                       const NorthLayout& north, SitesLayout& sites);
// (The sites are placed there too, where the lanes can reach: make_sites.)

// A lane's way, drawn again (the abbey's way up, found over the ground once shaped).
void set_lane(RoadsLayout& roads, LaneTo to, const std::vector<Vec2>& points);
// Distance to the nearest lane's middle (`limit` if farther).
float distance_to_lanes(const RoadsLayout& roads, float x, float z, float limit = 1e9f);
// Whether (x, z) is in one of the places along the roads (no trees nor rocks there).
bool in_roadside_place(const RoadsLayout& roads, float x, float z, float margin = 0);

// The valley's own trees and rocks (saved) on a lane, or in a place along
// the roads, go (before the west's are built: see generate_wilds).
void clear_lanes(World& world);
// The objects along the roads, appended to World::objects (after the
// north's, before the wilds'): signposts, shrines, the inn, the farmstead,
// the sawmill, the props by the path. Not saved.
void build_roads(World& world);

// The villagers along the roads (Aubin, Rosalie), each at its place, if not there yet.
void add_road_npcs(World& world);

}  // namespace monstres
