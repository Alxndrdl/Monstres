#include "monstres/core/world.hpp"

#include "monstres/core/battle.hpp"
#include "monstres/core/variants.hpp"

#include <algorithm>
#include <cmath>
#include <span>
#include <utility>

namespace monstres {

namespace {

// Random value in [lo, hi] with a 1 cm step: integers from the RNG converted
// exactly, so the layout is identical on every platform.
float random_range(Rng& rng, float lo, float hi) {
  const auto steps = static_cast<uint32_t>((hi - lo) * 100.0f + 0.5f);
  return lo + static_cast<float>(rng.below(steps + 1)) / 100.0f;
}

Angle16 any_yaw(Rng& rng) { return static_cast<Angle16>(rng.below(65536)); }
Angle16 quarter_yaw(Rng& rng) { return static_cast<Angle16>(rng.below(4) << 14); }

ObjectKind pick_kind(Rng& rng) {
  const uint32_t r = rng.below(100);
  if (r < 30) return ObjectKind::Tree;
  if (r < 45) return ObjectKind::Pine;
  if (r < 68) return ObjectKind::Rock;
  if (r < 78) return ObjectKind::Log;
  if (r < 88) return ObjectKind::Stump;
  if (r < 96) return ObjectKind::Bush;
  return ObjectKind::Crate;
}

Object random_object(Rng& rng, float x, float z) {
  Object o;
  o.kind = pick_kind(rng);
  o.pos = {x, 0, z};
  switch (o.kind) {
    case ObjectKind::Tree: {
      const float s = random_range(rng, 0.8f, 1.4f);
      o.size = {s, s, s};
      o.yaw = any_yaw(rng);
      break;
    }
    case ObjectKind::Pine: {
      const float s = random_range(rng, 0.8f, 1.5f);
      o.size = {s, s, s};
      o.yaw = any_yaw(rng);
      break;
    }
    case ObjectKind::Rock:
      o.size = {random_range(rng, 0.4f, 1.6f), random_range(rng, 0.3f, 1.4f), random_range(rng, 0.4f, 1.6f)};
      o.yaw = quarter_yaw(rng);
      break;
    case ObjectKind::Log:
      o.size = {random_range(rng, 1.0f, 2.0f), random_range(rng, 0.25f, 0.4f), 0};
      o.yaw = quarter_yaw(rng);
      break;
    case ObjectKind::Stump:
      o.size = {random_range(rng, 0.3f, 0.5f), random_range(rng, 0.3f, 0.7f), 0};
      o.yaw = any_yaw(rng);
      break;
    case ObjectKind::Bush:
      o.size = {random_range(rng, 0.5f, 0.9f), random_range(rng, 0.5f, 0.9f), 0};
      o.yaw = any_yaw(rng);
      break;
    default: {  // Crate
      o.kind = ObjectKind::Crate;
      const float h = random_range(rng, 0.4f, 0.7f);
      o.size = {h, h, h};
      o.yaw = quarter_yaw(rng);
      break;
    }
  }
  o.variant = static_cast<uint8_t>(rng.below(256));
  return o;
}

// Space an object takes for placement: its collision box, widened to the
// foliage for trees so that crowns do not grow into each other.
Box footprint(const Object& o) {
  Box b = object_box(o);
  float r = 0;
  if (o.kind == ObjectKind::Tree) r = 1.2f * o.size.x;
  if (o.kind == ObjectKind::Pine) r = 1.0f * o.size.x;
  if (r > 0) {
    b.min.x = o.pos.x - r;
    b.max.x = o.pos.x + r;
    b.min.z = o.pos.z - r;
    b.max.z = o.pos.z + r;
  }
  return b;
}

void generate_objects(World& w) {
  const float limit = rules::kWorldHalfSize - 3.0f;
  const float gap = rules::kObjectGap;
  uint32_t nature = 0;  // the village is already there
  for (int attempt = 0; attempt < 50000 && nature < rules::kObjectCount; ++attempt) {
    const float x = random_range(w.rng, -limit, limit);
    const float z = random_range(w.rng, -limit, limit);
    const Object o = random_object(w.rng, x, z);

    // Keep the spawn area clear.
    const Box b = footprint(o);
    const float dx = std::max({b.min.x, -b.max.x, 0.0f});
    const float dz = std::max({b.min.z, -b.max.z, 0.0f});
    if (dx * dx + dz * dz < rules::kSpawnClearRadius * rules::kSpawnClearRadius) continue;

    // Not on a slope too steep for its size (a flat rock would float).
    if (!w.ground.empty()) {
      const Box box = object_box(o);
      const float center = w.ground.height(o.pos.x, o.pos.z);
      float lowest = center;
      for (float cx : {box.min.x, box.max.x}) {
        for (float cz : {box.min.z, box.max.z}) lowest = std::min(lowest, w.ground.height(cx, cz));
      }
      if (center - lowest > 0.6f * (box.max.y - box.min.y) + 0.1f) continue;
    }

    // Keep the village, the lake and the ruins clear.
    if (in_village(w.village, o.pos.x, o.pos.z, 2.0f) || in_lake(w.village.lake, o.pos.x, o.pos.z, 2.5f) ||
        in_ruins(w.ruins, o.pos.x, o.pos.z, 2.0f)) {
      continue;
    }

    // Keep the path clear.
    const float reach = std::max(b.max.x - b.min.x, b.max.z - b.min.z) * 0.5f;
    const float keep_off = rules::kPathHalfWidth + reach + 0.3f;
    if (distance_to_path(w.path, o.pos.x, o.pos.z, keep_off + 1) < keep_off) continue;

    // Keep a gap between objects so the player can always walk between them.
    Box wide = b;
    wide.min = wide.min - Vec3{gap, 0, gap};
    wide.max = wide.max + Vec3{gap, 0, gap};
    const bool collides = std::any_of(w.objects.begin(), w.objects.end(),
                                      [&](const Object& other) { return overlaps(wide, footprint(other)); });
    if (!collides) {
      w.objects.push_back(o);
      ++nature;
    }
  }
}

// What bodies collide with during a tick: object boxes (computed once per
// tick), the ground, and the lake, which walkers cannot enter and swimmers
// cannot leave.
struct Surroundings {
  std::span<const Box> boxes;
  const Lake& lake;
  const Heightmap& ground;
  // Broad phase: box indices per cell of a grid over the world, so that a
  // body only tests the objects around it.
  static constexpr float kCell = 6.0f;
  static constexpr int kCells = static_cast<int>(2 * rules::kWorldHalfSize / kCell) + 2;
  // Box indices of cell c: cell_items[cell_start[c] .. cell_start[c + 1]),
  // in increasing order (two allocations per tick, not one per cell).
  std::vector<uint32_t> cell_start;
  std::vector<uint16_t> cell_items;

  static int cell_of(float v) {
    return std::clamp(static_cast<int>((v + rules::kWorldHalfSize) / kCell), 0, kCells - 1);
  }

  template <typename F>
  void for_cells(const Box& b, F f) const {
    for (int cx = cell_of(b.min.x); cx <= cell_of(b.max.x); ++cx) {
      for (int cz = cell_of(b.min.z); cz <= cell_of(b.max.z); ++cz) f(static_cast<size_t>(cx * kCells + cz));
    }
  }

  void build() {
    // Counting sort: count per cell, then fill.
    cell_start.assign(static_cast<size_t>(kCells * kCells) + 1, 0);
    for (const Box& b : boxes) for_cells(b, [&](size_t c) { ++cell_start[c + 1]; });
    for (size_t c = 1; c < cell_start.size(); ++c) cell_start[c] += cell_start[c - 1];
    cell_items.resize(cell_start.back());
    std::vector<uint32_t> fill(cell_start.begin(), cell_start.end() - 1);
    for (size_t i = 0; i < boxes.size(); ++i) {
      for_cells(boxes[i], [&](size_t c) { cell_items[fill[c]++] = static_cast<uint16_t>(i); });
    }
  }

  // Calls f(box) for every object box that may touch `area` (a box may be
  // visited more than once; callers do not mind).
  template <typename F>
  void near(const Box& area, F f) const {
    for_cells(area, [&](size_t c) {
      for (uint32_t k = cell_start[c]; k < cell_start[c + 1]; ++k) f(boxes[cell_items[k]]);
    });
  }

  bool any_overlap(const Box& area) const {
    bool hit = false;
    near(area, [&](const Box& b) { hit = hit || overlaps(area, b); });
    return hit;
  }
};

enum class Medium { Land, Water };

bool wrong_medium(const Surroundings& s, const Body& b, const BodyShape& shape, Medium medium) {
  if (medium == Medium::Water) return !in_lake(s.lake, b.pos.x, b.pos.z, -(shape.half_width + 0.6f));
  return in_lake(s.lake, b.pos.x, b.pos.z, shape.half_width * 0.5f);
}

constexpr float kStepUp = 0.01f;     // rise always allowed in one step (rounding)
constexpr float kGroundSnap = 0.25f; // walking downhill: stay on the ground if it is this close

// Moves a body along one axis, then pushes it out of any object it entered.
void move_axis(const Surroundings& s, Body& b, const BodyShape& shape, int axis, float delta,
               Medium medium = Medium::Land) {
  if (delta == 0) return;  // not moving: nothing to resolve
  float* coord = axis == 0 ? &b.pos.x : &b.pos.z;
  const float start = *coord;
  *coord += delta;
  const float h = shape.half_width;
  Box area = body_box(b, shape);
  area.min.x = std::min(area.min.x, area.min.x - (axis == 0 ? delta : 0));
  area.max.x = std::max(area.max.x, area.max.x - (axis == 0 ? delta : 0));
  area.min.z = std::min(area.min.z, area.min.z - (axis == 2 ? delta : 0));
  area.max.z = std::max(area.max.z, area.max.z - (axis == 2 ? delta : 0));
  s.near(area, [&](const Box& ob) {
    if (!overlaps(body_box(b, shape), ob)) return;
    const float lo = axis == 0 ? ob.min.x : ob.min.z;
    const float hi = axis == 0 ? ob.max.x : ob.max.z;
    *coord = delta > 0 ? lo - h : hi + h;
  });
  const float limit = rules::kWorldHalfSize - h;
  *coord = std::clamp(*coord, -limit, limit);
  // Walkers cannot go up a slope steeper than kMaxWalkSlope (but can jump
  // onto it: the ground then is below their feet).
  const bool too_steep = medium == Medium::Land &&
                         s.ground.height(b.pos.x, b.pos.z) - b.pos.y >
                             std::fabs(*coord - start) * rules::kMaxWalkSlope + kStepUp;
  // Pushed out of one object into another (a gap narrower than the body):
  // do not move along this axis at all.
  if (too_steep || s.any_overlap(body_box(b, shape)) || wrong_medium(s, b, shape, medium)) *coord = start;
}

void move_vertical(const Surroundings& s, Body& b, const BodyShape& shape) {  // walkers only
  const float previous_y = b.pos.y;
  const bool was_on_ground = b.on_ground;
  const float ground = s.ground.height(b.pos.x, b.pos.z);
  b.pos.y += b.velocity_y * rules::kDt;
  b.on_ground = false;
  // On the ground, or walking down a slope (not jumping): follow the ground.
  if (b.pos.y <= ground || (was_on_ground && b.velocity_y <= 0 && b.pos.y - ground < kGroundSnap)) {
    b.pos.y = ground;
    b.velocity_y = 0;
    b.on_ground = true;
  }
  constexpr float kEpsilon = 1e-4f;
  s.near(body_box(b, shape), [&](const Box& ob) {
    if (!overlaps(body_box(b, shape), ob)) return;
    if (previous_y >= ob.max.y - kEpsilon) {                    // landing on top
      b.pos.y = ob.max.y;
      b.on_ground = true;
    } else if (previous_y + shape.height <= ob.min.y + kEpsilon) {  // head hits the underside
      b.pos.y = ob.min.y - shape.height;
    } else {
      // Already touching it from the side (squeezed between two objects):
      // stay at the same height rather than being pushed through anything.
      b.pos.y = previous_y;
      b.on_ground = b.on_ground || previous_y <= ground;
    }
    b.velocity_y = 0;
  });
}

// One tick of walking and jumping. (dir_x, dir_z) has a length of at most 1.
void move_body(const Surroundings& s, Body& b, const BodyShape& shape, float dir_x, float dir_z,
               float speed, bool jump, float jump_speed) {
  const float len2 = dir_x * dir_x + dir_z * dir_z;
  if (len2 > 0.0f) {
    const float len = std::sqrt(len2);
    b.facing_x = dir_x / len;
    b.facing_z = dir_z / len;
  }
  if (jump && b.on_ground) b.velocity_y = jump_speed;
  b.velocity_y = std::max(b.velocity_y - rules::kGravity * rules::kDt, -rules::kMaxFallSpeed);

  const float x0 = b.pos.x, z0 = b.pos.z;
  move_axis(s, b, shape, 0, dir_x * speed * rules::kDt);
  move_axis(s, b, shape, 2, dir_z * speed * rules::kDt);
  move_vertical(s, b, shape);
  const float dx = b.pos.x - x0, dz = b.pos.z - z0;
  b.speed = std::sqrt(dx * dx + dz * dz) / rules::kDt;
}

// ---- Creatures ---------------------------------------------------------------

// Shapes match the drawn size (rig scale in src/render/characters.cpp).
constexpr SpeciesInfo kSpecies[] = {
    {{0.23f, 0.58f}, 4.0f, 6.5f, 6.5f},  // Companion
    {{0.45f, 0.72f}, 2.6f, 2.6f, 4.8f},  // Slime: moves only while hopping
    {{0.36f, 0.92f}, 1.5f, 1.5f, 4.2f},  // Mushroom
    {{0.28f, 0.70f}, 2.3f, 6.0f, 4.2f},  // Bird
    {{0.25f, 1.10f}, 1.4f, 1.4f, 0.0f},  // Villager
    {{0.18f, 0.40f}, 0.0f, 6.0f, 0.0f},  // Crow: flies at run_speed
    {{0.30f, 0.30f}, 1.3f, 1.3f, 5.0f},  // Fish: swims, jumps out of the water
};

Vec2 clamp_to_world(Vec2 p, float margin) {
  const float limit = rules::kWorldHalfSize - margin;
  return {std::clamp(p.x, -limit, limit), std::clamp(p.z, -limit, limit)};
}

bool inside_any_object(const World& w, const Body& b, const BodyShape& shape) {
  return std::any_of(w.objects.begin(), w.objects.end(),
                     [&](const Object& o) { return overlaps(body_box(b, shape), object_box(o)); });
}

// Unit direction from (x, z) towards the target, and the distance to it.
float direction_to(float x, float z, Vec2 target, float& dir_x, float& dir_z) {
  const float dx = target.x - x, dz = target.z - z;
  const float d = std::sqrt(dx * dx + dz * dz);
  dir_x = d > 0 ? dx / d : 0;
  dir_z = d > 0 ? dz / d : 0;
  return d;
}

void step_companion(World& w, const Surroundings& boxes, Creature& c) {
  const SpeciesInfo& info = species_info(c.species);
  Body& b = c.body;
  const Body& p = w.player;

  float to_player_x, to_player_z;
  const float player_distance = direction_to(b.pos.x, b.pos.z, {p.pos.x, p.pos.z}, to_player_x, to_player_z);
  if (player_distance > rules::kCompanionTeleportDistance) {
    // Lost behind: catch up instantly, just behind the player.
    b.pos = {p.pos.x - p.facing_x * rules::kCompanionFollowDistance, p.pos.y,
             p.pos.z - p.facing_z * rules::kCompanionFollowDistance};
    b.pos.y = std::max(b.pos.y, w.ground.height(b.pos.x, b.pos.z));
    if (inside_any_object(w, b, info.shape)) b.pos = p.pos;
    b.velocity_y = 0;
    c.timer = 0;
  }

  // Follow a point behind the player.
  c.target = clamp_to_world({p.pos.x - p.facing_x * rules::kCompanionFollowDistance,
                             p.pos.z - p.facing_z * rules::kCompanionFollowDistance}, info.shape.half_width);
  float dir_x, dir_z;
  const float d = direction_to(b.pos.x, b.pos.z, c.target, dir_x, dir_z);
  float speed = 0;
  if (d > 0.6f) {
    c.mind = Mind::Follow;
    speed = d > 4.0f ? std::max(info.run_speed, rules::kCompanionMinRunSpeed) : std::max(info.walk_speed, 3.0f);
  } else {
    c.mind = Mind::Idle;
    dir_x = dir_z = 0;
    if (player_distance > 0.1f) {  // look at the player
      b.facing_x = to_player_x;
      b.facing_z = to_player_z;
    }
  }
  // Jump when stuck against something, or to follow the player up onto
  // something (heights above the ground: a slope is not a reason to jump).
  const float player_up = p.pos.y - w.ground.height(p.pos.x, p.pos.z);
  const float own_up = b.pos.y - w.ground.height(b.pos.x, b.pos.z);
  bool jump = b.on_ground && (c.timer >= 8 || (player_up > own_up + 0.4f && player_distance < 2.5f));
  if (jump) c.timer = 0;
  if (c.species == Species::Slime && speed > 0) {  // a slime follows by hopping
    jump = jump || b.on_ground;
    if (b.on_ground) speed = 0;
  }
  move_body(boxes, b, info.shape, dir_x, dir_z, speed, jump, info.jump_speed);
  c.timer = (speed > 0 && b.on_ground && b.speed < 0.3f * speed) ? static_cast<uint16_t>(c.timer + 1) : 0;
}

void step_wild(World& w, const Surroundings& boxes, Creature& c) {
  const SpeciesInfo& info = species_info(c.species);
  Body& b = c.body;
  const Body& p = w.player;

  float away_x, away_z;
  const float player_distance = direction_to(p.pos.x, p.pos.z, {b.pos.x, b.pos.z}, away_x, away_z);
  if (c.species == Species::Bird && player_distance < rules::kBirdFleeDistance && c.mind != Mind::Flee) {
    c.mind = Mind::Flee;
    c.timer = 2 * rules::kTicksPerSecond;
    c.target = clamp_to_world({b.pos.x + away_x * 6, b.pos.z + away_z * 6}, 1.0f);
  }

  float dir_x = 0, dir_z = 0, speed = 0;
  bool jump = false;
  if (c.mind == Mind::Idle) {
    if (c.timer > 0) {
      --c.timer;
      // Birds hop on the spot from time to time.
      if (c.species == Species::Bird && b.on_ground && w.rng.below(200) == 0) jump = true;
    } else {
      c.mind = Mind::Wander;
      c.timer = 8 * rules::kTicksPerSecond;  // give up after 8 s
      c.target = clamp_to_world({c.home.x + random_range(w.rng, -rules::kWanderRadius, rules::kWanderRadius),
                                 c.home.z + random_range(w.rng, -rules::kWanderRadius, rules::kWanderRadius)},
                                1.0f);
    }
  } else {
    const float d = direction_to(b.pos.x, b.pos.z, c.target, dir_x, dir_z);
    if (d < 0.4f || c.timer == 0) {
      c.mind = Mind::Idle;
      c.timer = static_cast<uint16_t>(rules::kTicksPerSecond + w.rng.below(3 * rules::kTicksPerSecond));
      dir_x = dir_z = 0;
    } else {
      --c.timer;
      speed = c.mind == Mind::Flee ? info.run_speed : info.walk_speed;
    }
  }

  if (c.species == Species::Slime && speed > 0) {
    // Slimes hop: they jump as soon as they land and only move in the air.
    jump = b.on_ground;
    if (b.on_ground) speed = 0;
  }
  move_body(boxes, b, info.shape, dir_x, dir_z, speed, jump, info.jump_speed);
  if (speed > 0 && b.speed > 0) {  // keep facing the way it goes
    b.facing_x = dir_x;
    b.facing_z = dir_z;
  }
}

void step_villager(World& w, const Surroundings& s, Creature& c) {
  const SpeciesInfo& info = species_info(c.species);
  Body& b = c.body;
  const Body& p = w.player;
  float to_x, to_z;
  const float player_distance = direction_to(b.pos.x, b.pos.z, {p.pos.x, p.pos.z}, to_x, to_z);

  float dir_x = 0, dir_z = 0, speed = 0;
  if (player_distance < rules::kVillagerGreetDistance && std::fabs(p.pos.y - b.pos.y) < 1.5f) {
    // Stop, turn to the player and wave.
    c.mind = Mind::Greet;
    b.facing_x = to_x;
    b.facing_z = to_z;
    c.timer = static_cast<uint16_t>(rules::kTicksPerSecond / 2);
  } else if (c.mind == Mind::Greet || c.mind == Mind::Idle) {
    if (c.timer > 0) {
      --c.timer;
    } else {
      c.mind = Mind::Wander;
      c.timer = 10 * rules::kTicksPerSecond;
      const float r = rules::kVillageRadius - 4.0f;
      c.target = {c.home.x + random_range(w.rng, -r, r), c.home.z + random_range(w.rng, -r, r)};
    }
  } else {
    const float d = direction_to(b.pos.x, b.pos.z, c.target, dir_x, dir_z);
    if (d < 0.4f || c.timer == 0) {
      c.mind = Mind::Idle;
      c.timer = static_cast<uint16_t>(2 * rules::kTicksPerSecond + w.rng.below(4 * rules::kTicksPerSecond));
      dir_x = dir_z = 0;
    } else {
      --c.timer;
      speed = info.walk_speed;
    }
  }
  move_body(s, b, info.shape, dir_x, dir_z, speed, false, info.jump_speed);
}

void step_crow(World& w, const std::vector<Vec3>& perches, Creature& c) {
  const SpeciesInfo& info = species_info(c.species);
  Body& b = c.body;
  const Body& p = w.player;
  const float dx = p.pos.x - b.pos.x, dz = p.pos.z - b.pos.z;
  const bool player_near = dx * dx + dz * dz < rules::kCrowFleeDistance * rules::kCrowFleeDistance &&
                           std::fabs(p.pos.y - b.pos.y) < 7.0f;

  auto take_off = [&](bool flee) {
    if (perches.size() < 2) return;
    // Another perch; when fleeing, one far from the player.
    for (int attempt = 0; attempt < 8; ++attempt) {
      const Vec3 t = perches[w.rng.below(static_cast<uint32_t>(perches.size()))];
      const float tx = t.x - p.pos.x, tz = t.z - p.pos.z;
      const float mx = t.x - b.pos.x, mz = t.z - b.pos.z;
      if (mx * mx + mz * mz < 1.0f) continue;  // the same perch
      if (flee && tx * tx + tz * tz < 64.0f && attempt < 7) continue;
      c.target = {t.x, t.z};
      c.target_y = t.y;
      c.mind = Mind::Fly;
      b.on_ground = false;
      return;
    }
  };

  b.speed = 0;
  if (c.mind != Mind::Fly) {
    if (player_near) {
      take_off(true);
    } else if (c.timer > 0) {
      --c.timer;
    } else {
      take_off(false);
    }
    if (c.mind != Mind::Fly) return;
  }

  // Flying: cruise above the target, then glide down onto it.
  float dir_x, dir_z;
  const float d = direction_to(b.pos.x, b.pos.z, c.target, dir_x, dir_z);
  const float step = info.run_speed * rules::kDt;
  const float horizontal = std::min(step, d);
  b.pos.x += dir_x * horizontal;
  b.pos.z += dir_z * horizontal;
  const float wanted_y = d > 2.5f ? c.target_y + 3.0f : c.target_y;
  const float climb = 3.0f * rules::kDt;
  b.pos.y += std::clamp(wanted_y - b.pos.y, -climb, climb);
  if (d > 0) {
    b.facing_x = dir_x;
    b.facing_z = dir_z;
  }
  b.speed = horizontal / rules::kDt;
  if (d < 0.05f && std::fabs(b.pos.y - c.target_y) < 0.05f) {  // landed
    b.pos = {c.target.x, c.target_y, c.target.z};
    b.on_ground = true;
    c.mind = Mind::Idle;
    c.timer = static_cast<uint16_t>(4 * rules::kTicksPerSecond + w.rng.below(8 * rules::kTicksPerSecond));
  }
}

void step_fish(World& w, const Surroundings& s, Creature& c) {
  const SpeciesInfo& info = species_info(c.species);
  Body& b = c.body;
  float dir_x = 0, dir_z = 0, speed = 0;
  if (c.mind == Mind::Idle) {
    if (c.timer > 0) {
      --c.timer;
    } else {
      c.mind = Mind::Wander;
      c.timer = 8 * rules::kTicksPerSecond;
      for (int attempt = 0; attempt < 8; ++attempt) {
        const Lake& l = s.lake;
        c.target = {l.center.x + random_range(w.rng, -l.radius_x, l.radius_x),
                    l.center.z + random_range(w.rng, -l.radius_z, l.radius_z)};
        if (in_lake(l, c.target.x, c.target.z, -1.5f)) break;
      }
    }
  } else {
    const float d = direction_to(b.pos.x, b.pos.z, c.target, dir_x, dir_z);
    if (d < 0.4f || c.timer == 0) {
      c.mind = Mind::Idle;
      c.timer = static_cast<uint16_t>(rules::kTicksPerSecond + w.rng.below(3 * rules::kTicksPerSecond));
    } else {
      --c.timer;
      speed = info.walk_speed;
    }
  }
  // Swim on the surface; now and then, jump out of the water.
  if (b.on_ground && w.rng.below(500) == 0) {
    b.velocity_y = info.jump_speed;
    b.on_ground = false;
  }
  if (speed > 0) {
    b.facing_x = dir_x;
    b.facing_z = dir_z;
  }
  const float x0 = b.pos.x, z0 = b.pos.z;
  move_axis(s, b, info.shape, 0, dir_x * speed * rules::kDt, Medium::Water);
  move_axis(s, b, info.shape, 2, dir_z * speed * rules::kDt, Medium::Water);
  const float mx = b.pos.x - x0, mz = b.pos.z - z0;
  b.speed = std::sqrt(mx * mx + mz * mz) / rules::kDt;
  if (!b.on_ground) {
    b.velocity_y -= rules::kGravity * rules::kDt;
    b.pos.y += b.velocity_y * rules::kDt;
    if (b.pos.y <= rules::kSwimLevel) {
      b.pos.y = rules::kSwimLevel;
      b.velocity_y = 0;
      b.on_ground = true;  // back in the water
    }
  }
}

}  // namespace

Box body_box(const Body& b, const BodyShape& shape) {
  const float h = shape.half_width;
  return {{b.pos.x - h, b.pos.y, b.pos.z - h}, {b.pos.x + h, b.pos.y + shape.height, b.pos.z + h}};
}

Box player_box(const Player& p) { return body_box(p, kPlayerShape); }

const SpeciesInfo& species_info(Species species) {
  const auto i = static_cast<size_t>(species);
  return kSpecies[i < static_cast<size_t>(Species::Count) ? i : 0];
}

void spawn_creatures(World& w) {
  Rng rng(w.seed ^ 0x4352454154555245ULL, 11);  // own stream: objects stay the same
  w.creatures.clear();

  Creature companion;
  companion.species = Species::Companion;
  companion.body.pos = {0.8f, w.ground.height(0.8f, 1.2f), 1.2f};  // just behind the player, on the right
  companion.home = {0.8f, 1.2f};
  w.creatures.push_back(companion);

  const float limit = rules::kWorldHalfSize - 3.0f;
  for (int attempt = 0; attempt < 5000 && w.creatures.size() < 1 + rules::kWildMonsters; ++attempt) {
    Creature c;
    c.species = static_cast<Species>(1 + rng.below(3));
    c.body.pos = {random_range(rng, -limit, limit), 0, random_range(rng, -limit, limit)};
    c.body.pos.y = w.ground.height(c.body.pos.x, c.body.pos.z);
    c.variant = static_cast<uint8_t>(rng.below(256));
    const SinCos sc = sincos16(static_cast<Angle16>(rng.below(65536)));
    c.body.facing_x = sc.sin;
    c.body.facing_z = sc.cos;
    const float d2 = c.body.pos.x * c.body.pos.x + c.body.pos.z * c.body.pos.z;
    if (d2 < rules::kMonsterSpawnMinDistance * rules::kMonsterSpawnMinDistance) continue;
    if (inside_any_object(w, c.body, species_info(c.species).shape)) continue;
    if (in_village(w.village, c.body.pos.x, c.body.pos.z, 2.0f) ||
        in_lake(w.village.lake, c.body.pos.x, c.body.pos.z, 2.0f)) {
      continue;
    }
    c.home = {c.body.pos.x, c.body.pos.z};
    c.timer = static_cast<uint16_t>(rng.below(3 * rules::kTicksPerSecond));
    w.creatures.push_back(c);
  }

  const bool has_village = w.village.lake.radius_x > 0;
  if (!has_village) return;

  // Villagers, around the village square.
  const Vec2 center = w.village.center;
  for (uint32_t n = 0, attempt = 0; n < rules::kVillagers && attempt < 2000; ++attempt) {
    Creature c;
    c.species = Species::Villager;
    const float r = rules::kVillageRadius - 4.0f;
    c.body.pos = {center.x + random_range(rng, -r, r), 0, center.z + random_range(rng, -r, r)};
    c.body.pos.y = w.ground.height(c.body.pos.x, c.body.pos.z);
    c.variant = static_cast<uint8_t>(rng.below(256));
    c.home = center;
    c.timer = static_cast<uint16_t>(rng.below(4 * rules::kTicksPerSecond));
    if (inside_any_object(w, c.body, species_info(c.species).shape)) continue;
    if (in_lake(w.village.lake, c.body.pos.x, c.body.pos.z, 1.0f)) continue;
    w.creatures.push_back(c);
    ++n;
  }

  // Crows, perched on roofs.
  const std::vector<Vec3> perches = crow_perches(w);
  for (uint32_t n = 0; n < rules::kCrows && n < perches.size(); ++n) {
    Creature c;
    c.species = Species::Crow;
    const Vec3 perch = perches[(n * 3 + rng.below(3)) % perches.size()];
    c.body.pos = perch;
    c.target = {perch.x, perch.z};
    c.target_y = perch.y;
    c.home = {perch.x, perch.z};  // not used to move: where it was born (its variants)
    c.variant = static_cast<uint8_t>(rng.below(256));
    c.timer = static_cast<uint16_t>(3 * rules::kTicksPerSecond + rng.below(8 * rules::kTicksPerSecond));
    w.creatures.push_back(c);
  }

  // Fish, in the lake.
  const Lake& lake = w.village.lake;
  for (uint32_t n = 0, attempt = 0; n < rules::kFish && attempt < 1000; ++attempt) {
    Creature c;
    c.species = Species::Fish;
    c.body.pos = {lake.center.x + random_range(rng, -lake.radius_x, lake.radius_x), rules::kSwimLevel,
                  lake.center.z + random_range(rng, -lake.radius_z, lake.radius_z)};
    if (!in_lake(lake, c.body.pos.x, c.body.pos.z, -1.5f)) continue;
    c.home = {c.body.pos.x, c.body.pos.z};
    c.variant = static_cast<uint8_t>(rng.below(256));
    c.timer = static_cast<uint16_t>(rng.below(3 * rules::kTicksPerSecond));
    w.creatures.push_back(c);
    ++n;
  }
}

namespace {

Vec2 local_to_world(const Object& o, float lx, float lz) {
  // Same rotation as the renderer: yaw turns +X towards -Z.
  const SinCos sc = sincos16(o.yaw);
  return {o.pos.x + lx * sc.cos + lz * sc.sin, o.pos.z - lx * sc.sin + lz * sc.cos};
}

// Front direction (where the door is) of a quarter yaw: local -Z.
Vec2 front_of(Angle16 yaw) {
  const SinCos sc = sincos16(yaw);
  return {-sc.sin, -sc.cos};
}

bool can_place(const World& w, const Object& o, float gap, bool keep_off_path) {
  Box b = object_box(o);
  for (float x : {b.min.x, b.max.x}) {
    for (float z : {b.min.z, b.max.z}) {
      if (in_lake(w.village.lake, x, z, 1.0f)) return false;
      if (std::fabs(x) > rules::kWorldHalfSize - 1 || std::fabs(z) > rules::kWorldHalfSize - 1) return false;
    }
  }
  if (keep_off_path) {
    const float reach = std::max(b.max.x - b.min.x, b.max.z - b.min.z) * 0.5f;
    if (distance_to_path(w.path, o.pos.x, o.pos.z) < rules::kPathHalfWidth + reach + 0.4f) return false;
  }
  b.min = b.min - Vec3{gap, 0, gap};
  b.max = b.max + Vec3{gap, 0, gap};
  return std::none_of(w.objects.begin(), w.objects.end(),
                      [&](const Object& other) { return overlaps(b, object_box(other)); });
}

Object village_object(ObjectKind kind, Vec2 pos, Angle16 yaw, uint8_t variant = 0) {
  Object o;
  o.kind = kind;
  o.pos = {pos.x, 0, pos.z};
  o.size = {1, 1, 1};
  o.yaw = yaw;
  o.variant = variant;
  return o;
}

}  // namespace

void build_village(World& w) {
  const VillageLayout& v = w.village;
  if (v.lake.radius_x <= 0) return;
  Rng rng(w.seed ^ 0x56494C4C41474521ULL, 13);  // own stream
  std::erase_if(w.objects, [&](const Object& o) {
    return in_village(v, o.pos.x, o.pos.z, 2.0f) || in_lake(v.lake, o.pos.x, o.pos.z, 2.5f);
  });
  const Vec2 c = v.center;
  const float lake_side = v.lake.center.x > c.x ? 1.0f : -1.0f;

  // The well, on the square, away from the lake.
  w.objects.push_back(village_object(ObjectKind::Well, {c.x - lake_side * 4.0f, c.z}, 0));

  // Six houses around the square, doors towards it.
  ObjectKind types[] = {ObjectKind::HouseCottage, ObjectKind::HouseChimney, ObjectKind::HouseHut,
                        ObjectKind::HouseCottage, ObjectKind::HouseChimney, ObjectKind::HouseHut};
  for (int i = 5; i > 0; --i) std::swap(types[i], types[rng.below(static_cast<uint32_t>(i + 1))]);
  std::vector<Object> houses;
  // Candidate places around the square, away from the path, which runs
  // north-south through the middle (angles in degrees from +X towards +Z).
  constexpr int kSlots[] = {0, 40, 140, 180, 220, 320, 20, 160, 200, 340, 60, 120, 240, 300};
  int next_type = 0;
  for (int degrees : kSlots) {
    if (houses.size() == 6) break;
    const auto angle = static_cast<Angle16>((degrees * 65536 / 360 + static_cast<int>(rng.below(2185)) - 1092) & 0xFFFF);
    const SinCos sc = sincos16(angle);
    for (float radius : {9.5f + random_range(rng, 0, 1.5f), 12.5f}) {
      const Vec2 pos{c.x + sc.cos * radius, c.z + sc.sin * radius};
      // The quarter turn whose door faces the square best.
      Angle16 best = 0;
      float best_dot = -2;
      for (Angle16 yaw : {Angle16{0}, Angle16{16384}, Angle16{32768}, Angle16{49152}}) {
        const Vec2 f = front_of(yaw);
        const float d = f.x * (c.x - pos.x) + f.z * (c.z - pos.z);
        if (d > best_dot) {
          best_dot = d;
          best = yaw;
        }
      }
      const Object house = village_object(types[next_type], pos, best, static_cast<uint8_t>(rng.below(256)));
      if (can_place(w, house, 1.0f, true)) {
        w.objects.push_back(house);
        houses.push_back(house);
        ++next_type;
        break;
      }
    }
  }

  // A fenced yard behind the cottages and chimney houses, a barrel by some doors.
  for (const Object& h : houses) {
    const Box b = object_box(h);
    const float depth = (h.yaw >> 14) & 1 ? (b.max.x - b.min.x) / 2 : (b.max.z - b.min.z) / 2;
    if (h.kind != ObjectKind::HouseHut) {
      for (float along : {-2.0f, 0.0f, 2.0f}) {
        const Vec2 pos = local_to_world(h, along, depth + 2.2f);  // behind: local +Z
        const Object fence = village_object(ObjectKind::Fence, pos, h.yaw);
        if (can_place(w, fence, 0.05f, true)) w.objects.push_back(fence);
      }
    }
    if (rng.below(2) == 0) {
      const Vec2 pos = local_to_world(h, 1.6f, -(depth + 0.8f));  // beside the door
      const Object barrel = village_object(ObjectKind::Barrel, pos, static_cast<Angle16>(rng.below(65536)));
      if (can_place(w, barrel, 0.3f, true)) w.objects.push_back(barrel);
    }
  }

  // Lanterns along the path through the village, a signpost at each end.
  int lamp = 0;
  for (size_t i = 1; i + 1 < w.path.points.size(); i += 8) {
    const Vec2 p = w.path.points[i];
    if (std::fabs(p.z - c.z) > rules::kVillageRadius - 2) continue;
    const Vec2 q = w.path.points[i + 1];
    float tx = q.x - p.x, tz = q.z - p.z;
    const float len = std::sqrt(tx * tx + tz * tz);
    tx /= len;
    tz /= len;
    const float side = (lamp++ % 2) ? 1.0f : -1.0f;
    const float offset = rules::kPathHalfWidth + 0.8f;
    const Object o = village_object(ObjectKind::Lamp, {p.x - tz * offset * side, p.z + tx * offset * side}, 0);
    if (can_place(w, o, 0.3f, false)) w.objects.push_back(o);
  }
  for (float dz : {rules::kVillageRadius + 1.0f, -(rules::kVillageRadius + 1.0f)}) {
    const Vec2* nearest = &w.path.points.front();
    for (const Vec2& p : w.path.points) {
      if (std::fabs(p.z - (c.z + dz)) < std::fabs(nearest->z - (c.z + dz))) nearest = &p;
    }
    const Object sign = village_object(ObjectKind::Sign, {nearest->x + rules::kPathHalfWidth + 0.9f, nearest->z},
                                       dz > 0 ? Angle16{32768} : Angle16{0});
    if (can_place(w, sign, 0.3f, false)) w.objects.push_back(sign);
  }
}

void settle_objects(World& w) {
  constexpr float kSink = 0.05f;
  if (w.ground.empty()) return;  // flat: objects stay where they are
  for (Object& o : w.objects) {
    const Box b = object_box(o);
    const float center = w.ground.height(o.pos.x, o.pos.z);
    float lowest = center;
    for (float x : {b.min.x, b.max.x}) {
      for (float z : {b.min.z, b.max.z}) lowest = std::min(lowest, w.ground.height(x, z));
    }
    // A low object on a slope (a flat rock) would vanish into the ground:
    // sink it at most 40 % of its height, an edge may then float a little.
    o.pos.y = std::max(lowest - kSink, center - kSink - 0.4f * (b.max.y - b.min.y));
  }
}

void build_ruins(World& w) {
  const RuinsLayout& r = w.ruins;
  if (r.radius <= 0) return;
  // Own random stream. Every draw goes into a variable first: the order in
  // which a call's arguments are evaluated differs between compilers (GCC,
  // Clang), and the layout must be the same natively and in WebAssembly.
  Rng rng(w.seed ^ 0x5255494E53212121ULL, 17);
  auto any_yaw16 = [&] { return static_cast<Angle16>(rng.below(65536)); };
  auto quarter_yaw16 = [&] { return static_cast<Angle16>(rng.below(4) << 14); };
  auto variant8 = [&] { return static_cast<uint8_t>(rng.below(256)); };
  std::erase_if(w.objects, [&](const Object& o) { return in_ruins(r, o.pos.x, o.pos.z, 2.0f); });
  const Vec2 c = r.center;
  auto place = [&](ObjectKind kind, Vec2 pos, Angle16 yaw, Vec3 size, uint8_t variant, float gap) {
    Object o = village_object(kind, pos, yaw, variant);
    o.size = size;
    if (!can_place(w, o, gap, true)) return false;
    w.objects.push_back(o);
    return true;
  };

  // The tower, its door towards the path.
  const float to_path = w.path.points.empty() ? -1.0f : (distance_to_path(w.path, c.x - 1, c.z) <
                                                         distance_to_path(w.path, c.x + 1, c.z) ? -1.0f : 1.0f);
  const Angle16 door_yaw = to_path < 0 ? Angle16{16384} : Angle16{49152};  // front (-Z) turned to -X / +X
  place(ObjectKind::Tower, c, door_yaw, {1, 1, 1}, variant8(), 0.5f);

  // What is left of the square enclosure: wall pieces, an opening towards the path.
  constexpr float kHalf = 9.0f;
  for (int side = 0; side < 4; ++side) {
    for (int k = 0; k < 6; ++k) {
      const float along = -7.5f + 3.0f * static_cast<float>(k);
      const bool x_side = side < 2;  // the wall runs along Z, at x = ±kHalf
      const float sign = side % 2 ? 1.0f : -1.0f;
      const Vec2 pos = x_side ? Vec2{c.x + sign * kHalf, c.z + along} : Vec2{c.x + along, c.z + sign * kHalf};
      const bool entrance = x_side && sign == to_path && (k == 2 || k == 3);
      if (entrance || rng.below(100) < 40) continue;  // fallen long ago
      const Angle16 yaw = x_side ? Angle16{16384} : Angle16{0};
      place(ObjectKind::RuinWall, pos, yaw, {1, 1, 1}, variant8(), 0.0f);
    }
  }

  // A colonnade from the entrance towards the path, some columns fallen.
  for (int row = 0; row < 3; ++row) {
    for (float across : {-2.4f, 2.4f}) {
      const Vec2 pos{c.x + to_path * (11.0f + 2.8f * static_cast<float>(row)), c.z + across};
      if (rng.below(100) < 25) {
        const Angle16 yaw = quarter_yaw16();
        const uint8_t variant = variant8();
        place(ObjectKind::FallenPillar, {pos.x + to_path * 0.5f, pos.z + across * 0.3f}, yaw, {1, 1, 1}, variant, 0.2f);
      } else {
        const Angle16 yaw = any_yaw16();
        const float height = random_range(rng, 1.2f, 3.4f);
        const uint8_t variant = variant8();
        place(ObjectKind::Pillar, pos, yaw, {1, height, 1}, variant, 0.2f);
      }
    }
  }

  // Four rune stones in the courtyard, still glowing.
  for (int k = 0; k < 4; ++k) {
    const SinCos sc = sincos16(static_cast<Angle16>(8192 + k * 16384));
    place(ObjectKind::RuneStone, {c.x + sc.cos * 5.6f, c.z + sc.sin * 5.6f}, static_cast<Angle16>((k % 2) << 14),
          {1, 1, 1}, variant8(), 0.3f);
  }

  // Fallen columns and rubble scattered around.
  for (int n = 0, attempt = 0; n < 9 && attempt < 200; ++attempt) {
    const SinCos sc = sincos16(static_cast<Angle16>(rng.below(65536)));
    const float d = random_range(rng, 3.5f, r.radius - 1);
    const Vec2 pos{c.x + sc.cos * d, c.z + sc.sin * d};
    const bool rubble = n % 3 != 0;
    const Angle16 yaw = rubble ? any_yaw16() : quarter_yaw16();
    const uint8_t variant = variant8();
    const bool ok = place(rubble ? ObjectKind::Rubble : ObjectKind::FallenPillar, pos, yaw, {1, 1, 1}, variant, 0.4f);
    n += ok ? 1 : 0;
  }
}

Zone zone_at(const World& w, float x, float z) {
  if (in_lake(w.village.lake, x, z, 1.5f)) return Zone::Lake;
  if (w.village.lake.radius_x > 0 && in_village(w.village, x, z, 2.0f)) return Zone::Village;
  if (in_ruins(w.ruins, x, z, 3.0f)) return Zone::Ruins;
  return Zone::Meadow;
}

WizardStats wizard_stats(const Wizard& wiz) {
  const auto l = static_cast<uint16_t>(std::max<uint16_t>(wiz.level, 1) - 1);  // levels gained
  return {static_cast<uint16_t>(30 + 6 * l),  static_cast<uint16_t>(20 + 5 * l), static_cast<uint16_t>(6 + 2 * l),
          static_cast<uint16_t>(5 + 2 * l),   static_cast<uint16_t>(5 + l),     static_cast<uint16_t>(5 + l),
          100u * (l + 1u)};
}

const MonsterStats& monster_stats(Species species) {
  //                               hp atk def spd mag element
  static constexpr MonsterStats kStats[] = {
      {32, 9, 6, 11, 8, Element::Fire, true},     // Companion: Pompon, fox of embers
      {40, 5, 9, 4, 6, Element::Water, true},     // Slime
      {34, 6, 8, 3, 9, Element::Nature, true},    // Mushroom
      {22, 7, 4, 13, 5, Element::Air, true},      // Bird
      {0, 0, 0, 0, 0, Element::Light, false},     // Villager: not a monster
      {24, 8, 5, 12, 9, Element::Shadow, true, Element::Air},  // Crow
      {28, 6, 6, 8, 7, Element::Water, true},     // Fish
  };
  static_assert(std::size(kStats) == static_cast<size_t>(Species::Count));
  const auto i = static_cast<size_t>(species);
  return kStats[i < std::size(kStats) ? i : 0];
}

std::vector<Vec3> crow_perches(const World& w) {
  std::vector<Vec3> perches;
  auto add = [&](const Object& o, float lx, float y, float lz) {
    const Vec2 p = local_to_world(o, lx, lz);
    perches.push_back({p.x, o.pos.y + y, p.z});
  };
  for (const Object& o : w.objects) {
    switch (o.kind) {
      case ObjectKind::HouseCottage: add(o, -1.6f, 4.33f, 0); add(o, 1.6f, 4.33f, 0); break;
      case ObjectKind::HouseChimney: add(o, -1.1f, 5.63f, 0); add(o, 0.4f, 5.63f, 0); break;
      case ObjectKind::HouseHut: add(o, 0, 4.22f, 0); break;
      case ObjectKind::Well: add(o, 0, 2.45f, 0); break;
      case ObjectKind::Tower:  // three merlons left on the broken top
        add(o, 2.15f, 10.5f, 0);
        add(o, -1.075f, 10.5f, 1.862f);
        add(o, -1.075f, 10.5f, -1.862f);
        break;
      default: break;
    }
  }
  return perches;
}

bool is_round(ObjectKind kind) {
  return kind == ObjectKind::Tree || kind == ObjectKind::Pine || kind == ObjectKind::Stump ||
         kind == ObjectKind::Bush || kind == ObjectKind::HouseHut || kind == ObjectKind::Well ||
         kind == ObjectKind::Barrel || kind == ObjectKind::Lamp || kind == ObjectKind::Tower ||
         kind == ObjectKind::Pillar || kind == ObjectKind::Rubble;
}

Box object_box(const Object& o) {
  const Vec3 s = o.size;
  Vec3 half;
  switch (o.kind) {
    case ObjectKind::Tree:  half = {0.28f * s.x, 1.6f * s.x, 0.28f * s.x}; break;  // trunk only
    case ObjectKind::Pine:  half = {0.25f * s.x, 1.6f * s.x, 0.25f * s.x}; break;
    case ObjectKind::Rock:  half = {0.85f * s.x, 0.5f * s.y, 0.85f * s.z}; break;
    case ObjectKind::Log:   half = {s.x, s.y, s.y}; break;
    case ObjectKind::Stump: half = {s.x, 0.5f * s.y, s.x}; break;
    case ObjectKind::Bush:  half = {0.8f * s.x, 0.5f * s.y, 0.8f * s.x}; break;
    // Village: fixed sizes, matching the models of tools/make_models.py.
    case ObjectKind::HouseCottage: half = {2.5f, 2.15f, 2.0f}; break;
    case ObjectKind::HouseChimney: half = {2.25f, 2.8f, 2.25f}; break;
    case ObjectKind::HouseHut:     half = {2.0f, 2.1f, 2.0f}; break;
    case ObjectKind::Fence:        half = {1.0f, 0.45f, 0.07f}; break;
    case ObjectKind::Sign:         half = {0.12f, 0.9f, 0.12f}; break;
    case ObjectKind::Well:         half = {0.85f, 0.45f, 0.85f}; break;
    case ObjectKind::Barrel:       half = {0.35f, 0.45f, 0.35f}; break;
    case ObjectKind::Lamp:         half = {0.1f, 1.25f, 0.1f}; break;
    // Ruins: matching the models of tools/make_models.py.
    case ObjectKind::Tower:        half = {2.35f, 5.25f, 2.35f}; break;
    case ObjectKind::RuinWall:     half = o.variant & 1 ? Vec3{1.5f, 1.1f, 0.28f} : Vec3{1.5f, 0.5f, 0.28f}; break;
    case ObjectKind::Pillar:       half = {0.4f, 0.5f * s.y, 0.4f}; break;
    case ObjectKind::FallenPillar: half = {1.5f, 0.38f, 0.38f}; break;
    case ObjectKind::Rubble:       half = {0.75f, 0.3f, 0.75f}; break;
    case ObjectKind::RuneStone:    half = {0.35f, 0.95f, 0.18f}; break;
    default:                half = s; break;  // Crate
  }
  if (!is_round(o.kind) && ((o.yaw >> 14) & 1)) std::swap(half.x, half.z);  // turned 90° or 270°
  const Vec3 center = o.pos + Vec3{0, half.y, 0};
  return {center - half, center + half};
}

bool overlaps(const Box& a, const Box& b) {
  return a.min.x < b.max.x && a.max.x > b.min.x && a.min.y < b.max.y && a.max.y > b.min.y &&
         a.min.z < b.max.z && a.max.z > b.min.z;
}

World new_game(uint64_t seed) {
  World w;
  w.seed = seed;
  w.rng = Rng(seed);
  w.path = make_path(seed);
  w.village = make_village_layout(seed, w.path);
  w.ruins = make_ruins_layout(seed, w.path);
  w.ground = make_heightmap(seed, w.path, w.village, w.ruins);
  // Objects are placed as if on flat ground, then set on the relief.
  build_village(w);
  build_ruins(w);
  generate_objects(w);
  settle_objects(w);
  w.player.pos.y = w.ground.height(0, 0);
  spawn_creatures(w);
  assign_genes(w);
  assign_levels(w);
  capture(w, 0);  // the companion is the player's own
  w.team = {starting_companion(w.creatures[0].genes)};
  return w;
}

void step(World& w, const Input& in) {
  ++w.tick;
  w.clock = (w.clock + 1) % rules::kTicksPerDay;
  battle_command(w, in);  // may start or end a battle, or remove a captured monster
  const BattlePhase phase = w.battle.phase;

  // Joystick -> direction on the ground, relative to the camera (in a fight:
  // to the line from the companion to its enemy).
  float mx = static_cast<float>(std::max<int>(in.move_x, -127)) / 127.0f;
  float my = static_cast<float>(std::max<int>(in.move_y, -127)) / 127.0f;
  const float len2 = mx * mx + my * my;
  if (len2 > 1.0f) {
    const float len = std::sqrt(len2);
    mx /= len;
    my /= len;
  }
  float forward_x, forward_z;
  if (!battle_forward(w, forward_x, forward_z)) {
    const SinCos sc = sincos16(in.yaw);
    forward_x = -sc.sin;  // forward = (-sin, 0, -cos), right = (cos, 0, -sin)
    forward_z = -sc.cos;
  }
  const float dir_x = -mx * forward_z + my * forward_x;
  const float dir_z = mx * forward_x + my * forward_z;
  std::vector<Box> boxes;
  boxes.reserve(w.objects.size());
  for (const Object& o : w.objects) boxes.push_back(object_box(o));
  Surroundings s{boxes, w.village.lake, w.ground, {}, {}};
  s.build();

  // During a battle the magician waits: the joystick moves the companion.
  if (phase == BattlePhase::None) {
    move_body(s, w.player, kPlayerShape, dir_x, dir_z, rules::kWalkSpeed, in.jump, rules::kJumpSpeed);
  } else {
    move_body(s, w.player, kPlayerShape, 0, 0, 0, false, 0);
  }
  std::vector<Vec3> perches;
  for (size_t i = 0; i < w.creatures.size(); ++i) {
    Creature& c = w.creatures[i];
    if (i == 0 && w.active < 0) {  // in the crystal: it waits by the player, unseen
      c.body.pos = w.player.pos;
      continue;
    }
    if (phase == BattlePhase::Fight && (i == 0 || i == w.battle.enemy)) {
      const Steer st = i == 0 ? battle_ally(w, in, dir_x, dir_z) : battle_foe(w);
      const SpeciesInfo& info = species_info(c.species);
      move_body(s, c.body, info.shape, st.dir_x, st.dir_z, st.speed, st.jump, info.jump_speed);
      continue;
    }
    if (i == 0) {
      step_companion(w, s, c);
      continue;
    }
    switch (c.species) {
      case Species::Companion: step_companion(w, s, c); break;
      case Species::Villager: step_villager(w, s, c); break;
      case Species::Crow:
        if (perches.empty()) perches = crow_perches(w);
        step_crow(w, perches, c);
        break;
      case Species::Fish: step_fish(w, s, c); break;
      default: step_wild(w, s, c); break;
    }
  }
  battle_resolve(w);
  team_regen(w);
  discover(w);
}

}  // namespace monstres
