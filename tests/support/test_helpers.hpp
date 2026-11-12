#pragma once

#include "monstres/core/world.hpp"

#include <doctest.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <ostream>
#include <utility>
#include <vector>

namespace monstres {

// Built with the sanitizers (native-debug): about 40 times slower. The longest
// random games are then shorter (the release build plays them in full).
#if defined(__SANITIZE_ADDRESS__)
inline constexpr bool kSanitized = true;
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
inline constexpr bool kSanitized = true;
#else
inline constexpr bool kSanitized = false;
#endif
#else
inline constexpr bool kSanitized = false;
#endif

// How many worlds the tests that check every new world try.
inline constexpr uint64_t kManySeeds = kSanitized ? 8 : 30;

// Readable values in doctest failure messages.
inline std::ostream& operator<<(std::ostream& os, Vec3 v) {
  return os << '(' << v.x << ", " << v.y << ", " << v.z << ')';
}

// A new game for this seed, built once per run of the tests: worlds take a
// while to build, and many tests start from the same few. Copy it to change it.
inline const World& cached_game(uint64_t seed) {
  static std::map<uint64_t, World> games;
  auto it = games.find(seed);
  if (it == games.end()) it = games.emplace(seed, new_game(seed)).first;
  return it->second;
}

// The pairs of boxes that overlap (sort and sweep along X: fast for many boxes).
inline std::vector<std::pair<size_t, size_t>> overlapping_pairs(const std::vector<Box>& boxes) {
  std::vector<size_t> order(boxes.size());
  for (size_t i = 0; i < order.size(); ++i) order[i] = i;
  std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return boxes[a].min.x < boxes[b].min.x; });
  std::vector<std::pair<size_t, size_t>> pairs;
  for (size_t k = 0; k < order.size(); ++k) {
    const Box& a = boxes[order[k]];
    for (size_t m = k + 1; m < order.size() && boxes[order[m]].min.x < a.max.x; ++m) {
      if (overlaps(a, boxes[order[m]])) pairs.emplace_back(std::min(order[k], order[m]), std::max(order[k], order[m]));
    }
  }
  return pairs;
}

// A world with no objects, for tests that place things by hand.
inline World empty_world() {
  World w;
  w.rng = Rng(1);
  return w;
}

// Adds a crate (a plain box) resting on the ground, or floating with `bottom` > 0.
inline void add_box(World& w, Vec3 center_xz, Vec3 half, float bottom = 0) {
  Object o;
  o.kind = ObjectKind::Crate;
  o.pos = {center_xz.x, bottom, center_xz.z};
  o.size = half;
  w.objects.push_back(o);
}

inline Object make_object(ObjectKind kind, Vec3 pos, Vec3 size, Angle16 yaw = 0) {
  Object o;
  o.kind = kind;
  o.pos = pos;
  o.size = size;
  o.yaw = yaw;
  return o;
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
  CHECK(p.pos.y >= w.ground.height(p.pos.x, p.pos.z));  // never inside the ground
  const float limit = rules::kWorldHalfSize - rules::kPlayerHalfWidth;
  CHECK(std::fabs(p.pos.x) <= limit);
  CHECK(std::fabs(p.pos.z) <= limit);
  CHECK(std::fabs(p.facing_x * p.facing_x + p.facing_z * p.facing_z - 1.0f) < 1e-4f);
  // Plain loops, and an assertion only on failure: these run very often.
  // Objects are at most a few meters wide: farther ones are skipped at once.
  constexpr float kNear = 8.0f;
  const auto far = [](const Object& o, const Vec3& at) {
    return std::fabs(o.pos.x - at.x) > kNear || std::fabs(o.pos.z - at.z) > kNear;
  };
  for (const Object& o : w.objects) {
    if (far(o, p.pos)) continue;
    if (overlaps(player_box(p), object_box(o))) FAIL_CHECK("player inside the object at " << o.pos);
  }
  for (const Creature& c : w.creatures) {
    const BodyShape& shape = species_info(c.species).shape;
    CHECK(c.species < Species::Count);
    CHECK(std::fabs(c.body.pos.x) <= rules::kWorldHalfSize - shape.half_width + 1e-4f);
    CHECK(std::fabs(c.body.pos.z) <= rules::kWorldHalfSize - shape.half_width + 1e-4f);
    if (c.species == Species::Fish) {  // in the lake, at the surface or jumping
      CHECK(c.body.pos.y >= rules::kSwimLevel);
      CHECK(in_lake(w.village.lake, c.body.pos.x, c.body.pos.z));
      continue;
    }
    CHECK(c.body.pos.y >= w.ground.height(c.body.pos.x, c.body.pos.z));
    if (c.species == Species::Crow) continue;  // flies over everything
    CHECK_FALSE(in_lake(w.village.lake, c.body.pos.x, c.body.pos.z));
    for (const Object& o : w.objects) {
      if (far(o, c.body.pos)) continue;
      if (overlaps(body_box(c.body, shape), object_box(o))) FAIL_CHECK("creature inside the object at " << o.pos);
    }
  }
}

}  // namespace monstres
