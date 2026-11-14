#include "monstres/render/scene.hpp"

#include "monstres/core/battle.hpp"
#include "monstres/core/variants.hpp"

#include "monstres/render/characters.hpp"
#include "monstres/render/terrain_mesh.hpp"
#include "monstres/core/continent.hpp"
#include "scene_util.hpp"

#include <algorithm>
#include <cmath>

namespace monstres {

// ---- Matrices ----------------------------------------------------------------

Mat4 multiply(const Mat4& a, const Mat4& b) {
  Mat4 r;
  for (int col = 0; col < 4; ++col) {
    for (int row = 0; row < 4; ++row) {
      float sum = 0;
      for (int k = 0; k < 4; ++k) sum += a.m[k * 4 + row] * b.m[col * 4 + k];
      r.m[col * 4 + row] = sum;
    }
  }
  return r;
}

Mat4 inverse(const Mat4& in) {
  const float* m = in.m;
  float inv[16];
  inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
  inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
  inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
  inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
  inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
  inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
  inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
  inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
  inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
  inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
  inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
  inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
  inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
  inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
  inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
  inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
  const float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
  Mat4 r;
  if (det == 0) return r;
  for (int i = 0; i < 16; ++i) r.m[i] = inv[i] / det;
  return r;
}

Mat4 perspective(float fov_y, float aspect, float n, float f) {
  const float t = 1.0f / std::tan(fov_y / 2);
  Mat4 r;
  r.m[0] = t / aspect;
  r.m[5] = t;
  r.m[10] = (f + n) / (n - f);
  r.m[11] = -1;
  r.m[14] = 2 * f * n / (n - f);
  r.m[15] = 0;
  return r;
}

Mat4 look_at(float ex, float ey, float ez, float tx, float ty, float tz) {
  float fx = tx - ex, fy = ty - ey, fz = tz - ez;
  float len = std::sqrt(fx * fx + fy * fy + fz * fz);
  fx /= len; fy /= len; fz /= len;
  // side = forward x up(0,1,0)
  float sx = -fz, sy = 0, sz = fx;
  len = std::sqrt(sx * sx + sz * sz);
  sx /= len; sz /= len;
  // up = side x forward
  const float ux = sy * fz - sz * fy, uy = sz * fx - sx * fz, uz = sx * fy - sy * fx;
  Mat4 r;
  r.m[0] = sx;  r.m[4] = sy;  r.m[8] = sz;
  r.m[1] = ux;  r.m[5] = uy;  r.m[9] = uz;
  r.m[2] = -fx; r.m[6] = -fy; r.m[10] = -fz;
  r.m[12] = -(sx * ex + sy * ey + sz * ez);
  r.m[13] = -(ux * ex + uy * ey + uz * ez);
  r.m[14] = fx * ex + fy * ey + fz * ez;
  return r;
}

// ---- Objects -------------------------------------------------------------------

MeshId mesh_for(const Object& o) {
  switch (o.kind) {
    case ObjectKind::Tree:  return MeshId::Tree;
    case ObjectKind::Pine:  return MeshId::Pine;
    case ObjectKind::Rock:  return mesh_offset(MeshId::Rock0, o.variant % kRockVariants);
    case ObjectKind::Log:   return MeshId::Log;
    case ObjectKind::Stump: return MeshId::Stump;
    case ObjectKind::Bush:  return MeshId::Bush;
    case ObjectKind::HouseCottage: return MeshId::HouseCottage;
    case ObjectKind::HouseChimney: return MeshId::HouseChimney;
    case ObjectKind::HouseHut: return MeshId::HouseHut;
    case ObjectKind::Fence: return MeshId::Fence;
    case ObjectKind::Sign: return MeshId::Sign;
    case ObjectKind::Well: return MeshId::Well;
    case ObjectKind::Barrel: return MeshId::Barrel;
    case ObjectKind::Lamp: return MeshId::Lamp;
    case ObjectKind::Tower: return MeshId::Tower;
    case ObjectKind::RuinWall: return o.variant & 1 ? MeshId::RuinWallHigh : MeshId::RuinWallLow;
    case ObjectKind::Pillar: return MeshId::Pillar;
    case ObjectKind::FallenPillar: return MeshId::FallenPillar;
    case ObjectKind::Rubble: return MeshId::Rubble;
    case ObjectKind::RuneStone: return MeshId::RuneStone;
    case ObjectKind::CastleWall: return MeshId::CastleWall;
    case ObjectKind::CastleTower: return MeshId::CastleTower;
    case ObjectKind::CastleGate: return MeshId::CastleGate;
    case ObjectKind::CastleKeep: return MeshId::CastleKeep;
    case ObjectKind::CaveMouth: return MeshId::CaveMouth;
    case ObjectKind::Crystal: return MeshId::Crystal;
    case ObjectKind::Collider: return MeshId::Count;  // invisible
    case ObjectKind::Teleporter: return MeshId::Teleporter;
    default:                return MeshId::Crate;
  }
}

using scene_util::add;
using scene_util::kTau;
using scene_util::lerp;
using scene_util::rand01;

void add_shadow(DrawList& list, float x, float z, float rx, float rz, float yaw, float alpha, float y, Quat tilt) {
  Instance s;
  s.x = x;
  s.y = y;
  s.z = z;
  s.rotation = tilt * quat_yaw(yaw);
  s.sx = rx;
  s.sy = 1;
  s.sz = rz;
  s.tint = {1, 1, 1, alpha};
  add(list, MeshId::Shadow, s);
}

Quat ground_tilt(const World& w, float x, float z) {
  // Shortest rotation from (0, 1, 0) to n: axis up x n, half angle via (1 + n.y).
  const Vec3 n = w.ground.normal(x, z);
  const float qx = n.z, qz = -n.x, qw = 1 + n.y;
  const float len = std::sqrt(qx * qx + qz * qz + qw * qw);
  return {qx / len, 0, qz / len, qw / len};
}

void add_ground_shadow(DrawList& list, const World& w, float x, float z, float rx, float rz, float yaw, float alpha) {
  add_shadow(list, x, z, rx, rz, yaw, alpha, w.ground.height(x, z) + kShadowHeight, ground_tilt(w, x, z));
}

namespace {

// Teleporters: a circle of runes on the stones, bright and turning once the
// teleporter is known, sparks rising from it; a column of light where the
// player just left and arrived.
void add_teleporter_effects(const World& w, float time, DrawList& list) {
  for (const Object& o : valley_objects(w)) {
    if (o.kind != ObjectKind::Teleporter) continue;
    const float dx = o.pos.x - w.player.pos.x, dz = o.pos.z - w.player.pos.z;
    if (dx * dx + dz * dz > 120.0f * 120.0f) continue;
    const bool known = (w.teleporters >> o.variant) & 1u;
    Instance seal;
    seal.x = o.pos.x;
    seal.y = o.pos.y + 0.2f;
    seal.z = o.pos.z;
    seal.rotation = quat_yaw(known ? time * 0.6f : 0.0f);
    seal.sx = seal.sz = 1.3f;
    seal.tint = known ? Color{0.7f, 1.6f, 2.0f, 0.85f} : Color{0.6f, 0.7f, 0.8f, 0.25f};
    add(list, MeshId::RuneCircle, seal);
    if (!known) continue;
    for (int i = 0; i < 8; ++i) {
      const float t = std::fmod(time * 0.5f + static_cast<float>(i) / 8, 1.0f);
      const float a = kTau * static_cast<float>(i) / 8 + time * 0.8f;
      Instance spark;
      spark.x = o.pos.x + std::cos(a) * 1.1f;
      spark.z = o.pos.z + std::sin(a) * 1.1f;
      spark.y = o.pos.y + 0.2f + 2.2f * t;
      spark.sx = spark.sy = spark.sz = 0.06f * (1 - 0.5f * t);
      spark.rotation = quat_yaw(a);
      spark.tint = {0.9f, 1.7f, 2.0f, std::min(1.0f, t * 5) * (1 - t)};
      add(list, MeshId::Sparkle, spark);
    }
  }
  if (w.teleport_tick > 0 && w.tick - w.teleport_tick < 40) {
    const float t = static_cast<float>(w.tick - w.teleport_tick) / 40.0f;
    for (const Vec3 at : {w.teleport_from, w.player.pos}) {
      Instance column;
      column.x = at.x;
      column.y = at.y + 2.0f;
      column.z = at.z;
      column.sx = column.sz = 0.7f * (1 - t) + 0.1f;
      column.sy = 2.4f;
      column.rotation = quat_yaw(time * 3);
      column.tint = {0.9f, 1.7f, 2.0f, 0.6f * (1 - t)};
      add(list, MeshId::Sparkle, column);
    }
  }
}

// Smoke rising from chimneys, ripples around fish.
void add_village_effects(const World& w, float time, DrawList& list) {
  constexpr int kPuffs = 7;
  for (const Object& o : valley_objects(w)) {
    if (o.kind != ObjectKind::HouseChimney) continue;
    // Chimney top, in the house's space (see house_chimney in tools/make_models.py).
    const SinCos sc = sincos16(o.yaw);
    const float lx = 1.3f, lz = 0.9f;
    const float cx = o.pos.x + lx * sc.cos + lz * sc.sin, cz = o.pos.z - lx * sc.sin + lz * sc.cos;
    const float offset = rand01(w.seed, 30000, static_cast<uint64_t>(o.pos.x * 10 + 1000));
    for (int i = 0; i < kPuffs; ++i) {
      const float t = std::fmod(time * 0.22f + offset + static_cast<float>(i) / kPuffs, 1.0f);  // 0 = just out
      Instance puff;
      puff.x = cx + t * t * 2.5f + std::sin(time * 0.9f + static_cast<float>(i)) * 0.15f * t;
      puff.y = o.pos.y + 6.55f + t * 4.5f;
      puff.z = cz + t * t * 1.0f;
      const float size = 0.25f + 0.85f * t;
      puff.sx = puff.sy = puff.sz = size;
      puff.rotation = quat_yaw(static_cast<float>(i) * 1.7f + t);
      const float alpha = std::min(1.0f, t * 6.0f) * (1.0f - t);
      const float grey = 0.95f - 0.15f * t;
      puff.tint = {grey, grey, grey, alpha};
      add(list, MeshId::Smoke, puff);
    }
  }
  // Magic sparks drifting up around the rune stones and the top of the tower.
  for (size_t k = 0; k < valley_objects(w).size(); ++k) {
    const Object& o = w.objects[k];
    if (o.kind != ObjectKind::RuneStone && o.kind != ObjectKind::Tower) continue;
    const bool tower = o.kind == ObjectKind::Tower;
    const int sparks = tower ? 10 : 5;
    for (int i = 0; i < sparks; ++i) {
      const auto u = static_cast<uint64_t>(k * 16 + static_cast<size_t>(i));
      const float t = std::fmod(time * (0.18f + 0.1f * rand01(w.seed, 31000, u)) + rand01(w.seed, 31100, u), 1.0f);
      const float a = kTau * rand01(w.seed, 31200, u) + time * 0.6f;
      const float r = (tower ? 2.6f : 0.5f) + 0.4f * std::sin(time * 1.3f + static_cast<float>(i));
      Instance spark;
      spark.x = o.pos.x + std::cos(a) * r;
      spark.z = o.pos.z + std::sin(a) * r;
      spark.y = o.pos.y + (tower ? 8.5f + t * 4.5f : 0.4f + t * 2.0f);
      spark.sx = spark.sy = spark.sz = (tower ? 0.09f : 0.06f) * (1 - 0.5f * t);
      spark.rotation = quat_yaw(time * 2 + static_cast<float>(i));
      const bool violet = (i % 3) == 0;
      const float fade = std::min(1.0f, t * 5) * (1 - t);
      spark.tint = violet ? Color{1.6f, 1.2f, 2.0f, fade} : Color{1.2f, 1.8f, 2.0f, fade};
      add(list, MeshId::Sparkle, spark);
    }
  }
  for (const Creature& c : w.creatures) {
    if (c.species != Species::Fish) continue;
    const Body& b = c.body;
    Instance ring;
    ring.x = b.pos.x;
    ring.y = rules::kWaterLevel + 0.01f;
    ring.z = b.pos.z;
    if (!b.on_ground) {
      // A splash ring that widens while the fish is in the air.
      const float k = std::clamp(1.0f - b.velocity_y / species_info(c.species).jump_speed, 0.0f, 2.0f);
      ring.sx = ring.sz = 0.4f + 0.6f * k;
      ring.tint = {1, 1, 1, 1.0f - 0.4f * k};
    } else {
      // A gentle ring while swimming.
      const float pulse = std::fmod(time * 0.8f + b.pos.x, 1.0f);
      ring.sx = ring.sz = 0.45f + 0.5f * pulse;
      ring.tint = {1, 1, 1, (1 - pulse) * std::min(1.0f, 0.3f + b.speed)};
    }
    add(list, MeshId::Ripple, ring);
  }
}

}  // namespace

void build_far_landscape(DrawList& list) {
  list.batches.assign(kMeshCount, {});
  list.cached = true;
  list.far = true;
  ++list.version;
  add(list, MeshId::FarGround, Instance{});
  Instance water;
  water.material = kWater;
  add(list, MeshId::FarWater, water);
  Instance lava;
  lava.material = kGlowing;
  add(list, MeshId::Lava, lava);
}

void build_far_dynamic(const World& w, float time, DrawList& list) {
  list.batches.resize(kMeshCount);
  for (auto& batch : list.batches) batch.clear();
  list.far = true;
  // The active volcano smokes: big puffs rising from its crater, drifting
  // away with the wind; lit by the lava from below at night.
  const Crater c = volcano_crater();
  const float night = 1 - sky_at(hour_of_day(w)).daylight;
  constexpr int kPuffs = 16;
  for (int i = 0; i < kPuffs; ++i) {
    const float t = std::fmod(time * 0.012f + static_cast<float>(i) / kPuffs, 1.0f);  // 0 = just out
    Instance puff;
    puff.x = c.center.x + t * t * 380.0f + std::sin(time * 0.05f + static_cast<float>(i)) * 25.0f * t;
    puff.y = c.lava_level + 30.0f + t * 520.0f;
    puff.z = c.center.z - t * t * 160.0f;
    const float size = 35.0f + 190.0f * t;
    puff.sx = puff.sz = size;
    puff.sy = size * 0.8f;
    puff.rotation = quat_yaw(static_cast<float>(i) * 1.7f + t);
    const float alpha = std::min(1.0f, t * 5.0f) * (1.0f - t) * 0.85f;
    const float grey = 0.62f - 0.12f * t;
    const float glow = night * (1 - std::clamp(t * 3.0f, 0.0f, 1.0f));
    puff.tint = {grey + glow * 0.9f, grey + glow * 0.3f, grey, alpha};
    add(list, MeshId::Smoke, puff);
  }
}

namespace {

// A ring lying at the feet of a creature (on the ground, or on the water).
void add_ring(DrawList& list, const World& w, const Creature& c, float radius, Color tint) {
  const Body& b = c.body;
  Instance ring;
  ring.x = b.pos.x;
  ring.z = b.pos.z;
  const float ground = w.ground.height(b.pos.x, b.pos.z);
  const bool on_ground = c.species != Species::Fish && b.pos.y - ground < 0.05f;
  ring.y = (on_ground ? ground : b.pos.y) + kShadowHeight + 0.01f;
  if (on_ground) ring.rotation = ground_tilt(w, b.pos.x, b.pos.z);
  ring.sx = ring.sz = radius;
  ring.tint = tint;
  add(list, MeshId::Ripple, ring);
}

void add_spark(DrawList& list, float x, float y, float z, float size, Color c, float alpha, float spin = 0) {
  Instance s;
  s.x = x;
  s.y = y;
  s.z = z;
  s.sx = s.sy = s.sz = size;
  s.rotation = quat_yaw(spin);
  s.tint = {c.r, c.g, c.b, alpha};
  add(list, MeshId::Sparkle, s);
}

Element creature_element(const Creature& c) { return creature_elements(c.species, c.genes).first; }

// Training: the magician's shield, a bubble of light that flashes when hit.
void add_shield(const World& w, float time, DrawList& list) {
  const Body& p = w.player;
  const float mid = p.pos.y + rules::kPlayerHeight * 0.55f;
  const uint64_t since = w.tick - w.battle.shield.parried_tick;
  const float flash = w.battle.shield.parried_tick > 0 && since < 16 ? 1 - static_cast<float>(since) / 16.0f : 0;
  add_spark(list, p.pos.x, mid, p.pos.z, 1.05f + 0.04f * std::sin(time * 3) + 0.15f * flash,
            {0.7f + 1.2f * flash, 1.1f + 0.8f * flash, 2.0f, 1}, 0.18f + 0.3f * flash, time * 0.4f);
  for (int k = 0; k < 6; ++k) {  // runes of light turning around it
    const float a = time * 1.2f + kTau * static_cast<float>(k) / 6;
    add_spark(list, p.pos.x + std::cos(a) * 0.95f, mid + 0.35f * std::sin(time * 2 + static_cast<float>(k)),
              p.pos.z + std::sin(a) * 0.95f, 0.06f, {1.0f, 1.6f, 2.0f, 1}, 0.8f, a);
  }
  if (flash > 0) {
    Creature ring_at;  // at the player's feet
    ring_at.species = Species::Villager;
    ring_at.body = p;
    add_ring(list, w, ring_at, 0.9f + 0.7f * (1 - flash), {1.4f, 1.8f, 2.2f, flash});
  }
}

// After a victory: golden sparks rise around the companion; a column of
// light if it grew a level.
void add_victory_effects(const World& w, float time, DrawList& list) {
  const Battle& b = w.battle;
  const bool won = b.outcome == BattleOutcome::Victory || b.outcome == BattleOutcome::Captured;
  const uint64_t since = w.tick - b.outcome_tick;
  if (!won || b.outcome_tick == 0 || since >= 150 || w.active < 0 || w.creatures.empty()) return;
  const Creature& c = w.creatures[0];
  const float t = static_cast<float>(since) / 150.0f;
  const float fade = 1 - t * t;
  for (int k = 0; k < 14; ++k) {
    const float u = std::fmod(t * 2.2f + static_cast<float>(k) / 14, 1.0f);
    const float a = kTau * static_cast<float>(k) / 14 + time * 2.0f;
    const float r = 0.45f + 0.25f * std::sin(static_cast<float>(k) * 1.3f);
    add_spark(list, c.body.pos.x + std::cos(a) * r, c.body.pos.y + 0.1f + 1.6f * u, c.body.pos.z + std::sin(a) * r,
              0.07f * (1 - 0.5f * u), {2.0f, 1.7f, 0.7f, 1}, fade * (1 - u), a);
  }
  if (since < 30) add_ring(list, w, c, 0.4f + 1.6f * static_cast<float>(since) / 30, {2.0f, 1.7f, 0.8f, 1 - static_cast<float>(since) / 30});
  bool level_up = false;
  for (const XpGain& g : b.outcome_gains) {
    level_up = level_up || (static_cast<int>(g.slot) == w.active && g.slot < w.team.size() && w.team[g.slot].level > g.level_before);
  }
  if (level_up) {
    Instance column;
    column.x = c.body.pos.x;
    column.y = c.body.pos.y + 1.5f;
    column.z = c.body.pos.z;
    column.sx = column.sz = 0.35f + 0.1f * std::sin(time * 8);
    column.sy = 1.6f;
    column.rotation = quat_yaw(time);
    column.tint = {2.0f, 1.9f, 1.2f, 0.45f * fade};
    add(list, MeshId::Sparkle, column);
    for (int k = 0; k < 3; ++k) {
      const float a = time * 4 + kTau * static_cast<float>(k) / 3;
      add_spark(list, c.body.pos.x + std::cos(a) * 0.6f, c.body.pos.y + 0.4f + 0.9f * std::fmod(t * 3 + 0.33f * static_cast<float>(k), 1.0f),
                c.body.pos.z + std::sin(a) * 0.6f, 0.12f, {2.0f, 2.0f, 1.4f, 1}, fade, a);
    }
  }
}

// Spells: a heal makes green sparks rise around the monster out (the healer:
// around the magician too); the shield is a golden bubble around it.
void add_spell_effects(const World& w, float time, DrawList& list) {
  if (w.creatures.empty()) return;
  const auto rise = [&](const Body& at, uint64_t tick, Color color, float height) {
    const uint64_t since = w.tick - tick;
    if (tick == 0 || since >= 60) return;
    const float t = static_cast<float>(since) / 60.0f;
    for (int k = 0; k < 10; ++k) {
      const float u = std::fmod(t * 1.6f + static_cast<float>(k) / 10, 1.0f);
      const float a = kTau * static_cast<float>(k) / 10 + time * 3.0f;
      add_spark(list, at.pos.x + std::cos(a) * 0.45f, at.pos.y + 0.1f + height * u, at.pos.z + std::sin(a) * 0.45f,
                0.07f, color, (1 - t) * (1 - u), a);
    }
  };
  const Body& c = w.creatures[0].body;
  if (w.active >= 0 && w.spell_cast == SpellId::Heal) rise(c, w.spell_tick, {0.6f, 2.0f, 0.8f, 1}, 1.2f);
  if (w.active >= 0) rise(c, w.healed_tick, {0.9f, 2.0f, 1.2f, 1}, 1.4f);
  rise(w.player, w.healed_tick, {0.9f, 2.0f, 1.2f, 1}, 1.8f);
  if (w.active >= 0 && w.battle.ally.shield_ticks > 0) {
    const BodyShape& sh = species_info(w.creatures[0].species).shape;
    const float k = static_cast<float>(w.battle.ally.shield_ticks) / rules::kShieldTicks;
    add_spark(list, c.pos.x, c.pos.y + sh.height * 0.5f, c.pos.z, sh.height * 0.9f + 0.3f + 0.03f * std::sin(time * 9),
              {2.0f, 1.7f, 0.6f, 1}, 0.2f + 0.2f * k, time);
  }
  // Mercy: three pale sparks turn slowly around the monster until it holds a blow back.
  if (w.active >= 0 && w.battle.mercy) {
    const BodyShape& sh = species_info(w.creatures[0].species).shape;
    for (int k = 0; k < 3; ++k) {
      const float a = time * 1.8f + kTau * static_cast<float>(k) / 3;
      add_spark(list, c.pos.x + std::cos(a) * (sh.half_width + 0.3f), c.pos.y + sh.height * 0.6f,
                c.pos.z + std::sin(a) * (sh.half_width + 0.3f), 0.06f, {1.6f, 1.8f, 2.2f, 1}, 0.8f, a);
    }
  }
  // Detection: the staff's crystal pulses; a very rare monster near: a trail
  // of light runs from it towards the monster.
  if (w.detect_ticks > 0) {
    const Vec3 at = crystal_position(w.player);
    const float fade = std::min(1.0f, static_cast<float>(w.detect_ticks) / 120.0f);
    const float pulse = 0.5f + 0.5f * std::sin(time * 5.0f);
    for (int k = 0; k < 6; ++k) {
      const float a = time * 2.5f + kTau * static_cast<float>(k) / 6;
      add_spark(list, at.x + std::cos(a) * 0.2f, at.y + 0.05f * std::sin(a * 2), at.z + std::sin(a) * 0.2f, 0.05f,
                {1.2f, 1.4f, 2.4f, 1}, (0.5f + 0.4f * pulse) * fade, a);
    }
    if (const int found = detect_rare(w); found > 0) {
      const Body& m = w.creatures[static_cast<size_t>(found)].body;
      const float dx = m.pos.x - at.x, dz = m.pos.z - at.z;
      const float d = std::sqrt(dx * dx + dz * dz);
      if (d > 0.1f) {
        // Closer: brighter.
        const float near = 1.0f - std::min(d / rules::kDetectRadius, 1.0f) * 0.6f;
        for (int k = 0; k < 7; ++k) {
          const float u = std::fmod(time * 1.4f + static_cast<float>(k) / 7, 1.0f);
          const float r = 0.25f + 1.6f * u;
          add_spark(list, at.x + dx / d * r, at.y - 0.1f * u, at.z + dz / d * r, 0.07f - 0.03f * u,
                    {2.4f, 1.9f, 0.7f, 1}, (1 - u) * near * fade, time * 4 + static_cast<float>(k));
        }
        add_spark(list, at.x, at.y, at.z, 0.16f + 0.05f * pulse, {2.4f, 1.9f, 0.7f, 1}, 0.45f * near * fade, time);
      }
    }
  }
}

// Battle: the enemy's red ring, what it is preparing, projectiles, bursts,
// parries, hits and stuns.
void add_battle_effects(const World& w, float time, DrawList& list) {
  const Battle& b = w.battle;
  add_victory_effects(w, time, list);
  add_spell_effects(w, time, list);
  if (b.phase == BattlePhase::Training && !w.creatures.empty()) add_shield(w, time, list);
  const bool training = b.phase == BattlePhase::Training;
  if ((b.phase != BattlePhase::Fight && b.phase != BattlePhase::Prepare && !training) || w.creatures.empty()) return;
  const Creature& ally = w.creatures[0];

  // The foes' rings: the one aimed at red and beating, the others fainter;
  // fast while one prepares an attack.
  for (size_t k = 0; k < b.foes.size() && !training; ++k) {
    const Foe& f = b.foes[k];
    const Creature& foe = w.creatures[f.index];
    const float foe_radius = species_info(foe.species).shape.half_width * 2.2f + 0.15f;
    const bool aimed = b.target == k;
    const bool windup = f.fighter.state == FighterState::Windup;
    const float beat = windup ? 0.12f * std::sin(time * 22.0f) : aimed ? 0.05f * std::sin(time * 4.0f) : 0;
    const Color color = windup ? Color{1.6f, 0.25f, 0.2f, aimed ? 1.0f : 0.8f}
                        : aimed ? Color{1.0f, 0.3f, 0.25f, 0.85f}
                                : Color{0.9f, 0.55f, 0.3f, 0.4f};
    add_ring(list, w, foe, foe_radius * (1 + beat), color);
    if (!windup) continue;
    const Move& m = species_moves(foe.species)[f.fighter.move];
    const float total = f.fighter.move == 0 ? 12.0f : f.fighter.move == 1 ? 24.0f : 30.0f;
    const float progress = 1 - static_cast<float>(f.fighter.state_ticks) / total;
    if (m.shape == MoveShape::Burst) {  // where the burst will reach
      add_ring(list, w, foe, m.range * (0.25f + 0.75f * progress), {1.5f, 0.3f, 0.2f, 0.5f + 0.4f * progress});
    } else if (m.shape == MoveShape::Projectile) {  // a ball of its element grows in front of it
      const BodyShape& sh = species_info(foe.species).shape;
      add_spark(list, foe.body.pos.x + foe.body.facing_x * sh.half_width, foe.body.pos.y + sh.height * 0.55f,
                foe.body.pos.z + foe.body.facing_z * sh.half_width, 0.08f + 0.18f * progress,
                element_glow(creature_element(foe)), 0.9f, time * 6);
    }
  }

  // Projectiles: a glowing ball and its trail.
  for (const Projectile& p : b.projectiles) {
    const Color c = element_glow(p.element);
    add_spark(list, p.pos.x, p.pos.y, p.pos.z, 0.24f, c, 1, time * 9);
    for (int k = 1; k <= 3; ++k) {
      const float back = 0.22f * static_cast<float>(k);
      add_spark(list, p.pos.x - p.dir_x * back, p.pos.y, p.pos.z - p.dir_z * back, 0.2f - 0.04f * static_cast<float>(k),
                c, 0.7f - 0.18f * static_cast<float>(k), time * 7 + static_cast<float>(k));
    }
  }

  const size_t sides = 1 + (training ? 0 : b.foes.size());
  for (size_t side = 0; side < sides; ++side) {
    if (side == 0 && w.active < 0) continue;  // in the crystal
    const Fighter& f = side == 0 ? b.ally : b.foes[side - 1].fighter;
    const Creature& c = side == 0 ? ally : w.creatures[b.foes[side - 1].index];
    const Monster* mine = side == 0 ? active_monster(w) : nullptr;
    const Species species = mine ? mine->species : c.species;
    const BodyShape& sh = species_info(c.species).shape;
    const float mid = c.body.pos.y + sh.height * 0.5f;
    // A burst: a ring of its element spreading around it.
    const uint64_t since_attack = w.tick - f.attack_tick;
    if (f.attack_tick > 0 && f.attack_move == 2 && since_attack < 24) {
      const float t = static_cast<float>(since_attack) / 24.0f;
      const Move& m = species_moves(species)[2];
      const Color glow = element_glow(mine ? creature_elements(species, mine->genes).first : creature_element(c));
      add_ring(list, w, c, m.range * (0.3f + 0.7f * t), {glow.r, glow.g, glow.b, 1 - t});
      for (int k = 0; k < 10; ++k) {
        const float a = kTau * static_cast<float>(k) / 10 + t;
        const float r = m.range * t;
        add_spark(list, c.body.pos.x + std::cos(a) * r, c.body.pos.y + 0.3f + 0.4f * t, c.body.pos.z + std::sin(a) * r,
                  0.12f, glow, 1 - t, a);
      }
    }
    // Guarding: a bubble of light; a blocked hit flashes.
    if (f.state == FighterState::Parry) {
      add_spark(list, c.body.pos.x, mid, c.body.pos.z, sh.height * 0.9f + 0.2f, {0.8f, 1.2f, 2.0f, 1}, 0.28f, time);
    }
    if (f.parried_tick > 0 && w.tick - f.parried_tick < 14) {
      const float t = static_cast<float>(w.tick - f.parried_tick) / 14.0f;
      add_ring(list, w, c, sh.half_width * 2 + 0.8f * t, {1.8f, 1.9f, 2.2f, 1 - t});
    }
    // Hurt: sparks fly off.
    if (f.hit_tick > 0 && w.tick - f.hit_tick < 12) {
      const float t = static_cast<float>(w.tick - f.hit_tick) / 12.0f;
      for (int k = 0; k < 6; ++k) {
        const float a = kTau * static_cast<float>(k) / 6 + static_cast<float>(f.hit_tick % 7);
        const float r = 0.2f + 0.6f * t;
        add_spark(list, c.body.pos.x + std::cos(a) * r, mid + 0.4f * t, c.body.pos.z + std::sin(a) * r, 0.09f,
                  {2.0f, 1.8f, 1.2f, 1}, 1 - t, a);
      }
    }
    // Stunned: little stars turning above its head.
    if (f.state == FighterState::Stunned) {
      for (int k = 0; k < 3; ++k) {
        const float a = time * 5 + kTau * static_cast<float>(k) / 3;
        add_spark(list, c.body.pos.x + std::cos(a) * 0.3f, c.body.pos.y + sh.height + 0.25f,
                  c.body.pos.z + std::sin(a) * 0.3f, 0.07f, {2.0f, 1.9f, 0.6f, 1}, 1, a);
      }
    }
    // Dodging: a trail of light.
    if (f.state == FighterState::Dodge) {
      add_spark(list, c.body.pos.x, mid, c.body.pos.z, 0.18f, {1.6f, 1.7f, 2.0f, 1}, 0.5f, time);
    }
  }
}

}  // namespace

void build_dynamic(const World& w, float time, DrawList& list, int selected, const Frustum* view) {
  list.batches.resize(kMeshCount);
  for (auto& batch : list.batches) batch.clear();

  add_characters(w, time, list, view);
  add_village_effects(w, time, list);
  add_teleporter_effects(w, time, list);

  // A golden ring, gently pulsing, at the feet of the selected creature.
  if (selected >= 0 && static_cast<size_t>(selected) < w.creatures.size()) {
    const Creature& c = w.creatures[static_cast<size_t>(selected)];
    const float radius = species_info(c.species).shape.half_width * 2.2f + 0.15f;
    add_ring(list, w, c, radius * (1 + 0.06f * std::sin(time * 4.0f)), {1.0f, 0.85f, 0.25f, 1.0f});
  }
  add_battle_effects(w, time, list);
  add_crystal_effects(w, time, list);

  // At night: a glowing halo around each lantern (their light is in build_view).
  const float dark = night_amount(w);
  if (dark > 0.01f) {
    for (const Object& o : valley_objects(w)) {
      if (o.kind != ObjectKind::Lamp) continue;
      Instance halo;
      halo.x = o.pos.x;
      halo.y = o.pos.y + 2.29f;
      halo.z = o.pos.z;
      halo.sx = halo.sy = halo.sz = 0.45f + 0.03f * std::sin(time * 3 + o.pos.x);
      halo.tint = {1.6f, 1.15f, 0.55f, 0.35f * dark};
      add(list, MeshId::Sparkle, halo);
    }
  }

  // Clouds drifting with the wind, wrapping around.
  constexpr int kClouds = 16;
  constexpr float kSpan = 440.0f;
  for (int i = 0; i < kClouds; ++i) {
    const auto u = static_cast<uint64_t>(i);
    Instance c;
    const float x0 = kSpan * rand01(w.seed, 20000 + u) - kSpan / 2;
    c.x = std::fmod(x0 + time * 1.5f + kSpan * 10, kSpan) - kSpan / 2;
    c.z = kSpan * rand01(w.seed, 20100 + u) - kSpan / 2;
    c.y = lerp(32.0f, 50.0f, rand01(w.seed, 20200 + u));
    c.rotation = quat_yaw(0.6f * (rand01(w.seed, 20300 + u) - 0.5f));
    c.sx = lerp(14.0f, 28.0f, rand01(w.seed, 20400 + u));
    c.sy = lerp(4.0f, 7.0f, rand01(w.seed, 20500 + u));
    c.sz = lerp(8.0f, 15.0f, rand01(w.seed, 20600 + u));
    // Clouds glow by day (brighter than the lighting alone), turn grey-blue at night.
    c.tint = {lerp(1.45f, 0.16f, dark), lerp(1.45f, 0.18f, dark), lerp(1.45f, 0.26f, dark), 1};
    add(list, mesh_offset(MeshId::Cloud0, u % kCloudVariants), c);
  }
}

float camera_distance(const World& w, float tx, float ty, float tz, float dx, float dy, float dz, float wanted) {
  constexpr float kMargin = 0.25f;   // keep the camera this far from objects
  constexpr float kClosest = 1.2f;
  float best = wanted;
  // Only the objects around the line from the target to where the camera would be.
  const float ex = tx + dx * wanted, ey = ty + dy * wanted, ez = tz + dz * wanted;
  const Box around{{std::min(tx, ex) - 1, std::min(ty, ey) - 1, std::min(tz, ez) - 1},
                   {std::max(tx, ex) + 1, std::max(ty, ey) + 1, std::max(tz, ez) + 1}};
  std::vector<uint32_t> near;
  for_objects_near(w, around, [&](uint32_t k, const Box&) { near.push_back(k); });
  for (const uint32_t k : near) {
    const Box b = objects_index(w).boxes[k];
    // Ray (t, d) against the box widened by the margin (slab method).
    const float lo[3] = {b.min.x - kMargin, b.min.y - kMargin, b.min.z - kMargin};
    const float hi[3] = {b.max.x + kMargin, b.max.y + kMargin, b.max.z + kMargin};
    const float origin[3] = {tx, ty, tz}, dir[3] = {dx, dy, dz};
    float enter = 0, leave = wanted;
    bool hit = true;
    for (int a = 0; a < 3 && hit; ++a) {
      if (std::fabs(dir[a]) < 1e-6f) {
        hit = origin[a] > lo[a] && origin[a] < hi[a];
        continue;
      }
      float t0 = (lo[a] - origin[a]) / dir[a], t1 = (hi[a] - origin[a]) / dir[a];
      if (t0 > t1) std::swap(t0, t1);
      enter = std::max(enter, t0);
      leave = std::min(leave, t1);
      hit = enter <= leave;
    }
    if (hit && enter > 0) best = std::min(best, enter);
  }
  // The ground (a hill behind the player): stop just before it.
  constexpr float kStep = 0.2f;
  for (float t = kStep; t < best; t += kStep) {
    if (ty + dy * t < w.ground.height(tx + dx * t, tz + dz * t) + kMargin) {
      best = std::max(t - kStep, 0.0f);
      break;
    }
  }
  return std::max(best, kClosest);
}

namespace {

struct Ray {
  float origin[3];
  float dir[3];  // not normalized: t = 1 is the far plane
};

// Point (ndc_x, ndc_y, ndc_z) of the screen, back in the world.
void unproject(const Mat4& inv, float x, float y, float z, float out[3]) {
  const float* m = inv.m;
  const float v[4] = {x, y, z, 1};
  float r[4];
  for (int row = 0; row < 4; ++row) {
    r[row] = m[row] * v[0] + m[4 + row] * v[1] + m[8 + row] * v[2] + m[12 + row] * v[3];
  }
  for (int i = 0; i < 3; ++i) out[i] = r[i] / r[3];
}

Ray screen_ray(const View& view, float ndc_x, float ndc_y) {
  Ray ray;
  float far_point[3];
  unproject(view.inverse_view_projection, ndc_x, ndc_y, -1, ray.origin);
  unproject(view.inverse_view_projection, ndc_x, ndc_y, 1, far_point);
  for (int i = 0; i < 3; ++i) ray.dir[i] = far_point[i] - ray.origin[i];
  return ray;
}

float ray_box(const Ray& ray, const Box& b);

// Whether an object (but `skip`) is on the ray before `until` (the objects
// around that stretch of the ray only).
bool ray_hits_object(const World& w, const Ray& ray, float until, int skip) {
  float lo[3], hi[3];
  for (int i = 0; i < 3; ++i) {
    const float a = ray.origin[i], b = ray.origin[i] + ray.dir[i] * until;
    lo[i] = std::min(a, b) - 0.5f;
    hi[i] = std::max(a, b) + 0.5f;
  }
  bool hit = false;
  for_objects_near(w, Box{{lo[0], lo[1], lo[2]}, {hi[0], hi[1], hi[2]}}, [&](uint32_t k, const Box& b) {
    if (hit || static_cast<int>(k) == skip) return;
    const float t = ray_box(ray, b);
    hit = t >= 0 && t < until;
  });
  return hit;
}

// Where the ray enters the box (t in [0, 1]), or a negative value if it misses.
float ray_box(const Ray& ray, const Box& b) {
  const float lo[3] = {b.min.x, b.min.y, b.min.z};
  const float hi[3] = {b.max.x, b.max.y, b.max.z};
  float enter = 0, leave = 1;
  for (int a = 0; a < 3; ++a) {
    if (std::fabs(ray.dir[a]) < 1e-9f) {
      if (ray.origin[a] < lo[a] || ray.origin[a] > hi[a]) return -1;
      continue;
    }
    float t0 = (lo[a] - ray.origin[a]) / ray.dir[a], t1 = (hi[a] - ray.origin[a]) / ray.dir[a];
    if (t0 > t1) std::swap(t0, t1);
    enter = std::max(enter, t0);
    leave = std::min(leave, t1);
    if (enter > leave) return -1;
  }
  return enter;
}

}  // namespace

int pick_creature(const World& w, const View& view, float ndc_x, float ndc_y) {
  constexpr float kMargin = 0.2f;  // easier to tap on a phone
  const Ray ray = screen_ray(view, ndc_x, ndc_y);
  int best = -1;
  float best_t = 2;
  for (size_t i = 0; i < w.creatures.size(); ++i) {
    if (i == 0 && w.active < 0) continue;  // in the crystal
    const Creature& c = w.creatures[i];
    Box b = body_box(c.body, species_info(c.species).shape);
    b.min = b.min - Vec3{kMargin, kMargin * 0.5f, kMargin};
    b.max = b.max + Vec3{kMargin, kMargin, kMargin};
    const float t = ray_box(ray, b);
    if (t >= 0 && t < best_t) {
      best_t = t;
      best = static_cast<int>(i);
    }
  }
  if (best < 0) return -1;
  // Hidden behind an object or a hill?
  if (ray_hits_object(w, ray, best_t, -1)) return -1;
  float hit[3];
  for (int i = 0; i < 3; ++i) hit[i] = ray.origin[i] + ray.dir[i] * best_t;
  const float dx = hit[0] - ray.origin[0], dy = hit[1] - ray.origin[1], dz = hit[2] - ray.origin[2];
  const float length = std::sqrt(dx * dx + dy * dy + dz * dz);
  for (float d = 0.5f; d < length - 0.5f; d += 0.5f) {
    const float k = d / length;
    if (ray.origin[1] + dy * k < w.ground.height(ray.origin[0] + dx * k, ray.origin[2] + dz * k)) return -1;
  }
  return best;
}

int pick_object(const World& w, const View& view, float ndc_x, float ndc_y) {
  const Ray ray = screen_ray(view, ndc_x, ndc_y);
  int best = -1;
  float best_t = 2;
  for (size_t i = 0; i < valley_objects(w).size(); ++i) {
    const Object& o = w.objects[i];
    if (o.kind != ObjectKind::Sign && o.kind != ObjectKind::Teleporter) continue;
    // A signpost's planks, wider than the post (easier to tap); a teleporter's circle and stones.
    const float r = o.kind == ObjectKind::Sign ? 0.7f : 2.0f;
    const float h = o.kind == ObjectKind::Sign ? 1.9f : 1.8f;
    const Box b{{o.pos.x - r, o.pos.y, o.pos.z - r}, {o.pos.x + r, o.pos.y + h, o.pos.z + r}};
    const float t = ray_box(ray, b);
    if (t >= 0 && t < best_t) {
      best_t = t;
      best = static_cast<int>(i);
    }
  }
  if (best < 0) return -1;
  // Close enough to read it, and not hidden behind something.
  const Object& sign = w.objects[static_cast<size_t>(best)];
  const float px = sign.pos.x - w.player.pos.x, pz = sign.pos.z - w.player.pos.z;
  if (px * px + pz * pz > kReadDistance * kReadDistance) return -1;
  if (ray_hits_object(w, ray, best_t - 1e-4f, best)) return -1;
  return best;
}

Portrait build_portrait(const PortraitSubject& subject, float aspect) {
  Portrait p;
  p.list.batches.assign(kMeshCount, {});
  const float height = add_portrait_figure(p.list, subject, 0.6f);
  // Framed so that the figure fills about 80% of the height.
  const float distance = height * 1.3f + 0.3f;
  const float tx = 0, ty = height * 0.48f, tz = 0;
  const float ex = distance * 0.12f, ey = ty + distance * 0.22f, ez = distance;
  p.view.view_projection = multiply(perspective(0.8f, aspect, 0.05f, 50.0f), look_at(ex, ey, ez, tx, ty, tz));
  p.view.inverse_view_projection = inverse(p.view.view_projection);
  p.view.camera_x = ex;
  p.view.camera_y = ey;
  p.view.camera_z = ez;
  const float l = std::sqrt(0.5f * 0.5f + 0.9f * 0.9f + 0.8f * 0.8f);
  p.view.light_x = -0.5f / l;
  p.view.light_y = 0.9f / l;
  p.view.light_z = 0.8f / l;
  p.view.sun_x = p.view.light_x;
  p.view.sun_y = p.view.light_y;
  p.view.sun_z = p.view.light_z;
  p.view.fog = palette::kSkyHorizon;
  p.view.fog_start = 1000;
  p.view.fog_end = 2000;
  p.view.sky = false;
  return p;
}

bool creature_on_screen(const World& w, const View& view, int index, float& ndc_x, float& ndc_y) {
  if (index < -1 || index >= static_cast<int>(w.creatures.size())) return false;
  Vec3 p = w.player.pos + Vec3{0, rules::kPlayerHeight * 0.6f, 0};  // -1: the magician
  if (index >= 0) {
    const Creature& c = w.creatures[static_cast<size_t>(index)];
    p = c.body.pos + Vec3{0, species_info(c.species).shape.height * 0.5f, 0};
  }
  const float* m = view.view_projection.m;
  const float v[4] = {p.x, p.y, p.z, 1};
  float r[4];
  for (int row = 0; row < 4; ++row) {
    r[row] = m[row] * v[0] + m[4 + row] * v[1] + m[8 + row] * v[2] + m[12 + row] * v[3];
  }
  if (r[3] <= 0) return false;
  ndc_x = r[0] / r[3];
  ndc_y = r[1] / r[3];
  return true;
}

namespace {

struct SkyKey {
  float hour;
  SkyState state;
};

// Colors of the sky and of the light through the day (linear in between).
constexpr SkyState kNight{{0.015f, 0.025f, 0.075f, 1}, {0.05f, 0.07f, 0.16f, 1}, {0.10f, 0.12f, 0.22f, 1},
                          {0.05f, 0.06f, 0.09f, 1}, {0.16f, 0.20f, 0.32f, 1}, 0};
constexpr SkyState kDawn{{0.24f, 0.30f, 0.55f, 1}, {0.95f, 0.62f, 0.45f, 1}, {0.40f, 0.38f, 0.46f, 1},
                         {0.28f, 0.26f, 0.25f, 1}, {0.62f, 0.42f, 0.30f, 1}, 0.55f};
constexpr SkyState kDay{palette::kSkyZenith, palette::kSkyHorizon, {0.52f, 0.58f, 0.70f, 1},
                        {0.36f, 0.40f, 0.32f, 1}, {0.62f, 0.589f, 0.527f, 1}, 1};
constexpr SkyState kDusk{{0.18f, 0.18f, 0.42f, 1}, {1.0f, 0.52f, 0.33f, 1}, {0.38f, 0.32f, 0.42f, 1},
                         {0.27f, 0.23f, 0.22f, 1}, {0.60f, 0.36f, 0.24f, 1}, 0.45f};
constexpr SkyKey kSkyKeys[] = {{0, kNight}, {4.5f, kNight}, {6, kDawn}, {8, kDay}, {16.5f, kDay},
                               {18.5f, kDusk}, {20.5f, kNight}, {24, kNight}};

Color mix(Color a, Color b, float t) { return {lerp(a.r, b.r, t), lerp(a.g, b.g, t), lerp(a.b, b.b, t), 1}; }

}  // namespace

SkyState sky_at(float hour) {
  hour = std::fmod(std::fmod(hour, 24.0f) + 24.0f, 24.0f);
  for (size_t i = 1; i < std::size(kSkyKeys); ++i) {
    const SkyKey& a = kSkyKeys[i - 1];
    const SkyKey& b = kSkyKeys[i];
    if (hour > b.hour) continue;
    const float t = (hour - a.hour) / (b.hour - a.hour);
    return {mix(a.state.zenith, b.state.zenith, t), mix(a.state.horizon, b.state.horizon, t),
            mix(a.state.ambient_sky, b.state.ambient_sky, t), mix(a.state.ambient_ground, b.state.ambient_ground, t),
            mix(a.state.light, b.state.light, t), lerp(a.state.daylight, b.state.daylight, t)};
  }
  return kNight;
}

SkyDirections sky_directions(float hour) {
  // The sun rises in the east at 6:00, is highest at noon (ahead when
  // looking towards -Z), sets at 18:00. The moon is on the other side.
  const float a = (hour - 12.0f) / 12.0f * static_cast<float>(kPi);
  const float south_x = 0.47f, south_z = -0.88f;
  const float hx = std::cos(a) * south_x - std::sin(a) * south_z;
  const float hz = std::cos(a) * south_z + std::sin(a) * south_x;
  const float elevation = std::sin((hour - 6.0f) / 12.0f * static_cast<float>(kPi)) * 0.95f;
  const auto normalized = [](Vec3 v) {
    const float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return Vec3{v.x / l, v.y / l, v.z / l};
  };
  SkyDirections d;
  d.sun = normalized({std::cos(elevation) * hx, std::sin(elevation), std::cos(elevation) * hz});
  // The moon stays low enough to be seen while walking.
  d.moon = normalized({-d.sun.x, -d.sun.y * 0.4f, -d.sun.z});
  // Surfaces are lit from higher than the disc (the ground stays bright
  // with a low sun); at night by the moon.
  const Vec3 from = d.sun.y > -0.05f ? d.sun : d.moon;
  d.light = normalized({from.x, std::max(from.y, 0.0f) + 0.7f, from.z});
  return d;
}

float night_amount(const World& world) { return 1 - sky_at(hour_of_day(world)).daylight; }

namespace {

// Where the camera turns around, what it looks at, and how far it stays.
struct CameraGoal {
  float tx, ty, tz;           // pivot
  float look_x, look_y, look_z;
  float zoom;                 // times Camera::distance
};

CameraGoal camera_goal(const World& w, CameraSubject subject) {
  const Player& p = w.player;
  CameraGoal g{p.pos.x, p.pos.y + 1.0f, p.pos.z, p.pos.x, p.pos.y + 1.0f, p.pos.z, 1.0f};
  if (subject != CameraSubject::Companion || w.creatures.empty()) return g;
  // In a battle the camera follows the companion; in a fight it looks
  // between the companion and its enemy (the yaw is turned towards it, see
  // the platform code), when training between it and the magician.
  const Body& a = w.creatures[0].body;
  const Body* other = nullptr;
  float other_mid = 0.5f;
  if (const Foe* t = battle_target(w)) {
    other = &w.creatures[t->index].body;
  } else if (w.battle.phase == BattlePhase::Prepare && !w.battle.foes.empty()) {
    other = &w.creatures[w.battle.foes[0].index].body;
  } else if (w.battle.phase == BattlePhase::Training && w.battle.target != kNoTarget) {
    other = &w.player;
    other_mid = 0.8f;
  }
  // Aiming: closer, between the companion and its target. Moving freely (or
  // after a battle): the same camera as for the magician, on the companion.
  const float up = other ? 0.75f : 1.0f;
  g = {a.pos.x, a.pos.y + up, a.pos.z, a.pos.x, a.pos.y + up, a.pos.z, other ? 0.8f : 1.0f};
  if (other) {
    g.look_x = lerp(g.tx, other->pos.x, 0.4f);
    g.look_y = lerp(g.ty, other->pos.y + other_mid, 0.4f);
    g.look_z = lerp(g.tz, other->pos.z, 0.4f);
  }
  return g;
}

}  // namespace

CameraSubject camera_subject(const World& w) {
  if (w.battle.phase != BattlePhase::None) return CameraSubject::Companion;
  // Just after a battle: a moment on the companion (its victory), unless the player walks away.
  const Battle& b = w.battle;
  const bool after = b.outcome == BattleOutcome::Victory || b.outcome == BattleOutcome::Captured;
  if (after && w.tick - b.outcome_tick < kCameraHoldTicks && w.active >= 0 && w.player.speed < 0.5f) {
    return CameraSubject::Companion;
  }
  return CameraSubject::Magician;
}

void update_camera(Camera& cam, const World& w, float dt) {
  const CameraSubject wanted = camera_subject(w);
  if (!cam.smooth) {
    cam.smooth = true;
    cam.subject = cam.previous = wanted;
    cam.blend = 1;
    return;
  }
  if (wanted != cam.subject) {
    // From wherever the camera is (maybe halfway), towards the new subject.
    cam.previous = cam.blend < 0.5f ? cam.previous : cam.subject;
    cam.subject = wanted;
    cam.blend = cam.blend < 0.5f ? 1 - cam.blend : 0;
  }
  cam.blend = std::min(1.0f, cam.blend + dt / kCameraBlendSeconds);
}

View build_view(const World& w, const Camera& cam, float aspect, float time) {
  CameraGoal g = camera_goal(w, cam.smooth ? cam.subject : camera_subject(w));
  if (cam.smooth && cam.blend < 1) {
    // Gliding from one subject to the other (both may move meanwhile).
    const CameraGoal from = camera_goal(w, cam.previous);
    const float t = cam.blend * cam.blend * (3 - 2 * cam.blend);
    g = {lerp(from.tx, g.tx, t), lerp(from.ty, g.ty, t), lerp(from.tz, g.tz, t), lerp(from.look_x, g.look_x, t),
         lerp(from.look_y, g.look_y, t), lerp(from.look_z, g.look_z, t), lerp(from.zoom, g.zoom, t)};
  }
  const float tx = g.tx, ty = g.ty, tz = g.tz;
  const float look_x = g.look_x, look_y = g.look_y, look_z = g.look_z;
  const float wanted = cam.distance * g.zoom;

  // The camera sits behind the player, opposite to where it looks, and comes
  // closer when an object (a trunk, a rock...) is in between.
  const float cp = std::cos(cam.pitch), sp = std::sin(cam.pitch);
  const float fx = -std::sin(cam.yaw) * cp, fz = -std::cos(cam.yaw) * cp;
  const float distance = camera_distance(w, tx, ty, tz, -fx, sp, -fz, wanted);
  float ex = tx - fx * distance;
  float ey = ty + sp * distance;
  float ez = tz - fz * distance;
  ey = std::max(ey, w.ground.height(ex, ez) + 0.2f);  // never below the ground

  View v;
  const Mat4 look = look_at(ex, ey, ez, look_x, look_y, look_z);
  v.view_projection = multiply(perspective(1.0f, aspect, 0.1f, view_range::kNearFar), look);
  v.far_view_projection = multiply(perspective(1.0f, aspect, view_range::kFarNear, view_range::kFarFar), look);
  v.fog_start = view_range::kFogStart;
  v.fog_half = view_range::kFogHalf;
  v.fog_end = view_range::kFogEnd;
  v.inverse_view_projection = inverse(v.view_projection);
  v.camera_x = ex;
  v.camera_y = ey;
  v.camera_z = ez;

  // Sun, moon, sky and light from the time of day.
  const float hour = hour_of_day(w);
  const SkyState sky = sky_at(hour);
  const SkyDirections dirs = sky_directions(hour);
  v.sun_x = dirs.sun.x;
  v.sun_y = dirs.sun.y;
  v.sun_z = dirs.sun.z;
  v.moon_x = dirs.moon.x;
  v.moon_y = dirs.moon.y;
  v.moon_z = dirs.moon.z;
  v.light_x = dirs.light.x;
  v.light_y = dirs.light.y;
  v.light_z = dirs.light.z;
  v.sky_zenith = sky.zenith;
  v.sky_horizon = sky.horizon;
  v.fog = sky.horizon;
  v.light_color = sky.light;
  v.ambient_sky = sky.ambient_sky;
  v.ambient_ground = sky.ambient_ground;
  v.night = 1 - sky.daylight;
  v.light_count = world_lights(w, tx, tz, v.night, time, v.lights);
  v.time = time;
  // Grass and small things shrink away around the player (see build_landscape).
  v.fade_x = w.player.pos.x;
  v.fade_z = w.player.pos.z;
  v.fade_near_start = landscape::kGrassFadeStart;
  v.fade_near_end = landscape::kGrassFadeEnd;
  v.fade_far_start = landscape::kDetailFadeStart;
  v.fade_far_end = landscape::kDetailFadeEnd;
  v.fade_tree_start = landscape::kTreeFadeStart;
  v.fade_tree_end = landscape::kTreeFadeEnd;
  return v;
}

}  // namespace monstres
