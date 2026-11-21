#include "monstres/render/landscape.hpp"

#include "monstres/core/north.hpp"

#include "monstres/core/interior.hpp"
#include "monstres/core/west.hpp"

#include "monstres/render/scene.hpp"
#include "scene_util.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>

namespace monstres {

using scene_util::kTau;
using scene_util::rand01;

namespace {

constexpr float kDecorationHalfSize = rules::kWorldHalfSize + 13;  // grass and flowers up to here (x, south)
constexpr float kDecorationNorth = rules::kWorldNorth + 13;         // and north
constexpr int kGrassCells = static_cast<int>(2 * kDecorationHalfSize);  // 1 m cells, from -kDecorationHalfSize
// (Northwards the cells' j go below 0: the same cells as before the world grew north.)
constexpr int kGrassNorthCell = static_cast<int>(kDecorationHalfSize - kDecorationNorth);

// Mesh scale, matching the sizes of tools/make_models.py and the collision
// boxes of world.cpp.
void scale_for(const Object& o, Instance& in) {
  const Vec3 s = o.size;
  switch (o.kind) {
    case ObjectKind::Tree:
    case ObjectKind::Pine:
    case ObjectKind::Larch:
    case ObjectKind::Cairn: in.sx = in.sy = in.sz = s.x; break;
    case ObjectKind::Rock:  in.sx = s.x; in.sy = s.y; in.sz = s.z; break;
    case ObjectKind::Log:   in.sx = s.x; in.sy = s.y; in.sz = s.y; break;
    case ObjectKind::Stump:
    case ObjectKind::Bush:  in.sx = s.x; in.sy = s.y; in.sz = s.x; break;
    case ObjectKind::Crate: in.sx = s.x; in.sy = s.y; in.sz = s.z; in.y += s.y; break;  // centered cube
    case ObjectKind::Pillar: in.sx = in.sz = 1; in.sy = s.y; break;                    // unit height
    case ObjectKind::Crystal: in.sx = in.sy = in.sz = s.x; break;
    case ObjectKind::Rug:   in.sx = s.x; in.sz = s.z; break;
    case ObjectKind::Step:  in.sx = s.x; in.sy = s.y; in.sz = s.z; break;  // its base on the ground
    case ObjectKind::FruitTree:
    case ObjectKind::Meteorite: in.sx = in.sy = in.sz = s.x; break;
    case ObjectKind::Menhir: in.sx = in.sz = 1; in.sy = s.y; break;  // unit height
    case ObjectKind::Crops: {  // fits [-1, 1]: its half extents along X and Z (turned with it)
      const bool turned = (o.yaw >> 14) & 1;
      in.sx = turned ? s.z : s.x;
      in.sy = 1;
      in.sz = turned ? s.x : s.z;
      break;
    }
    case ObjectKind::Bridge: in.sx = s.x / 5.4f; in.sy = in.sz = 1; in.y += 2 * s.y; break;  // its deck on top of its box, as long
    case ObjectKind::HiddenCave: in.sx = in.sy = in.sz = rules::kHiddenCaveScale; break;           // the cave's mouth, smaller
    default:                in.sx = in.sy = in.sz = 1; break;  // village: real-size models
  }
}

float wind_for(ObjectKind kind) {
  switch (kind) {
    case ObjectKind::Tree:
    case ObjectKind::FruitTree: return 0.25f;
    case ObjectKind::Pine:
    case ObjectKind::Larch: return 0.15f;
    case ObjectKind::Bush: return 0.35f;
    default:               return 0;
  }
}

// Trees, rocks, logs, stumps, bushes: what the wilds are made of.
bool nature(ObjectKind kind) { return kind <= ObjectKind::Bush || kind == ObjectKind::Larch; }

// How far an object is drawn: landmarks always, trees simplified far away,
// small things only near the player.
enum class Detail { Landmark, Tree, Small };
Detail detail_of(const Object& o) {
  switch (o.kind) {
    case ObjectKind::Tree:
    case ObjectKind::Pine:
    case ObjectKind::Larch: return Detail::Tree;
    case ObjectKind::Rock: return o.size.x >= 1.0f || o.size.z >= 1.0f ? Detail::Landmark : Detail::Small;
    case ObjectKind::Log:
    case ObjectKind::Stump:
    case ObjectKind::Bush:
    case ObjectKind::Crate:
    case ObjectKind::Fence:
    case ObjectKind::Sign:
    case ObjectKind::Barrel:
    case ObjectKind::Lamp:
    case ObjectKind::Rubble:
    case ObjectKind::StoneWall:
    case ObjectKind::Minecart: return Detail::Small;
    default: return Detail::Landmark;
  }
}


// One object, ready to draw: its models up close, at a middle distance and
// from afar, and its soft shadow on the ground, sized to it.
LandscapeCache::Item make_item(const World& w, const Object& o) {
  LandscapeCache::Item item;
  item.x = o.pos.x;
  item.z = o.pos.z;
  const Detail detail = detail_of(o);
  item.nature = nature(o.kind);
  item.small = detail == Detail::Small;
  Instance& in = item.instance;
  in.x = o.pos.x;
  in.y = o.pos.y;
  in.z = o.pos.z;
  const float yaw = static_cast<float>(radians_from_angle16(o.yaw));
  in.rotation = quat_yaw(yaw);
  scale_for(o, in);
  in.wind = wind_for(o.kind);
  const float k = 0.88f + 0.24f * static_cast<float>(o.variant) / 255.0f;  // slight color variation
  in.tint = {k, k, k, 1};
  if (detail == Detail::Small) in.material = kFadeFar;  // shrinks away before the edge of the detail
  else if (item.nature) in.material = kFadeTree;     // shrinks away before landscape::kObjectsRadius
  item.full = item.mid = item.far = mesh_for(o);
  if (o.kind == ObjectKind::Tree) {
    item.mid = MeshId::TreeMid;
    item.far = MeshId::TreeFar;
  } else if (o.kind == ObjectKind::Pine) {
    item.far = MeshId::PineFar;
  } else if (o.kind == ObjectKind::Larch) {
    item.far = MeshId::LarchFar;
  } else if (o.kind == ObjectKind::Bush) {
    item.mid = MeshId::BushFar;
  }
  in.material = scene_util::material_for(item.full, in.material);

  const auto shadow = [&](float rx, float rz, float turn, float alpha = 1) {
    Instance& s = item.shadow;
    s.x = o.pos.x;
    s.y = w.ground.height(o.pos.x, o.pos.z) + kShadowHeight;
    s.z = o.pos.z;
    s.rotation = ground_tilt(w, o.pos.x, o.pos.z) * quat_yaw(turn);
    s.sx = rx;
    s.sy = 1;
    s.sz = rz;
    s.tint = {1, 1, 1, alpha};
    s.material = detail == Detail::Small ? kFadeFar : kLit;
    item.has_shadow = true;
  };
  const Vec3 s = o.size;
  switch (o.kind) {
    case ObjectKind::Tree:  shadow(1.7f * s.x, 1.7f * s.x, 0); break;
    case ObjectKind::Pine:  shadow(1.4f * s.x, 1.4f * s.x, 0); break;
    case ObjectKind::Rock:  shadow(1.25f * s.x, 1.25f * s.z, yaw); break;
    case ObjectKind::Log:   shadow(1.15f * s.x, 1.8f * s.y, yaw); break;
    case ObjectKind::Stump: shadow(1.5f * s.x, 1.5f * s.x, 0); break;
    case ObjectKind::Bush:  shadow(1.3f * s.x, 1.3f * s.x, 0); break;
    case ObjectKind::Crate: shadow(1.5f * s.x, 1.5f * s.z, yaw, 0.9f); break;
    case ObjectKind::HouseCottage: shadow(3.6f, 3.0f, yaw, 0.7f); break;
    case ObjectKind::HouseChimney: shadow(3.3f, 3.3f, yaw, 0.7f); break;
    case ObjectKind::HouseHut: shadow(3.2f, 3.2f, 0, 0.7f); break;
    case ObjectKind::Well: shadow(1.4f, 1.4f, 0); break;
    case ObjectKind::Barrel: shadow(0.6f, 0.6f, 0); break;
    case ObjectKind::Lamp:
    case ObjectKind::Sign: shadow(0.35f, 0.35f, 0, 0.8f); break;
    case ObjectKind::Tower: shadow(3.6f, 3.6f, 0, 0.75f); break;
    case ObjectKind::RuinWall: shadow(1.9f, 0.6f, yaw, 0.7f); break;
    case ObjectKind::Pillar: shadow(0.6f, 0.6f, 0, 0.8f); break;
    case ObjectKind::FallenPillar: shadow(1.8f, 0.6f, yaw, 0.7f); break;
    case ObjectKind::Rubble: shadow(1.0f, 1.0f, 0, 0.6f); break;
    case ObjectKind::RuneStone: shadow(0.6f, 0.4f, yaw, 0.7f); break;
    case ObjectKind::CastleWall: shadow(2.6f, 1.2f, yaw, 0.7f); break;
    case ObjectKind::CastleTower: shadow(3.2f, 3.2f, 0, 0.75f); break;
    case ObjectKind::CastleKeep: shadow(6.2f, 6.2f, yaw, 0.75f); break;
    case ObjectKind::Crystal: shadow(0.5f * s.x, 0.5f * s.x, 0, 0.5f); break;
    case ObjectKind::Teleporter: shadow(2.3f, 2.3f, 0, 0.6f); break;
    case ObjectKind::Table: shadow(1.0f, 0.65f, yaw, 0.6f); break;
    case ObjectKind::Chair: shadow(0.35f, 0.35f, yaw, 0.6f); break;
    case ObjectKind::Bed: shadow(0.75f, 1.3f, yaw, 0.6f); break;
    case ObjectKind::Shelf: shadow(0.8f, 0.35f, yaw, 0.5f); break;
    case ObjectKind::Hearth: shadow(1.0f, 0.5f, yaw, 0.5f); break;
    case ObjectKind::Farmhouse: shadow(4.8f, 4.2f, yaw, 0.7f); break;
    case ObjectKind::Barn: shadow(5.8f, 4.8f, yaw, 0.7f); break;
    case ObjectKind::FruitTree: shadow(1.6f * s.x, 1.6f * s.x, 0); break;
    case ObjectKind::Haystack: shadow(1.3f, 1.3f, 0, 0.7f); break;
    case ObjectKind::Windmill: shadow(3.6f, 3.6f, 0, 0.75f); break;
    case ObjectKind::WaterMill: shadow(4.0f, 3.4f, yaw, 0.7f); break;
    case ObjectKind::Cabin: shadow(4.0f, 3.4f, yaw, 0.7f); break;
    case ObjectKind::Tent: shadow(1.9f, 2.1f, yaw, 0.6f); break;
    case ObjectKind::Woodpile: shadow(1.8f, 0.8f, yaw, 0.6f); break;
    case ObjectKind::HollowTree: shadow(8.0f, 8.0f, 0, 0.8f); break;
    case ObjectKind::Burrow: shadow(2.8f, 2.8f, 0, 0.5f); break;
    case ObjectKind::Lighthouse: shadow(3.6f, 3.6f, 0, 0.75f); break;
    case ObjectKind::Menhir: shadow(0.7f, 0.7f, 0, 0.7f); break;
    case ObjectKind::Meteorite: shadow(1.3f * s.x, 1.3f * s.x, 0, 0.7f); break;
    case ObjectKind::Larch: shadow(1.2f * s.x, 1.2f * s.x, 0); break;
    case ObjectKind::Chalet: shadow(4.8f, 4.2f, yaw, 0.7f); break;
    case ObjectKind::Minecart: shadow(1.0f, 0.65f, yaw, 0.6f); break;
    case ObjectKind::StoneWall: shadow(0.8f, 0.45f, yaw, 0.5f); break;
    case ObjectKind::Cairn: shadow(0.6f * s.x, 0.6f * s.x, 0, 0.6f); break;
    default: break;
  }
  return item;
}

// Boxes of the objects around a place, by cells of 4 m: where grass does not grow.
class Obstacles {
public:
  Obstacles(Vec2 center, float radius) : x0_(center.x - radius), z0_(center.z - radius),
                                         n_(static_cast<int>(2 * radius / kCell) + 1),
                                         cells_(static_cast<size_t>(n_ * n_)) {}
  void add(const Box& b) {
    for (int i = cell(b.min.x - x0_); i <= cell(b.max.x - x0_); ++i) {
      for (int j = cell(b.min.z - z0_); j <= cell(b.max.z - z0_); ++j) cells_[static_cast<size_t>(i * n_ + j)].push_back(b);
    }
  }
  bool covered(float x, float z, float margin) const {
    const auto& boxes = cells_[static_cast<size_t>(cell(x - x0_) * n_ + cell(z - z0_))];
    return std::any_of(boxes.begin(), boxes.end(), [&](const Box& b) {
      return x > b.min.x - margin && x < b.max.x + margin && z > b.min.z - margin && z < b.max.z + margin;
    });
  }

private:
  static constexpr float kCell = 4.0f;
  int cell(float v) const { return std::clamp(static_cast<int>(v / kCell), 0, n_ - 1); }
  float x0_, z0_;
  int n_;
  std::vector<std::vector<Box>> cells_;
};


constexpr Color kFlowerColors[] = {
    {1.0f, 1.0f, 1.0f, 1}, {1.0f, 0.88f, 0.35f, 1}, {0.80f, 0.55f, 0.95f, 1},
    {1.0f, 0.62f, 0.75f, 1}, {0.55f, 0.70f, 1.0f, 1},
};

// What grows in the grass cell (i, j) of 1 m, if anything: a tuft of grass
// or a flower, somewhere in the cell.
void grass_cell(const World& w, const Obstacles& obstacles, int i, int j, std::vector<LandscapeCache::Grass>& out) {
  const auto ui = static_cast<uint64_t>(i), uj = static_cast<uint64_t>(j);
  const float x = -kDecorationHalfSize + (static_cast<float>(i) + rand01(w.seed, 1000 + ui, uj));
  const float z = -kDecorationHalfSize + (static_cast<float>(j) + rand01(w.seed, 2000 + ui, uj));
  const float on_path = distance_to_path(w.path, x, z, 2 * rules::kPathHalfWidth) / rules::kPathHalfWidth;
  if (on_path < 0.75f) return;
  // The north's snow and ice: no grass (north.hpp).
  if (w.north.valid() && z < rules::kFrostFrom + 120.0f) {
    if (in_frozen_lake(w.north, x, z) || north_snow(w.north, x, z, w.ground.height(x, z)) > 0.45f) return;
  }
  // The west's road, its stream's water and its fields.
  if (w.west.valid()) {
    if (distance_to_road(w.west.road, x, z, 2 * rules::kRoadHalfWidth) < 0.75f * rules::kRoadHalfWidth) return;
    float level = 0, width = 0;  // not under its water (nor right at its edge)
    const float g = w.ground.height(x, z);
    if (river_surface(w.west, x, z, g, 12.0f, level, width) < 12.0f && g < level + 0.15f) return;
    const float cx = x - w.west.crater.x, cz = z - w.west.crater.z;  // the crater's scorched bowl
    if (cx * cx + cz * cz < rules::kCraterRadius * rules::kCraterRadius * 0.8f) return;
  }
  if (obstacles.covered(x, z, 0.05f)) return;
  if (w.ground.normal(x, z).y < 0.8f) return;  // bare rock on cliffs
  if (in_lake(w.village.lake, x, z, 0.6f)) return;
  // The sea and the wet sand, on the shore only: the valley's hollows below
  // the sea's level are dry land (Heightmap::land_height, as Zone::Coast).
  if (w.ground.height(x, z) < rules::kSeaLevel + 0.6f && w.ground.land_height(x, z) < rules::kSeaLevel + 1.0f) return;
  if (w.ruins.radius > 0) {  // the paved courtyard of the ruins
    const float rx = x - w.ruins.center.x, rz = z - w.ruins.center.z;
    if (std::max(std::fabs(rx), std::fabs(rz)) < 8.5f && rand01(w.seed, 3500 + ui, uj) < 0.8f) return;
  }
  if (w.castle.radius > 0) {  // the castle's courtyard, paved
    const float kx = x - w.castle.center.x, kz = z - w.castle.center.z;
    if (std::max(std::fabs(kx), std::fabs(kz)) < rules::kCastleHalf + 0.5f && rand01(w.seed, 3600 + ui, uj) < 0.9f) return;
  }
  if (w.village.lake.radius_x > 0) {  // the trodden village square
    const float vx = x - w.village.center.x, vz = z - w.village.center.z;
    if (vx * vx + vz * vz < 6.0f * 6.0f) return;
  }
  const float roll = rand01(w.seed, 3000 + ui, uj);
  const float keep = rand01(w.seed, 11000 + ui, uj);
  // Sparse along the path edges, and thinner outside the playable area
  // (seen from afar only: saves a lot of triangles on small devices).
  float edge_factor = on_path < 1.15f ? 0.35f : 1.0f;
  if (rules::world_edge(x, z) > rules::kWorldHalfSize) edge_factor *= 0.45f;

  Instance in;
  in.x = x;
  in.y = w.ground.height(x, z) - 0.02f;
  in.z = z;
  in.rotation = quat_yaw(kTau * rand01(w.seed, 4000 + ui, uj));
  in.wind = 1;
  in.material = kFadeNear;
  const float k = 0.85f + 0.25f * rand01(w.seed, 5000 + ui, uj);
  in.tint = {k, k, k, 1};

  // Flowers grow in clusters.
  const float meadow = value_noise(w.seed + 21, x, z, 9.0f);
  const float flower_chance = (meadow > 0.6f ? 0.16f : 0.02f) * edge_factor;
  if (roll < flower_chance) {
    const float s = 0.8f + 0.5f * rand01(w.seed, 6000 + ui, uj);
    in.sx = in.sy = in.sz = s;
    in.tint = kFlowerColors[static_cast<size_t>(rand01(w.seed, 7000 + ui, uj) * 5) % 5];
    out.push_back({MeshId::Flower, in, keep});
    return;
  }
  // Tall grass in patches, short grass almost everywhere.
  const float tall = value_noise(w.seed + 20, x, z, 7.0f);
  if (tall > 0.62f && roll < 0.85f * edge_factor) {
    const float s = 0.55f + 0.35f * rand01(w.seed, 8000 + ui, uj);  // up to ~0.85 m
    in.sx = in.sz = 0.9f + 0.4f * rand01(w.seed, 9000 + ui, uj);
    in.sy = s;
    out.push_back({MeshId::GrassTall, in, keep});
  } else if (roll < 0.6f * edge_factor) {
    const float s = 0.7f + 0.7f * rand01(w.seed, 8000 + ui, uj);
    in.sx = in.sz = 0.8f + 0.5f * rand01(w.seed, 9000 + ui, uj);
    in.sy = s;
    out.push_back({MeshId::GrassShort, in, keep});
  }
}

// The simpler model of a tuft or a flower, a little farther away.
MeshId simpler_grass(MeshId id) {
  switch (id) {
    case MeshId::GrassShort: return MeshId::GrassShortFar;
    case MeshId::GrassTall: return MeshId::GrassTallFar;
    case MeshId::Flower: return MeshId::FlowerFar;
    default: return id;
  }
}

// ---- Chunks ----------------------------------------------------------------------

int chunk_x(float x) {
  return std::clamp(static_cast<int>(std::floor((x - landscape::kChunkOriginX) / landscape::kChunk)), 0, landscape::kChunksX - 1);
}
int chunk_z(float z) {
  return std::clamp(static_cast<int>(std::floor((z - landscape::kChunkOriginZ) / landscape::kChunk)), 0, landscape::kChunksZ - 1);
}
float chunk_start_x(int c) { return landscape::kChunkOriginX + landscape::kChunk * static_cast<float>(c); }
float chunk_start_z(int c) { return landscape::kChunkOriginZ + landscape::kChunk * static_cast<float>(c); }

// Squared distance from (x, z) to the nearest point of chunk (cx, cz).
float chunk_distance2(int cx, int cz, float x, float z) {
  const float x0 = chunk_start_x(cx), z0 = chunk_start_z(cz);
  const float dx = std::max({x0 - x, 0.0f, x - (x0 + landscape::kChunk)});
  const float dz = std::max({z0 - z, 0.0f, z - (z0 + landscape::kChunk)});
  return dx * dx + dz * dz;
}

// The chunks with the valley's buildings, drawn from everywhere.
bool valley_chunk(int cx, int cz) {
  const float lo = -rules::kValleyHalfSize, hi = rules::kValleyHalfSize;
  return chunk_start_x(cx) + landscape::kChunk > lo && chunk_start_x(cx) < hi && chunk_start_z(cz) + landscape::kChunk > lo &&
         chunk_start_z(cz) < hi;
}

std::array<Aabb, kMeshCount> g_mesh_bounds;
bool g_mesh_bounds_set = false;

double now_ms() {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

}  // namespace

// ---- Bounds ------------------------------------------------------------------------

void set_mesh_bounds(std::span<const MeshData> meshes) {
  for (size_t i = 0; i < kMeshCount; ++i) {
    Aabb b = empty_aabb();
    if (i < meshes.size()) {
      const std::vector<float>& v = meshes[i].vertices;
      for (size_t k = 0; k + 2 < v.size(); k += MeshData::kFloatsPerVertex) b.add(v[k], v[k + 1], v[k + 2]);
    }
    g_mesh_bounds[i] = b;
  }
  g_mesh_bounds_set = true;
}

Aabb mesh_bounds(MeshId id) {
  const auto i = static_cast<size_t>(id);
  if (g_mesh_bounds_set && i < kMeshCount && !g_mesh_bounds[i].empty()) return g_mesh_bounds[i];
  return {-8, -1, -8, 8, 20, 8};  // as big as the biggest model (the castle's keep)
}

Aabb instance_bounds(MeshId id, const Instance& in) {
  const Aabb m = mesh_bounds(id);
  // Turned around the vertical axis: within the circle through the box's corners.
  const float rx = std::max(std::fabs(m.min_x), std::fabs(m.max_x)), rz = std::max(std::fabs(m.min_z), std::fabs(m.max_z));
  float r = std::sqrt(rx * rx + rz * rz) * std::max(in.sx, in.sz);
  float lo = m.min_y * in.sy, hi = m.max_y * in.sy;
  if (std::fabs(in.rotation.x) + std::fabs(in.rotation.z) > 1e-4f) {  // tilted too (shadows on slopes)
    const float reach = std::max({r, std::fabs(lo), std::fabs(hi)});
    r = reach;
    lo = -reach;
    hi = reach;
  }
  if (in.wind > 0) r += 0.15f * hi * in.wind + 0.1f;  // swaying in the wind
  return {in.x - r, in.y + lo - 0.05f, in.z - r, in.x + r, in.y + hi + 0.05f, in.z + r};
}

// ---- The cache ---------------------------------------------------------------------

Vec2 landscape_center(float x, float z) {
  const auto snap = [](float v) { return std::round(v / landscape::kStep) * landscape::kStep; };
  return {snap(x), snap(z)};
}

bool landscape_can_wait(Vec2 player, Vec2 built) {
  const float dx = player.x - built.x, dz = player.z - built.z;
  return dx * dx + dz * dz < kWaitReach * kWaitReach;
}

void LandscapeCache::clear() {
  chunks_.clear();
  seed_ = ~0ULL;
  object_count_ = 0;
  objects_ = nullptr;
}

void LandscapeCache::check_world(const World& w) {
  if (seed_ == w.seed && object_count_ == w.objects.size() && objects_ == w.objects.data() && !chunks_.empty()) return;
  clear();
  seed_ = w.seed;
  object_count_ = w.objects.size();
  objects_ = w.objects.data();
  chunks_.resize(static_cast<size_t>(landscape::kChunksX) * landscape::kChunksZ);
}

size_t LandscapeCache::chunks_with_grass() const {
  return static_cast<size_t>(std::count_if(chunks_.begin(), chunks_.end(), [](const Chunk& c) { return c.grass_done; }));
}

const LandscapeCache::Chunk& LandscapeCache::chunk(const World& w, int cx, int cz, bool grass) {
  check_world(w);
  Chunk& c = chunks_[static_cast<size_t>(cx * landscape::kChunksZ + cz)];
  if (!c.objects_done) {
    // Its objects: those whose position is in it (through the index, each once).
    const ObjectIndex& ix = objects_index(w);
    for (int i = cx * 4; i < cx * 4 + 4; ++i) {
      for (int j = cz * 4; j < cz * 4 + 4; ++j) {
        const auto cell = static_cast<size_t>(i * ObjectIndex::kCellsZ + j);
        for (uint32_t k = ix.cell_start[cell]; k < ix.cell_start[cell + 1]; ++k) {
          const Object& o = w.objects[ix.items[k]];
          if (ObjectIndex::cell_x(o.pos.x) != i || ObjectIndex::cell_z(o.pos.z) != j) continue;
          if (mesh_for(o) == MeshId::Count) continue;  // invisible
          c.items.push_back(make_item(w, o));
        }
      }
    }
    c.objects_done = true;
  }
  if (grass && !c.grass_done) {
    // Grass cells whose corner is in the chunk, where no object stands.
    const float x0 = chunk_start_x(cx), z0 = chunk_start_z(cz);
    Obstacles obstacles({x0 + landscape::kChunk / 2, z0 + landscape::kChunk / 2}, landscape::kChunk / 2 + 2);
    const Box area{{x0 - 2, -1e9f, z0 - 2}, {x0 + landscape::kChunk + 2, 1e9f, z0 + landscape::kChunk + 2}};
    for_objects_near(w, area, [&](uint32_t k, const Box& b) {
      const Object& o = w.objects[k];
      // The fields: no grass among the crops (walked through: no box of their own).
      obstacles.add(o.kind == ObjectKind::Crops ? Box{o.pos - o.size, o.pos + o.size} : b);
    });
    const auto first_x = [](float start) {
      return std::clamp(static_cast<int>(std::ceil(start + kDecorationHalfSize)), 0, kGrassCells);
    };
    const auto first_z = [](float start) {
      return std::clamp(static_cast<int>(std::ceil(start + kDecorationHalfSize)), kGrassNorthCell, kGrassCells);
    };
    const int i0 = first_x(x0), i1 = first_x(x0 + landscape::kChunk), j0 = first_z(z0), j1 = first_z(z0 + landscape::kChunk);
    for (int i = i0; i < i1; ++i) {
      for (int j = j0; j < j1; ++j) grass_cell(w, obstacles, i, j, c.grass);
    }
    c.grass_done = true;
  }
  return c;
}

bool LandscapeCache::ready(const World& w, Vec2 center) {
  check_world(w);
  const float reach2 = landscape::kObjectsRadius * landscape::kObjectsRadius;
  const float grass2 = landscape::kGrassRadius * landscape::kGrassRadius;
  for (int cx = 0; cx < landscape::kChunksX; ++cx) {
    for (int cz = 0; cz < landscape::kChunksZ; ++cz) {
      const Chunk& c = chunks_[static_cast<size_t>(cx * landscape::kChunksZ + cz)];
      const float d2 = chunk_distance2(cx, cz, center.x, center.z);
      if (!c.objects_done && (d2 <= reach2 || valley_chunk(cx, cz))) return false;
      if (!c.grass_done && d2 <= grass2) return false;
    }
  }
  return true;
}

int LandscapeCache::prepare(const World& w, Vec2 at, double budget_ms) {
  check_world(w);
  const double start = now_ms();
  // What the landscape will need when the player has walked a little farther.
  const float reach = landscape::kObjectsRadius + 1.5f * landscape::kStep;
  const float grass = landscape::kGrassRadius + 1.5f * landscape::kStep;
  struct Missing {
    float d2;
    int cx, cz;
    bool grass;
  };
  std::vector<Missing> missing;
  const int lo_x = chunk_x(at.x - reach), hi_x = chunk_x(at.x + reach);
  const int lo_z = chunk_z(at.z - reach), hi_z = chunk_z(at.z + reach);
  for (int cx = 0; cx < landscape::kChunksX; ++cx) {
    for (int cz = 0; cz < landscape::kChunksZ; ++cz) {
      Chunk& c = chunks_[static_cast<size_t>(cx * landscape::kChunksZ + cz)];
      const float d2 = chunk_distance2(cx, cz, at.x, at.z);
      const bool near = cx >= lo_x && cx <= hi_x && cz >= lo_z && cz <= hi_z;
      const bool want_objects = (near && d2 <= reach * reach) || valley_chunk(cx, cz);
      const bool want_grass = near && d2 <= grass * grass;
      if (c.grass_done && d2 > (grass + 64) * (grass + 64)) {  // far behind: its grass is not needed any more
        c.grass_done = false;
        std::vector<Grass>().swap(c.grass);
      }
      if ((want_objects && !c.objects_done) || (want_grass && !c.grass_done)) missing.push_back({d2, cx, cz, want_grass});
    }
  }
  std::sort(missing.begin(), missing.end(), [](const Missing& a, const Missing& b) { return a.d2 < b.d2; });
  int done = 0;
  for (const Missing& m : missing) {
    if (done > 0 && now_ms() - start >= budget_ms) break;
    chunk(w, m.cx, m.cz, m.grass);
    ++done;
  }
  return done;
}

// ---- Building ------------------------------------------------------------------------

void build_landscape(const World& w, DrawList& list, Vec2 center, LandscapeCache& cache, Season season) {
  using landscape::kBins;
  cache.check_world(w);
  list.batches.assign(kMeshCount, {});
  list.parts.assign(kMeshCount, {});
  list.mesh_parts.clear();
  list.visible.clear();
  list.cached = true;
  list.seasonal = true;
  ++list.version;
  auto& bins = cache.bins_;
  auto& bounds = cache.bin_bounds_;
  bins.resize(kMeshCount * kBins);
  for (auto& b : bins) b.clear();
  bounds.assign(kMeshCount * kBins, empty_aabb());

  // Each instance into the bin of its sector around the center.
  const auto emit = [&](MeshId mesh, const Instance& in) {
    if (mesh == MeshId::Count) return;
    const float dx = in.x - center.x, dz = in.z - center.z;
    const float angle = std::atan2(dz, dx);  // -pi..pi
    int sector = static_cast<int>((angle + kPi) / (2 * kPi) * landscape::kSectors);
    sector = std::clamp(sector, 0, landscape::kSectors - 1);
    const int bin = (dx * dx + dz * dz < landscape::kInnerBand * landscape::kInnerBand ? 0 : landscape::kSectors) + sector;
    const size_t k = static_cast<size_t>(mesh) * kBins + static_cast<size_t>(bin);
    bins[k].push_back(in);
    bounds[k].add(instance_bounds(mesh, in));
  };

  // The season: which model of a tree, and what grows or lies in the grass.
  const bool winter = season == Season::Winter, autumn = season == Season::Autumn, spring = season == Season::Spring;
  const auto in_season = [&](MeshId mesh) {
    if (!winter) return mesh;
    switch (mesh) {
      case MeshId::Tree:
      case MeshId::FruitTree: return MeshId::TreeBare;
      case MeshId::TreeMid:
      case MeshId::TreeFar: return MeshId::TreeBareFar;
      default: return mesh;
    }
  };
  const auto chance = [&](float x, float z, uint64_t salt) {
    return rand01(w.seed ^ salt, static_cast<uint64_t>(std::lround(x * 16.0f) + (1 << 20)),
                  static_cast<uint64_t>(std::lround(z * 16.0f) + (1 << 20)));
  };
  // Fallen leaves lying at (x, z), on the slope of the ground.
  const auto litter = [&](float x, float z, float size, uint64_t salt) {
    Instance in;
    in.x = x;
    in.z = z;
    in.y = w.ground.height(x, z);
    in.rotation = ground_tilt(w, x, z) * quat_yaw(kTau * chance(x, z, salt + 1));
    in.sx = in.sz = size;
    in.material = kFadeNear;
    emit(MeshId::LeafLitter, in);
  };

  constexpr float near2 = landscape::kDetailRadius * landscape::kDetailRadius;
  constexpr float full2 = landscape::kFullRadius * landscape::kFullRadius;
  constexpr float reach2 = landscape::kObjectsRadius * landscape::kObjectsRadius;
  constexpr float grass2 = landscape::kGrassRadius * landscape::kGrassRadius;
  const float r = landscape::kObjectsRadius;
  const int lo_x = chunk_x(center.x - r), hi_x = chunk_x(center.x + r);
  const int lo_z = chunk_z(center.z - r), hi_z = chunk_z(center.z + r);
  for (int cx = 0; cx < landscape::kChunksX; ++cx) {
    for (int cz = 0; cz < landscape::kChunksZ; ++cz) {
      const bool around = cx >= lo_x && cx <= hi_x && cz >= lo_z && cz <= hi_z &&
                          chunk_distance2(cx, cz, center.x, center.z) <= reach2;
      if (!around && !valley_chunk(cx, cz)) continue;
      const bool grass = chunk_distance2(cx, cz, center.x, center.z) <= grass2;
      const LandscapeCache::Chunk& c = cache.chunk(w, cx, cz, grass);
      for (const LandscapeCache::Item& item : c.items) {
        const float dx = item.x - center.x, dz = item.z - center.z;
        const float d2 = dx * dx + dz * dz;
        if (item.nature && d2 > reach2) continue;  // too far: not drawn (the buildings are, everywhere)
        const bool near = d2 < near2;
        if (!near && item.small) continue;
        const bool leafy = item.full == MeshId::Tree || item.full == MeshId::FruitTree || item.full == MeshId::Bush;
        if (!winter || !leafy || item.full == MeshId::Bush) {
          emit(near ? (d2 < full2 ? item.full : item.mid) : item.far, item.instance);
          if (near && item.has_shadow) emit(MeshId::Shadow, item.shadow);
        } else {  // bare: the fruit tree as a smaller tree, a fainter shadow
          Instance in = item.instance;
          if (item.full == MeshId::FruitTree) in.sx = in.sy = in.sz = in.sx * 0.72f;
          emit(in_season(near ? (d2 < full2 ? item.full : item.mid) : item.far), in);
          if (near && item.has_shadow) {
            Instance shade = item.shadow;
            shade.tint.a *= 0.45f;
            emit(MeshId::Shadow, shade);
          }
        }
        if (autumn && leafy && d2 < grass2) {  // their leaves on the ground around them
          const float spread = item.instance.sx * (item.full == MeshId::Bush ? 0.9f : 1.5f);
          const int patches = item.full == MeshId::Bush ? 1 : 3;
          for (int k = 0; k < patches; ++k) {
            const float a = kTau * chance(item.x, item.z, 0x1EAF0ULL + static_cast<uint64_t>(k));
            const float out = spread * std::sqrt(chance(item.x, item.z, 0x1EAF8ULL + static_cast<uint64_t>(k)));
            litter(item.x + out * std::cos(a), item.z + out * std::sin(a),
                   0.8f + 0.5f * chance(item.x, item.z, 0x1EB00ULL + static_cast<uint64_t>(k)), 0x1EB10ULL + static_cast<uint64_t>(k));
          }
        }
      }
      if (!grass) continue;
      for (const LandscapeCache::Grass& g : c.grass) {
        const float dx = g.instance.x - center.x, dz = g.instance.z - center.z;
        const float d2 = dx * dx + dz * dz;
        if (d2 > grass2) continue;
        // Thinning out: fewer tufts, a little wider, farther away.
        const float d = std::sqrt(d2);
        const float share = d <= landscape::kGrassThinStart
                                ? 1.0f
                                : 1.0f - (1.0f - landscape::kGrassThinLeast) * (d - landscape::kGrassThinStart) /
                                             (landscape::kGrassRadius - landscape::kGrassThinStart);
        if (g.keep >= share) continue;
        // The season: no flowers in winter, less grass and lower (under the
        // snow); fewer flowers in autumn, a few leaves; more flowers in spring.
        MeshId kind = g.mesh;
        Instance in = g.instance;
        const float roll = chance(in.x, in.z, 0x5EA50ULL);
        if (winter) {
          if (kind == MeshId::Flower || roll > 0.4f) continue;
          in.sy *= kind == MeshId::GrassTall ? 0.65f : 0.7f;
        } else if (autumn) {
          if (kind == MeshId::Flower && roll > 0.3f) continue;
          if (kind == MeshId::GrassShort && roll > 0.975f) litter(in.x, in.z, 0.6f + 0.4f * chance(in.x, in.z, 3), 0x1EB20ULL);
        } else if (spring && kind == MeshId::GrassShort && roll < 0.14f) {
          kind = MeshId::Flower;
          in.sx = in.sy = in.sz = 0.8f + 0.5f * chance(in.x, in.z, 4);
          in.tint = kFlowerColors[static_cast<size_t>(chance(in.x, in.z, 5) * 5) % 5];
        }
        const MeshId mesh = d <= landscape::kGrassFullRadius ? kind : simpler_grass(kind);
        if (share >= 1.0f) {
          emit(mesh, in);
          continue;
        }
        const float widen = std::sqrt(1.0f / share);
        in.sx *= widen;
        in.sz *= widen;
        emit(mesh, in);
      }
    }
  }

  // Each batch: its bins one after the other, each one a part.
  for (size_t m = 0; m < kMeshCount; ++m) {
    std::vector<Instance>& batch = list.batches[m];
    for (int b = 0; b < kBins; ++b) {
      const size_t k = m * kBins + static_cast<size_t>(b);
      if (bins[k].empty()) continue;
      list.parts[m].push_back({{static_cast<uint32_t>(batch.size()), static_cast<uint32_t>(bins[k].size())}, bounds[k]});
      batch.insert(batch.end(), bins[k].begin(), bins[k].end());
    }
  }
}

bool follow_player(const World& w, DrawList& list, LandscapeCache& cache, Vec2& built, double budget_ms, bool all_now,
                   int* prepared, Season season) {
  const Vec2 player{w.player.pos.x, w.player.pos.z};
  const Vec2 center = landscape_center(player.x, player.z);
  int n = 0;
  bool rebuilt = false;
  if (center == built) {
    n = cache.prepare(w, player, budget_ms);  // what the next steps will need
  } else if (!all_now && !cache.ready(w, center) && landscape_can_wait(player, built)) {
    n = cache.prepare(w, center, budget_ms);  // a little longer: its chunks first
  } else {
    built = center;
    build_landscape(w, list, center, cache, season);  // what is still missing is computed now
    rebuilt = true;
  }
  if (prepared) *prepared += n;
  return rebuilt;
}

void build_landscape(const World& w, DrawList& list, Vec2 center, Season season) {
  LandscapeCache cache;
  build_landscape(w, list, center, cache, season);
}

void build_landscape(const World& w, DrawList& list, Season season) {
  build_landscape(w, list, landscape_center(w.player.pos.x, w.player.pos.z), season);
}

// One object inside (interior.hpp), with its shadow: as in the landscape,
// and turned upside down or sideways when it hangs from the ceiling or
// sticks out of a wall.
void add_interior_object(const World& w, const Object& o, DrawList& list) {
  if (o.kind == ObjectKind::Step && o.variant < kLandingVariant) return;  // the stair: in the room's mesh
  const LandscapeCache::Item item = make_item(w, o);
  Instance in = item.instance;
  in.material = scene_util::material_for(item.full, kLit);
  const Quat yaw = quat_yaw(static_cast<float>(radians_from_angle16(o.yaw)));
  if (o.mount == Object::kHanging) in.rotation = yaw * quat_x(static_cast<float>(kPi));
  if (o.mount == Object::kOnWall) in.rotation = yaw * quat_x(static_cast<float>(kPi) / 2);
  scene_util::add(list, item.full, in);
  if (item.has_shadow && o.mount == 0) scene_util::add(list, MeshId::Shadow, item.shadow);
  if (o.kind == ObjectKind::Hearth) {
    Instance fire = in;
    fire.tint = {1, 1, 1, 1};
    scene_util::add(list, MeshId::HearthFire, fire);
  }
}

}  // namespace monstres
