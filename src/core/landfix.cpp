#include "monstres/core/landfix.hpp"

#include "monstres/core/interior.hpp"
#include "monstres/core/north.hpp"
#include "monstres/core/roads.hpp"
#include "monstres/core/sites.hpp"
#include "monstres/core/west.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <queue>
#include <vector>

namespace monstres {

namespace {

float smooth(float e0, float e1, float x) {
  const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
  return t * t * (3 - 2 * t);
}

float segment_distance(Vec2 a, Vec2 b, Vec2 p, float* t_out = nullptr) {
  const float abx = b.x - a.x, abz = b.z - a.z, len2 = abx * abx + abz * abz;
  const float t = len2 > 0 ? std::clamp(((p.x - a.x) * abx + (p.z - a.z) * abz) / len2, 0.0f, 1.0f) : 0.0f;
  if (t_out) *t_out = t;
  return std::hypot(a.x + abx * t - p.x, a.z + abz * t - p.z);
}

float polyline_distance(const std::vector<Vec2>& pts, Vec2 p, float* along = nullptr) {
  float best = 1e30f, run = 0, best_along = 0;
  for (size_t i = 0; i + 1 < pts.size(); ++i) {
    float t = 0;
    const float d = segment_distance(pts[i], pts[i + 1], p, &t);
    const float len = std::hypot(pts[i + 1].x - pts[i].x, pts[i + 1].z - pts[i].z);
    if (d < best) {
      best = d;
      best_along = run + t * len;
    }
    run += len;
  }
  if (along) *along = best_along;
  return best;
}

// Over the relief's grid points around c (half `reach`), each once.
template <typename F>
void each_point(Heightmap& m, float x0, float z0, float x1, float z1, F f) {
  const int i0 = Heightmap::i_of(x0), i1 = std::min(Heightmap::i_of(x1) + 1, Heightmap::kPointsX - 1);
  const int j0 = Heightmap::j_of(z0), j1 = std::min(Heightmap::j_of(z1) + 1, Heightmap::kPointsZ - 1);
  for (int i = i0; i <= i1; ++i) {
    for (int j = j0; j <= j1; ++j) f(Heightmap::x_of(i), Heightmap::z_of(j), m.heights[Heightmap::index(i, j)]);
  }
}

// ---- The water left inland ----

// The sea as it is drawn (build_water_mesh): squares of 16 m from the
// relief's corner, wet where some of their ground is under the sea.
constexpr float kSquare = 16.0f;
constexpr int kSquaresX = static_cast<int>(2 * rules::kReliefHalfSize / kSquare);
constexpr int kSquaresZ = static_cast<int>((rules::kReliefHalfSize + rules::kReliefNorth) / kSquare);

bool wet_square(const Heightmap& m, int i, int j) {
  const float x0 = Heightmap::kMinX + kSquare * static_cast<float>(i), z0 = Heightmap::kMinZ + kSquare * static_cast<float>(j);
  for (float a = x0; a <= x0 + kSquare; a += 2) {
    for (float b = z0; b <= z0 + kSquare; b += 2) {
      if (m.height(a, b) < rules::kSeaLevel + 0.2f && m.sea(a, b)) return true;
    }
  }
  return false;
}

// The wet squares in pieces: -1 dry, 0 the open sea (it reaches past the
// world's edge), then each piece of water shut in by the land.
std::vector<int> water_pieces(const Heightmap& m, int& count) {
  std::vector<int> piece(static_cast<size_t>(kSquaresX * kSquaresZ), -2);
  const auto at = [&](int i, int j) -> int& { return piece[static_cast<size_t>(i * kSquaresZ + j)]; };
  for (int i = 0; i < kSquaresX; ++i) {
    for (int j = 0; j < kSquaresZ; ++j) at(i, j) = wet_square(m, i, j) ? -3 : -1;
  }
  const auto open = [&](int i, int j) {
    const float x = Heightmap::kMinX + kSquare * (static_cast<float>(i) + 0.5f);
    const float z = Heightmap::kMinZ + kSquare * (static_cast<float>(j) + 0.5f);
    return i == 0 || j == 0 || i == kSquaresX - 1 || j == kSquaresZ - 1 || !rules::in_world(x, z, -kSquare * 0.5f);
  };
  count = 1;
  std::vector<std::pair<int, int>> stack;
  std::vector<std::pair<int, int>> cells;
  for (int i = 0; i < kSquaresX; ++i) {
    for (int j = 0; j < kSquaresZ; ++j) {
      if (at(i, j) != -3) continue;
      cells.clear();
      stack = {{i, j}};
      at(i, j) = -4;
      bool reaches_out = false;
      while (!stack.empty()) {
        const auto [a, b] = stack.back();
        stack.pop_back();
        cells.push_back({a, b});
        reaches_out = reaches_out || open(a, b);
        const int da[] = {1, -1, 0, 0}, db[] = {0, 0, 1, -1};
        for (int k = 0; k < 4; ++k) {
          const int u = a + da[k], v = b + db[k];
          if (u < 0 || v < 0 || u >= kSquaresX || v >= kSquaresZ || at(u, v) != -3) continue;
          at(u, v) = -4;
          stack.push_back({u, v});
        }
      }
      const int id = reaches_out ? 0 : count++;
      for (const auto& [a, b] : cells) at(a, b) = id;
    }
  }
  return piece;
}

// Chaikin's corners cut, twice: a winding line, not a grid's.
std::vector<Vec2> soften(std::vector<Vec2> pts) {
  for (int pass = 0; pass < 3 && pts.size() > 2; ++pass) {
    std::vector<Vec2> out{pts.front()};
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
      const Vec2 a = pts[i], b = pts[i + 1];
      out.push_back({a.x * 0.75f + b.x * 0.25f, a.z * 0.75f + b.z * 0.25f});
      out.push_back({a.x * 0.25f + b.x * 0.75f, a.z * 0.25f + b.z * 0.75f});
    }
    out.push_back(pts.back());
    pts = std::move(out);
  }
  return pts;
}

// A channel dug along `pts`: its bed under the sea, its banks going down to it.
void dig_channel(Heightmap& m, const std::vector<Vec2>& pts, uint64_t seed) {
  float x0 = 1e30f, z0 = 1e30f, x1 = -1e30f, z1 = -1e30f;
  for (const Vec2& p : pts) {
    x0 = std::min(x0, p.x);
    z0 = std::min(z0, p.z);
    x1 = std::max(x1, p.x);
    z1 = std::max(z1, p.z);
  }
  const float sea = rules::kSeaLevel;
  constexpr float kReach = 24.0f;
  each_point(m, x0 - kReach, z0 - kReach, x1 + kReach, z1 + kReach, [&](float x, float z, float& h) {
    float along = 0;
    const float d = polyline_distance(pts, {x, z}, &along);
    if (d > kReach) return;
    const float half = 2.6f + 2.2f * value_noise(seed + 1301, along, 0, 40.0f);  // wider here, narrower there
    const float bed = sea - 0.5f - 0.8f * (1 - std::min(1.0f, (d / half) * (d / half)));
    // The banks: steeper where the ground stands high (a little gorge), never straight.
    const float wobble = 1.4f * (value_noise(seed + 1302, x, z, 6.0f) - 0.5f);
    const float blend = 3.0f + 2.2f * std::max(0.0f, h - sea);  // (walkable: never much steeper than 1 in 1.5)
    const float k = 1 - smooth(half, half + blend, d + wobble);
    const float target = d < half ? bed : sea - 0.3f;
    if (target < h) h += (target - h) * k;
  });
  // Where the sea is (the continent's grid): along the channel too.
  for (int i = 0; i < Heightmap::kLandPointsX; ++i) {
    for (int j = 0; j < Heightmap::kLandPointsZ; ++j) {
      const float x = Heightmap::kMinX + Heightmap::kLandCell * static_cast<float>(i);
      const float z = Heightmap::kMinZ + Heightmap::kLandCell * static_cast<float>(j);
      if (x < x0 - 12 || x > x1 + 12 || z < z0 - 12 || z > z1 + 12) continue;
      if (polyline_distance(pts, {x, z}) < 7.5f) {
        float& l = m.land[static_cast<size_t>(i * Heightmap::kLandPointsZ + j)];
        l = std::min(l, sea - 0.6f);
      }
    }
  }
}

// A hollow's water filled up into a low meadow, gently uneven: every grid
// point below it, from its water out, as far as the ground stays lower
// (its edge where the slopes around rise above it: no square's edge).
void fill_piece(Heightmap& m, const std::vector<int>& piece, int id, uint64_t seed) {
  const auto top = [&](float x, float z) { return rules::kSeaLevel + 0.6f + 0.9f * value_noise(seed + 1311, x, z, 22.0f); };
  const auto square = [&](float x, float z) {
    const int i = std::clamp(static_cast<int>((x - Heightmap::kMinX) / kSquare), 0, kSquaresX - 1);
    const int j = std::clamp(static_cast<int>((z - Heightmap::kMinZ) / kSquare), 0, kSquaresZ - 1);
    return piece[static_cast<size_t>(i * kSquaresZ + j)];
  };
  std::vector<uint8_t> seen(m.heights.size(), 0);
  std::vector<std::pair<int, int>> stack;
  for (int i = 0; i < Heightmap::kPointsX; ++i) {
    for (int j = 0; j < Heightmap::kPointsZ; ++j) {
      const float x = Heightmap::x_of(i), z = Heightmap::z_of(j);
      if (square(x, z) != id || m.at(i, j) >= top(x, z)) continue;
      seen[Heightmap::index(i, j)] = 1;
      stack.push_back({i, j});
    }
  }
  std::vector<std::pair<int, int>> filled;
  while (!stack.empty()) {
    const auto [i, j] = stack.back();
    stack.pop_back();
    const float x = Heightmap::x_of(i), z = Heightmap::z_of(j);
    m.heights[Heightmap::index(i, j)] = top(x, z);
    filled.push_back({i, j});
    const int di[] = {1, -1, 0, 0}, dj[] = {0, 0, 1, -1};
    for (int k = 0; k < 4; ++k) {
      const int u = i + di[k], v = j + dj[k];
      if (u < 0 || v < 0 || u >= Heightmap::kPointsX || v >= Heightmap::kPointsZ || seen[Heightmap::index(u, v)]) continue;
      const float ux = Heightmap::x_of(u), vz = Heightmap::z_of(v);
      if (m.at(u, v) >= top(ux, vz) || square(ux, vz) == 0) continue;  // the slopes around; the open sea
      seen[Heightmap::index(u, v)] = 1;
      stack.push_back({u, v});
    }
  }
  // Where it meets lower ground (the open sea's shore): going down to it
  // gently, a beach, not a step.
  std::vector<float> from_top(m.heights.size(), -1e30f);
  std::vector<std::pair<int, int>> ring = filled, next;
  for (const auto& [i, j] : filled) from_top[Heightmap::index(i, j)] = m.at(i, j);
  for (int step = 1; step <= 10 && !ring.empty(); ++step) {
    next.clear();
    for (const auto& [i, j] : ring) {
      const float level = from_top[Heightmap::index(i, j)];
      const int di[] = {1, -1, 0, 0}, dj[] = {0, 0, 1, -1};
      for (int k = 0; k < 4; ++k) {
        const int u = i + di[k], v = j + dj[k];
        if (u < 0 || v < 0 || u >= Heightmap::kPointsX || v >= Heightmap::kPointsZ) continue;
        const size_t at = Heightmap::index(u, v);
        const float ramp = level - 0.6f * Heightmap::kCellSize;  // 1 in 1.7 at most
        if (from_top[at] >= ramp) continue;
        from_top[at] = ramp;
        next.push_back({u, v});
      }
    }
    for (const auto& [u, v] : next) {
      float& h = m.heights[Heightmap::index(u, v)];
      h = std::max(h, from_top[Heightmap::index(u, v)]);
    }
    ring.swap(next);
  }
}

}  // namespace

WaterFix join_waters(World& w) {
  WaterFix fix;
  Heightmap& m = w.ground;
  if (m.empty() || m.land.empty()) return fix;
  int count = 0;
  std::vector<int> piece = water_pieces(m, count);
  if (count <= 1) return fix;

  // Where a channel may go: a grid of 4 m; not near a way, a place, the river, the valley.
  constexpr float kCell = 4.0f;
  constexpr int NX = static_cast<int>(2 * rules::kReliefHalfSize / kCell), NZ = static_cast<int>((rules::kReliefHalfSize + rules::kReliefNorth) / kCell);
  const auto center = [&](int i, int j) {
    return Vec2{Heightmap::kMinX + kCell * (static_cast<float>(i) + 0.5f), Heightmap::kMinZ + kCell * (static_cast<float>(j) + 0.5f)};
  };
  std::vector<uint8_t> blocked(static_cast<size_t>(NX * NZ), 0);
  const auto stamp = [&](const std::vector<Vec2>& line, float r) {
    const int reach = static_cast<int>(std::ceil(r / kCell)) + 1;
    for (size_t k = 0; k < line.size(); k += 2) {
      const Vec2 p = line[k];
      const int ci = static_cast<int>((p.x - Heightmap::kMinX) / kCell), cj = static_cast<int>((p.z - Heightmap::kMinZ) / kCell);
      for (int a = ci - reach; a <= ci + reach; ++a) {
        for (int b = cj - reach; b <= cj + reach; ++b) {
          if (a < 0 || b < 0 || a >= NX || b >= NZ) continue;
          const Vec2 c = center(a, b);
          if (std::hypot(c.x - p.x, c.z - p.z) < r) blocked[static_cast<size_t>(a * NZ + b)] = 1;
        }
      }
    }
  };
  stamp(w.path.points, 14.0f);
  if (w.west.valid()) {
    stamp(w.west.road.points, 12.0f);
    stamp(w.west.stream.points, 14.0f);
  }
  for (const Road& lane : w.roads.lanes) stamp(lane.points, 10.0f);
  for (int i = 0; i < NX; ++i) {
    for (int j = 0; j < NZ; ++j) {
      const Vec2 c = center(i, j);
      if (blocked[static_cast<size_t>(i * NZ + j)]) continue;
      const bool place = std::max(std::fabs(c.x), std::fabs(c.z)) < rules::kValleyHalfSize + 12.0f ||
                         in_village(w.village, c.x, c.z, 10.0f) || in_ruins(w.ruins, c.x, c.z, 10.0f) ||
                         in_castle(w.castle, c.x, c.z, 10.0f) || in_cave_hill(w.cave, c.x, c.z, 10.0f) ||
                         in_west_place(w.west, c.x, c.z, 10.0f) || in_north_place(w.north, c.x, c.z, 10.0f) ||
                         in_roadside_place(w.roads, c.x, c.z, 10.0f) || in_site(w.sites, c.x, c.z, 10.0f);
      if (place) blocked[static_cast<size_t>(i * NZ + j)] = 1;
    }
  }

  // From the open sea outwards: the cheapest way to it from everywhere
  // (low ground first, a little noise for the bends).
  const auto square_of = [&](Vec2 p) {
    const int i = std::clamp(static_cast<int>((p.x - Heightmap::kMinX) / kSquare), 0, kSquaresX - 1);
    const int j = std::clamp(static_cast<int>((p.z - Heightmap::kMinZ) / kSquare), 0, kSquaresZ - 1);
    return piece[static_cast<size_t>(i * kSquaresZ + j)];
  };
  const auto water = [&](Vec2 p) { return m.sea(p.x, p.z) && (!rules::in_world(p.x, p.z) || m.height(p.x, p.z) < rules::kSeaLevel); };
  std::vector<float> cost(static_cast<size_t>(NX * NZ), 1e30f), length(static_cast<size_t>(NX * NZ), 0);
  std::vector<int> from(static_cast<size_t>(NX * NZ), -1);
  using Node = std::pair<float, int>;
  std::priority_queue<Node, std::vector<Node>, std::greater<>> open;
  for (int i = 0; i < NX; ++i) {
    for (int j = 0; j < NZ; ++j) {
      const Vec2 c = center(i, j);
      if (square_of(c) == 0 && water(c)) {
        cost[static_cast<size_t>(i * NZ + j)] = 0;
        open.push({0.0f, i * NZ + j});
      }
    }
  }
  const auto step_cost = [&](Vec2 c) {
    if (water(c)) return 0.3f;
    const float above = m.height(c.x, c.z) - rules::kSeaLevel;
    if (above > rules::kChannelDeepest) return -1.0f;
    return 1.0f + 0.9f * std::max(0.0f, above) + 0.8f * value_noise(w.seed + 1320, c.x, c.z, 30.0f);
  };
  while (!open.empty()) {
    const auto [c0, k] = open.top();
    open.pop();
    if (c0 > cost[static_cast<size_t>(k)]) continue;
    const int i = k / NZ, j = k % NZ;
    for (int di = -1; di <= 1; ++di) {
      for (int dj = -1; dj <= 1; ++dj) {
        if (!di && !dj) continue;
        const int u = i + di, v = j + dj;
        if (u < 0 || v < 0 || u >= NX || v >= NZ) continue;
        const int n = u * NZ + v;
        if (blocked[static_cast<size_t>(n)]) continue;
        const float c = step_cost(center(u, v));
        if (c < 0) continue;
        const float len = (di && dj ? 1.4142f : 1.0f) * kCell;
        const float total = c0 + c * len;
        if (total < cost[static_cast<size_t>(n)]) {
          cost[static_cast<size_t>(n)] = total;
          length[static_cast<size_t>(n)] = length[static_cast<size_t>(k)] + (water(center(u, v)) ? 0.0f : len);
          from[static_cast<size_t>(n)] = k;
          open.push({total, n});
        }
      }
    }
  }

  // Each shut-in piece: its cheapest cell, the way from it to the sea.
  std::vector<bool> joined(static_cast<size_t>(count), false);
  for (int id = 1; id < count; ++id) {
    int best = -1;
    for (int i = 0; i < NX; ++i) {
      for (int j = 0; j < NZ; ++j) {
        const Vec2 c = center(i, j);
        const int n = i * NZ + j;
        if (square_of(c) != id || !water(c) || cost[static_cast<size_t>(n)] >= 1e29f) continue;
        if (best < 0 || cost[static_cast<size_t>(n)] < cost[static_cast<size_t>(best)]) best = n;
      }
    }
    if (best < 0 || length[static_cast<size_t>(best)] > rules::kChannelLongest) continue;
    std::vector<Vec2> line;
    for (int n = best; n >= 0; n = from[static_cast<size_t>(n)]) line.push_back(center(n / NZ, n % NZ));
    // Only the dry part needs digging (from the last water of the piece to the first of the sea).
    size_t a = 0, b = line.size();
    while (a + 1 < line.size() && water(line[a + 1])) ++a;
    while (b > a + 1 && water(line[b - 2])) --b;
    if (b <= a + 1) continue;
    std::vector<Vec2> dry(line.begin() + static_cast<long>(a), line.begin() + static_cast<long>(b));
    dig_channel(m, soften(dry), w.seed + static_cast<uint64_t>(id));
    joined[static_cast<size_t>(id)] = true;
    ++fix.channels;
  }
  // What is still shut in: filled.
  int left = 0;
  piece = water_pieces(m, left);
  for (int id = 1; id < left; ++id) {
    // (Never the water under the port's piers.)
    bool piers = false;
    for (const Vec2& p : w.sites.piers) {
      const int i = std::clamp(static_cast<int>((p.x + w.sites.port_sea.x * 6.0f - Heightmap::kMinX) / kSquare), 0, kSquaresX - 1);
      const int j = std::clamp(static_cast<int>((p.z + w.sites.port_sea.z * 6.0f - Heightmap::kMinZ) / kSquare), 0, kSquaresZ - 1);
      piers = piers || piece[static_cast<size_t>(i * kSquaresZ + j)] == id;
    }
    if (piers) continue;
    fill_piece(m, piece, id, w.seed);
    ++fix.filled;
  }
  return fix;
}

// ---- The buildings ----

bool seated_kind(ObjectKind k) {
  return k == ObjectKind::HouseCottage || k == ObjectKind::HouseChimney || k == ObjectKind::HouseHut || k == ObjectKind::Farmhouse ||
         k == ObjectKind::Cabin || k == ObjectKind::Chalet || k == ObjectKind::Barn || k == ObjectKind::MineEntrance ||
         k == ObjectKind::Grotto;
}

void seat_buildings(World& w, size_t first) {
  Heightmap& m = w.ground;
  if (m.empty() || m.seated) return;  // (once: the wilds drawn again over it find it done)
  m.seated = true;
  struct Pad {
    Box core;          // its walls, and a little more (none: an entrance in a rock face)
    Vec2 door, front;  // where one stands to go in; the way out of it
    Vec2 at;           // its middle
    float level;
    bool face;         // an entrance in a rock face: only the ground before it
  };
  std::vector<Pad> pads;
  for (size_t k = first; k < w.objects.size(); ++k) {
    const Object& o = w.objects[k];
    if (!seated_kind(o.kind)) continue;
    if (std::max(std::fabs(o.pos.x), std::fabs(o.pos.z)) < rules::kValleyHalfSize + 12.0f) continue;  // the valley's: saved
    const SinCos sc = sincos16(o.yaw);
    const Vec2 front{-sc.sin, -sc.cos};
    Pad p;
    p.at = {o.pos.x, o.pos.z};
    p.front = front;
    p.face = o.kind == ObjectKind::MineEntrance || o.kind == ObjectKind::Grotto;
    const Box b = object_box(o);
    p.core = b;
    p.core.min = p.core.min - Vec3{rules::kPadMargin, 0, rules::kPadMargin};
    p.core.max = p.core.max + Vec3{rules::kPadMargin, 0, rules::kPadMargin};
    if (place_behind(o.kind) != PlaceKind::Outside) {
      p.door = door_use_spot(o);
    } else {  // a barn: its great doors in its front
      const float half = 0.5f * (std::fabs(front.x) > 0.5f ? b.max.x - b.min.x : b.max.z - b.min.z);
      p.door = {o.pos.x + front.x * (half + 0.6f), o.pos.z + front.z * (half + 0.6f)};
    }
    if (p.face) {
      p.level = m.height(o.pos.x, o.pos.z);  // its sill
    } else {
      // As much dug as filled: the ground over its walls, and before its door (counted twice).
      float sum = 2 * m.height(p.door.x, p.door.z) + m.height(o.pos.x, o.pos.z);
      int n = 3;
      for (const float u : {0.25f, 0.75f}) {
        for (const float v : {0.25f, 0.75f}) {
          sum += m.height(b.min.x + (b.max.x - b.min.x) * u, b.min.z + (b.max.z - b.min.z) * v);
          ++n;
        }
      }
      p.level = sum / static_cast<float>(n);
    }
    pads.push_back(p);
  }
  if (pads.empty()) return;
  // Buildings side by side (a farm's house and barn): one level for them all, a yard.
  {
    std::vector<size_t> group(pads.size());
    for (size_t a = 0; a < pads.size(); ++a) group[a] = a;
    const auto root = [&](size_t a) {
      while (group[a] != a) a = group[a] = group[group[a]];
      return a;
    };
    const auto gap = [](const Box& a, const Box& b) {
      const float dx = std::max({a.min.x - b.max.x, b.min.x - a.max.x, 0.0f});
      const float dz = std::max({a.min.z - b.max.z, b.min.z - a.max.z, 0.0f});
      return std::hypot(dx, dz);
    };
    for (size_t a = 0; a < pads.size(); ++a) {
      for (size_t b = a + 1; b < pads.size(); ++b) {
        if (!pads[a].face && !pads[b].face && gap(pads[a].core, pads[b].core) < 10.0f) group[root(a)] = root(b);
      }
    }
    std::vector<float> sum(pads.size(), 0);
    std::vector<int> count(pads.size(), 0);
    for (size_t a = 0; a < pads.size(); ++a) {
      sum[root(a)] += pads[a].level;
      ++count[root(a)];
    }
    for (size_t a = 0; a < pads.size(); ++a) pads[a].level = sum[root(a)] / static_cast<float>(count[root(a)]);
  }
  // How much of a pad's level the ground at (x, z) takes, 0..1.
  const auto weight = [&](const Pad& p, float x, float z) {
    const Vec2 q{x, z};
    const Vec2 apron_end{p.door.x + p.front.x * rules::kPadApron, p.door.z + p.front.z * rules::kPadApron};
    float d = segment_distance(p.door, apron_end, q) - (p.face ? 3.4f : 1.8f);  // before its door
    if (p.face) {  // its frame's width, from the sill out
      const float along = (x - p.at.x) * p.front.x + (z - p.at.z) * p.front.z;
      if (along < -2.6f) return 0.0f;  // the rock behind stays (from past the grid cell behind the sill)
      d = std::min(d, segment_distance(p.at, p.door, q) - 3.4f);
    } else {
      const float dx = std::max({p.core.min.x - x, 0.0f, x - p.core.max.x});
      const float dz = std::max({p.core.min.z - z, 0.0f, z - p.core.max.z});
      d = std::min(d, std::hypot(dx, dz));
    }
    const float wobble = 2.0f * value_noise(w.seed + 1330, x, z, 7.0f) - 0.6f;  // an uneven edge
    if (d <= 0) return 1.0f;
    // Wider where there is more to make up: never steeper than about 1 in 2.
    const float blend = std::clamp(2.4f * std::fabs(p.level - m.height(x, z)), rules::kPadBlend, rules::kPadReach - 2.0f);
    return 1 - smooth(0.0f, blend, d + std::max(0.0f, wobble));
  };
  // Each grid point near a pad once: the levels of the pads reaching it,
  // weighed, taken as much as the strongest asks.
  const float r = rules::kPadReach + rules::kPadApron;
  const auto square = [&](const Pad& q, float& ax, float& az, float& bx, float& bz) {
    ax = std::min(q.core.min.x, q.door.x) - r;
    bx = std::max(q.core.max.x, q.door.x) + r;
    az = std::min(q.core.min.z, q.door.z) - r;
    bz = std::max(q.core.max.z, q.door.z) + r;
  };
  std::vector<uint8_t> done(m.heights.size(), 0);
  for (const Pad& p : pads) {
    float ax, az, bx, bz;
    square(p, ax, az, bx, bz);
    const int i0 = Heightmap::i_of(ax), i1 = std::min(Heightmap::i_of(bx) + 1, Heightmap::kPointsX - 1);
    const int j0 = Heightmap::j_of(az), j1 = std::min(Heightmap::j_of(bz) + 1, Heightmap::kPointsZ - 1);
    for (int i = i0; i <= i1; ++i) {
      for (int j = j0; j <= j1; ++j) {
        const size_t at = Heightmap::index(i, j);
        if (done[at]) continue;
        done[at] = 1;
        const float x = Heightmap::x_of(i), z = Heightmap::z_of(j);
        float strongest = 0, sum = 0, levels = 0;
        for (const Pad& q : pads) {
          float qx0, qz0, qx1, qz1;
          square(q, qx0, qz0, qx1, qz1);
          if (x < qx0 || x > qx1 || z < qz0 || z > qz1) continue;
            const float k = weight(q, x, z);
          if (k <= 0) continue;
          strongest = std::max(strongest, k);
          const float pull = k * k * k * k;  // a pad's own ground is its own, not its neighbour's
          sum += pull;
          levels += pull * q.level;
        }
        if (strongest > 0) m.heights[at] += (levels / sum - m.heights[at]) * strongest;
      }
    }
  }
}

}  // namespace monstres
