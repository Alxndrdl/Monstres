#pragma once

#include "monstres/core/math.hpp"
#include "monstres/core/rng.hpp"
#include "monstres/core/terrain.hpp"
#include "monstres/core/village.hpp"

#include <cstdint>
#include <vector>

namespace monstres {

// Coordinates: meters. Y is up, the ground is the plane y = 0.
// With a camera yaw of 0, "forward" is -Z and "right" is +X.

namespace rules {
inline constexpr int32_t kTicksPerSecond = 60;
inline constexpr float kDt = 1.0f / kTicksPerSecond;
inline constexpr float kWorldHalfSize = 45.0f;  // the ground spans [-45, 45] on X and Z
inline constexpr float kPlayerHalfWidth = 0.28f; // collision box of the explorer
inline constexpr float kPlayerHeight = 1.1f;
inline constexpr float kWalkSpeed = 5.0f;       // m/s
inline constexpr float kJumpSpeed = 7.0f;       // m/s, jumps ~1.2 m high
inline constexpr float kGravity = 20.0f;        // m/s²
inline constexpr float kMaxFallSpeed = 30.0f;
inline constexpr uint32_t kObjectCount = 150;   // trees, rocks... outside the village
inline constexpr float kSpawnClearRadius = 4.0f;
inline constexpr float kObjectGap = 0.6f;       // free space kept between objects
inline constexpr uint32_t kWildMonsters = 16;
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
}  // namespace rules

// What the player does during one tick. Integers, so that replays and saved
// inputs reproduce exactly.
struct Input {
  int8_t move_x = 0;   // joystick right (+127) / left (-127)
  int8_t move_y = 0;   // joystick forward (+127) / back (-127)
  Angle16 yaw = 0;     // camera direction: movement is relative to it
  bool jump = false;   // jump this tick (if on the ground)
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

// What a creature is doing.
enum class Mind : uint8_t { Idle, Wander, Flee, Follow, Greet, Fly };

struct Creature {
  Species species = Species::Slime;
  Body body;
  Mind mind = Mind::Idle;
  uint16_t timer = 0;  // ticks left in the current activity (or stuck ticks while following)
  Vec2 target;         // where it is going
  float target_y = 0;  // flying creatures: height of the target
  Vec2 home;           // wild monsters wander around it
  uint8_t variant = 0; // color variation, visual only
  bool operator==(const Creature&) const = default;
};

struct World {
  uint64_t seed = 0;   // the world's layout: path, objects, decoration
  uint64_t tick = 0;
  Rng rng;
  Path path;           // derived from the seed (not saved)
  VillageLayout village;  // derived from the seed (not saved)
  Body player;
  std::vector<Object> objects;
  std::vector<Creature> creatures;  // creatures[0] is the companion
  bool operator==(const World&) const = default;
};

World new_game(uint64_t seed);

// Places the companion next to the player and the wild monsters around the
// world (replaces any existing creatures). Uses its own random stream from
// the seed, so objects are not affected. Called by new_game, and when
// loading saves from before creatures existed.
void spawn_creatures(World& world);

// Builds the village (houses, well, fences, signs...) as objects, removing
// any other object in its way or in the lake. Used by new_game, and when
// loading saves from before the village existed.
void build_village(World& world);

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
