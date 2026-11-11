#pragma once

#include "monstres/core/heightmap.hpp"
#include "monstres/core/math.hpp"
#include "monstres/core/rng.hpp"
#include "monstres/core/terrain.hpp"
#include "monstres/core/village.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace monstres {

// Coordinates: meters. Y is up; the ground is World::ground (hills, y = 0
// around the village and in hand-made worlds without a heightmap).
// With a camera yaw of 0, "forward" is -Z and "right" is +X.

namespace rules {
inline constexpr int32_t kTicksPerSecond = 60;
inline constexpr float kDt = 1.0f / kTicksPerSecond;
inline constexpr float kWorldHalfSize = 75.0f;  // the playable ground spans [-75, 75] on X and Z
inline constexpr float kPlayerHalfWidth = 0.28f; // collision box of the explorer
inline constexpr float kPlayerHeight = 1.1f;
inline constexpr float kWalkSpeed = 5.0f;       // m/s
inline constexpr float kJumpSpeed = 7.0f;       // m/s, jumps ~1.2 m high
inline constexpr float kGravity = 20.0f;        // m/s²
inline constexpr float kMaxFallSpeed = 30.0f;
inline constexpr uint32_t kObjectCount = 380;   // trees, rocks... outside the village and the ruins
inline constexpr float kSpawnClearRadius = 4.0f;
inline constexpr float kObjectGap = 0.6f;       // free space kept between objects
inline constexpr uint32_t kWildMonsters = 40;
inline constexpr uint32_t kVillagers = 8;
inline constexpr uint32_t kCrows = 5;
inline constexpr uint32_t kFish = 4;
inline constexpr float kVillagerGreetDistance = 2.5f;
inline constexpr float kCrowFleeDistance = 4.0f;
inline constexpr float kMonsterSpawnMinDistance = 8.0f;  // from the player's start
inline constexpr float kWanderRadius = 8.0f;             // around a monster's home
inline constexpr float kBirdFleeDistance = 3.5f;
inline constexpr float kCompanionFollowDistance = 1.3f;  // behind the player
inline constexpr float kCompanionTeleportDistance = 14.0f;
// Time of day: one hour of the game lasts one real minute, a day 24 minutes.
inline constexpr uint32_t kTicksPerHour = 60 * kTicksPerSecond;
inline constexpr uint32_t kTicksPerDay = 24 * kTicksPerHour;
inline constexpr uint32_t kStartClock = 8 * kTicksPerHour;  // a new game starts at 8:00
}  // namespace rules

// Orders given from the menus (context menu on the right), see battle.hpp.
enum class Command : uint8_t {
  None,
  Engage,   // start a battle against creatures[Input::target]
  Flee,     // the companion runs back to the player, then the battle ends
  Capture,  // the enemy joins the team (it must be weakened first)
  Summon,   // team[Input::target] comes out of the staff's crystal (or goes back in, if it is out)
};

// What the player does during one tick. Integers, so that replays and saved
// inputs reproduce exactly.
struct Input {
  int8_t move_x = 0;   // joystick right (+127) / left (-127)
  int8_t move_y = 0;   // joystick forward (+127) / back (-127)
  Angle16 yaw = 0;     // camera direction: movement is relative to it (not in a battle)
  bool jump = false;   // jump this tick (if on the ground); in a battle: dodge
  uint8_t attack = 0;  // in a battle: 1 quick attack, 2 and 3 special attacks
  bool parry = false;  // in a battle
  Command command = Command::None;
  uint16_t target = 0; // Engage: index of the creature; Summon: index in the team
  bool operator==(const Input&) const = default;
};

enum class ObjectKind : uint8_t {
  Tree,   // leafy tree. size.x = scale
  Pine,   // conifer. size.x = scale
  Rock,   // size = half width, height, half depth
  Log,    // fallen trunk lying along its yaw. size.x = half length, size.y = radius
  Stump,  // size.x = radius, size.y = height
  Bush,   // size.x = radius, size.y = height
  Crate,  // plain box. size = half extents (also handy in tests)
  // Village (fixed sizes, size is unused):
  HouseCottage,  // 5 x 4 m, gable roof
  HouseChimney,  // 4.5 x 4.5 m, two floors, smoking chimney
  HouseHut,      // round, 4 m wide, thatched cone roof
  Fence,         // one 2 m segment, along its yaw
  Sign,          // signpost
  Well,
  Barrel,
  Lamp,          // lantern on a post
  // Ruins of the tower (fixed sizes unless said otherwise):
  Tower,         // the abandoned magician's tower, round, about 11 m high
  RuinWall,      // a 3 m broken wall along its yaw; variant odd = high (not jumpable)
  Pillar,        // standing column, size.y = height
  FallenPillar,  // a column lying along its yaw, 3 m long
  Rubble,        // a low pile of stones
  RuneStone,     // standing stone with glowing runes
  Count
};

// A fixed object of the world. Its collision box is derived from these
// values by object_box(): a narrow box for tree trunks, the full shape for
// rocks, logs, stumps, bushes and crates.
struct Object {
  ObjectKind kind = ObjectKind::Crate;
  Vec3 pos;            // center of the base
  Vec3 size;           // meaning depends on the kind (see ObjectKind)
  Angle16 yaw = 0;     // multiples of 90° for non-round kinds (rocks, logs, crates)
  uint8_t variant = 0; // picks a shape and a color variation, visual only
  bool operator==(const Object&) const = default;
};

// Anything that walks and jumps: the player and the creatures.
struct Body {
  Vec3 pos;            // center of the bottom face (feet)
  float velocity_y = 0;
  bool on_ground = true;
  float facing_x = 0;  // unit vector of the last movement direction
  float facing_z = -1;
  float speed = 0;     // horizontal speed during the last tick (m/s), drives animations
  bool operator==(const Body&) const = default;
};
using Player = Body;

struct BodyShape {
  float half_width;
  float height;
};

enum class Species : uint8_t {
  Companion,  // Pompon, follows the player
  Slime,      // Gloubi, moves by hopping
  Mushroom,   // Champi, waddles slowly
  Bird,       // Piou, runs away when the player comes close
  Villager,   // village people, variant = look
  Crow,       // Croa, perches on roofs, flies away when approached
  Fish,       // Bloup, swims in the lake and jumps out of it
  Count
};

struct SpeciesInfo {
  BodyShape shape;
  float walk_speed;
  float run_speed;
  float jump_speed;
};
const SpeciesInfo& species_info(Species species);

// What makes one monster differ from the others of its species (variants,
// see variants.hpp): one allele per gene of its species (colors, shapes),
// and two very rare forms. Visual only, except the elemental form, which
// changes its element.
inline constexpr size_t kMaxGenes = 4;  // the sex, then up to three others
struct Genes {
  std::array<uint8_t, kMaxGenes> allele{};  // index into each gene's alleles; 0 = the most common
  bool chromatic = false;                   // unusual colors
  bool elemental = false;                   // another element than its species
  bool operator==(const Genes&) const = default;
};

// What a creature is doing. Battle: fighting the companion (battle.hpp).
enum class Mind : uint8_t { Idle, Wander, Flee, Follow, Greet, Fly, Battle };

struct Creature {
  Species species = Species::Slime;
  Body body;
  Mind mind = Mind::Idle;
  uint16_t timer = 0;  // ticks left in the current activity (or stuck ticks while following)
  Vec2 target;         // where it is going
  float target_y = 0;  // flying creatures: height of the target
  Vec2 home;           // wild monsters wander around it
  uint8_t variant = 0; // villagers: their look (clothes, hair...), visual only
  Genes genes{};       // monsters: their variants
  uint8_t level = 1;   // wild monsters: their strength (the companion's is in World::team)
  bool operator==(const Creature&) const = default;
};

// A monster of the player's team. team[World::active] is the companion: out
// of the staff's crystal, it is creatures[0], follows the player and fights
// for it. team[0] is Pompon, the first companion.
struct Monster {
  Species species = Species::Companion;
  Genes genes{};
  uint16_t level = 1;
  uint32_t xp = 0;     // towards the next level (monster_xp_to_next)
  uint16_t hp = 0;     // hit points left (monster_fight_stats(...).hp at most)
  bool operator==(const Monster&) const = default;
};

// The player's magician. Level and experience are not used by the rules yet.
struct Wizard {
  std::string name;    // chosen by the player (UTF-8, at most kMaxNameBytes), may be empty
  uint16_t level = 1;
  uint32_t xp = 0;
  bool operator==(const Wizard&) const = default;
};
inline constexpr size_t kMaxNameBytes = 24;

// Characteristics derived from the level (character sheet).
struct WizardStats {
  uint16_t hp, mana, power, wisdom, agility, endurance;
  uint32_t xp_to_next;  // experience needed for the next level
};
WizardStats wizard_stats(const Wizard& wizard);

// Elements of magic, for monsters (and later spells).
// Count also means "none" (a monster with a single element).
enum class Element : uint8_t { Fire, Water, Nature, Air, Shadow, Light, Electric, Normal, Count };

// Base characteristics of a monster species (bestiary). Villagers have none.
struct MonsterStats {
  uint16_t hp, attack, defense, speed, magic;
  Element element;
  bool is_monster;                    // false for villagers
  Element element2 = Element::Count;  // second element, if any
};
const MonsterStats& monster_stats(Species species);

// Named parts of the world, for maps and the interface.
enum class Zone : uint8_t { Meadow, Village, Lake, Ruins, Count };

// What the player knows of each variant of each species (bestiary): every
// variant listed by variants.hpp, in the same order.
enum class Discovery : uint8_t { Unknown, Seen, Captured };
inline constexpr size_t kMaxVariants = 16;  // per species
inline constexpr size_t kSpeciesCount = static_cast<size_t>(Species::Count);
using Dex = std::array<std::array<Discovery, kMaxVariants>, kSpeciesCount>;

// A battle between the companion and one wild monster (see battle.hpp). Not
// saved: a game loaded during a battle goes on without it.
enum class BattlePhase : uint8_t { None, Fight, Flee };
enum class BattleOutcome : uint8_t { None, Victory, Defeat, Fled, Captured };

// What one side of a battle is doing.
enum class FighterState : uint8_t {
  Ready,
  Windup,   // about to attack (the enemy shows it: time to dodge or parry)
  Recover,  // just attacked
  Dodge,    // dashing, cannot be hit
  Parry,    // guarding: the next hit is blocked
  Stunned,  // its attack was parried
};

struct Fighter {
  std::array<uint16_t, 3> cooldown{};  // ticks before each attack can be used again
  FighterState state = FighterState::Ready;
  uint16_t state_ticks = 0;  // ticks left in this state
  uint8_t move = 0;          // Windup: the attack coming (0..2)
  uint16_t dodge_cooldown = 0;
  uint16_t parry_cooldown = 0;
  float dodge_x = 0, dodge_z = 0;
  // For the interface and the drawing: when things happened (World::tick, 0: never).
  uint64_t attack_tick = 0;  // last attack (attack_move: which one)
  uint8_t attack_move = 0;
  uint64_t hit_tick = 0;     // last time it was hurt (last_damage: how much)
  uint16_t last_damage = 0;
  uint64_t parried_tick = 0; // last hit it blocked
  bool operator==(const Fighter&) const = default;
};

struct Projectile {
  Vec3 pos;            // center
  float dir_x = 0, dir_z = 0;
  uint16_t ticks = 0;  // left before it fades
  bool from_ally = true;
  uint8_t move = 0;    // attack of the thrower (1: its first special attack)
  Element element = Element::Normal;
  uint16_t damage = 0;
  bool operator==(const Projectile&) const = default;
};

struct Battle {
  BattlePhase phase = BattlePhase::None;
  uint16_t enemy = 0;  // index of the wild creature
  uint16_t enemy_hp = 0, enemy_max_hp = 0;
  Fighter ally, foe;
  std::vector<Projectile> projectiles;
  uint16_t timer = 0;  // Flee: ticks spent running back
  // How the last battle ended, for the interface.
  BattleOutcome outcome = BattleOutcome::None;
  uint64_t outcome_tick = 0;
  uint32_t outcome_xp = 0;
  Species outcome_species = Species::Companion;
  bool operator==(const Battle&) const = default;
};

// What the staff's crystal just did, for the drawing (not saved): a monster
// went in (recalled, or captured: runes first), another came out.
struct CrystalTrace {
  uint64_t tick = 0;   // when (0: never)
  bool captured = false;
  Species species = Species::Companion;
  Genes genes{};
  Body body;           // where it was
  bool operator==(const CrystalTrace&) const = default;
};

struct World {
  uint64_t seed = 0;   // the world's layout: path, objects, decoration
  uint64_t tick = 0;
  Rng rng;
  Path path;           // derived from the seed (not saved)
  VillageLayout village;  // derived from the seed (not saved)
  RuinsLayout ruins;   // derived from the seed (not saved)
  Heightmap ground;    // derived from the seed (not saved)
  Body player;
  std::vector<Object> objects;
  std::vector<Creature> creatures;  // creatures[0] is the companion
  Wizard wizard;
  Dex dex{};           // variants seen and captured
  std::vector<Monster> team;  // team[0]: Pompon
  int8_t active = 0;   // team member out of the crystal (creatures[0]), -1: none
  CrystalTrace went_in;        // not saved
  uint64_t came_out_tick = 0;  // not saved: when creatures[0] was summoned
  Battle battle;       // not saved
  uint32_t clock = rules::kStartClock;  // time of day: ticks since midnight, < kTicksPerDay
  bool operator==(const World&) const = default;
};

World new_game(uint64_t seed);

// The companion out of the crystal (creatures[0]), or nullptr.
inline const Monster* active_monster(const World& w) {
  return w.active >= 0 && static_cast<size_t>(w.active) < w.team.size() && !w.creatures.empty()
             ? &w.team[static_cast<size_t>(w.active)]
             : nullptr;
}
inline Monster* active_monster(World& w) {
  return const_cast<Monster*>(active_monster(static_cast<const World&>(w)));
}

// Time of day in hours, [0, 24).
inline float hour_of_day(const World& world) {
  return static_cast<float>(world.clock) / static_cast<float>(rules::kTicksPerHour);
}

// Places the companion next to the player and the wild monsters around the
// world (replaces any existing creatures). Uses its own random stream from
// the seed, so objects are not affected. Called by new_game, and when
// loading saves from before creatures existed.
void spawn_creatures(World& world);

// Builds the village (houses, well, fences, signs...) as objects, removing
// any other object in its way or in the lake. Used by new_game, and when
// loading saves from before the village existed.
void build_village(World& world);

// Builds the ruins of the tower (tower, walls, columns, rubble, rune stones)
// as objects, removing any other object in their way.
void build_ruins(World& world);

// Which named part of the world (x, z) is in.
Zone zone_at(const World& world, float x, float z);

// Sets every object on the ground: its base goes to the lowest point of the
// ground under it, slightly sunk, so that no edge floats on a slope. Low
// objects sink at most 40 % of their height (an edge may float a little).
void settle_objects(World& world);

// Places on roofs (and the well) where crows land.
std::vector<Vec3> crow_perches(const World& world);

// Advances the world by exactly one fixed tick (1/60 s). Same world + same
// input always gives the same result, on every platform.
void step(World& world, const Input& input);

struct Box {
  Vec3 min, max;
};
inline constexpr BodyShape kPlayerShape{rules::kPlayerHalfWidth, rules::kPlayerHeight};
Box body_box(const Body& b, const BodyShape& shape);
Box player_box(const Player& p);
Box object_box(const Object& o);
bool overlaps(const Box& a, const Box& b);  // strict: touching is not overlapping
bool is_round(ObjectKind kind);             // any yaw allowed

}  // namespace monstres
