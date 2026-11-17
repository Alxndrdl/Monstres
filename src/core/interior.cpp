#include "monstres/core/interior.hpp"

#include "monstres/core/battle.hpp"
#include "monstres/core/heightmap.hpp"
#include "monstres/core/variants.hpp"

#include <algorithm>
#include <cmath>

namespace monstres {

namespace {

float unit(Rng& r) { return static_cast<float>(r.next()) / 4294967296.0f; }
// Plain arithmetic for what plays (identical everywhere): angles as Angle16.
Angle16 any_angle(Rng& r) { return static_cast<Angle16>(r.below(65536)); }
Vec2 dir16(Angle16 a) {  // (cos, sin)
  const SinCos sc = sincos16(a);
  return {sc.cos, sc.sin};
}
float distance2d(float x, float z) { return std::sqrt(x * x + z * z); }
constexpr Angle16 kStairStart = 22756;  // 125 degrees
constexpr Angle16 kStairStep = 3171;    // 0.304 rad: one step
constexpr int kStairSteps = 30;
float range(Rng& r, float lo, float hi) { return lo + (hi - lo) * unit(r); }

// Same rotation as world.cpp and the renderer: yaw turns +X towards -Z.
Vec2 local_to_world(const Object& o, float lx, float lz) {
  const SinCos sc = sincos16(o.yaw);
  return {o.pos.x + lx * sc.cos + lz * sc.sin, o.pos.z - lx * sc.sin + lz * sc.cos};
}
Vec2 front_of(Angle16 yaw) {  // local -Z
  const SinCos sc = sincos16(yaw);
  return {-sc.sin, -sc.cos};
}
// The yaw whose local +Z points along (dx, dz) (a unit vector).
Angle16 yaw_towards(float dx, float dz) { return angle16_from_radians(std::atan2(dx, dz)); }

float length(Vec3 v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
Vec3 normalized(Vec3 v, Vec3 fallback = {0, 1, 0}) {
  const float l = length(v);
  return l > 1e-6f ? v * (1.0f / l) : fallback;
}
float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

// Smooth minimum (polynomial): the two surfaces meet in a fillet `k` wide.
float smin(float a, float b, float k) {
  if (k <= 0) return std::min(a, b);
  const float h = std::max(k - std::fabs(a - b), 0.0f) / k;
  return std::min(a, b) - h * h * k * 0.25f;
}

// The building's hash: each house its own furniture.
uint64_t place_seed(uint64_t seed, PlaceKind kind, const Object& door) {
  uint64_t h = seed ^ (0x494E5445524E4F52ULL + static_cast<uint64_t>(kind));
  for (const float v : {door.pos.x, door.pos.z}) {
    h = (h ^ static_cast<uint64_t>(static_cast<int64_t>(std::lround(v * 16.0f)))) * 0x100000001b3ULL;
  }
  return h;
}

bool is_house(ObjectKind k) {
  return k == ObjectKind::HouseCottage || k == ObjectKind::HouseChimney || k == ObjectKind::HouseHut;
}

PlaceKind place_of(ObjectKind k) {
  if (k == ObjectKind::CaveMouth) return PlaceKind::Cave;
  if (is_house(k)) return PlaceKind::House;
  if (k == ObjectKind::Tower) return PlaceKind::Tower;
  return PlaceKind::Outside;
}

// Half depth of a house's front (its door is there, local -Z).
float house_front(ObjectKind k) {
  return k == ObjectKind::HouseCottage ? 2.0f : k == ObjectKind::HouseChimney ? 2.25f : 2.1f;
}

// A building's door, from outside: where one stands to use it (local x, z),
// the middle of its opening (local x, y, z), its size, and where one comes
// out (local x, z).
struct DoorFrame {
  float use_x, use_z;
  Vec3 glow;
  float width, height;
  float out_x, out_z;
};
DoorFrame frame_of(ObjectKind k) {
  if (k == ObjectKind::CaveMouth) return {0, 2.0f, {0, 1.9f, 4.55f}, 4.3f, 3.7f, 0, -1.8f};
  if (k == ObjectKind::Tower) return {0, -2.9f, {0, 1.25f, -2.45f}, 1.24f, 2.5f, 0, -3.8f};
  const float f = house_front(k);
  return {0, -f - 0.7f, {0, 0.95f, -f - 0.05f}, 0.9f, 1.9f, 0, -f - 1.4f};
}

// Signed distance (positive inside) of a piece of floor.
float piece_distance(const RoomPiece& p, float x, float z, uint64_t noise) {
  if (p.radius <= 0) {  // a box around a
    const float qx = std::fabs(x - p.a.x) - p.half.x, qz = std::fabs(z - p.a.z) - p.half.z;
    const float ox = std::max(qx, 0.0f), oz = std::max(qz, 0.0f);
    return -(std::sqrt(ox * ox + oz * oz) + std::min(std::max(qx, qz), 0.0f));
  }
  const float abx = p.b.x - p.a.x, abz = p.b.z - p.a.z;
  const float len2 = abx * abx + abz * abz;
  const float t = len2 > 0 ? std::clamp(((x - p.a.x) * abx + (z - p.a.z) * abz) / len2, 0.0f, 1.0f) : 0.0f;
  const float dx = x - (p.a.x + abx * t), dz = z - (p.a.z + abz * t);
  float r = p.radius;
  if (p.wobble > 0) {
    const float n = 0.7f * value_noise(noise, x, z, 4.0f) + 0.3f * value_noise(noise + 1, x, z, 1.6f);
    r += p.wobble * (n - 0.5f) * 2.0f;
  }
  return r - std::sqrt(dx * dx + dz * dz);
}

}  // namespace

// ---- Shape -------------------------------------------------------------------

float wall_distance(const Interior& in, float x, float z) {
  float d = -1e9f;
  for (const RoomPiece& p : in.pieces) {
    const float pd = piece_distance(p, x, z, in.noise);
    d = in.rounding > 0 ? -smin(-d, -pd, 2.0f) : std::max(d, pd);  // caves: the pieces flow into each other
  }
  return d;
}

float ceiling_height(const Interior& in, float x, float z) {
  float sum = 0, weights = 0;
  for (const RoomPiece& p : in.pieces) {
    const float w = std::max(piece_distance(p, x, z, in.noise) + 2.0f, 0.01f);
    sum += p.height * w * w;
    weights += w * w;
  }
  float h = weights > 0 ? sum / weights : 3.0f;
  if (in.rounding > 0) h += (value_noise(in.noise + 2, x, z, 5.0f) - 0.5f) * 1.6f;
  return h;
}

float open_distance(const Interior& in, Vec3 p) {
  const float k = in.rounding;
  return smin(smin(p.y, wall_distance(in, p.x, p.z), k), ceiling_height(in, p.x, p.z) - p.y, k);
}

Vec3 surface_normal(const Interior& in, Vec3 p) {
  constexpr float e = 0.05f;
  const Vec3 g{open_distance(in, p + Vec3{e, 0, 0}) - open_distance(in, p - Vec3{e, 0, 0}),
               open_distance(in, p + Vec3{0, e, 0}) - open_distance(in, p - Vec3{0, e, 0}),
               open_distance(in, p + Vec3{0, 0, e}) - open_distance(in, p - Vec3{0, 0, e})};
  return normalized(g);
}

// ---- Building interiors -------------------------------------------------------

Interior make_interior(uint64_t seed, PlaceKind kind, const Object& door) {
  Interior in;
  in.kind = kind;
  const uint64_t s = place_seed(seed, kind, door);
  in.noise = s;
  in.entry = {0, 0, -1.2f};
  in.entry_facing = {0, -1};
  in.exits.push_back({{0, 0, -0.4f}, {0, -1}, ExitKind::Out});
  if (kind == PlaceKind::Cave) {
    // A corridor from the mouth, then chambers joined by tunnels, each away
    // from the others (deeper into the hill, towards -Z).
    in.rounding = 1.0f;
    in.entry = {0, 0, -0.5f};
    in.exits[0] = {{0, 0, 1.3f}, {0, -1}, ExitKind::Out};
    // Roomy: there must be space to fight.
    in.pieces.push_back({{0, 1.0f}, {0, -8.0f}, 3.0f, {}, 4.5f, 0.4f});
    struct Chamber {
      Vec2 at;
      float r;
    };
    std::vector<Chamber> chambers{{{0, -17.0f}, 9.5f}};
    in.pieces.push_back({{0, -17.0f}, {0, -17.0f}, 9.5f, {}, 9.0f, 1.4f});
    Rng rng(s, 91);
    for (int attempt = 0; attempt < 60 && chambers.size() < 6; ++attempt) {
      const Chamber& from = chambers[rng.below(static_cast<uint32_t>(chambers.size()))];
      const Vec2 a = dir16(any_angle(rng));
      const float r = range(rng, 7.0f, 11.0f);
      const float d = from.r + r + range(rng, 6.0f, 10.0f);
      const Vec2 at{from.at.x + a.x * d, from.at.z + a.z * d};
      const float height = range(rng, 7.0f, 11.0f);
      const float tunnel = range(rng, 3.2f, 4.0f);
      if (at.z > -10.0f - r) continue;  // not back towards the mouth
      bool free = true;
      for (const Chamber& c : chambers) {
        const float dx = c.at.x - at.x, dz = c.at.z - at.z;
        free = free && dx * dx + dz * dz > (c.r + r + 3.5f) * (c.r + r + 3.5f);
      }
      if (!free) continue;
      in.pieces.push_back({from.at, at, tunnel, {}, 5.0f, 0.6f});
      in.pieces.push_back({at, at, r, {}, height, 1.4f});
      chambers.push_back({at, r});
    }
  } else if (kind == PlaceKind::Tower) {
    // One tall round hall; a stair along its wall up to a landing under the trapdoor.
    in.pieces.push_back({{0, -4.5f}, {0, -4.5f}, 4.5f, {}, 10.8f, 0});
  } else {
    switch (door.kind) {
      case ObjectKind::HouseChimney: in.pieces.push_back({{0, -3.3f}, {}, 0, {3.3f, 3.3f}, 3.0f, 0}); break;
      case ObjectKind::HouseHut: in.pieces.push_back({{0, -3.4f}, {0, -3.4f}, 3.4f, {}, 3.2f, 0}); break;
      default: in.pieces.push_back({{0, -3.0f}, {}, 0, {3.6f, 3.0f}, 2.8f, 0}); break;
    }
  }
  // Around everything.
  in.bounds = {{1e9f, 0, 1e9f}, {-1e9f, 0, -1e9f}};
  for (const RoomPiece& p : in.pieces) {
    const float reach = p.radius > 0 ? p.radius + p.wobble + 0.5f : 0.5f;
    const float hx = p.radius > 0 ? reach : p.half.x + reach, hz = p.radius > 0 ? reach : p.half.z + reach;
    for (const Vec2 c : {p.a, p.radius > 0 ? p.b : p.a}) {
      in.bounds.min.x = std::min(in.bounds.min.x, c.x - hx);
      in.bounds.max.x = std::max(in.bounds.max.x, c.x + hx);
      in.bounds.min.z = std::min(in.bounds.min.z, c.z - hz);
      in.bounds.max.z = std::max(in.bounds.max.z, c.z + hz);
    }
    in.bounds.max.y = std::max(in.bounds.max.y, p.height + 1.5f);
  }
  in.bounds.min.y = -0.5f;
  if (kind == PlaceKind::Tower) {
    // The landing (see interior_objects): the way up to the roof.
    const Vec2 a = dir16(static_cast<Angle16>(kStairStart + kStairStep * kStairSteps + kStairStep / 2));
    in.top = {2.4f * a.x, 0.3f * (kStairSteps + 1), -4.5f + 2.4f * a.z};
    in.exits.push_back({in.top, {0, -1}, ExitKind::Roof});
  }
  return in;
}

namespace {

Object thing(ObjectKind kind, float x, float y, float z, Angle16 yaw = 0, uint8_t variant = 0) {
  Object o;
  o.kind = kind;
  o.pos = {x, y, z};
  o.size = {1, 1, 1};
  o.yaw = yaw;
  o.variant = variant;
  return o;
}

constexpr Angle16 kQuarter = 16384;

// Where a ray from `from` along `dir` first meets a surface (false: none within `reach`).
bool cast(const Interior& in, Vec3 from, Vec3 dir, float reach, Vec3& hit) {
  float t = 0;
  for (int k = 0; k < 96 && t < reach; ++k) {
    const Vec3 p = from + dir * t;
    const float d = open_distance(in, p);
    if (d < 0.02f) {
      hit = p;
      return true;
    }
    t += std::max(d * 0.8f, 0.03f);
  }
  return false;
}

void furnish_cave(const Interior& in, Rng& rng, std::vector<Object>& out) {
  // The chambers (the tunnels stay clear).
  std::vector<const RoomPiece*> chambers, tunnels;
  for (const RoomPiece& p : in.pieces) (p.a == p.b ? chambers : tunnels).push_back(&p);
  const auto clear_of_tunnels = [&](float x, float z, float margin) {
    for (const RoomPiece* t : tunnels) {
      if (piece_distance(*t, x, z, in.noise) > -margin) return false;
    }
    return distance2d(x, z - 0.5f) > 3.0f + margin;  // the way in
  };
  const auto in_chamber = [&](float& x, float& z, float edge) {
    const RoomPiece& c = *chambers[rng.below(static_cast<uint32_t>(chambers.size()))];
    const Vec2 a = dir16(any_angle(rng));
    const float d = c.radius * std::sqrt(unit(rng));
    x = c.a.x + a.x * d;
    z = c.a.z + a.z * d;
    return wall_distance(in, x, z) > edge;
  };
  std::vector<Box> taken;
  const auto free_at = [&](const Box& b) {
    for (const Box& t : taken) {
      if (overlaps(b, t)) return false;
    }
    return true;
  };
  // Rocks fallen on the floor.
  for (int n = 0, attempt = 0; n < 30 && attempt < 900; ++attempt) {
    float x, z;
    const float sx = range(rng, 0.4f, 1.3f), sy = range(rng, 0.4f, 1.2f), sz = range(rng, 0.4f, 1.3f);
    const auto yaw = static_cast<Angle16>(rng.below(4) * kQuarter);
    const auto variant = static_cast<uint8_t>(rng.below(256));
    if (!in_chamber(x, z, std::max(sx, sz) + 0.6f) || !clear_of_tunnels(x, z, std::max(sx, sz) + 0.8f)) continue;
    Object o = thing(ObjectKind::Rock, x, -0.05f, z, yaw, variant);
    o.size = {sx, sy, sz};
    Box b = object_box(o);
    b.min = b.min - Vec3{0.7f, 0, 0.7f};
    b.max = b.max + Vec3{0.7f, 0, 0.7f};
    if (!free_at(b)) continue;
    taken.push_back(b);
    out.push_back(o);
    ++n;
  }
  // Crystals: standing on the floor, hanging from the ceiling, out of the walls.
  const auto crystal = [&](Vec3 at, float scale, uint8_t mount, Angle16 yaw) {
    Object o = thing(ObjectKind::Crystal, at.x, at.y, at.z, yaw, static_cast<uint8_t>(rng.below(256)));
    o.size = {scale, scale, scale};
    o.mount = mount;
    out.push_back(o);
  };
  for (int n = 0, attempt = 0; n < 18 && attempt < 200; ++attempt) {
    float x, z;
    const float scale = range(rng, 0.45f, 1.2f);
    const auto yaw = static_cast<Angle16>(rng.below(65536));
    if (!in_chamber(x, z, 0.8f) || !clear_of_tunnels(x, z, 1.0f)) continue;
    Object probe = thing(ObjectKind::Crystal, x, 0, z);
    probe.size = {scale, scale, scale};
    if (!free_at(object_box(probe))) continue;
    taken.push_back(object_box(probe));
    crystal({x, -0.03f, z}, scale, 0, yaw);
    ++n;
  }
  // The biggest cluster, in the middle of the first chamber's far side.
  crystal({chambers[0]->a.x + 2.5f, -0.05f, chambers[0]->a.z - 2.0f}, 2.2f, 0, 9000);
  for (int n = 0, attempt = 0; n < 26 && attempt < 200; ++attempt) {
    float x, z;
    const float scale = range(rng, 0.4f, 1.0f);
    const auto yaw = static_cast<Angle16>(rng.below(65536));
    if (!in_chamber(x, z, 1.0f)) continue;
    Vec3 hit;
    if (!cast(in, {x, 1.0f, z}, {0, 1, 0}, 14.0f, hit)) continue;
    crystal(hit + Vec3{0, 0.05f, 0}, scale, Object::kHanging, yaw);
    ++n;
  }
  for (int n = 0, attempt = 0; n < 34 && attempt < 300; ++attempt) {
    const RoomPiece& p = in.pieces[rng.below(static_cast<uint32_t>(in.pieces.size()))];
    const float t = unit(rng);
    const Vec2 a = dir16(any_angle(rng));
    const float y = range(rng, 0.4f, 4.0f), scale = range(rng, 0.35f, 0.9f);
    const Vec3 from{p.a.x + (p.b.x - p.a.x) * t, y, p.a.z + (p.b.z - p.a.z) * t};
    Vec3 hit;
    if (open_distance(in, from) < 0.5f || !cast(in, from, {a.x, 0, a.z}, 16.0f, hit)) continue;
    if (distance2d(hit.x, hit.z - 0.5f) < 2.5f) continue;  // not by the way out
    const Vec3 normal = surface_normal(in, hit);
    if (std::fabs(normal.y) > 0.6f) continue;  // a wall, not the floor or the ceiling
    crystal(hit - Vec3{normal.x, 0, normal.z} * 0.1f, scale, Object::kOnWall, yaw_towards(normal.x, normal.z));
    ++n;
  }
}

void furnish_house(const Interior& in, const Object& door, Rng& rng, std::vector<Object>& out) {
  const auto v = [&]() { return static_cast<uint8_t>(rng.below(256)); };
  const RoomPiece& room = in.pieces[0];
  const bool round = room.radius > 0;
  const float hx = round ? room.radius * 0.72f : room.half.x;   // the walls' inner reach
  const float back = round ? room.a.z - room.radius * 0.72f : room.a.z - room.half.z;
  const float cz = room.a.z;
  const bool flip = rng.below(2) == 1;  // furniture on one side or the other
  const float side = flip ? -1.0f : 1.0f;
  // The table and its chairs, in the middle.
  const float tx = side * range(rng, 0.3f, 0.9f), tz = cz + range(rng, -0.2f, 0.4f);
  const bool turned = rng.below(2) == 1;
  out.push_back(thing(ObjectKind::Table, tx, 0, tz, turned ? kQuarter : 0, v()));
  const float ax = turned ? 0 : 1.05f, az = turned ? 1.05f : 0;  // along the long sides
  const float bx = turned ? 0.75f : 0, bz = turned ? 0 : 0.75f;
  const uint32_t chairs = 2 + rng.below(3);
  const struct {
    float x, z;
    Angle16 yaw;
  } spots[] = {{tx - bx, tz - bz, static_cast<Angle16>(turned ? 3 * kQuarter : 0)},
               {tx + bx, tz + bz, static_cast<Angle16>(turned ? kQuarter : 2 * kQuarter)},
               {tx - ax, tz - az, static_cast<Angle16>(turned ? 0 : kQuarter)},
               {tx + ax, tz + az, static_cast<Angle16>(turned ? 2 * kQuarter : 3 * kQuarter)}};
  for (uint32_t k = 0; k < chairs; ++k) out.push_back(thing(ObjectKind::Chair, spots[k].x, 0, spots[k].z, spots[k].yaw, v()));
  // Against the back wall: the hearth (its back to the wall), a bed in a corner.
  const bool hearth = door.kind != ObjectKind::HouseHut || rng.below(2) == 0;
  if (hearth) out.push_back(thing(ObjectKind::Hearth, -side * 0.2f, 0, back + 0.4f, 2 * kQuarter, v()));
  out.push_back(thing(ObjectKind::Bed, -side * (hx - 0.65f), 0, back + 1.1f, 2 * kQuarter, v()));
  if (door.kind == ObjectKind::HouseChimney) {
    out.push_back(thing(ObjectKind::Bed, side * (hx - 0.65f), 0, back + 1.1f, 2 * kQuarter, v()));
  }
  // Shelves on the side walls, a rug, barrels and crates by the door.
  out.push_back(thing(ObjectKind::Shelf, side * (hx - 0.25f), 0, cz + (round ? 0.6f : 0.9f), side > 0 ? kQuarter : 3 * kQuarter, v()));
  if (!round && rng.below(2) == 0) {
    out.push_back(thing(ObjectKind::Shelf, -side * (hx - 0.25f), 0, cz - 0.2f, side > 0 ? 3 * kQuarter : kQuarter, v()));
  }
  Object rug = thing(ObjectKind::Rug, tx, 0.01f, tz, turned ? kQuarter : 0, v());
  rug.size = {1.3f, 0.02f, 0.95f};
  out.push_back(rug);
  const float near_door = -1.0f;
  out.push_back(thing(ObjectKind::Barrel, -side * (hx - 0.5f), 0, near_door - 0.2f, 0, v()));
  if (rng.below(2) == 0) out.push_back(thing(ObjectKind::Barrel, -side * (hx - 0.5f), 0, near_door - 0.95f, 0, v()));
  if (!round) {
    Object crate = thing(ObjectKind::Crate, side * (hx - 0.5f), 0, near_door + 0.05f, 0, v());
    crate.size = {0.35f, 0.35f, 0.35f};
    out.push_back(crate);
  }
}

void furnish_tower(const Interior& in, Rng& rng, std::vector<Object>& out) {
  // The stair: one step every 0.304 rad along the wall, 0.3 m higher each.
  for (int i = 0; i < kStairSteps; ++i) {
    const Vec2 a = dir16(static_cast<Angle16>(kStairStart + kStairStep * i));
    Object o = thing(ObjectKind::Step, 3.5f * a.x, 0.3f * static_cast<float>(i), -4.5f + 3.5f * a.z, 0,
                     static_cast<uint8_t>(i));
    o.size = {0.55f, 0.15f, 0.55f};
    out.push_back(o);
  }
  Object landing = thing(ObjectKind::Step, in.top.x, in.top.y - 0.3f, in.top.z, 0, 200);
  landing.size = {1.3f, 0.15f, 1.3f};
  out.push_back(landing);
  Object hatch = thing(ObjectKind::Hatch, in.top.x, in.pieces[0].height - 0.02f, in.top.z);
  hatch.mount = Object::kHanging;  // in the ceiling, over the landing
  out.push_back(hatch);
  // Crystals in the walls light the way up; rubble on the floor.
  for (int k = 0; k < 9; ++k) {
    const Vec2 a = dir16(static_cast<Angle16>(2100 + 7282 * k));  // a ninth of a turn apart
    const float y = 1.6f + 0.95f * static_cast<float>(k);
    const Vec3 n{-a.x, 0, -a.z};
    Object c = thing(ObjectKind::Crystal, 4.45f * a.x, y, -4.5f + 4.45f * a.z, yaw_towards(n.x, n.z),
                     static_cast<uint8_t>(rng.below(256)));
    c.size = {0.5f, 0.5f, 0.5f};
    c.mount = Object::kOnWall;
    out.push_back(c);
  }
  for (int k = 0; k < 3; ++k) {
    const Vec2 a = dir16(any_angle(rng));
    const float d = range(rng, 0.8f, 2.0f);
    Object o = thing(ObjectKind::Rubble, d * a.x, 0, -4.5f + d * a.z, 0, static_cast<uint8_t>(rng.below(256)));
    if (distance2d(o.pos.x, o.pos.z + 1.2f) < 1.8f) continue;  // the way in
    out.push_back(o);
  }
  out.push_back(thing(ObjectKind::RuneStone, 0, 0, -6.6f, 2 * kQuarter));
}

}  // namespace

std::vector<Object> interior_objects(uint64_t seed, const Interior& in, const Object& door) {
  std::vector<Object> out;
  Rng rng(place_seed(seed, in.kind, door), 17);
  switch (in.kind) {
    case PlaceKind::Cave: furnish_cave(in, rng, out); break;
    case PlaceKind::House: furnish_house(in, door, rng, out); break;
    case PlaceKind::Tower: furnish_tower(in, rng, out); break;
    default: break;
  }
  return out;
}

// ---- Doors --------------------------------------------------------------------

DoorNear door_near(const World& w, float reach) {
  DoorNear best;
  best.distance = reach;
  const Vec3 p = w.player.pos;
  if (inside(w)) {
    for (const Exit& e : w.interior.exits) {
      const float d = distance2d(p.x - e.at.x, p.z - e.at.z);
      if (d >= best.distance || std::fabs(p.y - e.at.y) > 1.2f) continue;
      best.action = e.kind == ExitKind::Roof ? DoorAction::Climb : DoorAction::Leave;
      best.place = w.place.kind;
      best.building = w.place.door < w.outdoors.objects.size() ? w.outdoors.objects[w.place.door].kind : ObjectKind::Count;
      best.object = w.place.door;
      best.distance = d;
      if (e.kind == ExitKind::Roof) {
        best.at = {e.at.x, w.interior.pieces[0].height - 0.06f, e.at.z};  // the trapdoor overhead
        best.facing = {0, 0};  // lying flat: the trapdoor
        best.width = best.height = 1.1f;
      } else if (w.place.kind == PlaceKind::Cave) {
        best.at = {0, 1.8f, 3.4f};
        best.facing = {0, -1};
        best.width = 3.6f;
        best.height = 3.2f;
      } else {
        best.at = {0, w.place.kind == PlaceKind::Tower ? 1.25f : 0.95f, -0.03f};
        best.facing = {0, -1};
        best.width = w.place.kind == PlaceKind::Tower ? 1.24f : 0.9f;
        best.height = w.place.kind == PlaceKind::Tower ? 2.5f : 1.9f;
      }
    }
    return best;
  }
  const Box around{{p.x - 8, p.y - 12, p.z - 8}, {p.x + 8, p.y + 2, p.z + 8}};
  for_objects_near(w, around, [&](uint32_t k, const Box&) {
    const Object& o = w.objects[k];
    const PlaceKind place = place_of(o.kind);
    if (place == PlaceKind::Outside) return;
    if (o.kind == ObjectKind::Tower && p.y > o.pos.y + rules::kRoofHeight - 0.6f) {
      // On its roof: the trapdoor in the middle.
      const float d = distance2d(p.x - o.pos.x, p.z - o.pos.z);
      if (d >= best.distance) return;
      best = {DoorAction::Descend, place, o.kind, k, o.pos + Vec3{0, rules::kRoofHeight + 0.03f, 0}, {0, 0}, 1.1f, 1.1f, d};
      return;
    }
    const DoorFrame f = frame_of(o.kind);
    const Vec2 use = local_to_world(o, f.use_x, f.use_z);
    const float d = distance2d(p.x - use.x, p.z - use.z);
    if (d >= best.distance || std::fabs(p.y - o.pos.y) > 1.5f) return;
    const Vec2 g = local_to_world(o, f.glow.x, f.glow.z);
    best = {DoorAction::Enter, place, o.kind, k, {g.x, o.pos.y + f.glow.y, g.z}, front_of(o.yaw), f.width, f.height, d};
  });
  return best;
}

namespace {

// The companion comes along: just behind the player (or by its side).
void bring_companion(World& w) {
  if (w.creatures.empty()) return;
  Creature& c = w.creatures[0];
  const Body& p = w.player;
  c.body = Body{};
  c.body.pos = {p.pos.x - p.facing_x * 1.0f, p.pos.y, p.pos.z - p.facing_z * 1.0f};
  if (inside(w) && wall_distance(w.interior, c.body.pos.x, c.body.pos.z) < 0.6f) c.body.pos = p.pos;
  if (!inside(w) && !w.ground.empty()) c.body.pos.y = std::max(c.body.pos.y, w.ground.height(c.body.pos.x, c.body.pos.z));
  if (inside_any_object(w, c.body, species_info(c.species).shape)) c.body.pos = p.pos;
  c.body.facing_x = p.facing_x;
  c.body.facing_z = p.facing_z;
  if (c.mind != Mind::KnockedOut) c.mind = Mind::Idle;
  c.timer = 0;
}

void leave(World& w, bool to_roof) {
  if (!inside(w) || w.place.door >= w.outdoors.objects.size()) return;
  Creature companion = w.creatures.empty() ? Creature{} : w.creatures[0];
  const bool had_companion = !w.creatures.empty();
  Outdoors& out = w.outdoors;
  w.objects = std::move(out.objects);
  w.wild_objects = out.wild_objects;
  w.ground = std::move(out.ground);
  w.creatures.clear();
  if (had_companion) w.creatures.push_back(companion);
  for (Creature& c : out.creatures) w.creatures.push_back(std::move(c));
  const Object door = w.objects[w.place.door];
  w.place = {};
  w.interior = {};
  w.outdoors = {};
  w.way_out_ticks = 0;  // found
  Body& p = w.player;
  p = Body{};
  const Vec2 front = front_of(door.yaw);
  if (to_roof) {
    const Vec2 at = local_to_world(door, 0, -1.0f);
    p.pos = {at.x, door.pos.y + rules::kRoofHeight, at.z};
  } else {
    const DoorFrame f = frame_of(door.kind);
    const Vec2 at = local_to_world(door, f.out_x, f.out_z);
    p.pos = {at.x, w.ground.height(at.x, at.z), at.z};
  }
  p.facing_x = front.x;
  p.facing_z = front.z;
  bring_companion(w);
}

}  // namespace

bool enter_place(World& w, Place place, bool from_roof) {
  if (inside(w) || place.door >= w.objects.size()) return false;
  const Object door = w.objects[place.door];
  if (place_of(door.kind) != place.kind || place.kind == PlaceKind::Outside) return false;
  Outdoors& out = w.outdoors;
  out.objects = std::move(w.objects);
  out.wild_objects = w.wild_objects;
  out.ground = std::move(w.ground);
  out.player = w.player;
  out.creatures.clear();
  for (size_t i = 1; i < w.creatures.size(); ++i) out.creatures.push_back(std::move(w.creatures[i]));
  if (w.creatures.size() > 1) w.creatures.resize(1);
  w.objects.clear();
  w.wild_objects = 0;
  w.ground = Heightmap{};
  w.place = place;
  w.interior = make_interior(w.seed, place.kind, door);
  w.objects = interior_objects(w.seed, w.interior, door);
  Body& p = w.player;
  p = Body{};
  const bool top = from_roof && place.kind == PlaceKind::Tower;
  p.pos = top ? w.interior.top : w.interior.entry;
  p.facing_x = top ? 0.0f : w.interior.entry_facing.x;
  p.facing_z = top ? 1.0f : w.interior.entry_facing.z;
  bring_companion(w);
  map_way_out(w.interior, w.objects);
  if (place.kind == PlaceKind::Cave) spawn_cristis(w);
  return true;
}

bool use_door(World& w) {
  if (w.battle.phase != BattlePhase::None) return false;
  const DoorNear d = door_near(w);
  switch (d.action) {
    case DoorAction::Enter: if (!enter_place(w, {d.place, d.object}, false)) return false; break;
    case DoorAction::Descend: if (!enter_place(w, {PlaceKind::Tower, d.object}, true)) return false; break;
    case DoorAction::Leave: leave(w, false); break;
    case DoorAction::Climb: leave(w, true); break;
    default: return false;
  }
  w.door_tick = w.tick;
  return true;
}

World outside_world(const World& w) {
  World o = w;
  if (!inside(w)) return o;
  o.objects = w.outdoors.objects;
  o.wild_objects = w.outdoors.wild_objects;
  o.ground = w.outdoors.ground;
  o.creatures.resize(std::min<size_t>(o.creatures.size(), 1));
  for (const Creature& c : w.outdoors.creatures) o.creatures.push_back(c);
  o.player = w.outdoors.player;
  if (!o.creatures.empty()) {
    o.creatures[0].body = o.player;
    o.creatures[0].body.pos = o.player.pos;
  }
  o.place = {};
  o.interior = {};
  o.outdoors = {};
  return o;
}

const Body& map_body(const World& w) { return inside(w) ? w.outdoors.player : w.player; }

// ---- Climbing ------------------------------------------------------------------

Vec3 climb_direction(const Body& b, float fx, float fz, float mx, float my) {
  const Vec3 n = b.up;
  const Vec3 d{fx, 0, fz};
  const float dn = dot(d, n);
  const Vec3 tangent = d - n * dn;
  // Pushing into a wall goes up it, pulling away goes down.
  const Vec3 up_t = normalized(Vec3{0, 1, 0} - n * n.y, {0, 0, 0});
  const Vec3 forward = normalized(tangent - up_t * dn, Vec3{b.facing_x, b.facing_y, b.facing_z});
  const Vec3 right = cross(forward, n);
  const Vec3 dir = right * mx + forward * my;
  const float l = length(dir);
  return l > 1.0f ? dir * (1.0f / l) : dir;
}

Vec3 carried(Vec3 v, Vec3 from, Vec3 to) {
  const float c = dot(from, to);
  if (c < -0.999f) return v * -1.0f;  // turned right over (never in a tick)
  const Vec3 k = cross(from, to);
  return v * c + cross(k, v) + k * (dot(k, v) / (1.0f + c));
}

namespace {

// The joystick's forward on a surface, from the way on the ground (fx, fz).
Vec3 forward_on(const Body& b, float fx, float fz) {
  const Vec3 n = b.up;
  const Vec3 d{fx, 0, fz};
  const float dn = dot(d, n);
  const Vec3 up_t = normalized(Vec3{0, 1, 0} - n * n.y, {0, 0, 0});
  return normalized(d - n * dn - up_t * dn, Vec3{b.facing_x, b.facing_y, b.facing_z});
}

Vec3 along(Vec3 v, Vec3 n, Vec3 fallback) { return normalized(v - n * dot(v, n), fallback); }

}  // namespace

void climb_steered(World& w, Body& b, float fx, float fz, bool aimed, Angle16 yaw, float mx, float my, float speed) {
  if (!w.climbing) {
    w.climbing = true;
    w.climb_forward = forward_on(b, fx, fz);
    w.climb_yaw = yaw;
  }
  // The camera turned: forward turns as much, around the surface's normal.
  const auto delta = static_cast<Angle16>(yaw - w.climb_yaw);
  w.climb_yaw = yaw;
  if (delta != 0) {
    const SinCos sc = sincos16(delta);
    const Vec3 v = w.climb_forward, n = b.up;
    w.climb_forward = v * sc.cos + cross(n, v) * sc.sin + n * (dot(n, v) * (1 - sc.cos));
  }
  const Vec3 fallback{b.facing_x, b.facing_y, b.facing_z};
  const Vec3 forward = aimed ? forward_on(b, fx, fz) : along(w.climb_forward, b.up, fallback);
  const Vec3 right = cross(forward, b.up);
  Vec3 dir = right * mx + forward * my;
  const float l = length(dir);
  if (l > 1.0f) dir = dir * (1.0f / l);
  const Vec3 before = b.up;
  climb(w, b, dir, speed);
  w.climb_forward = along(carried(forward, before, b.up), b.up, fallback);
}

namespace {

// Signed distance to a box with rounded edges (positive outside).
float box_distance(const Box& b, Vec3 p, float round) {
  const Vec3 c = (b.min + b.max) * 0.5f;
  const Vec3 h = (b.max - b.min) * 0.5f;
  const float qx = std::fabs(p.x - c.x) - std::max(h.x - round, 0.0f);
  const float qy = std::fabs(p.y - c.y) - std::max(h.y - round, 0.0f);
  const float qz = std::fabs(p.z - c.z) - std::max(h.z - round, 0.0f);
  const float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f), oz = std::max(qz, 0.0f);
  return std::sqrt(ox * ox + oy * oy + oz * oz) + std::min(std::max(qx, std::max(qy, qz)), 0.0f) - round;
}

}  // namespace

float surface_distance(const World& w, Vec3 p) {
  float d = inside(w) ? open_distance(w.interior, p) : p.y - w.ground.height(p.x, p.z);
  constexpr float kReach = 1.5f;
  const Box around{p - Vec3{kReach, kReach, kReach}, p + Vec3{kReach, kReach, kReach}};
  for_objects_near(w, around, [&](uint32_t, const Box& b) {
    if (b.min.x == b.max.x || b.min.y == b.max.y || b.min.z == b.max.z) return;  // walked over (a rug, a teleporter)
    d = smin(d, box_distance(b, p, 0.06f), 0.3f);
  });
  return d;
}

Vec3 surface_normal(const World& w, Vec3 p) {
  constexpr float e = 0.04f;
  const Vec3 g{surface_distance(w, p + Vec3{e, 0, 0}) - surface_distance(w, p - Vec3{e, 0, 0}),
               surface_distance(w, p + Vec3{0, e, 0}) - surface_distance(w, p - Vec3{0, e, 0}),
               surface_distance(w, p + Vec3{0, 0, e}) - surface_distance(w, p - Vec3{0, 0, e})};
  return normalized(g);
}

void climb(const World& w, Body& b, Vec3 dir, float speed) {
  const Vec3 start = b.pos;
  Vec3 p = b.pos + dir * (speed * rules::kDt);
  // Back onto the surface: along its normal, to where the open begins.
  for (int k = 0; k < 3; ++k) {
    const float d = surface_distance(w, p);
    p = p - surface_normal(w, p) * d;
  }
  const float limit = rules::kWorldHalfSize - 1.0f;
  const bool lost = !std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) ||
                    std::fabs(surface_distance(w, p)) > 0.3f || std::fabs(p.x) > limit || std::fabs(p.z) > limit;
  // Outside: not into the lake, nor the sea.
  const bool wet = !inside(w) && (in_lake(w.village.lake, p.x, p.z, 0.2f) ||
                                  (w.ground.sea(p.x, p.z) && p.y < rules::kSeaLevel - 0.3f));
  if (lost || wet) p = start;  // stay
  b.pos = p;
  b.up = surface_normal(w, p);
  if (dir.x != 0 || dir.y != 0 || dir.z != 0) {
    const Vec3 f = normalized(dir - b.up * dot(dir, b.up), {b.facing_x, b.facing_y, b.facing_z});
    b.facing_x = f.x;
    b.facing_y = f.y;
    b.facing_z = f.z;
  }
  b.velocity_y = 0;
  b.on_ground = true;
  b.speed = length(p - start) / rules::kDt;
}

// ---- The way out ------------------------------------------------------------------

void map_way_out(Interior& in, const std::vector<Object>& objects) {
  const float c = Interior::kNavCell;
  in.nav_x = static_cast<int>((in.bounds.max.x - in.bounds.min.x) / c) + 1;
  in.nav_z = static_cast<int>((in.bounds.max.z - in.bounds.min.z) / c) + 1;
  in.nav.assign(static_cast<size_t>(in.nav_x) * static_cast<size_t>(in.nav_z), Interior::kNoWay);
  if (in.exits.empty()) return;
  // Walkable: clear of the walls, and of what stands on the floor (a stair's
  // steps high up are not in the way).
  std::vector<uint8_t> open(in.nav.size(), 0);
  for (int i = 0; i < in.nav_x; ++i) {
    for (int k = 0; k < in.nav_z; ++k) {
      const float x = in.bounds.min.x + c * (static_cast<float>(i) + 0.5f);
      const float z = in.bounds.min.z + c * (static_cast<float>(k) + 0.5f);
      // Walkers keep 0.63 m from the walls; by the door, the way goes up to it.
      const float to_door = distance2d(x - in.exits[0].at.x, z - in.exits[0].at.z);
      open[static_cast<size_t>(i * in.nav_z + k)] = wall_distance(in, x, z) > (to_door < 1.6f ? 0.0f : 0.9f);
    }
  }
  for (const Object& o : objects) {
    const Box b = object_box(o);
    if (b.min.y > 1.0f || b.max.y < 0.05f || b.min.x == b.max.x) continue;
    const int i0 = std::max(0, static_cast<int>((b.min.x - 0.4f - in.bounds.min.x) / c));
    const int i1 = std::min(in.nav_x - 1, static_cast<int>((b.max.x + 0.4f - in.bounds.min.x) / c));
    const int k0 = std::max(0, static_cast<int>((b.min.z - 0.4f - in.bounds.min.z) / c));
    const int k1 = std::min(in.nav_z - 1, static_cast<int>((b.max.z + 0.4f - in.bounds.min.z) / c));
    for (int i = i0; i <= i1; ++i) {
      for (int k = k0; k <= k1; ++k) open[static_cast<size_t>(i * in.nav_z + k)] = 0;
    }
  }
  // Steps from the door, one cell after the other (breadth first).
  const Vec3 door = in.exits[0].at;
  const int di = std::clamp(static_cast<int>((door.x - in.bounds.min.x) / c), 0, in.nav_x - 1);
  const int dk = std::clamp(static_cast<int>((door.z - in.bounds.min.z) / c), 0, in.nav_z - 1);
  std::vector<int> queue{di * in.nav_z + dk};
  in.nav[static_cast<size_t>(queue[0])] = 0;
  for (size_t q = 0; q < queue.size(); ++q) {
    const int cell = queue[q];
    const int i = cell / in.nav_z, k = cell % in.nav_z;
    for (const auto& [ni, nk] : {std::pair{i + 1, k}, std::pair{i - 1, k}, std::pair{i, k + 1}, std::pair{i, k - 1}}) {
      if (ni < 0 || nk < 0 || ni >= in.nav_x || nk >= in.nav_z) continue;
      const auto n = static_cast<size_t>(ni * in.nav_z + nk);
      if (!open[n] || in.nav[n] != Interior::kNoWay) continue;
      in.nav[n] = static_cast<uint16_t>(in.nav[static_cast<size_t>(cell)] + 1);
      queue.push_back(static_cast<int>(n));
    }
  }
}

bool way_out(const World& w, Vec2& dir) {
  const Interior& in = w.interior;
  if (!inside(w) || in.nav.empty() || in.exits.empty()) return false;
  const float c = Interior::kNavCell;
  const Vec3 p = w.player.pos;
  const Vec3 door = in.exits[0].at;
  const auto steps = [&](int i, int k) {
    return i < 0 || k < 0 || i >= in.nav_x || k >= in.nav_z ? Interior::kNoWay
                                                             : in.nav[static_cast<size_t>(i * in.nav_z + k)];
  };
  int i = static_cast<int>((p.x - in.bounds.min.x) / c), k = static_cast<int>((p.z - in.bounds.min.z) / c);
  // Not on a cell of the way (against a rock, up a stair): the nearest one around.
  if (steps(i, k) == Interior::kNoWay) {
    int best_i = -1, best_k = -1;
    float best = 1e9f;
    for (int a = -6; a <= 6; ++a) {
      for (int b = -6; b <= 6; ++b) {
        const float d2 = static_cast<float>(a * a + b * b);
        if (steps(i + a, k + b) != Interior::kNoWay && d2 < best) {
          best = d2;
          best_i = i + a;
          best_k = k + b;
        }
      }
    }
    if (best_i < 0) return false;
    i = best_i;
    k = best_k;
  }
  // A few cells ahead along the way: downhill on the steps (diagonals too).
  for (int n = 0; n < 6 && steps(i, k) > 0; ++n) {
    int next_i = i, next_k = k;
    uint16_t lowest = steps(i, k);
    for (int a = -1; a <= 1; ++a) {
      for (int b = -1; b <= 1; ++b) {
        const uint16_t s = steps(i + a, k + b);
        // A diagonal only if both sides are open (no cutting a wall's corner).
        if ((a != 0 && b != 0) && (steps(i + a, k) == Interior::kNoWay || steps(i, k + b) == Interior::kNoWay)) continue;
        if (s < lowest) {
          lowest = s;
          next_i = i + a;
          next_k = k + b;
        }
      }
    }
    if (next_i == i && next_k == k) break;
    i = next_i;
    k = next_k;
  }
  Vec2 to{in.bounds.min.x + c * (static_cast<float>(i) + 0.5f) - p.x, in.bounds.min.z + c * (static_cast<float>(k) + 0.5f) - p.z};
  if (steps(i, k) == 0 || distance2d(door.x - p.x, door.z - p.z) < 1.5f) to = {door.x - p.x, door.z - p.z};
  const float l = distance2d(to.x, to.z);
  if (l < 1e-3f) return false;
  dir = {to.x / l, to.z / l};
  return true;
}

// ---- Cristi ---------------------------------------------------------------------

void spawn_cristis(World& w) {
  const Interior& in = w.interior;
  std::vector<const RoomPiece*> chambers;
  for (const RoomPiece& p : in.pieces) {
    if (p.a == p.b) chambers.push_back(&p);
  }
  if (chambers.empty()) return;
  Rng& rng = w.rng;
  const uint32_t count = rules::kCaveCristisMin + rng.below(rules::kCaveCristisMore);
  for (uint32_t n = 0, attempt = 0; n < count && attempt < 200; ++attempt) {
    const RoomPiece& c = *chambers[rng.below(static_cast<uint32_t>(chambers.size()))];
    const Vec2 a = dir16(any_angle(rng));
    const float d = c.radius * std::sqrt(unit(rng));
    const float x = c.a.x + a.x * d, z = c.a.z + a.z * d;
    const uint32_t where = rng.below(10);  // 4 on the floor, 3 on the ceiling, 3 on a wall
    const Vec2 wall_a = dir16(any_angle(rng));
    const float wall_y = range(rng, 0.8f, 3.5f);
    Genes genes = roll_genes(Species::Cristi, Zone::Cave, rng);
    const auto level = static_cast<uint8_t>(5 + rng.below(4));
    const auto variant = static_cast<uint8_t>(rng.below(256));
    const auto turn = static_cast<Angle16>(rng.below(65536));
    if (wall_distance(in, x, z) < 1.0f || distance2d(x, z) < 6.0f) continue;  // not by the way in
    Vec3 at{x, 0, z};
    if (where >= 4) {
      Vec3 hit;
      const Vec3 dir = where < 7 ? Vec3{0, 1, 0} : Vec3{wall_a.x, 0, wall_a.z};
      if (!cast(in, {x, where < 7 ? 1.0f : wall_y, z}, dir, 16.0f, hit)) continue;
      at = hit;
    }
    Creature b;
    b.species = Species::Cristi;
    b.body.pos = at;
    b.body.up = where < 4 ? Vec3{0, 1, 0} : surface_normal(in, at);
    // Facing along the surface.
    const SinCos sc = sincos16(turn);
    const Vec3 f = normalized(Vec3{sc.sin, 0.3f, sc.cos} - b.body.up * dot(Vec3{sc.sin, 0.3f, sc.cos}, b.body.up),
                              {1, 0, 0});
    b.body.facing_x = f.x;
    b.body.facing_y = f.y;
    b.body.facing_z = f.z;
    b.home = {at.x, at.z};
    b.genes = genes;
    b.level = level;
    b.variant = variant;
    b.mind = Mind::Idle;
    add_creature(w, b);
    ++n;
  }
}

void step_cristi(World& w, Creature& c) {
  Body& b = c.body;
  const Body& p = w.player;
  const Vec3 away = b.pos - p.pos;
  const float d = length(away);
  if (c.mind != Mind::Flee && d < rules::kCristiWake) {
    c.mind = Mind::Flee;
    c.timer = static_cast<uint16_t>(rules::kTicksPerSecond + w.rng.below(rules::kTicksPerSecond));
  }
  if (c.mind != Mind::Flee) {
    c.mind = Mind::Idle;
    b.speed = 0;
    return;
  }
  const float ax = away.x, az = away.z;
  const float l = std::sqrt(ax * ax + az * az);
  const Vec3 dir = climb_direction(b, l > 1e-4f ? ax / l : b.facing_x, l > 1e-4f ? az / l : b.facing_z, 0, 1);
  climb(w, b, dir, species_info(c.species).run_speed);
  if (c.timer > 0) --c.timer;
  if (c.timer == 0) {
    c.mind = Mind::Idle;
    c.home = {b.pos.x, b.pos.z};
  }
}

}  // namespace monstres
