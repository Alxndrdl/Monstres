#include "monstres/render/terrain_mesh.hpp"

#include "monstres/core/continent.hpp"

#include "monstres/render/models.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace monstres {

namespace {

constexpr Color kGrassDark{0.33f, 0.52f, 0.24f, 1};
constexpr Color kGrassLight{0.47f, 0.66f, 0.31f, 1};
constexpr Color kGrassDry{0.58f, 0.66f, 0.34f, 1};
constexpr Color kEarth{0.48f, 0.38f, 0.26f, 1};
constexpr Color kPath{0.60f, 0.47f, 0.32f, 1};
constexpr Color kSand{0.80f, 0.72f, 0.52f, 1};
constexpr Color kLakeBed{0.36f, 0.42f, 0.36f, 1};
constexpr Color kSquare{0.58f, 0.50f, 0.38f, 1};  // trodden earth of the village square
constexpr Color kSlope{0.50f, 0.44f, 0.34f, 1};   // bare earth and stones on steep slopes
constexpr Color kPaving{0.60f, 0.58f, 0.54f, 1};  // old flagstones of the ruins' courtyard
constexpr Color kForestFloor{0.27f, 0.40f, 0.20f, 1};  // shaded grass and moss under the trees
constexpr Color kCastlePaving{0.58f, 0.56f, 0.53f, 1};
constexpr Color kCaveRock{0.46f, 0.44f, 0.43f, 1};

Color lerp(Color a, Color b, float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

float smooth(float e0, float e1, float x) {
  const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
  return t * t * (3 - 2 * t);
}

// Makes every triangle of a flat mesh face up (counter-clockwise seen from above).
void face_up(MeshData& m) {
  constexpr size_t kv = MeshData::kFloatsPerVertex;
  for (size_t t = 0; t < m.vertices.size(); t += 3 * kv) {
    const float* a = &m.vertices[t];
    const float* b = &m.vertices[t + kv];
    const float* c = &m.vertices[t + 2 * kv];
    const float ny = (c[0] - a[0]) * (b[2] - a[2]) - (b[0] - a[0]) * (c[2] - a[2]);
    if (ny < 0) {
      std::swap_ranges(m.vertices.begin() + static_cast<std::ptrdiff_t>(t + kv),
                       m.vertices.begin() + static_cast<std::ptrdiff_t>(t + 2 * kv),
                       m.vertices.begin() + static_cast<std::ptrdiff_t>(t + 2 * kv));
    }
  }
}

void vertex(MeshData& m, float x, float y, float z, Color c, Vec3 n = {0, 1, 0}) {
  m.vertices.insert(m.vertices.end(), {x, y, z, n.x, n.y, n.z, c.r, c.g, c.b, c.a});
}

// 0 at the shore, 1 in the middle of the lake, negative outside.
float lake_depth_factor(const Lake& lake, float x, float z) {
  if (lake.radius_x <= 0) return -1;
  const float dx = (x - lake.center.x) / lake.radius_x, dz = (z - lake.center.z) / lake.radius_z;
  return 1 - std::sqrt(dx * dx + dz * dz);
}

Color base_ground_color(uint64_t seed, float x, float z) {
  const float large = value_noise(seed + 1, x, z, 11.0f);
  const float small = value_noise(seed + 2, x, z, 3.0f);
  Color c = lerp(kGrassDark, kGrassLight, 0.65f * large + 0.35f * small);
  c = lerp(c, kGrassDry, smooth(0.62f, 0.85f, value_noise(seed + 3, x, z, 17.0f)) * 0.7f);
  // Patches of bare earth.
  const float earth = value_noise(seed + 4, x, z, 6.0f) * 0.75f + small * 0.25f;
  return lerp(c, kEarth, smooth(0.70f, 0.80f, earth) * 0.85f);
}

Color ground_color(const World& w, float x, float z) {
  Color c = base_ground_color(w.seed, x, z);
  // Hilltops a little drier, steep slopes bare.
  const float h = w.ground.height(x, z);
  c = lerp(c, kGrassDry, smooth(2.5f, 8.0f, h) * 0.45f);
  const float steep = 1 - w.ground.normal(x, z).y;  // 0 flat, 0.29 at 45°
  c = lerp(c, kSlope, smooth(0.10f, 0.25f, steep) * 0.8f);
  if (w.village.lake.radius_x > 0) {
    // The village square: trodden earth fading into the grass.
    const float dx = x - w.village.center.x, dz = z - w.village.center.z;
    const float square = 1 - smooth(4.5f, 8.0f, std::sqrt(dx * dx + dz * dz) + value_noise(w.seed + 30, x, z, 2.0f) * 1.5f);
    c = lerp(c, kSquare, square * 0.9f);
    // Sandy shore, then the lake bed.
    const float depth = lake_depth_factor(w.village.lake, x, z);
    c = lerp(c, kSand, smooth(-0.18f, -0.02f, depth));
    c = lerp(c, kLakeBed, smooth(0.08f, 0.35f, depth));
  }
  if (w.forest.radius_x > 0) {
    // Under the trees: darker grass and moss, fading out at the edge of the forest.
    const float fx = (x - w.forest.center.x) / w.forest.radius_x, fz = (z - w.forest.center.z) / w.forest.radius_z;
    const float inside = 1 - smooth(0.75f, 1.05f, std::sqrt(fx * fx + fz * fz));
    c = lerp(c, kForestFloor, inside * (0.55f + 0.3f * value_noise(w.seed + 33, x, z, 5.0f)));
  }
  if (w.castle.radius > 0) {
    // The castle's courtyard: flagstones.
    const float dx = x - w.castle.center.x, dz = z - w.castle.center.z;
    const float inside = 1 - smooth(rules::kCastleHalf - 1.0f, rules::kCastleHalf + 1.5f, std::max(std::fabs(dx), std::fabs(dz)));
    const Color stone = lerp(kCastlePaving, kEarth, 0.2f * value_noise(w.seed + 34, x, z, 1.5f));
    c = lerp(c, stone, inside * 0.85f);
  }
  if (w.cave.radius > 0) {
    // The cave's hill: grey rock, a little grass on top.
    const float dx = x - w.cave.center.x, dz = z - w.cave.center.z;
    const float rock = 1 - smooth(w.cave.radius - 1.0f, w.cave.radius + 2.0f, std::sqrt(dx * dx + dz * dz));
    c = lerp(c, lerp(kCaveRock, kGrassDark, 0.35f * smooth(0.55f, 0.8f, value_noise(w.seed + 35, x, z, 4.0f))), rock * 0.9f);
  }
  if (w.ruins.radius > 0) {
    // The courtyard of the ruins: worn flagstones, grass growing through.
    const float dx = x - w.ruins.center.x, dz = z - w.ruins.center.z;
    const float inside = 1 - smooth(7.0f, 9.5f, std::max(std::fabs(dx), std::fabs(dz)));
    const float worn = value_noise(w.seed + 31, x, z, 2.5f);
    const Color stone = lerp(kPaving, kEarth, 0.25f * value_noise(w.seed + 32, x, z, 1.0f));
    c = lerp(c, stone, inside * (0.35f + 0.6f * smooth(0.3f, 0.6f, worn)));
  }
  // The wilds: the continent's forest floor, the beach and the sea floor.
  if (region_at(x, z) == Region::Forest) c = lerp(c, kForestFloor, 0.6f);
  // Near the shore only: the continent's ground there is close to the sea (around the valley it is 2 m above).
  const float coast = smooth(rules::kSeaLevel + 1.5f, rules::kSeaLevel + 0.2f, w.ground.land_height(x, z));
  c = lerp(c, kSand, smooth(rules::kSeaLevel + 3.0f, rules::kSeaLevel + 0.8f, h) * coast);
  c = lerp(c, kLakeBed, smooth(rules::kSeaLevel, rules::kSeaLevel - 2.5f, h) * coast);
  return c;
}

}  // namespace

float ground_height(const World& w, float x, float z) { return w.ground.height(x, z); }

int tile_of(float v) {
  return std::clamp(static_cast<int>(std::floor((v + terrain::kDetailHalfSize) / terrain::kTile)), 0, terrain::kTiles - 1);
}

float tile_start(int t) { return -terrain::kDetailHalfSize + terrain::kTile * static_cast<float>(t); }

std::vector<TileKey> detailed_tiles(float x, float z) {
  const int pi = tile_of(x), pj = tile_of(z);
  const auto lod_at = [&](int i, int j) {
    const int d = std::max(std::abs(i - pi), std::abs(j - pj));
    for (int l = 0; l < terrain::kLods - 1; ++l) {
      if (d <= terrain::kLodReach[l]) return l;
    }
    return terrain::kLods - 1;
  };
  const auto inside = [](int i, int j) { return i >= 0 && j >= 0 && i < terrain::kTiles && j < terrain::kTiles; };
  std::vector<TileKey> keys;
  const int r = terrain::kLodReach[terrain::kLods - 2];
  for (int i = pi - r; i <= pi + r; ++i) {
    for (int j = pj - r; j <= pj + r; ++j) {
      if (!inside(i, j)) continue;
      const int lod = lod_at(i, j);
      if (lod >= terrain::kLods - 1) continue;
      uint8_t stitch = 0;
      const int sides[4][2] = {{i - 1, j}, {i + 1, j}, {i, j - 1}, {i, j + 1}};
      for (int s = 0; s < 4; ++s) {
        if (inside(sides[s][0], sides[s][1]) && lod_at(sides[s][0], sides[s][1]) > lod) stitch |= static_cast<uint8_t>(1 << s);
      }
      keys.push_back({static_cast<int8_t>(i), static_cast<int8_t>(j), static_cast<uint8_t>(lod), stitch});
    }
  }
  return keys;
}

namespace {

uint32_t pack(Color c) {
  const auto byte = [](float v) { return static_cast<uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
  return byte(c.r) | (byte(c.g) << 8) | (byte(c.b) << 16) | (255u << 24);
}
Color unpack(uint32_t p) {
  return {static_cast<float>(p & 255u) / 255.0f, static_cast<float>((p >> 8) & 255u) / 255.0f,
          static_cast<float>((p >> 16) & 255u) / 255.0f, 1};
}

// Appends a square of n x n cells of `cell` meters from (x0, z0), on the
// relief's grid points. A side in `stitch` follows the coarser neighbor's
// edge (cells twice as big): its points in between are on the straight
// line between theirs. Returns where it is.
Aabb add_cells(MeshData& m, const World& w, GroundColors& colors, float x0, float z0, float cell, int n, uint8_t stitch) {
  constexpr float d = terrain::kDetailHalfSize;
  constexpr float kGrid = Heightmap::kCellSize;
  constexpr int kPoints = Heightmap::kPoints;
  if (colors.packed.size() != static_cast<size_t>(kPoints) * kPoints) colors.packed.assign(static_cast<size_t>(kPoints) * kPoints, 0);
  const auto index = [&](float v) { return std::clamp(static_cast<int>(std::lround((v + d) / kGrid)), 0, kPoints - 1); };
  const auto height = [&](float x, float z) { return w.ground.empty() ? 0.0f : w.ground.at(index(x), index(z)); };
  struct Point {
    float y;
    Color c;
    Vec3 n;
  };
  const int p = n + 1;
  std::vector<Point> points(static_cast<size_t>(p * p));
  Aabb box = empty_aabb();
  for (int a = 0; a <= n; ++a) {
    for (int b = 0; b <= n; ++b) {
      const float x = x0 + cell * static_cast<float>(a), z = z0 + cell * static_cast<float>(b);
      float y = height(x, z);
      if ((b & 1) && (((stitch & 1) && a == 0) || ((stitch & 2) && a == n))) y = 0.5f * (height(x, z - cell) + height(x, z + cell));
      if ((a & 1) && (((stitch & 4) && b == 0) || ((stitch & 8) && b == n))) y = 0.5f * (height(x - cell, z) + height(x + cell, z));
      uint32_t& c = colors.packed[static_cast<size_t>(index(x) * kPoints + index(z))];
      if ((c >> 24) == 0) c = pack(ground_color(w, x, z));
      points[static_cast<size_t>(a * p + b)] = Point{y, unpack(c), w.ground.normal(x, z)};
      box.add(x, y, z);
    }
  }
  for (int a = 0; a < n; ++a) {
    for (int b = 0; b < n; ++b) {
      const float xa = x0 + cell * static_cast<float>(a), xb = xa + cell;
      const float za = z0 + cell * static_cast<float>(b), zb = za + cell;
      const auto at = [&](int i, int j) -> const Point& { return points[static_cast<size_t>((a + i) * p + b + j)]; };
      const Point &p00 = at(0, 0), &p10 = at(1, 0), &p01 = at(0, 1), &p11 = at(1, 1);
      // Counter-clockwise seen from above; the same triangles as Heightmap::height (in 2 m cells).
      vertex(m, xa, p00.y, za, p00.c, p00.n); vertex(m, xa, p01.y, zb, p01.c, p01.n); vertex(m, xb, p11.y, zb, p11.c, p11.n);
      vertex(m, xa, p00.y, za, p00.c, p00.n); vertex(m, xb, p11.y, zb, p11.c, p11.n); vertex(m, xb, p10.y, za, p10.c, p10.n);
    }
  }
  box.min_y -= 0.1f;
  box.max_y += 0.1f;
  return box;
}

int cells_per_tile(int lod) { return static_cast<int>(std::lround(terrain::kTile / terrain::kLodCells[lod])); }

double now_ms() {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

}  // namespace

MeshData build_coarse_ground(const World& world, GroundColors& colors) {
  MeshData m;
  constexpr int coarse = terrain::kLods - 1;
  const int n = cells_per_tile(coarse);
  m.vertices.reserve(static_cast<size_t>(terrain::kTiles * terrain::kTiles * n * n * 6) * MeshData::kFloatsPerVertex);
  for (int i = 0; i < terrain::kTiles; ++i) {
    for (int j = 0; j < terrain::kTiles; ++j) {
      const auto first = static_cast<uint32_t>(m.vertex_count());
      const Aabb box = add_cells(m, world, colors, tile_start(i), tile_start(j), terrain::kLodCells[coarse], n, 0);
      m.parts.push_back({{first, static_cast<uint32_t>(m.vertex_count()) - first}, box});
    }
  }
  return m;
}

MeshData build_ground_tile(const World& world, GroundColors& colors, TileKey key) {
  MeshData m;
  const int n = cells_per_tile(key.lod);
  m.vertices.reserve(static_cast<size_t>(n * n * 6) * MeshData::kFloatsPerVertex);
  const Aabb box = add_cells(m, world, colors, tile_start(key.i), tile_start(key.j), terrain::kLodCells[key.lod], n, key.stitch);
  m.parts.push_back({{0, static_cast<uint32_t>(m.vertex_count())}, box});
  return m;
}

MeshData build_ground_mesh(const World& world) {
  GroundColors colors;
  const MeshData coarse = build_coarse_ground(world, colors);
  const std::vector<TileKey> keys = detailed_tiles(world.player.pos.x, world.player.pos.z);
  MeshData m;
  for (int i = 0; i < terrain::kTiles; ++i) {
    for (int j = 0; j < terrain::kTiles; ++j) {
      if (std::any_of(keys.begin(), keys.end(), [&](const TileKey& k) { return k.i == i && k.j == j; })) continue;
      const DrawRange r = coarse.parts[tile_part_index(i, j)].range;
      const auto from = coarse.vertices.begin() + static_cast<std::ptrdiff_t>(r.first * MeshData::kFloatsPerVertex);
      m.vertices.insert(m.vertices.end(), from, from + static_cast<std::ptrdiff_t>(r.count * MeshData::kFloatsPerVertex));
    }
  }
  for (const TileKey& k : keys) {
    const MeshData tile = build_ground_tile(world, colors, k);
    m.vertices.insert(m.vertices.end(), tile.vertices.begin(), tile.vertices.end());
  }
  return m;
}

// ---- The detailed tiles drawn ----------------------------------------------------

void GroundTiles::reset() {
  slots_.assign(terrain::kTileSlots, Slot{});
  drawn_.clear();
  drawn_keys_.clear();
  frame_ = 0;
}

int GroundTiles::find(TileKey key) const {
  for (size_t s = 0; s < slots_.size(); ++s) {
    if (slots_[s].used && slots_[s].key == key) return static_cast<int>(s);
  }
  return -1;
}

bool GroundTiles::update(const World& world, GroundColors& colors, Vec2 player, double budget_ms, Upload upload, void* user) {
  if (slots_.empty()) reset();
  ++frame_;
  const std::vector<TileKey> wanted = detailed_tiles(player.x, player.z);
  if (wanted == drawn_keys_) {
    for (int s : drawn_) slots_[static_cast<size_t>(s)].last_drawn = frame_;
    return false;
  }
  const bool all_now = drawn_.empty();
  const double start = now_ms();
  int built_now = 0;
  const auto is_wanted = [&](TileKey k) { return std::find(wanted.begin(), wanted.end(), k) != wanted.end(); };
  for (const TileKey& key : wanted) {
    if (find(key) >= 0) continue;
    if (!all_now && built_now > 0 && now_ms() - start >= budget_ms) break;
    // A free slot, or the one drawn longest ago that is neither drawn nor wanted.
    int slot = -1;
    for (size_t s = 0; s < slots_.size(); ++s) {
      const Slot& c = slots_[s];
      if (!c.used) {
        slot = static_cast<int>(s);
        break;
      }
      if (std::find(drawn_.begin(), drawn_.end(), static_cast<int>(s)) != drawn_.end() || is_wanted(c.key)) continue;
      if (slot < 0 || c.last_drawn < slots_[static_cast<size_t>(slot)].last_drawn) slot = static_cast<int>(s);
    }
    if (slot < 0) break;  // cannot happen: more slots than two sets of tiles
    const MeshData mesh = build_ground_tile(world, colors, key);
    upload(user, static_cast<size_t>(slot), mesh);
    slots_[static_cast<size_t>(slot)] = {key, true, frame_, mesh.parts.front().bounds};
    ++built_;
    ++built_now;
  }
  std::vector<int> slots;
  for (const TileKey& key : wanted) {
    const int s = find(key);
    if (s < 0) return false;  // not all ready yet: the tiles before stay
    slots.push_back(s);
  }
  drawn_ = std::move(slots);
  drawn_keys_ = wanted;
  for (int s : drawn_) slots_[static_cast<size_t>(s)].last_drawn = frame_;
  return true;
}

void GroundTiles::fill_list(DrawList& list, const std::vector<Part>& coarse_parts, const std::vector<Part>& path_parts) const {
  const size_t n = kMeshCount + terrain::kTileSlots;
  list.batches.assign(n, {});
  list.parts.assign(n, {});
  list.mesh_parts.assign(n, {});
  list.visible.clear();
  list.cached = true;
  list.far = false;
  ++list.version;
  const auto ground = static_cast<size_t>(MeshId::Ground);
  list.batches[ground].push_back(Instance{});
  list.mesh_parts[ground] = coarse_parts;
  for (const TileKey& k : drawn_keys_) {
    const size_t part = tile_part_index(k.i, k.j);
    if (part < list.mesh_parts[ground].size()) list.mesh_parts[ground][part].hidden = true;  // drawn in detail
  }
  const auto path = static_cast<size_t>(MeshId::Path);
  list.batches[path].push_back(Instance{});
  list.mesh_parts[path] = path_parts;
  Instance water;
  water.material = kWater;  // the shader adds waves and reflections
  list.batches[static_cast<size_t>(MeshId::Water)].push_back(water);
  for (int s : drawn_) {
    const size_t i = kMeshCount + static_cast<size_t>(s);
    list.batches[i].push_back(Instance{});
    list.parts[i].push_back({{0, 1}, slots_[static_cast<size_t>(s)].bounds});
  }
}

MeshData build_water_mesh(const World& world) {
  MeshData m;
  m.transparent = true;
  // The sea, where the relief goes below it (clear in the shallows, deep blue
  // farther): squares of 8 m at its level, as the continent's water beyond.
  if (!world.ground.empty()) {
    constexpr float kCell = 8.0f;
    const float d = terrain::kDetailHalfSize;
    const int n = static_cast<int>(2 * d / kCell);
    const auto water = [&](float x, float z) {
      const float depth = rules::kSeaLevel - world.ground.height(x, z);
      return Color{0.18f - 0.08f * smooth(0.0f, 20.0f, depth), 0.44f - 0.18f * smooth(0.0f, 20.0f, depth),
                   0.56f - 0.16f * smooth(0.0f, 20.0f, depth), 0.55f + 0.45f * smooth(0.0f, 14.0f, depth)};
    };
    const float y = rules::kSeaLevel;
    for (int i = 0; i < n; ++i) {
      for (int j = 0; j < n; ++j) {
        const float x0 = -d + kCell * static_cast<float>(i), z0 = -d + kCell * static_cast<float>(j);
        const float x1 = x0 + kCell, z1 = z0 + kCell;
        const float lowest = std::min({world.ground.height(x0, z0), world.ground.height(x1, z0), world.ground.height(x0, z1),
                                       world.ground.height(x1, z1), world.ground.height(x0 + 4, z0 + 4)});
        if (lowest > rules::kSeaLevel + 0.2f || !world.ground.sea(x0 + 4, z0 + 4)) continue;  // not a hollow of the hills
        vertex(m, x0, y, z0, water(x0, z0)); vertex(m, x0, y, z1, water(x0, z1)); vertex(m, x1, y, z1, water(x1, z1));
        vertex(m, x0, y, z0, water(x0, z0)); vertex(m, x1, y, z1, water(x1, z1)); vertex(m, x1, y, z0, water(x1, z0));
      }
    }
  }
  const Lake& lake = world.village.lake;
  if (lake.radius_x <= 0) {
    face_up(m);
    return m;
  }
  constexpr int kSegments = 48;
  constexpr float kTau = 6.28318530718f;
  const float y = rules::kWaterLevel;
  const float rings[] = {0.0f, 0.55f, 0.85f, 1.03f};  // fraction of the radius
  const Color colors[] = {{0.16f, 0.38f, 0.52f, 0.9f}, {0.20f, 0.45f, 0.58f, 0.86f},
                          {0.28f, 0.56f, 0.64f, 0.72f}, {0.42f, 0.68f, 0.70f, 0.0f}};
  auto point = [&](int ring, int seg, float& x, float& z) {
    const float a = kTau * static_cast<float>(seg) / kSegments;
    x = lake.center.x + std::cos(a) * lake.radius_x * rings[ring];
    z = lake.center.z + std::sin(a) * lake.radius_z * rings[ring];
  };
  for (int ring = 0; ring < 3; ++ring) {
    for (int seg = 0; seg < kSegments; ++seg) {
      float ax, az, bx, bz, cx, cz, dx, dz;
      point(ring, seg, ax, az);
      point(ring, seg + 1, bx, bz);
      point(ring + 1, seg + 1, cx, cz);
      point(ring + 1, seg, dx, dz);
      if (ring > 0) {
        vertex(m, ax, y, az, colors[ring]); vertex(m, cx, y, cz, colors[ring + 1]); vertex(m, bx, y, bz, colors[ring]);
      }
      vertex(m, ax, y, az, colors[ring]); vertex(m, dx, y, dz, colors[ring + 1]); vertex(m, cx, y, cz, colors[ring + 1]);
    }
  }
  face_up(m);
  return m;
}

MeshData build_path_mesh(const World& world) {
  MeshData m;
  m.transparent = true;
  const auto& pts = world.path.points;
  if (pts.size() < 2) return m;
  const size_t n = pts.size();

  // Across the path: soft edge, dirt, center, dirt, soft edge.
  constexpr int kColumns = 5;
  const float offsets[kColumns] = {-1.2f, -0.75f, 0.0f, 0.75f, 1.2f};
  const float alphas[kColumns] = {0.0f, 1.0f, 1.0f, 1.0f, 0.0f};

  auto point = [&](size_t i, int col, float& x, float& z, Color& c) {
    const Vec2 prev = pts[i > 0 ? i - 1 : i], next = pts[i + 1 < n ? i + 1 : i];
    float tx = next.x - prev.x, tz = next.z - prev.z;
    const float len = std::sqrt(tx * tx + tz * tz);
    tx /= len;
    tz /= len;
    const float wobble = 0.9f + 0.2f * value_noise(world.seed + 9, static_cast<float>(i), static_cast<float>(col), 3.0f);
    const float w = rules::kPathHalfWidth * offsets[col] * wobble;
    x = pts[i].x - tz * w;
    z = pts[i].z + tx * w;
    const float shade = 0.92f + 0.12f * value_noise(world.seed + 10, x, z, 1.5f);
    // Fade out over the last few meters at both ends.
    const float end_fade = std::min(smooth(0.0f, 8.0f, static_cast<float>(i)),
                                    smooth(0.0f, 8.0f, static_cast<float>(n - 1 - i)));
    c = {kPath.r * shade, kPath.g * shade, kPath.b * shade, alphas[col] * end_fade};
  };

  // By stretches of the path, each a part (culling).
  constexpr size_t kStretch = 48;
  uint32_t part_first = 0;
  Aabb part_box = empty_aabb();
  for (size_t i = 0; i + 1 < n; ++i) {
    if (i % kStretch == 0 && i > 0) {
      m.parts.push_back({{part_first, static_cast<uint32_t>(m.vertex_count()) - part_first}, part_box});
      part_first = static_cast<uint32_t>(m.vertex_count());
      part_box = empty_aabb();
    }
    for (int col = 0; col + 1 < kColumns; ++col) {
      float ax, az, bx, bz, cx, cz, dx, dz;
      Color ca, cb, cc, cd;
      point(i, col, ax, az, ca);
      point(i, col + 1, bx, bz, cb);
      point(i + 1, col + 1, cx, cz, cc);
      point(i + 1, col, dx, dz, cd);
      // Following the ground, just above it.
      auto y = [&](float x, float z) { return ground_height(world, x, z) + terrain::kPathHeight; };
      vertex(m, ax, y(ax, az), az, ca); vertex(m, bx, y(bx, bz), bz, cb); vertex(m, cx, y(cx, cz), cz, cc);
      vertex(m, ax, y(ax, az), az, ca); vertex(m, cx, y(cx, cz), cz, cc); vertex(m, dx, y(dx, dz), dz, cd);
      for (float* v = &m.vertices[m.vertices.size() - 6 * MeshData::kFloatsPerVertex]; v < m.vertices.data() + m.vertices.size();
           v += MeshData::kFloatsPerVertex) {
        part_box.add(v[0], v[1], v[2]);
      }
    }
  }
  m.parts.push_back({{part_first, static_cast<uint32_t>(m.vertex_count()) - part_first}, part_box});
  face_up(m);  // swaps vertices within triangles: the parts stay the same
  return m;
}

}  // namespace monstres
