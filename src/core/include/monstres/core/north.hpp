#pragma once

#include "monstres/core/world.hpp"

namespace monstres {

// The north: from the terrace up to the foot of the Monts Givrés (the world
// grew north to z = -1220). Along the path (NorthLayout, derived from the
// seed, see make_north_layout):
//
// - the alpine pastures (Zone::Alpine): meadows crossed by low dry-stone
//   walls, Laini and Cabri grazing;
// - the old mine in a rocky rise (inside: timbered galleries, Caillou and
//   Taupi); Gaston, the old miner, waits by its entrance: a giant Caillou
//   took the deepest gallery (QuestId::MineGuardian);
// - the refuge by the path: a stone chalet (a house inside), Solène its
//   keeper, who heals the team, and a teleporter;
// - the larch woods (Zone::Larches), gold in autumn, and the hot springs:
//   standing in their steaming pool heals the team, slowly;
// - beyond, the mountains rise (Zone::Frost): rocks, snow, steep slopes.
//   The path goes up a valley to the col, the frozen lake beside it
//   (Zone::FrozenLake: walked on), cairns along the way, a teleporter at the col.

namespace rules {
inline constexpr float kNorthFrom = -560.0f;       // z: the north's places and wilds begin about here
inline constexpr float kLarchesFrom = -830.0f;     // z: the larch woods (more or less)
inline constexpr float kFrostFrom = -1030.0f;      // z: the mountains' slopes (more or less)
inline constexpr float kRefugeRadius = 13.0f;      // its flat ground
inline constexpr float kSpringsRadius = 7.0f;      // the springs' flat ground
inline constexpr float kSpringsPool = 3.6f;        // the pool, inside its ring of stones
inline constexpr float kSpringsDepth = 0.7f;
inline constexpr uint16_t kSpringsHealEvery = 2 * kTicksPerSecond;  // standing in the pool: a hit point each, to each monster
inline constexpr float kMineYard = 7.0f;           // the flat ground before the mine's entrance
inline constexpr float kMineRise = 9.0f;           // its rocky rise, behind
inline constexpr float kMineRiseRadius = 26.0f;
inline constexpr float kValleyHalf = 24.0f;        // the valley up to the col: its flat floor's half width
inline constexpr float kValleySlope = 0.55f;       // its sides, beyond (walkable)
inline constexpr float kValleyRise = 0.15f;        // its floor goes up this much a meter towards the col
inline constexpr float kWallHeight = 0.62f;        // the dry-stone walls: stepped over
inline constexpr uint8_t kGuardianLevel = 14, kGuardianEscortLevel = 10;
}  // namespace rules

// QuestState::progress of the mine's quest.
inline constexpr uint8_t kGuardianBeaten = 1;  // the giant Caillou beaten (or caught)

// Where the north's places are (World::north), from the seed, the path and
// the west (its terrace); empty when the world has no relief.
NorthLayout make_north_layout(uint64_t seed, const Path& path, const WestLayout& west);

// The relief's last touches in the north (make_heightmap): the places'
// flat ground (at the height of the ground there), the mine's rocky rise,
// the springs' pool, the valley up to the col (the mountains' slopes cut
// back around the path: walkable), the frozen lake's flat ice. Sets the
// lake's level and the valley's floor.
void shape_north(Heightmap& ground, NorthLayout& north, const Path& path, uint64_t seed);

// Whether (x, z) is in the frozen lake (its ice), moved out by `margin`; and
// how far, in its radii: 0 in the middle, 1 on its shore.
bool in_frozen_lake(const NorthLayout& north, float x, float z, float margin = 0);
float frozen_lake_distance(const NorthLayout& north, float x, float z);
// Whether (x, z) belongs to one of the north's places (no trees or rocks
// drawn at random there): the refuge, the mine's yard, the springs, the
// lake, the col; the walls' lines.
bool in_north_place(const NorthLayout& north, float x, float z, float margin = 0);
// In the springs' pool (the team heals there).
bool in_springs(const NorthLayout& north, float x, float z);
// How snowy the ground is at (x, z), whose height is h (0..1): the
// mountains' slopes, higher and further north; not the lake's ice.
float north_snow(const NorthLayout& north, float x, float z, float h);

// The objects of the north, appended to World::objects (build_west's, then
// the wilds'): the refuge and its teleporter, the mine's entrance, its carts,
// the springs, the walls, the cairns, the col's teleporter. Not saved. The
// valley's own objects are far away (nothing to clear).
void build_north(World& world);

// What the wilds grow in the north (generate_wilds): the pastures' grass
// and few trees, the larch woods, the mountains' rocks. `o` is what was
// drawn there; returns the chance (0..1) that it stays, and may change it.
float north_wilds(const World& world, Object& o, float x, float z, float chance);

// The north's villagers (Solène, Gaston), each at its place, if not there yet.
void add_north_npcs(World& world);

// Each tick (from step()): the hot springs heal the team of the player
// standing in them.
void north_step(World& world);

// Inside the mine: the Caillou and Taupi digging there, and the guardian in
// its deepest gallery until it is beaten (interior.cpp, on entering).
void spawn_miners(World& world);
// The guardian beaten or caught (battle.cpp): the quest can be told to Gaston.
void guardian_down(World& world, const Creature& creature);

}  // namespace monstres
