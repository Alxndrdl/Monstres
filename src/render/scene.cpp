#include "monstres/render/scene.hpp"

#include "monstres/render/characters.hpp"
#include "monstres/render/terrain_mesh.hpp"

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
    default:                return MeshId::Crate;
  }
}

namespace {

constexpr float kTau = 6.28318530718f;
constexpr float kDecorationHalfSize = rules::kWorldHalfSize + 13; // grass and flowers up to here
constexpr float kForestOuter = rules::kWorldHalfSize + 19;        // trees beyond the edges, up to here

// Deterministic pseudo-random number in [0, 1) for decoration.
float rand01(uint64_t seed, uint64_t a, uint64_t b = 0) {
  uint64_t z = seed * 0x9E3779B97F4A7C15ULL + a * 0xC2B2AE3D27D4EB4FULL + b * 0x165667B19E3779F9ULL + 0x632BE59BD9B4E019ULL;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  z ^= z >> 31;
  return static_cast<float>(z >> 40) / static_cast<float>(1ULL << 24);
}

float lerp(float a, float b, float t) { return a + (b - a) * t; }

void add(DrawList& list, MeshId id, const Instance& in) { list.batches[static_cast<size_t>(id)].push_back(in); }

}  // namespace

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

// Mesh scale, matching the sizes of tools/make_models.py and the collision
// boxes of world.cpp.
void scale_for(const Object& o, Instance& in) {
  const Vec3 s = o.size;
  switch (o.kind) {
    case ObjectKind::Tree:
    case ObjectKind::Pine:  in.sx = in.sy = in.sz = s.x; break;
    case ObjectKind::Rock:  in.sx = s.x; in.sy = s.y; in.sz = s.z; break;
    case ObjectKind::Log:   in.sx = s.x; in.sy = s.y; in.sz = s.y; break;
    case ObjectKind::Stump:
    case ObjectKind::Bush:  in.sx = s.x; in.sy = s.y; in.sz = s.x; break;
    case ObjectKind::Crate: in.sx = s.x; in.sy = s.y; in.sz = s.z; in.y += s.y; break;  // centered cube
    case ObjectKind::Pillar: in.sx = in.sz = 1; in.sy = s.y; break;                    // unit height
    default:                in.sx = in.sy = in.sz = 1; break;  // village: real-size models
  }
}

float wind_for(ObjectKind kind) {
  switch (kind) {
    case ObjectKind::Tree: return 0.25f;
    case ObjectKind::Pine: return 0.15f;
    case ObjectKind::Bush: return 0.35f;
    default:               return 0;
  }
}

void add_object(DrawList& list, const World& w, const Object& o) {
  Instance in;
  in.x = o.pos.x;
  in.y = o.pos.y;
  in.z = o.pos.z;
  const float yaw = static_cast<float>(radians_from_angle16(o.yaw));
  in.rotation = quat_yaw(yaw);
  scale_for(o, in);
  in.wind = wind_for(o.kind);
  const float k = 0.88f + 0.24f * static_cast<float>(o.variant) / 255.0f;  // slight color variation
  in.tint = {k, k, k, 1};
  add(list, mesh_for(o), in);

  // Soft shadow on the ground, sized to the object.
  const Vec3 s = o.size;
  switch (o.kind) {
    case ObjectKind::Tree:  add_ground_shadow(list, w, o.pos.x, o.pos.z, 1.7f * s.x, 1.7f * s.x, 0); break;
    case ObjectKind::Pine:  add_ground_shadow(list, w, o.pos.x, o.pos.z, 1.4f * s.x, 1.4f * s.x, 0); break;
    case ObjectKind::Rock:  add_ground_shadow(list, w, o.pos.x, o.pos.z, 1.25f * s.x, 1.25f * s.z, yaw); break;
    case ObjectKind::Log:   add_ground_shadow(list, w, o.pos.x, o.pos.z, 1.15f * s.x, 1.8f * s.y, yaw); break;
    case ObjectKind::Stump: add_ground_shadow(list, w, o.pos.x, o.pos.z, 1.5f * s.x, 1.5f * s.x, 0); break;
    case ObjectKind::Bush:  add_ground_shadow(list, w, o.pos.x, o.pos.z, 1.3f * s.x, 1.3f * s.x, 0); break;
    case ObjectKind::Crate: add_ground_shadow(list, w, o.pos.x, o.pos.z, 1.5f * s.x, 1.5f * s.z, yaw, 0.9f); break;
    case ObjectKind::HouseCottage: add_ground_shadow(list, w, o.pos.x, o.pos.z, 3.6f, 3.0f, yaw, 0.7f); break;
    case ObjectKind::HouseChimney: add_ground_shadow(list, w, o.pos.x, o.pos.z, 3.3f, 3.3f, yaw, 0.7f); break;
    case ObjectKind::HouseHut: add_ground_shadow(list, w, o.pos.x, o.pos.z, 3.2f, 3.2f, 0, 0.7f); break;
    case ObjectKind::Well: add_ground_shadow(list, w, o.pos.x, o.pos.z, 1.4f, 1.4f, 0); break;
    case ObjectKind::Barrel: add_ground_shadow(list, w, o.pos.x, o.pos.z, 0.6f, 0.6f, 0); break;
    case ObjectKind::Lamp:
    case ObjectKind::Sign: add_ground_shadow(list, w, o.pos.x, o.pos.z, 0.35f, 0.35f, 0, 0.8f); break;
    case ObjectKind::Tower: add_ground_shadow(list, w, o.pos.x, o.pos.z, 3.6f, 3.6f, 0, 0.75f); break;
    case ObjectKind::RuinWall: add_ground_shadow(list, w, o.pos.x, o.pos.z, 1.9f, 0.6f, yaw, 0.7f); break;
    case ObjectKind::Pillar: add_ground_shadow(list, w, o.pos.x, o.pos.z, 0.6f, 0.6f, 0, 0.8f); break;
    case ObjectKind::FallenPillar: add_ground_shadow(list, w, o.pos.x, o.pos.z, 1.8f, 0.6f, yaw, 0.7f); break;
    case ObjectKind::Rubble: add_ground_shadow(list, w, o.pos.x, o.pos.z, 1.0f, 1.0f, 0, 0.6f); break;
    case ObjectKind::RuneStone: add_ground_shadow(list, w, o.pos.x, o.pos.z, 0.6f, 0.4f, yaw, 0.7f); break;
    default: break;
  }
}

// Trees, pines and a few rocks beyond the edges of the playable area, so the
// world ends in a forest instead of a wall.
std::vector<Object> border_forest(const World& w) {
  std::vector<Object> forest;
  constexpr float kCell = 4.0f;
  const int cells = static_cast<int>(2 * kForestOuter / kCell);
  for (int i = 0; i < cells; ++i) {
    for (int j = 0; j < cells; ++j) {
      const auto ui = static_cast<uint64_t>(i), uj = static_cast<uint64_t>(j);
      const float x = -kForestOuter + kCell * (static_cast<float>(i) + 0.2f + 0.6f * rand01(w.seed, 100 + ui, uj));
      const float z = -kForestOuter + kCell * (static_cast<float>(j) + 0.2f + 0.6f * rand01(w.seed, 200 + ui, uj));
      const float edge = std::max(std::fabs(x), std::fabs(z));
      if (edge < rules::kWorldHalfSize + 2.0f) continue;
      const float density = std::clamp((edge - rules::kWorldHalfSize) / 10.0f, 0.35f, 0.8f);
      if (rand01(w.seed, 300 + ui, uj) > density) continue;
      if (distance_to_path(w.path, x, z, rules::kPathHalfWidth + 3.0f) < rules::kPathHalfWidth + 2.0f) continue;
      Object o;
      o.pos = {x, w.ground.height(x, z) - 0.1f, z};
      o.variant = static_cast<uint8_t>(rand01(w.seed, 400 + ui, uj) * 255);
      o.yaw = static_cast<Angle16>(rand01(w.seed, 500 + ui, uj) * 65535);
      const float r = rand01(w.seed, 600 + ui, uj);
      const float s = 0.9f + 0.6f * rand01(w.seed, 700 + ui, uj);
      if (r < 0.48f) {
        o.kind = ObjectKind::Pine;
        o.size = {s, s, s};
      } else if (r < 0.88f) {
        o.kind = ObjectKind::Tree;
        o.size = {s, s, s};
      } else if (r < 0.96f) {
        o.kind = ObjectKind::Rock;
        o.size = {0.8f * s, 0.6f * s, 0.9f * s};
        o.yaw = static_cast<Angle16>(o.yaw & 0xC000);
      } else {
        o.kind = ObjectKind::Bush;
        o.size = {0.7f * s, 0.7f * s, 0};
      }
      forest.push_back(o);
    }
  }
  return forest;
}

bool covered(const std::vector<Box>& boxes, float x, float z, float margin) {
  return std::any_of(boxes.begin(), boxes.end(), [&](const Box& b) {
    return x > b.min.x - margin && x < b.max.x + margin && z > b.min.z - margin && z < b.max.z + margin;
  });
}

void add_vegetation(const World& w, const std::vector<Box>& obstacles, DrawList& list) {
  static constexpr Color kFlowerColors[] = {
      {1.0f, 1.0f, 1.0f, 1}, {1.0f, 0.88f, 0.35f, 1}, {0.80f, 0.55f, 0.95f, 1},
      {1.0f, 0.62f, 0.75f, 1}, {0.55f, 0.70f, 1.0f, 1},
  };
  constexpr float kCell = 1.0f;
  const int cells = static_cast<int>(2 * kDecorationHalfSize / kCell);
  for (int i = 0; i < cells; ++i) {
    for (int j = 0; j < cells; ++j) {
      const auto ui = static_cast<uint64_t>(i), uj = static_cast<uint64_t>(j);
      const float x = -kDecorationHalfSize + kCell * (static_cast<float>(i) + rand01(w.seed, 1000 + ui, uj));
      const float z = -kDecorationHalfSize + kCell * (static_cast<float>(j) + rand01(w.seed, 2000 + ui, uj));
      const float on_path = distance_to_path(w.path, x, z, 2 * rules::kPathHalfWidth) / rules::kPathHalfWidth;
      if (on_path < 0.75f) continue;
      if (covered(obstacles, x, z, 0.05f)) continue;
      if (in_lake(w.village.lake, x, z, 0.6f)) continue;
      if (w.ruins.radius > 0) {  // the paved courtyard of the ruins
        const float rx = x - w.ruins.center.x, rz = z - w.ruins.center.z;
        if (std::max(std::fabs(rx), std::fabs(rz)) < 8.5f && rand01(w.seed, 3500 + ui, uj) < 0.8f) continue;
      }
      if (w.village.lake.radius_x > 0) {  // the trodden village square
        const float vx = x - w.village.center.x, vz = z - w.village.center.z;
        if (vx * vx + vz * vz < 6.0f * 6.0f) continue;
      }
      const float roll = rand01(w.seed, 3000 + ui, uj);
      // Sparse along the path edges, and thinner outside the playable area
      // (seen from afar only: saves a lot of triangles on small devices).
      float edge_factor = on_path < 1.15f ? 0.35f : 1.0f;
      if (std::max(std::fabs(x), std::fabs(z)) > rules::kWorldHalfSize) edge_factor *= 0.45f;

      Instance in;
      in.x = x;
      in.y = w.ground.height(x, z) - 0.02f;
      in.z = z;
      in.rotation = quat_yaw(kTau * rand01(w.seed, 4000 + ui, uj));
      in.wind = 1;
      const float k = 0.85f + 0.25f * rand01(w.seed, 5000 + ui, uj);
      in.tint = {k, k, k, 1};

      // Flowers grow in clusters.
      const float meadow = value_noise(w.seed + 21, x, z, 9.0f);
      const float flower_chance = (meadow > 0.6f ? 0.16f : 0.02f) * edge_factor;
      if (roll < flower_chance) {
        const float s = 0.8f + 0.5f * rand01(w.seed, 6000 + ui, uj);
        in.sx = in.sy = in.sz = s;
        in.tint = kFlowerColors[static_cast<size_t>(rand01(w.seed, 7000 + ui, uj) * 5) % 5];
        add(list, MeshId::Flower, in);
        continue;
      }
      // Tall grass in patches, short grass almost everywhere.
      const float tall = value_noise(w.seed + 20, x, z, 7.0f);
      if (tall > 0.62f && roll < 0.85f * edge_factor) {
        const float s = 0.55f + 0.35f * rand01(w.seed, 8000 + ui, uj);  // up to ~0.85 m
        in.sx = in.sz = 0.9f + 0.4f * rand01(w.seed, 9000 + ui, uj);
        in.sy = s;
        add(list, MeshId::GrassTall, in);
      } else if (roll < 0.6f * edge_factor) {
        const float s = 0.7f + 0.7f * rand01(w.seed, 8000 + ui, uj);
        in.sx = in.sz = 0.8f + 0.5f * rand01(w.seed, 9000 + ui, uj);
        in.sy = s;
        add(list, MeshId::GrassShort, in);
      }
    }
  }
}

void add_mountains(const World& w, DrawList& list) {
  constexpr int kCount = 18;
  for (int i = 0; i < kCount; ++i) {
    const auto u = static_cast<uint64_t>(i);
    const float angle = kTau * (static_cast<float>(i) + 0.6f * rand01(w.seed, 10000 + u)) / kCount;
    const float distance = 200.0f + 70.0f * rand01(w.seed, 10100 + u);
    Instance in;
    in.x = std::cos(angle) * distance;
    in.z = std::sin(angle) * distance;
    in.rotation = quat_yaw(kTau * rand01(w.seed, 10200 + u));
    const float width = 55.0f + 40.0f * rand01(w.seed, 10300 + u);
    in.sx = width;
    in.sz = width * (0.8f + 0.4f * rand01(w.seed, 10400 + u));
    in.sy = 24.0f + 30.0f * rand01(w.seed, 10500 + u);
    add(list, mesh_offset(MeshId::Mountain0, u % kMountainVariants), in);
  }
}

// Smoke rising from chimneys, ripples around fish.
void add_village_effects(const World& w, float time, DrawList& list) {
  constexpr int kPuffs = 7;
  for (const Object& o : w.objects) {
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
  for (size_t k = 0; k < w.objects.size(); ++k) {
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

void build_landscape(const World& w, DrawList& list) {
  list.batches.assign(kMeshCount, {});
  list.cached = true;
  ++list.version;
  add(list, MeshId::Ground, Instance{});
  add(list, MeshId::Path, Instance{});
  Instance water;
  water.unused = 1;  // the shader adds waves and reflections
  add(list, MeshId::Water, water);

  std::vector<Box> obstacles;
  for (const Object& o : w.objects) {
    add_object(list, w, o);
    obstacles.push_back(object_box(o));
  }
  for (const Object& o : border_forest(w)) {
    add_object(list, w, o);
    obstacles.push_back(object_box(o));
  }
  add_vegetation(w, obstacles, list);
  add_mountains(w, list);
}

void build_dynamic(const World& w, float time, DrawList& list, int selected) {
  list.batches.resize(kMeshCount);
  for (auto& batch : list.batches) batch.clear();

  add_characters(w, time, list);
  add_village_effects(w, time, list);

  // A golden ring, gently pulsing, at the feet of the selected creature.
  if (selected >= 0 && static_cast<size_t>(selected) < w.creatures.size()) {
    const Creature& c = w.creatures[static_cast<size_t>(selected)];
    const Body& b = c.body;
    const float radius = species_info(c.species).shape.half_width * 2.2f + 0.15f;
    const float pulse = 1 + 0.06f * std::sin(time * 4.0f);
    Instance ring;
    ring.x = b.pos.x;
    ring.z = b.pos.z;
    const float ground = w.ground.height(b.pos.x, b.pos.z);
    const bool on_ground = c.species != Species::Fish && b.pos.y - ground < 0.05f;
    ring.y = (on_ground ? ground : b.pos.y) + kShadowHeight + 0.01f;
    if (on_ground) ring.rotation = ground_tilt(w, b.pos.x, b.pos.z);
    ring.sx = ring.sz = radius * pulse;
    ring.tint = {1.0f, 0.85f, 0.25f, 1.0f};
    add(list, MeshId::Ripple, ring);
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
    c.tint = {1.45f, 1.45f, 1.45f, 1};  // clouds glow: brighter than the lighting alone
    add(list, mesh_offset(MeshId::Cloud0, u % kCloudVariants), c);
  }
}

float camera_distance(const World& w, float tx, float ty, float tz, float dx, float dy, float dz, float wanted) {
  constexpr float kMargin = 0.25f;   // keep the camera this far from objects
  constexpr float kClosest = 1.2f;
  float best = wanted;
  for (const Object& o : w.objects) {
    const Box b = object_box(o);
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
  for (const Object& o : w.objects) {
    const float t = ray_box(ray, object_box(o));
    if (t >= 0 && t < best_t) return -1;
  }
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
  if (index < 0 || static_cast<size_t>(index) >= w.creatures.size()) return false;
  const Creature& c = w.creatures[static_cast<size_t>(index)];
  const Vec3 p = c.body.pos + Vec3{0, species_info(c.species).shape.height * 0.5f, 0};
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

View build_view(const World& w, const Camera& cam, float aspect, float time) {
  const Player& p = w.player;
  const float tx = p.pos.x, ty = p.pos.y + 1.0f, tz = p.pos.z;

  // The camera sits behind the player, opposite to where it looks, and comes
  // closer when an object (a trunk, a rock...) is in between.
  const float cp = std::cos(cam.pitch), sp = std::sin(cam.pitch);
  const float fx = -std::sin(cam.yaw) * cp, fz = -std::cos(cam.yaw) * cp;
  const float distance = camera_distance(w, tx, ty, tz, -fx, sp, -fz, cam.distance);
  float ex = tx - fx * distance;
  float ey = ty + sp * distance;
  float ez = tz - fz * distance;
  ey = std::max(ey, w.ground.height(ex, ez) + 0.2f);  // never below the ground

  View v;
  v.view_projection = multiply(perspective(1.0f, aspect, 0.1f, 800.0f), look_at(ex, ey, ez, tx, ty, tz));
  v.inverse_view_projection = inverse(v.view_projection);
  v.camera_x = ex;
  v.camera_y = ey;
  v.camera_z = ez;

  // The sun, low enough to be seen when looking ahead; the light comes from
  // the same side but higher, so the ground stays bright.
  auto normalized = [](float x, float y, float z, float& ox, float& oy, float& oz) {
    const float l = std::sqrt(x * x + y * y + z * z);
    ox = x / l;
    oy = y / l;
    oz = z / l;
  };
  normalized(0.45f, 0.27f, -0.85f, v.sun_x, v.sun_y, v.sun_z);
  normalized(0.45f, 0.95f, -0.85f, v.light_x, v.light_y, v.light_z);
  v.sky_zenith = palette::kSkyZenith;
  v.sky_horizon = palette::kSkyHorizon;
  v.fog = palette::kSkyHorizon;
  v.time = time;
  return v;
}

}  // namespace monstres
