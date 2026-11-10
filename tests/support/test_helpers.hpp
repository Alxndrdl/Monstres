#pragma once

#include "monstres/core/world.hpp"

#include <doctest.h>

#include <cmath>
#include <ostream>

namespace monstres {

// Readable values in doctest failure messages.
inline std::ostream& operator<<(std::ostream& os, Vec3 v) {
  return os << '(' << v.x << ", " << v.y << ", " << v.z << ')';
}

// A world with no objects, for tests that place things by hand.
inline World empty_world() {
  World w;
  w.rng = Rng(1);
  return w;
}

// Adds a box resting on the ground (or floating, with `bottom` > 0).
inline void add_box(World& w, Vec3 center_xz, Vec3 half, float bottom = 0) {
  w.objects.push_back({{center_xz.x, bottom + half.y, center_xz.z}, half, 0});
}

inline Input move(int8_t x, int8_t y, Angle16 yaw = 0, bool jump = false) {
  return {x, y, yaw, jump};
}
inline Input forward(Angle16 yaw = 0) { return move(0, 127, yaw); }
inline Input jump_input() { return move(0, 0, 0, true); }

inline void run(World& w, const Input& in, int ticks) {
  for (int i = 0; i < ticks; ++i) step(w, in);
}

// Rules that must hold after every tick of any game.
inline void check_invariants(const World& w) {
  const Player& p = w.player;
  CHECK(std::isfinite(p.pos.x));
  CHECK(std::isfinite(p.pos.y));
  CHECK(std::isfinite(p.pos.z));
  CHECK(p.pos.y >= 0.0f);
  const float limit = rules::kWorldHalfSize - rules::kPlayerHalfWidth;
  CHECK(std::fabs(p.pos.x) <= limit);
  CHECK(std::fabs(p.pos.z) <= limit);
  CHECK(std::fabs(p.facing_x * p.facing_x + p.facing_z * p.facing_z - 1.0f) < 1e-4f);
  for (const Object& o : w.objects) {
    CHECK_MESSAGE(!overlaps(player_box(p), object_box(o)), "player inside the object at ", o.center);
  }
}

}  // namespace monstres
