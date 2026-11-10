#include "monstres/core/world.hpp"

#include <algorithm>
#include <cmath>

namespace monstres {

namespace {

constexpr uint8_t kPaletteSize = 6;

// Random value in [lo, hi] with a 1 cm step: integers from the RNG converted
// exactly, so the layout is identical on every platform.
float random_range(Rng& rng, float lo, float hi) {
  const auto steps = static_cast<uint32_t>((hi - lo) * 100.0f + 0.5f);
  return lo + static_cast<float>(rng.below(steps + 1)) / 100.0f;
}

void generate_objects(World& w) {
  const float limit = rules::kWorldHalfSize - 3.0f;
  while (w.objects.size() < rules::kObjectCount) {
    Object o;
    o.half = {random_range(w.rng, 0.4f, 2.0f), random_range(w.rng, 0.2f, 1.6f),
              random_range(w.rng, 0.4f, 2.0f)};
    o.center = {random_range(w.rng, -limit, limit), o.half.y, random_range(w.rng, -limit, limit)};
    o.color = static_cast<uint8_t>(w.rng.below(kPaletteSize));

    // Keep the spawn area clear.
    const float dx = std::max(std::fabs(o.center.x) - o.half.x, 0.0f);
    const float dz = std::max(std::fabs(o.center.z) - o.half.z, 0.0f);
    if (dx * dx + dz * dz < rules::kSpawnClearRadius * rules::kSpawnClearRadius) continue;

    // No two objects inside each other (a small gap so the player never gets stuck).
    Box b = object_box(o);
    b.min = b.min - Vec3{0.5f, 0, 0.5f};
    b.max = b.max + Vec3{0.5f, 0, 0.5f};
    const bool collides = std::any_of(w.objects.begin(), w.objects.end(),
                                      [&](const Object& other) { return overlaps(b, object_box(other)); });
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

Box object_box(const Object& o) { return {o.center - o.half, o.center + o.half}; }

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
