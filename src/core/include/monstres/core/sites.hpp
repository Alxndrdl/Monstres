#pragma once

#include "monstres/core/world.hpp"

#include <functional>
#include <utility>

namespace monstres {

// The sites of the wilds (SitesLayout, derived from the seed and the other
// layouts, see make_sites; not saved). Lanes lead to most of them (roads.cpp):
//
// - the fishing port, on the coast south of the lighthouse: a quay at the
//   water's edge, wooden piers out over the sea (walked onto), boats
//   moored along them, fishermen's houses, racks of fish and nets drying,
//   a fish market, a teleporter; Loïc the old fisherman, Maëlle the fishmonger;
// - the abbey, up in the mountains east of the col: walls round a great
//   court, its gatehouse, the church (the temple: inside, its monks fight
//   the magician one after the other, then the abbot: World::trials), the
//   bell tower, the dorter and the refectory, a cloister's garden, the
//   kitchen garden, beehives, graves; a way winding up the slope to its gate
//   (cut into the mountain); a teleporter; Gauthier the gatekeeper heals;
// - the camp, in the plains near the sea: tents round a fire, a hut, a
//   palisade, racks of hides, a lookout's teleporter; Ysolde the ranger
//   heals, Tristan, a trainer, fights;
// - the old watchtower on the cliffs over the sea (the ruins' tower: climbed
//   up to its broken top), Armand the watchman;
// - the quarry: a pit dug in steps, blocks of cut stone, a crane, carts on
//   their rails, a hut; Raoul the quarryman;
// - the chapel and its graves, within a low wall; Sylvain the hermit;
// - the apiary: hives in rows among fruit trees, a hut; Héloïse the beekeeper;
// - the dolmen by its mound, the wreck on the beach, the old aqueduct's arches.

namespace rules {
inline constexpr float kPortHalf = 30.0f;         // the port's quay: along the shore, on each side of its middle
inline constexpr float kPortDepth = 34.0f;        // and inland from the shore
inline constexpr float kPortLevel = kSeaLevel + 1.1f;  // the quay, above the sea
inline constexpr float kPierHalf = 1.2f;          // a pier's deck: half its width
inline constexpr float kPierDeck = 0.25f;         // its top above the quay
inline constexpr float kAbbeyHalfX = 46.0f;       // the abbey's walls: half sizes
inline constexpr float kAbbeyHalfZ = 34.0f;
inline constexpr float kTempleHalfX = 12.0f, kTempleHalfZ = 9.5f;  // its church (matching the model)
inline constexpr float kAbbeyWayHalf = 2.2f;      // the way up to its gate: its flat middle (half)
inline constexpr float kSeaCampRadius = 17.0f;    // the camp's ground
inline constexpr float kLookoutRadius = 9.0f;
inline constexpr float kQuarryRadius = 15.0f;     // its pit's floor
inline constexpr float kQuarryDepth = 5.5f;       // below the ground around
inline constexpr float kQuarryRim = 9.0f;         // its stepped sides, out from the floor
inline constexpr float kChapelRadius = 15.0f;
inline constexpr float kApiaryRadius = 13.0f;
inline constexpr float kDolmenRadius = 11.0f;
inline constexpr float kWreckRadius = 9.0f;
inline constexpr float kAqueductArch = 8.0f;      // each arch, along it
inline constexpr int kAqueductArches = 13;
// The abbey's trials: its monks' levels, the abbot's, and what beating him the first time gives.
inline constexpr uint8_t kMonkLevels[3] = {15, 17, 19};
inline constexpr uint8_t kAbbotLevel = 23;
inline constexpr uint32_t kAbbotCoins = 250, kAbbotPotions = 3;
inline constexpr uint8_t kTrialsAll = 0x0F;      // World::trials: the three monks and the abbot
}  // namespace rules

// What make_roads knows of the land when it places the sites: where a lane
// can reach, the ground's height (near enough), the side of the terrace's
// edge (scarp_side), on its grid; the places along the roads (middles, radii).
struct SiteHints {
  std::function<bool(float, float)> reachable;
  std::function<float(float, float)> ground;
  std::function<float(float, float)> edge;
  std::vector<std::pair<Vec2, float>> keep_off;
};

// Where the sites are, from the seed and the other layouts; empty when the
// world has no relief (or no west and north). make_roads calls it.
SitesLayout make_sites(uint64_t seed, const Path& path, const VillageLayout& village, const RuinsLayout& ruins,
                       const ForestLayout& forest, const CaveLayout& cave, const CastleLayout& castle, const WestLayout& west,
                       const NorthLayout& north, const SiteHints& hints);

// The relief's last touches for the sites (build_layouts, after the
// north's): the port's quay, the abbey's ground cut into the mountain and
// its way up, the camp's, the quarry's pit, the chapel's, the apiary's, the
// lookout's flat ground, the dolmen's mound. Sets the levels.
void shape_sites(Heightmap& ground, SitesLayout& sites, uint64_t seed);

// Whether (x, z) belongs to one of the sites (no trees nor rocks drawn at
// random there), moved out by `margin`.
bool in_site(const SitesLayout& sites, float x, float z, float margin = 0);
// Inside the abbey's walls.
bool in_abbey(const SitesLayout& sites, float x, float z, float margin = 0);
// Bare ground (no grass drawn): the quarry's pit and its benches, round the camp's fire.
bool bare_site(const SitesLayout& sites, float x, float z);
// On the port's quay (moved out by `margin`).
bool in_port(const SitesLayout& sites, float x, float z, float margin = 0);

// The objects of the sites, appended to World::objects (after the roads',
// before the wilds'). Not saved.
void build_sites(World& world);

// The sites' villagers (outside), each at its place, if not there yet.
void add_site_npcs(World& world);

// Inside the temple: the monks, each at its place, and the abbot by the
// altar (interior.cpp, on entering). Not saved (their victories are: World::trials).
void spawn_monks(World& world);
// Where a monk waits in the temple (interior coordinates).
Vec2 monk_spot(uint8_t npc);

}  // namespace monstres
