#pragma once

#include "monstres/core/world.hpp"

namespace monstres {

// How the villagers and the wild monsters live from hour to hour, by the
// weather and the season (World::sky), and with one another (Creature::act).
//
// The villagers' routines: those who have a house near their place (a
// cottage, a hut, a farmhouse, a cabin, a chalet, the lighthouse) go home at
// night (each its own hours), when it rains or snows hard, and in winter
// (short days, and many a while by the hearth). They walk to their door and
// go in (Activity::Home: not seen, not met outside); far from the player, at
// once. In their house, the player finds them (spawn_residents): they talk,
// heal, sell, as outside. The trainers waiting for a battle, the abbey's
// gatekeeper and the arena's master stay at their place in all weathers.
//
// The wild monsters (not the ones with a part to play: Tag::None only):
// - the time of day: most sleep at night (Chipou by day), curled up by a fire
//   when one burns near (the cold ones), and wake when the player comes close;
// - the weather: those who mind the rain go under a tree, Champi takes the
//   shade of one at noon in summer; Gloubi, Champi and Ploufi roam farther
//   in the rain, Zapou dashes about in a storm, Givri in the snow;
// - the land: they go and drink at the water's edge (more on hot days), the
//   grazers nibble the grass, the birds peck;
// - the others: sheep and goats keep together, the young of a kind run
//   after one another, Chipou and Givri chase their prey (which flees), and
//   a monster of the companion's kind comes to sniff it.

namespace rules {
inline constexpr float kHomeReach = 45.0f;      // a villager's house: the nearest within this of its place
inline constexpr uint32_t kRoutineEvery = 30;   // ticks between a villager's looks at the time and the sky
inline constexpr float kSeenDistance = 45.0f;   // farther from the player, a villager goes in or out at once
inline constexpr float kRainHome = 0.45f, kRainOut = 0.25f;    // rain sending the villagers home, letting them out
inline constexpr float kSnowHome = 0.6f, kSnowOut = 0.35f;
inline constexpr float kStormHome = 0.3f, kStormOut = 0.15f;
inline constexpr uint32_t kWinterHearth = 40;   // in winter: out of 100 two-hour whiles, so many at home
inline constexpr float kWakeDistance = 4.0f;    // a sleeping monster wakes when the player comes this close
inline constexpr float kShelterReach = 14.0f;   // a tree to go under, this far at most
inline constexpr float kWaterReach = 22.0f;     // water to drink, this far at most
inline constexpr float kFireReach = 18.0f;      // a fire to sleep by
inline constexpr float kPreyReach = 10.0f;      // a prey seen, chased
inline constexpr float kPreyFlee = 5.0f;        // a hunter this close: the prey runs
inline constexpr float kKinReach = 8.0f;        // one of its kind to play with, to sniff
inline constexpr float kHerdReach = 18.0f;      // the herd it keeps to
}  // namespace rules

// Whether this villager goes home (night, rain, winter), if it has a house.
bool keeps_routine(uint8_t npc);
// Whether it would rather be at home now (with `home`: it is, so it stays
// a little longer in the rain).
bool wants_home(const World& world, const Creature& villager, bool home);
// The villager's house (outside): the spot before its door. False: none.
bool home_door(const World& world, const Creature& villager, Vec2& door);
// A villager's routine, outside, every tick (any distance from the player):
// going home, coming out. Moves it at once out of the player's sight.
void villager_routine(World& world, Creature& villager);
// Going into a house: who lives there and is in comes inside (enter_place).
void spawn_residents(World& world);
// Whether a creature is about (not in its house).
inline bool about(const Creature& c) { return c.act != Activity::Home; }

// The wild monsters. What a monster does next, when its rest ends (instead
// of wandering about): sets its mind, target, timer and activity. False:
// nothing special, it wanders (how far: wander_reach).
bool wild_decide(World& world, Creature& c);
float wander_reach(const World& world, const Creature& c);
// Every tick, before it moves: the sleeper wakes, the chaser follows its
// prey, the prey runs from its hunter... False: it stays still this tick.
bool wild_watch(World& world, Creature& c);
// Whether it goes at a run (chasing, playing, fleeing).
bool wild_runs(const Creature& c);
// Whether it lies asleep (or curled up by a fire).
inline bool asleep(const Creature& c) {
  return c.act == Activity::Sleep || (c.act == Activity::Warm && c.mind == Mind::Idle);
}
// The creature with this id, or -1.
int creature_by_id(const World& world, uint32_t id);
// Water to drink from near (x, z): a dry spot at its edge, and the way to the water.
bool water_edge(const World& world, Vec2 from, float reach, Vec2& edge, Vec2& facing);

const char* activity_name(Activity a);

}  // namespace monstres
