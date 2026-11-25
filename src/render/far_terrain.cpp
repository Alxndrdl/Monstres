#include "monstres/render/far_terrain.hpp"

#include "monstres/core/heightmap.hpp"
#include "monstres/render/cull.hpp"

#include <algorithm>
#include <cmath>

namespace monstres {

namespace {

using far_terrain::kCells;
using far_terrain::kLevels;
using far_terrain::kOuter;

constexpr Color kGrass{0.40f, 0.60f, 0.28f, 1};
constexpr Color kGrassDry{0.58f, 0.64f, 0.34f, 1};
constexpr Color kForest{0.20f, 0.34f, 0.17f, 1};
constexpr Color kMarsh{0.34f, 0.38f, 0.22f, 1};
constexpr Color kSand{0.84f, 0.74f, 0.52f, 1};
constexpr Color kDesert{0.86f, 0.70f, 0.46f, 1};
constexpr Color kMesa{0.70f, 0.42f, 0.28f, 1};
constexpr Color kRock{0.50f, 0.48f, 0.47f, 1};
constexpr Color kSnow{0.94f, 0.96f, 0.99f, 1};
constexpr Color kBasalt{0.24f, 0.21f, 0.21f, 1};
constexpr Color kEmber{0.62f, 0.24f, 0.12f, 1};
constexpr Color kSeaFloor{0.30f, 0.36f, 0.38f, 1};
constexpr Color kPaving{0.64f, 0.60f, 0.52f, 1};

Color lerp(Color a, Color b, float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

float smooth(float e0, float e1, float x) {
  const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
  return t * t * (3 - 2 * t);
}

// Cut into parts by where they are, for culling: by sectors around the
// playable area, then by distance.
constexpr int kSectors = 24;
constexpr float kBands[] = {1500.0f, 3000.0f};
constexpr float kBig = 1500.0f;


void vertex(MeshData& m, float x, float y, float z, Color c, Vec3 n = {0, 1, 0}) {
  m.vertices.insert(m.vertices.end(), {x, y, z, n.x, n.y, n.z, c.r, c.g, c.b, c.a});
}

}  // namespace

// The ground's color: by region, height and steepness.
Color far_color(const Continent& k, float x, float z, float h, Vec3 n, const Heightmap* ground) {
  const uint64_t seed = k.seed;
  const float steep = 1 - n.y;
  Color c = lerp(kGrass, kGrassDry, value_noise(seed + 600, x, z, 140.0f) * 0.7f);
  const auto& marks = continent_landmarks();
  for (const Landmark& m : marks) {
    switch (m.region) {
      case Region::Forest: c = lerp(c, kForest, smooth(0.0f, 0.25f, place_shape(m.region, x, z))); break;
      case Region::Marsh: c = lerp(c, kMarsh, smooth(0.0f, 0.3f, place_shape(m.region, x, z))); break;
      case Region::Capital: c = lerp(c, kPaving, smooth(0.0f, 0.3f, place_shape(m.region, x, z)) * 0.8f); break;
      default: break;
    }
  }
  // The desert in the south, its mesas reddish.
  c = lerp(c, lerp(kDesert, kMesa, smooth(0.15f, 0.4f, steep) + smooth(30.0f, 45.0f, h) * 0.6f), desert_share(x, z));
  // Steep slopes: rock; high up: snow (not on the steepest faces).
  c = lerp(c, kRock, smooth(0.18f, 0.4f, steep) + smooth(110.0f, 200.0f, h) * 0.8f);
  const float line = snow_line(seed, x, z);
  const float snow = ground ? ground->snow(x, z, h) : smooth(line, line + 40.0f, h);
  c = lerp(c, kSnow, snow * (1 - smooth(0.45f, 0.65f, steep)));
  // The volcanic peninsula: black rock and ash, glowing around the crater.
  const Crater crater = volcano_crater();
  const float to_crater = std::sqrt((x - crater.center.x) * (x - crater.center.x) + (z - crater.center.z) * (z - crater.center.z));
  c = lerp(c, kBasalt, volcano_share(x, z) * 0.85f + smooth(700.0f, 450.0f, to_crater) * 0.15f);
  c = lerp(c, kEmber, smooth(crater.radius * 1.4f, crater.radius * 0.9f, to_crater));
  // Beaches, then the sea floor.
  // (Not around the playable area: its ground comes down to 0 there, just above the sea.)
  const float from_start = std::max(std::fabs(x), std::fabs(z));
  c = lerp(c, kSand, smooth(rules::kSeaLevel + 3.0f, rules::kSeaLevel + 0.5f, h) * (1 - desert_share(x, z)) *
                         smooth(330.0f, 480.0f, from_start));
  for (const Pond& p : continent_ponds()) c = lerp(c, kSeaFloor, smooth(-0.05f, 0.15f, pond_shape(p, x, z)) * smooth(p.level + 0.5f, p.level - 1.5f, h));
  if (float level = 0; ground && in_river(ground->rivers, x, z, h, level)) c = lerp(c, kSeaFloor, smooth(level + 0.1f, level - 0.6f, h));
  c = lerp(c, kSeaFloor, smooth(rules::kSeaLevel, rules::kSeaLevel - 3.0f, h));
  if (h < rules::kSeaLevel - 0.05f) c.a = kUnderWaterAlpha;  // under the sea: no snow on it
  return c;
}

namespace {

// Makes every triangle face up (counter-clockwise seen from above).
void face_up(MeshData& m) {
  constexpr size_t kv = MeshData::kFloatsPerVertex;
  for (size_t t = 0; t < m.vertices.size(); t += 3 * kv) {
    const float* a = &m.vertices[t];
    const float* b = &m.vertices[t + kv];
    const float* c = &m.vertices[t + 2 * kv];
    if ((c[0] - a[0]) * (b[2] - a[2]) - (b[0] - a[0]) * (c[2] - a[2]) < 0) {
      std::swap_ranges(m.vertices.begin() + static_cast<std::ptrdiff_t>(t + kv),
                       m.vertices.begin() + static_cast<std::ptrdiff_t>(t + 2 * kv),
                       m.vertices.begin() + static_cast<std::ptrdiff_t>(t + 2 * kv));
    }
  }
}

Vec3 far_normal(const Continent& k, float x, float z, float step) {
  const float gx = (k.height(x + step, z) - k.height(x - step, z)) / (2 * step);
  const float gz = (k.height(x, z + step) - k.height(x, z - step)) / (2 * step);
  const float len = std::sqrt(gx * gx + 1 + gz * gz);
  return {-gx / len, 1 / len, -gz / len};
}

// A vertex index along a ring's grid: whether it is a multiple of `cell` (from the origin).
bool on_grid(float v, float cell) {
  const float q = v / cell;
  return std::fabs(q - std::round(q)) < 1e-3f;
}

}  // namespace

namespace {

// The ground at a point of a ring: the continent's with its folds (as the
// continent's tiles have it: heightmap.hpp), those too fine for the ring
// left out; by the relief, the relief's tiles themselves.
float far_point(const Continent& k, const Heightmap* ground, float spacing, float x, float z) {
  if (!ground || ground->empty()) return k.at_point(x, z);
  if (rules::relief_inset(x, z) > -(rules::kOuterBlend + 4.0f)) return ground->height(x, z);
  return outer_ground(k.seed, x, z, k.at_point(x, z) + ground->rise(x, z), spacing);
}

// The rings' outer squares around `center` (the origin: the first holds the whole relief).
const float* ring_sizes(Vec2 center) { return center.x == 0 && center.z == 0 ? far_terrain::kOuter : far_terrain::kOuterAway; }

float ring_height(const Continent& k, const Heightmap* ground, int level, float x, float z, Vec2 center) {
  const float cell = kCells[level];
  // On the outer edge of a ring (but the last): between two vertices of the
  // coarser ring outside, follow the straight line between them.
  if (level < kLevels - 1) {
    const float e = ring_sizes(center)[level], coarse = kCells[level + 1], fine = kCells[level];
    const float spacing = kCells[level + 1];  // (as the coarser ring has them)
    const float rx = std::fabs(x - center.x), rz = std::fabs(z - center.z);
    if (std::fabs(rx - e) < 1e-3f && !on_grid(z, coarse)) {
      return 0.5f * (far_point(k, ground, spacing, x, z - fine) + far_point(k, ground, spacing, x, z + fine));
    }
    if (std::fabs(rz - e) < 1e-3f && !on_grid(x, coarse)) {
      return 0.5f * (far_point(k, ground, spacing, x - fine, z) + far_point(k, ground, spacing, x + fine, z));
    }
    if (std::fabs(rx - e) < 1e-3f || std::fabs(rz - e) < 1e-3f) return far_point(k, ground, spacing, x, z);
  }
  return far_point(k, ground, cell, x, z);
}

}  // namespace

Vec2 far_center_for(Vec2 player, Vec2 now) {
  const float dx = player.x - now.x, dz = player.z - now.z;
  if (dx * dx + dz * dz < far_terrain::kRecenter * far_terrain::kRecenter) return now;
  const auto snap = [](float v) { return std::round(v / far_terrain::kCenterStep) * far_terrain::kCenterStep; };
  const Vec2 c{snap(player.x), snap(player.z)};
  // Back near the start: around the origin again (its rings hold the relief).
  return c.x * c.x + c.z * c.z < far_terrain::kRecenter * far_terrain::kRecenter ? Vec2{0, 0} : c;
}

float far_vertex_height(const Continent& k, int level, float x, float z, const Heightmap* ground, Vec2 center) {
  return ring_height(k, ground, level, x, z, center);
}

MeshData build_far_ground_mesh(const Continent& k, const Heightmap* ground, Vec2 center) {
  MeshData m;
  if (k.empty()) return m;
  struct Point {
    float y;
    Color c;
    Vec3 n;
  };
  const float* sizes = ring_sizes(center);
  const bool around_origin = center.x == 0 && center.z == 0;
  std::vector<Point> grid;
  std::vector<int64_t> ids;  // vertex of each grid point, -1 until used (indexed: each point once)
  std::vector<uint8_t> drawn;
  for (int level = 0; level < kLevels; ++level) {
    const float cell = kCells[level], outer = sizes[level];
    const float inner = level == 0 ? 0.0f : sizes[level - 1];
    const int n = static_cast<int>(std::lround(2 * outer / cell));
    const int points = n + 1;
    const auto cx_of = [&](int i) { return center.x - outer + cell * static_cast<float>(i); };
    const auto cz_of = [&](int j) { return center.z - outer + cell * static_cast<float>(j); };
    // Its cells: not those inside the ring's inner square (a finer ring's),
    // nor, around the origin, those wholly on the relief (the World's ground;
    // one going over its edge is drawn, under it). Away from the start, the
    // relief too (from more than 2 km, beyond the near scene, it is these).
    drawn.assign(static_cast<size_t>(n) * n, 0);
    for (int i = 0; i < n; ++i) {
      for (int j = 0; j < n; ++j) {
        const float x0 = cx_of(i), z0 = cz_of(j), x1 = x0 + cell, z1 = z0 + cell;
        const float mx = 0.5f * (x0 + x1) - center.x, mz = 0.5f * (z0 + z1) - center.z;
        if (std::max(std::fabs(mx), std::fabs(mz)) < inner) continue;
        const float in = std::min({rules::relief_inset(x0, z0), rules::relief_inset(x1, z0), rules::relief_inset(x0, z1), rules::relief_inset(x1, z1)});
        if (in > -1e-3f && around_origin) continue;
        drawn[static_cast<size_t>(i * n + j)] = 1;
      }
    }
    grid.assign(static_cast<size_t>(points) * points, Point{});
    ids.assign(grid.size(), -1);
    // Its heights first (and one more all round, for the slopes), then the
    // slopes from the heights around each point, then the colors.
    const int wide = points + 2;
    std::vector<float> heights(static_cast<size_t>(wide) * wide, 0.0f);
    std::vector<uint8_t> needed(static_cast<size_t>(wide) * wide, 0);
    std::vector<uint8_t> corner(static_cast<size_t>(points) * points, 0);
    for (int i = 0; i < n; ++i) {
      for (int j = 0; j < n; ++j) {
        if (!drawn[static_cast<size_t>(i * n + j)]) continue;
        for (int a = 0; a <= 1; ++a) {
          for (int b = 0; b <= 1; ++b) corner[static_cast<size_t>((i + a) * points + j + b)] = 1;
        }
      }
    }
    for (int i = 0; i < points; ++i) {
      for (int j = 0; j < points; ++j) {
        if (!corner[static_cast<size_t>(i * points + j)]) continue;
        for (int a = 0; a <= 2; ++a) {
          for (int b = 0; b <= 2; ++b) needed[static_cast<size_t>((i + a) * wide + j + b)] = 1;
        }
      }
    }
    for (int i = 0; i < wide; ++i) {
      for (int j = 0; j < wide; ++j) {
        if (!needed[static_cast<size_t>(i * wide + j)]) continue;
        const float x = cx_of(i - 1), z = cz_of(j - 1);
        heights[static_cast<size_t>(i * wide + j)] = ring_height(k, ground, level, x, z, center);
      }
    }
    for (int i = 0; i < points; ++i) {
      for (int j = 0; j < points; ++j) {
        if (!corner[static_cast<size_t>(i * points + j)]) continue;
        const float x = cx_of(i), z = cz_of(j);
        const auto h = [&](int a, int b) { return heights[static_cast<size_t>((i + 1 + a) * wide + j + 1 + b)]; };
        const float y = h(0, 0);
        const float gx = (h(1, 0) - h(-1, 0)) / (2 * cell), gz = (h(0, 1) - h(0, -1)) / (2 * cell);
        const float len = std::sqrt(gx * gx + 1 + gz * gz);
        const Vec3 nrm = ground ? Vec3{-gx / len, 1 / len, -gz / len} : far_normal(k, x, z, cell);
        grid[static_cast<size_t>(i * points + j)] = {y, far_color(k, x, z, y, nrm, ground), nrm};
      }
    }
    for (int i = 0; i < n; ++i) {
      for (int j = 0; j < n; ++j) {
        if (!drawn[static_cast<size_t>(i * n + j)]) continue;
        const Point& p00 = grid[static_cast<size_t>(i * points + j)];
        const Point& p10 = grid[static_cast<size_t>((i + 1) * points + j)];
        const Point& p01 = grid[static_cast<size_t>(i * points + j + 1)];
        const Point& p11 = grid[static_cast<size_t>((i + 1) * points + j + 1)];
        // The deep ocean: no ground (the water there is opaque).
        const float deep = rules::kSeaLevel - 20.0f;
        if (p00.y < deep && p10.y < deep && p01.y < deep && p11.y < deep) continue;
        const auto id = [&](int a, int b) {
          const size_t g = static_cast<size_t>((i + a) * points + j + b);
          if (ids[g] < 0) {
            ids[g] = static_cast<int64_t>(m.vertex_count());
            const Point& p = grid[g];
            vertex(m, cx_of(i + a), p.y, cz_of(j + b), p.c, p.n);
          }
          return static_cast<uint32_t>(ids[g]);
        };
        m.indices.insert(m.indices.end(), {id(0, 0), id(0, 1), id(1, 1), id(0, 0), id(1, 1), id(1, 0)});
      }
    }
  }
  // Along its inner edge (the relief's), a band going down, facing the
  // playable area: if the near ground there is a little lower, no gap opens.
  // (Away from the start, the relief is in the mesh: no band.)
  const float e = rules::kReliefHalfSize, north = rules::kReliefNorth, cell = kCells[0];
  for (int side = 0; around_origin && side < 4; ++side) {
    // Sides 0 and 1 (x = e, -e) go along z from the north; 2 and 3 (z = e, -north) along x.
    const float from = side < 2 ? -north : -e;
    const int n = static_cast<int>(std::lround((side < 2 ? e + north : 2 * e) / cell));
    for (int i = 0; i < n; ++i) {
      const float t0 = from + cell * static_cast<float>(i), t1 = t0 + cell;
      const float ax = side == 0 ? e : side == 1 ? -e : t0, az = side == 0 || side == 1 ? t0 : side == 2 ? e : -north;
      const float bx = side == 0 ? e : side == 1 ? -e : t1, bz = side == 0 || side == 1 ? t1 : side == 2 ? e : -north;
      const float ay = far_vertex_height(k, 0, ax, az, ground, center), by = far_vertex_height(k, 0, bx, bz, ground, center);
      const float drop = far_terrain::kSkirt;
      const Vec3 inward{side == 0 ? -1.0f : side == 1 ? 1.0f : 0.0f, 0, side == 2 ? -1.0f : side == 3 ? 1.0f : 0.0f};
      const Color ca = far_color(k, ax, az, ay, {0, 1, 0}, ground), cb = far_color(k, bx, bz, by, {0, 1, 0}, ground);
      // Two triangles, turned so that their front faces the middle.
      float tri[2][3][3] = {{{ax, ay, az}, {ax, ay - drop, az}, {bx, by - drop, bz}}, {{ax, ay, az}, {bx, by - drop, bz}, {bx, by, bz}}};
      for (auto& t : tri) {
        const float ux = t[1][0] - t[0][0], uy = t[1][1] - t[0][1], uz = t[1][2] - t[0][2];
        const float vx = t[2][0] - t[0][0], vy = t[2][1] - t[0][1], vz = t[2][2] - t[0][2];
        const float nx = uy * vz - uz * vy, nz = ux * vy - uy * vx;
        if (nx * inward.x + nz * inward.z < 0) std::swap(t[1], t[2]);
        for (const auto& v : t) {
          const bool first = v[0] == ax && v[2] == az;
          m.indices.push_back(static_cast<uint32_t>(m.vertex_count()));
          vertex(m, v[0], v[1], v[2], first ? ca : cb, inward);
        }
      }
    }
  }
  split_into_parts(m, center.x, center.z, kSectors, kBands, kBig);  // culling
  return m;
}

MeshData build_far_water_mesh(const Continent& k, const Heightmap* ground) {
  MeshData m;
  m.transparent = true;
  if (k.empty()) return m;
  // The continent's own ground, raised by the relief's rim (as the sea is found: Heightmap::sea).
  const auto land = [&](float x, float z) {
    return k.height(x, z) + (ground && !ground->empty() && rules::relief_inset(x, z) <= 0 ? ground->rise(x, z) : 0.0f);
  };
  // The ocean: a grid over the continent (clear in the shallows, opaque
  // where deep), then wide opaque strips out to the horizon.
  const Color deep{0.10f, 0.26f, 0.40f, 1.0f};
  const auto water = [&](float x, float z) {
    const float depth = rules::kSeaLevel - land(x, z);
    return Color{0.18f - 0.08f * smooth(0.0f, 20.0f, depth), 0.44f - 0.18f * smooth(0.0f, 20.0f, depth),
                 0.56f - 0.16f * smooth(0.0f, 20.0f, depth), 0.55f + 0.45f * smooth(0.0f, 14.0f, depth)};
  };
  constexpr float kCell = 80.0f;
  const float x0 = Continent::kMinX + 10.0f, x1 = Continent::kMaxX - 10.0f;  // multiples of 80 from the grid's start
  const float z0 = Continent::kMinZ + 10.0f, z1 = Continent::kMaxZ - 10.0f;
  const float y = rules::kSeaLevel;
  const int nx = static_cast<int>((x1 - x0) / kCell), nz = static_cast<int>((z1 - z0) / kCell);
  const float ex = x0 + kCell * static_cast<float>(nx), ez = z0 + kCell * static_cast<float>(nz);
  for (int i = 0; i < nx; ++i) {
    for (int j = 0; j < nz; ++j) {
      const float ax = x0 + kCell * static_cast<float>(i), az = z0 + kCell * static_cast<float>(j);
      const float bx = ax + kCell, bz = az + kCell;
      // Well above the sea everywhere: no water.
      const float high = rules::kSeaLevel + 6.0f;
      if (land(ax, az) > high && land(bx, az) > high && land(ax, bz) > high && land(bx, bz) > high &&
          land(0.5f * (ax + bx), 0.5f * (az + bz)) > high) {
        continue;
      }
      vertex(m, ax, y, az, water(ax, az)); vertex(m, ax, y, bz, water(ax, bz)); vertex(m, bx, y, bz, water(bx, bz));
      vertex(m, ax, y, az, water(ax, az)); vertex(m, bx, y, bz, water(bx, bz)); vertex(m, bx, y, az, water(bx, az));
    }
  }
  // Out to the horizon: four opaque strips around the grid.
  const float r = far_terrain::kOceanReach;
  const float quads[4][4] = {{-r, -r, r, z0}, {-r, ez, r, r}, {-r, z0, x0, ez}, {ex, z0, r, ez}};
  for (const auto& q : quads) {
    vertex(m, q[0], y, q[1], deep); vertex(m, q[0], y, q[3], deep); vertex(m, q[2], y, q[3], deep);
    vertex(m, q[0], y, q[1], deep); vertex(m, q[2], y, q[3], deep); vertex(m, q[2], y, q[1], deep);
  }
  // The lake and the marsh: flat water at their own level.
  constexpr int kSegments = 120;
  constexpr float kTau = 6.28318530718f;
  for (const Pond& p : continent_ponds()) {
    const Color mid{0.20f, 0.42f, 0.50f, 0.88f}, shore{0.30f, 0.52f, 0.55f, 0.6f};
    for (int s = 0; s < kSegments; ++s) {
      const float a0 = kTau * static_cast<float>(s) / kSegments, a1 = kTau * static_cast<float>(s + 1) / kSegments;
      const float s0 = 1.02f * pond_outline(p, a0), s1 = 1.02f * pond_outline(p, a1);  // its wavy shore
      const float px0 = p.center.x + std::cos(a0) * p.radius_x * s0, pz0 = p.center.z + std::sin(a0) * p.radius_z * s0;
      const float px1 = p.center.x + std::cos(a1) * p.radius_x * s1, pz1 = p.center.z + std::sin(a1) * p.radius_z * s1;
      vertex(m, p.center.x, p.level, p.center.z, mid); vertex(m, px1, p.level, pz1, shore); vertex(m, px0, p.level, pz0, shore);
    }
  }
  // The rivers (rivers.hpp), seen from afar: their water a little wider and
  // above the far ground (coarser than theirs), every few meters.
  if (ground && !ground->empty()) {
    const Color river{0.22f, 0.46f, 0.58f, 0.85f};
    for (const River& rv : ground->rivers) {
      const size_t n = rv.points.size();
      if (n < 2 || rv.level.size() != n) continue;
      constexpr size_t kEvery = 5;
      const auto lift = [&](size_t i) {
        const Vec2 p = rv.points[i];
        return std::max(rv.level[i], far_point(k, ground, 20.0f, p.x, p.z) + 0.6f);
      };
      for (size_t i = 0; i + kEvery < n; i += kEvery) {
        const size_t e = std::min(i + kEvery, n - 1);
        const Vec2 a = rv.points[i], b = rv.points[e];
        const float dx = b.x - a.x, dz = b.z - a.z, len = std::max(1e-3f, std::sqrt(dx * dx + dz * dz));
        const float wa = std::max(3.0f, rv.width[i] + 1.0f), wb = std::max(3.0f, rv.width[e] + 1.0f);
        const float ox = -dz / len, oz = dx / len;
        const float ya = lift(i), yb = lift(e);
        vertex(m, a.x - ox * wa, ya, a.z - oz * wa, river); vertex(m, a.x + ox * wa, ya, a.z + oz * wa, river); vertex(m, b.x + ox * wb, yb, b.z + oz * wb, river);
        vertex(m, a.x - ox * wa, ya, a.z - oz * wa, river); vertex(m, b.x + ox * wb, yb, b.z + oz * wb, river); vertex(m, b.x - ox * wb, yb, b.z - oz * wb, river);
      }
    }
  }
  face_up(m);
  split_into_parts(m, 0, 0, kSectors, kBands, kBig);  // culling
  return m;
}

MeshData build_lava_mesh(const Continent& k) {
  MeshData m;
  if (k.empty()) return m;
  // A glowing disc at the bottom of the crater (drawn as glowing: bright by day and by night).
  const Crater c = volcano_crater();
  constexpr int kSegments = 24;
  constexpr float kTau = 6.28318530718f;
  const Color hot{2.4f, 0.9f, 0.25f, 1}, rim{1.6f, 0.35f, 0.1f, 1};
  for (int s = 0; s < kSegments; ++s) {
    const float a0 = kTau * static_cast<float>(s) / kSegments, a1 = kTau * static_cast<float>(s + 1) / kSegments;
    const float r = c.radius * 0.8f;
    vertex(m, c.center.x, c.lava_level, c.center.z, hot);
    vertex(m, c.center.x + std::cos(a1) * r, c.lava_level, c.center.z + std::sin(a1) * r, rim);
    vertex(m, c.center.x + std::cos(a0) * r, c.lava_level, c.center.z + std::sin(a0) * r, rim);
  }
  face_up(m);
  return m;
}

}  // namespace monstres
