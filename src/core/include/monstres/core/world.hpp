#pragma once

#include "monstres/core/math.hpp"
#include "monstres/core/rng.hpp"

#include <cstdint>
#include <vector>

namespace monstres {

// Coordinates: meters. Y is up, the ground is the plane y = 0.
// With a camera yaw of 0, "forward" is -Z and "right" is +X.

namespace rules {
inline constexpr int32_t kTicksPerSecond = 60;
inline constexpr float kDt = 1.0f / kTicksPerSecond;
inline constexpr float kWorldHalfSize = 30.0f;  // the ground spans [-30, 30] on X and Z
inline constexpr float kPlayerHalfWidth = 0.4f; // the player is a 0.8 m cube
inline constexpr float kPlayerHeight = 0.8f;
inline constexpr float kWalkSpeed = 5.0f;       // m/s
inline constexpr float kJumpSpeed = 7.0f;       // m/s, jumps ~1.2 m high
inline constexpr float kGravity = 20.0f;        // m/s²
inline constexpr float kMaxFallSpeed = 30.0f;
inline constexpr uint32_t kObjectCount = 80;
inline constexpr float kSpawnClearRadius = 4.0f;
inline constexpr float kObjectGap = 0.6f;       // free space kept between objects
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

struct Player {
  Vec3 pos;            // center of the bottom face (feet)
  float velocity_y = 0;
  bool on_ground = true;
  float facing_x = 0;  // unit vector of the last movement direction
  float facing_z = -1;
  bool operator==(const Player&) const = default;
};

struct World {
  uint64_t tick = 0;
  Rng rng;
  Player player;
  std::vector<Object> objects;
  bool operator==(const World&) const = default;
};

World new_game(uint64_t seed);

// Advances the world by exactly one fixed tick (1/60 s). Same world + same
// input always gives the same result, on every platform.
void step(World& world, const Input& input);

struct Box {
  Vec3 min, max;
};
Box player_box(const Player& p);
Box object_box(const Object& o);
bool overlaps(const Box& a, const Box& b);  // strict: touching is not overlapping
bool is_round(ObjectKind kind);             // any yaw allowed

}  // namespace monstres
