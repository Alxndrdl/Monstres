#include "monstres/render/water_mesh.hpp"

#include "monstres/core/heightmap.hpp"
#include "monstres/core/rivers.hpp"
#include "monstres/core/water.hpp"
#include "monstres/core/west.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace monstres {

namespace {

using water_tiles::kTile;

float tile_x0(int i) { return Heightmap::kMinX + kTile * static_cast<float>(i); }
float tile_z0(int j) { return Heightmap::kMinZ + kTile * static_cast<float>(j); }

// Whether a tile is the relief's own (inside its rectangle: no far terrain under it).
bool in_relief(int i, int j) {
  const int nx = static_cast<int>(2 * rules::kReliefHalfSize / kTile), nz = static_cast<int>((rules::kReliefHalfSize + rules::kReliefNorth) / kTile);
  return i >= 0 && j >= 0 && i < nx && j < nz;
}

float smooth(float e0, float e1, float x) {
  const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
  return t * t * (3 - 2 * t);
}

double now_ms() {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

constexpr float kDry = -std::numeric_limits<float>::infinity();

}  // namespace

MeshData build_water_tile(const World& w, int i, int j, float cell) {
  MeshData m;
  m.transparent = true;
  m.double_sided = true;  // seen from under it too (water.hpp: diving)
  if (w.ground.empty()) return m;
  const int n = static_cast<int>(kTile / cell);
  const int s = n + 3;  // a point more on each side (the shore's points take their neighbours' level)
  const float x0 = tile_x0(i) - cell, z0 = tile_z0(j) - cell;
  std::vector<float> level(static_cast<size_t>(s * s), kDry), ground(static_cast<size_t>(s * s), 0.0f);
  std::vector<WaterKind> kind(static_cast<size_t>(s * s), WaterKind::None);
  const auto at = [&](int a, int b) { return static_cast<size_t>(a * s + b); };
  bool any = false;
  for (int a = 0; a < s; ++a) {
    for (int b = 0; b < s; ++b) {
      WaterSpot ws;
      const float x = x0 + cell * static_cast<float>(a), z = z0 + cell * static_cast<float>(b);
      if (water_at(w, x, z, ws)) {
        level[at(a, b)] = ws.level;
        ground[at(a, b)] = ws.ground;
        kind[at(a, b)] = ws.kind;
        any = true;
      } else {
        ground[at(a, b)] = w.ground.height(x, z);
      }
    }
  }
  if (!any) return m;
  // A dry point by the water: at the highest level around it (drawn past
  // the shore, where the ground rises over it and hides it).
  std::vector<float> drawn = level;
  std::vector<WaterKind> drawn_kind = kind;
  for (int a = 1; a + 1 < s; ++a) {
    for (int b = 1; b + 1 < s; ++b) {
      if (level[at(a, b)] != kDry) continue;
      float best = kDry;
      for (int da = -1; da <= 1; ++da) {
        for (int db = -1; db <= 1; ++db) {
          if (level[at(a + da, b + db)] > best) {
            best = level[at(a + da, b + db)];
            drawn_kind[at(a, b)] = kind[at(a + da, b + db)];
          }
        }
      }
      drawn[at(a, b)] = best;
    }
  }
  // A river's water ends along its smooth course (fading over its last
  // meter), not along the ground's grid where its banks rise (in steps
  // across it, going aslant): the stream's and the continent's rivers'.
  const auto river_edge = [&](float x, float z, float g) {
    float best = 1e9f, half = 0;
    if (w.west.valid()) {
      float lv = 0, width = 0;
      const float d = river_surface(w.west, x, z, g, 16.0f, lv, width);
      if (d - width < best - half) best = d, half = width;
    }
    if (RiverSpot r; !w.ground.rivers.empty() && nearest_river(w.ground.rivers, x, z, 14.0f, r) && r.distance - r.width < best - half) {
      best = r.distance;
      half = r.width;
    }
    return best > 1e8f ? 1.0f : smooth(half + 0.9f, half - 0.6f, best);
  };
  // The tile's own points (1..n+1), their colors: by the depth, white where it falls.
  const Color foam{0.86f, 0.92f, 0.96f, 0.9f};
  std::vector<uint32_t> index(static_cast<size_t>(s * s), UINT32_MAX);
  Aabb box = empty_aabb();
  const auto vertex = [&](int a, int b) {
    uint32_t& id = index[at(a, b)];
    if (id != UINT32_MAX) return id;
    const float x = x0 + cell * static_cast<float>(a), z = z0 + cell * static_cast<float>(b), y = drawn[at(a, b)];
    const float depth = y - ground[at(a, b)];
    const float d = smooth(0.0f, 20.0f, depth);
    Color c{0.18f - 0.08f * d, 0.44f - 0.18f * d, 0.56f - 0.16f * d, 0.58f + 0.42f * smooth(0.0f, 10.0f, depth)};
    // Falling (a step down to a neighbour): white.
    float drop = 0;
    for (int da = -1; da <= 1; ++da) {
      for (int db = -1; db <= 1; ++db) {
        const float o = drawn[at(std::clamp(a + da, 0, s - 1), std::clamp(b + db, 0, s - 1))];
        if (o != kDry) drop = std::max(drop, std::fabs(y - o));
      }
    }
    const float white = smooth(0.5f * cell, 1.0f * cell, drop);
    c = {c.r + (foam.r - c.r) * white, c.g + (foam.g - c.g) * white, c.b + (foam.b - c.b) * white, c.a + (foam.a - c.a) * white};
    if (drawn_kind[at(a, b)] == WaterKind::River) c.a *= river_edge(x, z, ground[at(a, b)]);
    id = static_cast<uint32_t>(m.vertex_count());
    m.vertices.insert(m.vertices.end(), {x, y, z, 0.0f, 1.0f, 0.0f, c.r, c.g, c.b, c.a});
    box.add(x, y, z);
    return id;
  };
  for (int a = 1; a <= n; ++a) {
    for (int b = 1; b <= n; ++b) {
      // A cell with some water in it (a corner under water), all its corners known.
      const bool wet = level[at(a, b)] != kDry || level[at(a + 1, b)] != kDry || level[at(a, b + 1)] != kDry ||
                       level[at(a + 1, b + 1)] != kDry;
      if (!wet || drawn[at(a, b)] == kDry || drawn[at(a + 1, b)] == kDry || drawn[at(a, b + 1)] == kDry ||
          drawn[at(a + 1, b + 1)] == kDry) {
        continue;
      }
      const uint32_t p00 = vertex(a, b), p01 = vertex(a, b + 1), p11 = vertex(a + 1, b + 1), p10 = vertex(a + 1, b);
      m.indices.insert(m.indices.end(), {p00, p01, p11, p00, p11, p10});
    }
  }
  if (!m.indices.empty()) m.parts.push_back({{0, static_cast<uint32_t>(m.indices.size())}, box});
  return m;
}

namespace {

// Tiles appended into one mesh, a part per tile.
void append(MeshData& to, const MeshData& t) {
  if (t.indices.empty()) return;
  const auto base = static_cast<uint32_t>(to.vertex_count());
  const auto first = static_cast<uint32_t>(to.indices.size());
  to.vertices.insert(to.vertices.end(), t.vertices.begin(), t.vertices.end());
  for (const uint32_t v : t.indices) to.indices.push_back(base + v);
  to.parts.push_back({{first, static_cast<uint32_t>(t.indices.size())}, t.parts.front().bounds});
}

constexpr int kReliefTilesX = static_cast<int>(2 * rules::kReliefHalfSize / kTile);
constexpr int kReliefTilesZ = static_cast<int>((rules::kReliefHalfSize + rules::kReliefNorth) / kTile);

}  // namespace

MeshData build_water_mesh(const World& w) {
  MeshData m;
  m.transparent = true;
  m.double_sided = true;
  for (int i = 0; i < kReliefTilesX; ++i) {
    for (int j = 0; j < kReliefTilesZ; ++j) append(m, build_water_tile(w, i, j, water_tiles::kCoarse));
  }
  return m;
}

void WaterTiles::reset() {
  fine_.clear();
  coarse_.clear();
  drawn_.clear();
  mesh_ = {};
}

bool WaterTiles::update(const World& w, Vec2 player, double budget_ms) {
  ++frame_;
  // The fine tiles around the player (nearest first), the relief's others coarse.
  const int ci = static_cast<int>(std::floor((player.x - Heightmap::kMinX) / kTile));
  const int cj = static_cast<int>(std::floor((player.z - Heightmap::kMinZ) / kTile));
  const int r = static_cast<int>(std::ceil(water_tiles::kReach / kTile)) + 1;
  std::vector<std::pair<float, Drawn>> near;
  for (int i = ci - r; i <= ci + r; ++i) {
    for (int j = cj - r; j <= cj + r; ++j) {
      const float dx = tile_x0(i) + kTile / 2 - player.x, dz = tile_z0(j) + kTile / 2 - player.z;
      const float d2 = dx * dx + dz * dz;
      if (d2 < water_tiles::kReach * water_tiles::kReach) near.push_back({d2, {i, j, true}});
    }
  }
  std::sort(near.begin(), near.end(), [](const auto& a, const auto& b) {
    return a.first != b.first ? a.first < b.first : (a.second.i != b.second.i ? a.second.i < b.second.i : a.second.j < b.second.j);
  });
  std::vector<Drawn> wanted;
  for (const auto& [d, t] : near) wanted.push_back(t);
  for (int i = 0; i < kReliefTilesX; ++i) {
    for (int j = 0; j < kReliefTilesZ; ++j) {
      const bool fine = std::any_of(near.begin(), near.end(), [&](const auto& t) { return t.second.i == i && t.second.j == j; });
      if (!fine) wanted.push_back({i, j, false});
    }
  }
  const double start = now_ms();
  bool missing = false;
  for (const Drawn& t : wanted) {
    std::vector<Built>& store = t.fine ? fine_ : coarse_;
    const auto it = std::find_if(store.begin(), store.end(), [&](const Built& b) { return b.i == t.i && b.j == t.j; });
    if (it != store.end()) {
      it->used = frame_;
      continue;
    }
    if (now_ms() - start > budget_ms) {
      missing = true;
      continue;
    }
    if (t.fine && fine_.size() >= water_tiles::kKept) {  // the longest unused one goes
      auto oldest = std::min_element(fine_.begin(), fine_.end(), [](const Built& a, const Built& b) { return a.used < b.used; });
      fine_.erase(oldest);
    }
    store.push_back({t.i, t.j, build_water_tile(w, t.i, t.j, t.fine ? water_tiles::kFine : water_tiles::kCoarse), frame_});
  }
  // What can be drawn: a fine tile when built, else its coarse one (the relief's).
  std::vector<Drawn> now;
  for (const Drawn& t : wanted) {
    const auto has = [&](const std::vector<Built>& store) {
      return std::any_of(store.begin(), store.end(), [&](const Built& b) { return b.i == t.i && b.j == t.j; });
    };
    if (has(t.fine ? fine_ : coarse_)) now.push_back(t);
    else if (t.fine && in_relief(t.i, t.j) && has(coarse_)) now.push_back({t.i, t.j, false});
  }
  // Drawn once all are there (meanwhile the ones before stay), or when none were.
  if (missing && !drawn_.empty()) return false;
  if (now == drawn_) return false;
  drawn_ = now;
  mesh_ = {};
  mesh_.transparent = true;
  mesh_.double_sided = true;
  for (const Drawn& t : drawn_) {
    const std::vector<Built>& store = t.fine ? fine_ : coarse_;
    const auto it = std::find_if(store.begin(), store.end(), [&](const Built& b) { return b.i == t.i && b.j == t.j; });
    append(mesh_, it->mesh);
  }
  return true;
}

}  // namespace monstres
