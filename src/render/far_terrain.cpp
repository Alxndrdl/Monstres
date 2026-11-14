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

float ellipse(Vec2 c, float rx, float rz, float x, float z) {  // 1 in the middle, 0 at the edge
  const float dx = (x - c.x) / rx, dz = (z - c.z) / rz;
  return 1 - std::sqrt(dx * dx + dz * dz);
}

void vertex(MeshData& m, float x, float y, float z, Color c, Vec3 n = {0, 1, 0}) {
  m.vertices.insert(m.vertices.end(), {x, y, z, n.x, n.y, n.z, c.r, c.g, c.b, c.a});
}

// The ground's color: by region, height and steepness.
Color far_color(const Continent& k, float x, float z, float h, Vec3 n) {
  const uint64_t seed = k.seed;
  const float steep = 1 - n.y;
  Color c = lerp(kGrass, kGrassDry, value_noise(seed + 600, x, z, 140.0f) * 0.7f);
  const auto& marks = continent_landmarks();
  for (const Landmark& m : marks) {
    switch (m.region) {
      case Region::Forest: c = lerp(c, kForest, smooth(0.0f, 0.25f, ellipse(m.at, m.radius * 1.3f, m.radius * 0.95f, x, z))); break;
      case Region::Marsh: c = lerp(c, kMarsh, smooth(0.0f, 0.3f, ellipse(m.at, m.radius * 1.35f, m.radius * 0.9f, x, z))); break;
      case Region::Capital: c = lerp(c, kPaving, smooth(0.0f, 0.3f, ellipse(m.at, m.radius, m.radius, x, z)) * 0.8f); break;
      default: break;
    }
  }
  // The desert in the south, its mesas reddish.
  c = lerp(c, lerp(kDesert, kMesa, smooth(0.15f, 0.4f, steep) + smooth(30.0f, 45.0f, h) * 0.6f), smooth(1000.0f, 1250.0f, z));
  // Steep slopes: rock; high up: snow (not on the steepest faces).
  c = lerp(c, kRock, smooth(0.18f, 0.4f, steep) + smooth(110.0f, 200.0f, h) * 0.8f);
  const float snow_line = rules::kSnowLine + (value_noise(seed + 601, x, z, 60.0f) - 0.5f) * 50.0f;
  c = lerp(c, kSnow, smooth(snow_line, snow_line + 40.0f, h) * (1 - smooth(0.45f, 0.65f, steep)));
  // The volcanic peninsula: black rock and ash, glowing around the crater.
  const Crater crater = volcano_crater();
  const float to_crater = std::sqrt((x - crater.center.x) * (x - crater.center.x) + (z - crater.center.z) * (z - crater.center.z));
  c = lerp(c, kBasalt, smooth(2650.0f, 2950.0f, x) * 0.85f + smooth(700.0f, 450.0f, to_crater) * 0.15f);
  c = lerp(c, kEmber, smooth(crater.radius * 1.4f, crater.radius * 0.9f, to_crater));
  // Beaches, then the sea floor.
  // (Not around the playable area: its ground comes down to 0 there, just above the sea.)
  const float from_start = std::max(std::fabs(x), std::fabs(z));
  c = lerp(c, kSand, smooth(rules::kSeaLevel + 3.0f, rules::kSeaLevel + 0.5f, h) * (1 - smooth(1000.0f, 1250.0f, z)) *
                         smooth(330.0f, 480.0f, from_start));
  for (const Pond& p : continent_ponds()) c = lerp(c, kSeaFloor, smooth(-0.05f, 0.15f, ellipse(p.center, p.radius_x, p.radius_z, x, z)) * smooth(p.level + 0.5f, p.level - 1.5f, h));
  c = lerp(c, kSeaFloor, smooth(rules::kSeaLevel, rules::kSeaLevel - 3.0f, h));
  return c;
}

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

float far_vertex_height(const Continent& k, int level, float x, float z) {
  // On the outer edge of a ring (but the last): between two vertices of the
  // coarser ring outside, follow the straight line between them.
  if (level < kLevels - 1) {
    const float e = kOuter[level], coarse = kCells[level + 1], fine = kCells[level];
    if (std::fabs(std::fabs(x) - e) < 1e-3f && !on_grid(z, coarse)) {
      return 0.5f * (k.at_point(x, z - fine) + k.at_point(x, z + fine));
    }
    if (std::fabs(std::fabs(z) - e) < 1e-3f && !on_grid(x, coarse)) {
      return 0.5f * (k.at_point(x - fine, z) + k.at_point(x + fine, z));
    }
  }
  return k.at_point(x, z);
}

MeshData build_far_ground_mesh(const Continent& k) {
  MeshData m;
  if (k.empty()) return m;
  struct Point {
    float y;
    Color c;
    Vec3 n;
  };
  std::vector<Point> grid;
  std::vector<int64_t> ids;  // vertex of each grid point, -1 until used (indexed: each point once)
  for (int level = 0; level < kLevels; ++level) {
    const float cell = kCells[level], outer = kOuter[level];
    const float inner = level == 0 ? rules::kReliefHalfSize : kOuter[level - 1];
    const int n = static_cast<int>(std::lround(2 * outer / cell));
    const int points = n + 1;
    grid.assign(static_cast<size_t>(points) * points, Point{});
    ids.assign(grid.size(), -1);
    const auto coord = [&](int i) { return -outer + cell * static_cast<float>(i); };
    for (int i = 0; i < points; ++i) {
      for (int j = 0; j < points; ++j) {
        const float x = coord(i), z = coord(j);
        if (std::max(std::fabs(x), std::fabs(z)) < inner - 1e-3f) continue;  // inside: not used
        const float y = far_vertex_height(k, level, x, z);
        const Vec3 nrm = far_normal(k, x, z, cell);
        grid[static_cast<size_t>(i * points + j)] = {y, far_color(k, x, z, y, nrm), nrm};
      }
    }
    for (int i = 0; i < n; ++i) {
      for (int j = 0; j < n; ++j) {
        const float x0 = coord(i), z0 = coord(j), x1 = x0 + cell, z1 = z0 + cell;
        // Inside the ring's inner square: drawn by a finer ring (or the World's ground).
        const float cx = 0.5f * (x0 + x1), cz = 0.5f * (z0 + z1);
        if (std::max(std::fabs(cx), std::fabs(cz)) < inner) continue;
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
            vertex(m, coord(i + a), p.y, coord(j + b), p.c, p.n);
          }
          return static_cast<uint32_t>(ids[g]);
        };
        m.indices.insert(m.indices.end(), {id(0, 0), id(0, 1), id(1, 1), id(0, 0), id(1, 1), id(1, 0)});
      }
    }
  }
  // Along its inner edge (the relief's), a band going down, facing the
  // playable area: if the near ground there is a little lower, no gap opens.
  const float e = rules::kReliefHalfSize, cell = kCells[0];
  const int n = static_cast<int>(std::lround(2 * e / cell));
  for (int side = 0; side < 4; ++side) {
    for (int i = 0; i < n; ++i) {
      const float t0 = -e + cell * static_cast<float>(i), t1 = t0 + cell;
      const float ax = side == 0 ? e : side == 1 ? -e : t0, az = side == 0 || side == 1 ? t0 : side == 2 ? e : -e;
      const float bx = side == 0 ? e : side == 1 ? -e : t1, bz = side == 0 || side == 1 ? t1 : side == 2 ? e : -e;
      const float ay = far_vertex_height(k, 0, ax, az), by = far_vertex_height(k, 0, bx, bz);
      const float drop = far_terrain::kSkirt;
      const Vec3 inward{side == 0 ? -1.0f : side == 1 ? 1.0f : 0.0f, 0, side == 2 ? -1.0f : side == 3 ? 1.0f : 0.0f};
      const Color ca = far_color(k, ax, az, ay, {0, 1, 0}), cb = far_color(k, bx, bz, by, {0, 1, 0});
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
  split_into_parts(m, 0, 0, kSectors, kBands, kBig);  // culling
  return m;
}

MeshData build_far_water_mesh(const Continent& k) {
  MeshData m;
  m.transparent = true;
  if (k.empty()) return m;
  // The ocean: a grid over the continent (clear in the shallows, opaque
  // where deep), then wide opaque strips out to the horizon.
  const Color deep{0.10f, 0.26f, 0.40f, 1.0f};
  const auto water = [&](float x, float z) {
    const float depth = rules::kSeaLevel - k.height(x, z);
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
      if (k.height(ax, az) > high && k.height(bx, az) > high && k.height(ax, bz) > high && k.height(bx, bz) > high &&
          k.height(0.5f * (ax + bx), 0.5f * (az + bz)) > high) {
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
  constexpr int kSegments = 40;
  constexpr float kTau = 6.28318530718f;
  for (const Pond& p : continent_ponds()) {
    const Color mid{0.20f, 0.42f, 0.50f, 0.88f}, shore{0.30f, 0.52f, 0.55f, 0.6f};
    for (int s = 0; s < kSegments; ++s) {
      const float a0 = kTau * static_cast<float>(s) / kSegments, a1 = kTau * static_cast<float>(s + 1) / kSegments;
      const float px0 = p.center.x + std::cos(a0) * p.radius_x * 1.02f, pz0 = p.center.z + std::sin(a0) * p.radius_z * 1.02f;
      const float px1 = p.center.x + std::cos(a1) * p.radius_x * 1.02f, pz1 = p.center.z + std::sin(a1) * p.radius_z * 1.02f;
      vertex(m, p.center.x, p.level, p.center.z, mid); vertex(m, px1, p.level, pz1, shore); vertex(m, px0, p.level, pz0, shore);
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
