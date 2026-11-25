#include "monstres/render/outer_mesh.hpp"

#include "monstres/core/heightmap.hpp"
#include "monstres/render/far_terrain.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace monstres {

namespace {

using near_outer::kCell;
using near_outer::kTile;

float tile_x0(int i) { return Heightmap::kMinX + kTile * static_cast<float>(i); }
float tile_z0(int j) { return Heightmap::kMinZ + kTile * static_cast<float>(j); }

// Whether a tile is the relief's own (inside its rectangle).
bool in_relief(int i, int j) {
  const int nx = static_cast<int>(2 * rules::kReliefHalfSize / kTile), nz = static_cast<int>((rules::kReliefHalfSize + rules::kReliefNorth) / kTile);
  return i >= 0 && j >= 0 && i < nx && j < nz;
}

void vertex(MeshData& m, float x, float y, float z, Vec3 n, Color c) {
  m.vertices.insert(m.vertices.end(), {x, y, z, n.x, n.y, n.z, c.r, c.g, c.b, c.a});
}

Color lerp(Color a, Color b, float t) {
  return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

double now_ms() {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

}  // namespace

MeshData build_outer_tile(const World& w, const Continent& k, int i, int j) {
  MeshData m;
  const Heightmap& g = w.ground;
  if (g.empty() || in_relief(i, j)) return m;
  constexpr int n = static_cast<int>(kTile / kCell);
  const float x0 = tile_x0(i), z0 = tile_z0(j);
  Aabb box = empty_aabb();
  for (int a = 0; a <= n; ++a) {
    for (int b = 0; b <= n; ++b) {
      const float x = x0 + kCell * static_cast<float>(a), z = z0 + kCell * static_cast<float>(b);
      const float y = g.height(x, z);
      const Vec3 nrm = g.normal(x, z);
      vertex(m, x, y, z, nrm, far_color(k, x, z, y, nrm, &g));
      box.add(x, y, z);
    }
  }
  for (int a = 0; a < n; ++a) {
    for (int b = 0; b < n; ++b) {
      const auto id = [&](int u, int v) { return static_cast<uint32_t>((a + u) * (n + 1) + b + v); };
      // (As the relief's grid splits its cells: along the diagonal from (x0, z0) to (x1, z1).)
      m.indices.insert(m.indices.end(), {id(0, 0), id(0, 1), id(1, 1), id(0, 0), id(1, 1), id(1, 0)});
    }
  }
  // Along the relief's edge: a band going down (its coarser ground there may be a little lower).
  for (int side = 0; side < 4; ++side) {
    const int ni = side == 0 ? i - 1 : side == 1 ? i + 1 : i, nj = side == 2 ? j - 1 : side == 3 ? j + 1 : j;
    if (!in_relief(ni, nj)) continue;
    for (int t = 0; t < n; ++t) {
      const int a0 = side == 0 ? 0 : side == 1 ? n : t, b0 = side < 2 ? t : side == 2 ? 0 : n;
      const int a1 = side < 2 ? a0 : t + 1, b1 = side < 2 ? t + 1 : b0;
      const float ax = x0 + kCell * a0, az = z0 + kCell * b0, bx = x0 + kCell * a1, bz = z0 + kCell * b1;
      const float ay = g.height(ax, az), by = g.height(bx, bz);
      const Vec3 out{side == 0 ? -1.0f : side == 1 ? 1.0f : 0.0f, 0, side == 2 ? -1.0f : side == 3 ? 1.0f : 0.0f};
      const Color ca = far_color(k, ax, az, ay, {0, 1, 0}, &g), cb = far_color(k, bx, bz, by, {0, 1, 0}, &g);
      const uint32_t base = static_cast<uint32_t>(m.vertex_count());
      vertex(m, ax, ay, az, out, ca);
      vertex(m, ax, ay - near_outer::kSkirt, az, out, ca);
      vertex(m, bx, by - near_outer::kSkirt, bz, out, cb);
      vertex(m, bx, by, bz, out, cb);
      // Both faces (the relief's side and the tile's): the band is thin.
      m.indices.insert(m.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3, base, base + 2, base + 1, base, base + 3, base + 2});
      box.add(ax, ay - near_outer::kSkirt, az);
    }
  }
  m.parts.push_back({{0, static_cast<uint32_t>(m.indices.size())}, box});
  return m;
}

MeshData build_outer_water(const World& w, int i, int j) {
  MeshData m;
  m.transparent = true;
  const Heightmap& g = w.ground;
  if (g.empty() || in_relief(i, j)) return m;
  const float x0 = tile_x0(i), z0 = tile_z0(j), x1 = x0 + kTile, z1 = z0 + kTile;
  Aabb box = empty_aabb();
  const Color clear{0.18f, 0.44f, 0.56f, 0.72f}, foam{0.86f, 0.92f, 0.96f, 0.88f};
  for (const River& r : g.rivers) {
    const size_t n = r.points.size();
    if (n < 2 || r.level.size() != n) continue;
    const auto across = [&](size_t k) {
      const Vec2 a = r.points[k > 0 ? k - 1 : 0], b = r.points[std::min(k + 1, n - 1)];
      const float dx = b.x - a.x, dz = b.z - a.z, len = std::max(1e-4f, std::sqrt(dx * dx + dz * dz));
      return Vec2{-dz / len, dx / len};
    };
    // How white the water is at a point: by its waterfalls, falling, and churned just below.
    std::vector<float> white(n, 0.0f);
    for (size_t k = 0; k + 1 < n; ++k) {
      if (r.level[k] - r.level[k + 1] < 0.8f) continue;  // (falling there)
      for (size_t q = k; q < std::min(n, k + 5); ++q) white[q] = std::max(white[q], q <= k + 1 ? 1.0f : 0.6f - 0.15f * static_cast<float>(q - k - 2));
    }
    for (size_t c = 0; c < r.boxes.size(); ++c) {
      const auto& b = r.boxes[c];
      if (b[2] < x0 - 12 || b[0] > x1 + 12 || b[3] < z0 - 12 || b[1] > z1 + 12) continue;
      const size_t first = c * rules::kRiverChunk, last = std::min(first + rules::kRiverChunk, n - 1);
      for (size_t k = first; k < last; ++k) {
        const Vec2 p = r.points[k];
        // (Each segment in one tile: where it starts; over the relief's edge, the last one's beyond it.)
        Vec2 owner = p;
        for (size_t q = k; q > 0 && rules::relief_inset(owner.x, owner.z) > 0; --q) owner = r.points[q - 1];
        if (owner.x < x0 || owner.x >= x1 || owner.z < z0 || owner.z >= z1) continue;
        const Vec2 q = r.points[k + 1];
        const Vec2 na = across(k), nb = across(k + 1);
        const float wa = r.width[k] + 0.8f, wb = r.width[k + 1] + 0.8f;
        const float ya = r.level[k] + 0.02f, yb = r.level[k + 1] + 0.02f;
        // (Falling: the sheet faces out along the river.)
        const float drop = ya - yb;
        Vec3 up{0, 1, 0};
        if (drop > 0.3f) {
          const float dx = q.x - p.x, dz = q.z - p.z, run = std::max(1e-3f, std::sqrt(dx * dx + dz * dz));
          const float l = std::sqrt(drop * drop + run * run);
          up = {dx / run * drop / l, run / l, dz / run * drop / l};
        }
        const auto color = [&](size_t at) { return lerp(clear, foam, white[at]); };
        const auto base = static_cast<uint32_t>(m.vertex_count());
        vertex(m, p.x - na.x * wa, ya, p.z - na.z * wa, up, color(k));
        vertex(m, p.x + na.x * wa, ya, p.z + na.z * wa, up, color(k));
        vertex(m, q.x + nb.x * wb, yb, q.z + nb.z * wb, up, color(k + 1));
        vertex(m, q.x - nb.x * wb, yb, q.z - nb.z * wb, up, color(k + 1));
        // Both faces (whichever way the segment turns).
        m.indices.insert(m.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3, base, base + 2, base + 1, base, base + 3, base + 2});
        box.add(p.x - wa, ya, p.z - wa);
        box.add(p.x + wa, ya, p.z + wa);
        box.add(q.x, yb, q.z);
      }
    }
  }
  if (!m.indices.empty()) m.parts.push_back({{0, static_cast<uint32_t>(m.indices.size())}, box});
  return m;
}

std::vector<std::pair<int, int>> outer_tiles_around(float x, float z) {
  std::vector<std::pair<int, int>> out;
  const int ci = static_cast<int>(std::floor((x - Heightmap::kMinX) / kTile)), cj = static_cast<int>(std::floor((z - Heightmap::kMinZ) / kTile));
  const int r = static_cast<int>(std::ceil(near_outer::kReach / kTile)) + 1;
  for (int i = ci - r; i <= ci + r; ++i) {
    for (int j = cj - r; j <= cj + r; ++j) {
      if (in_relief(i, j)) continue;
      const float cx = tile_x0(i) + kTile / 2 - x, cz = tile_z0(j) + kTile / 2 - z;
      if (cx * cx + cz * cz < near_outer::kReach * near_outer::kReach) out.push_back({i, j});
    }
  }
  // Nearest first.
  std::sort(out.begin(), out.end(), [&](const auto& a, const auto& b) {
    const float ax = tile_x0(a.first) + kTile / 2 - x, az = tile_z0(a.second) + kTile / 2 - z;
    const float bx = tile_x0(b.first) + kTile / 2 - x, bz = tile_z0(b.second) + kTile / 2 - z;
    const float da = ax * ax + az * az, db = bx * bx + bz * bz;
    return da != db ? da < db : a < b;
  });
  return out;
}

void OuterGround::reset() {
  cache_.clear();
  drawn_.clear();
  mesh_ = {};
  water_ = {};
}

bool OuterGround::update(const World& w, const Continent& k, Vec2 player, double budget_ms) {
  ++frame_;
  const std::vector<std::pair<int, int>> wanted = outer_tiles_around(player.x, player.z);
  const double start = now_ms();
  bool missing = false;
  for (const auto& [i, j] : wanted) {
    const auto it = std::find_if(cache_.begin(), cache_.end(), [&](const Built& b) { return b.i == i && b.j == j; });
    if (it != cache_.end()) {
      it->used = frame_;
      continue;
    }
    if (now_ms() - start > budget_ms) {
      missing = true;
      continue;
    }
    if (cache_.size() >= near_outer::kKept) {  // the longest unused one goes
      auto oldest = std::min_element(cache_.begin(), cache_.end(), [](const Built& a, const Built& b) { return a.used < b.used; });
      cache_.erase(oldest);
    }
    cache_.push_back({i, j, build_outer_tile(w, k, i, j), build_outer_water(w, i, j), frame_});
  }
  // Drawn once all are there (meanwhile the ones before stay), or when none were.
  if (missing && !drawn_.empty()) return false;
  std::vector<std::pair<int, int>> now;
  for (const auto& t : wanted) {
    if (std::any_of(cache_.begin(), cache_.end(), [&](const Built& b) { return b.i == t.first && b.j == t.second; })) now.push_back(t);
  }
  if (now == drawn_) return false;
  drawn_ = now;
  mesh_ = {};
  water_ = {};
  water_.transparent = true;
  for (const auto& [i, j] : drawn_) {
    const auto it = std::find_if(cache_.begin(), cache_.end(), [&](const Built& b) { return b.i == i && b.j == j; });
    if (const MeshData& wt = it->water; !wt.indices.empty()) {
      const auto wbase = static_cast<uint32_t>(water_.vertex_count());
      const auto wfirst = static_cast<uint32_t>(water_.indices.size());
      water_.vertices.insert(water_.vertices.end(), wt.vertices.begin(), wt.vertices.end());
      for (const uint32_t v : wt.indices) water_.indices.push_back(wbase + v);
      water_.parts.push_back({{wfirst, static_cast<uint32_t>(wt.indices.size())}, wt.parts.front().bounds});
    }
    const MeshData& t = it->mesh;
    if (t.indices.empty()) continue;
    const auto base = static_cast<uint32_t>(mesh_.vertex_count());
    const auto first = static_cast<uint32_t>(mesh_.indices.size());
    mesh_.vertices.insert(mesh_.vertices.end(), t.vertices.begin(), t.vertices.end());
    for (const uint32_t v : t.indices) mesh_.indices.push_back(base + v);
    mesh_.parts.push_back({{first, static_cast<uint32_t>(t.indices.size())}, t.parts.front().bounds});
  }
  return true;
}

void OuterGround::add(DrawList& list) const {
  const size_t slot = near_outer::kOuterMesh, water = near_outer::kOuterWaterMesh;
  if (list.batches.size() <= water) {
    list.batches.resize(water + 1);
    list.parts.resize(water + 1);
    list.mesh_parts.resize(water + 1);
  }
  list.batches[slot].clear();
  list.mesh_parts[slot].clear();
  list.batches[water].clear();
  list.mesh_parts[water].clear();
  ++list.version;
  if (!mesh_.parts.empty()) {
    Instance solid;
    solid.material = kSolid;
    list.batches[slot].push_back(solid);
    list.mesh_parts[slot] = mesh_.parts;
  }
  if (!water_.parts.empty()) {
    Instance river;
    river.material = kWater;  // the shader adds waves and reflections
    list.batches[water].push_back(river);
    list.mesh_parts[water] = water_.parts;
  }
}

}  // namespace monstres
