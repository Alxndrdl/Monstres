#include "monstres/core/world.hpp"

#include <algorithm>
#include <cmath>
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
    case ObjectKind::Crate:
    case ObjectKind::Count: {
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
  for (int attempt = 0; attempt < 20000 && w.objects.size() < rules::kObjectCount; ++attempt) {
    const float x = random_range(w.rng, -limit, limit);
    const float z = random_range(w.rng, -limit, limit);
    const Object o = random_object(w.rng, x, z);

    // Keep the spawn area clear.
    const Box b = footprint(o);
    const float dx = std::max({b.min.x, -b.max.x, 0.0f});
    const float dz = std::max({b.min.z, -b.max.z, 0.0f});
    if (dx * dx + dz * dz < rules::kSpawnClearRadius * rules::kSpawnClearRadius) continue;

    // Keep a gap between objects so the player can always walk between them.
    Box wide = b;
    wide.min = wide.min - Vec3{gap, 0, gap};
    wide.max = wide.max + Vec3{gap, 0, gap};
    const bool collides = std::any_of(w.objects.begin(), w.objects.end(),
                                      [&](const Object& other) { return overlaps(wide, footprint(other)); });
    if (!collides) w.objects.push_back(o);
  }
}

// Moves the player along one axis, then pushes it out of any object it entered.
void move_axis(World& w, int axis, float delta) {
  Player& p = w.player;
  float* coord = axis == 0 ? &p.pos.x : &p.pos.z;
  *coord += delta;
  const float h = rules::kPlayerHalfWidth;
  for (const Object& o : w.objects) {
    const Box ob = object_box(o);
    if (!overlaps(player_box(p), ob)) continue;
    const float lo = axis == 0 ? ob.min.x : ob.min.z;
    const float hi = axis == 0 ? ob.max.x : ob.max.z;
    *coord = delta > 0 ? lo - h : hi + h;
  }
  const float limit = rules::kWorldHalfSize - h;
  *coord = std::clamp(*coord, -limit, limit);
}

void move_vertical(World& w) {
  Player& p = w.player;
  p.pos.y += p.velocity_y * rules::kDt;
  p.on_ground = false;
  if (p.pos.y <= 0) {
    p.pos.y = 0;
    p.velocity_y = 0;
    p.on_ground = true;
  }
  for (const Object& o : w.objects) {
    const Box ob = object_box(o);
    if (!overlaps(player_box(p), ob)) continue;
    if (p.velocity_y <= 0) {  // landing on top
      p.pos.y = ob.max.y;
      p.on_ground = true;
    } else {                  // head hits the underside
      p.pos.y = ob.min.y - rules::kPlayerHeight;
    }
    p.velocity_y = 0;
  }
}

}  // namespace

Box player_box(const Player& p) {
  const float h = rules::kPlayerHalfWidth;
  return {{p.pos.x - h, p.pos.y, p.pos.z - h},
          {p.pos.x + h, p.pos.y + rules::kPlayerHeight, p.pos.z + h}};
}

bool is_round(ObjectKind kind) {
  return kind == ObjectKind::Tree || kind == ObjectKind::Pine || kind == ObjectKind::Stump ||
         kind == ObjectKind::Bush;
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
  w.rng = Rng(seed);
  generate_objects(w);
  return w;
}

void step(World& w, const Input& in) {
  ++w.tick;
  Player& p = w.player;

  // Joystick -> direction on the ground, relative to the camera.
  float mx = static_cast<float>(std::max<int>(in.move_x, -127)) / 127.0f;
  float my = static_cast<float>(std::max<int>(in.move_y, -127)) / 127.0f;
  const float len2 = mx * mx + my * my;
  if (len2 > 1.0f) {
    const float len = std::sqrt(len2);
    mx /= len;
    my /= len;
  }
  const SinCos sc = sincos16(in.yaw);
  // forward = (-sin, 0, -cos), right = (cos, 0, -sin)
  const float dir_x = mx * sc.cos - my * sc.sin;
  const float dir_z = -mx * sc.sin - my * sc.cos;

  if (len2 > 0.0f) {
    const float len = std::sqrt(dir_x * dir_x + dir_z * dir_z);
    p.facing_x = dir_x / len;
    p.facing_z = dir_z / len;
  }

  if (in.jump && p.on_ground) p.velocity_y = rules::kJumpSpeed;
  p.velocity_y = std::max(p.velocity_y - rules::kGravity * rules::kDt, -rules::kMaxFallSpeed);

  move_axis(w, 0, dir_x * rules::kWalkSpeed * rules::kDt);
  move_axis(w, 2, dir_z * rules::kWalkSpeed * rules::kDt);
  move_vertical(w);
}

}  // namespace monstres
