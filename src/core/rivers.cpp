#include "monstres/core/rivers.hpp"

#include "monstres/core/continent.hpp"
#include "monstres/core/heightmap.hpp"
#include "monstres/core/village.hpp"

#include <algorithm>
#include <cmath>
#include <queue>

namespace monstres {

namespace {

using rules::kRiverStep;

float length(Vec2 v) { return std::sqrt(v.x * v.x + v.z * v.z); }
Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.z + b.z}; }
Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.z - b.z}; }
Vec2 operator*(Vec2 a, float k) { return {a.x * k, a.z * k}; }
float distance(Vec2 a, Vec2 b) { return length(a - b); }
Vec2 normalized(Vec2 v) {
  const float l = length(v);
  return l > 1e-6f ? v * (1.0f / l) : Vec2{1, 0};
}

float smooth(float e0, float e1, float x) {
  const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
  return t * t * (3 - 2 * t);
}

uint64_t mix64(uint64_t x) {
  x += 0x9E3779B97F4A7C15ULL;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
  return x ^ (x >> 31);
}
float hash01(uint64_t seed, uint64_t k) { return static_cast<float>(mix64(seed ^ mix64(k)) >> 40) / static_cast<float>(1ULL << 24); }

// ---- The ways: the lowest, over the continent's grid (every 20 m) ----

constexpr int kStride = 2;  // grid points of 10 m
constexpr float kNode = Continent::kCell * kStride;
constexpr int kNodesX = (Continent::kPointsX - 1) / kStride + 1, kNodesZ = (Continent::kPointsZ - 1) / kStride + 1;

struct Node {
  int i, j;
};
Vec2 node_at(Node n) { return {Continent::kMinX + kNode * static_cast<float>(n.i), Continent::kMinZ + kNode * static_cast<float>(n.j)}; }
Node node_of(Vec2 p) {
  return {std::clamp(static_cast<int>(std::lround((p.x - Continent::kMinX) / kNode)), 0, kNodesX - 1),
          std::clamp(static_cast<int>(std::lround((p.z - Continent::kMinZ) / kNode)), 0, kNodesZ - 1)};
}

// The cheapest way from `from` to a node where `goal` holds: going up costs
// a lot (water goes down), low ground a little less; never into `forbidden`
// (the sea, the playable area's relief and around it). Random by patches
// (the noise): no two rivers alike.
template <typename Goal, typename Forbidden>
std::vector<Vec2> lowest_way(const std::vector<float>& heights, uint64_t seed, Vec2 from, Goal goal, Forbidden forbidden, uint64_t salt) {
  const auto H = [&](int i, int j) { return heights[static_cast<size_t>(i) * kNodesZ + static_cast<size_t>(j)]; };
  const size_t count = static_cast<size_t>(kNodesX) * kNodesZ;
  std::vector<float> cost(count, 1e30f);
  std::vector<int32_t> back(count, -1);
  const auto id = [](int i, int j) { return static_cast<size_t>(i) * kNodesZ + static_cast<size_t>(j); };
  using Item = std::pair<float, int32_t>;
  std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
  const Node s = node_of(from);
  cost[id(s.i, s.j)] = 0;
  open.push({0.0f, static_cast<int32_t>(id(s.i, s.j))});
  int32_t found = -1;
  while (!open.empty()) {
    const auto [c, at] = open.top();
    open.pop();
    if (c > cost[static_cast<size_t>(at)]) continue;
    const int i = at / kNodesZ, j = at % kNodesZ;
    const Vec2 p = node_at({i, j});
    if (goal(p, H(i, j))) {
      found = at;
      break;
    }
    for (int di = -1; di <= 1; ++di) {
      for (int dj = -1; dj <= 1; ++dj) {
        if (di == 0 && dj == 0) continue;
        const int ni = i + di, nj = j + dj;
        if (ni < 0 || nj < 0 || ni >= kNodesX || nj >= kNodesZ) continue;
        const Vec2 q = node_at({ni, nj});
        const float hq = H(ni, nj);
        if (forbidden(q, hq) && !goal(q, hq)) continue;
        const float len = (di != 0 && dj != 0) ? kNode * 1.4142f : kNode;
        const float rough = value_noise(seed + salt, q.x, q.z, 170.0f);
        const float step = len * (0.6f + 1.2f * rough) + 40.0f * std::max(0.0f, hq - H(i, j)) + 0.01f * len * std::max(hq, 0.0f);
        const size_t nid = id(ni, nj);
        if (c + step < cost[nid]) {
          cost[nid] = c + step;
          back[nid] = at;
          open.push({c + step, static_cast<int32_t>(nid)});
        }
      }
    }
  }
  std::vector<Vec2> way;
  for (int32_t at = found; at >= 0; at = back[static_cast<size_t>(at)]) way.push_back(node_at({at / kNodesZ, at % kNodesZ}));
  std::reverse(way.begin(), way.end());
  if (!way.empty()) way.front() = from;
  return way;
}

// Rounded (corners cut, a few times over), its ends kept.
std::vector<Vec2> rounded(std::vector<Vec2> pts, int times) {
  for (int t = 0; t < times && pts.size() > 2; ++t) {
    std::vector<Vec2> out{pts.front()};
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
      const Vec2 a = pts[i], b = pts[i + 1];
      if (i > 0) out.push_back(a * 0.75f + b * 0.25f);
      if (i + 2 < pts.size()) out.push_back(a * 0.25f + b * 0.75f);
    }
    out.push_back(pts.back());
    pts = std::move(out);
  }
  return pts;
}

// A point every `step` meters along a line.
std::vector<Vec2> resampled(const std::vector<Vec2>& pts, float step) {
  std::vector<Vec2> out;
  if (pts.empty()) return out;
  out.push_back(pts.front());
  float carry = 0;
  for (size_t i = 0; i + 1 < pts.size(); ++i) {
    const Vec2 a = pts[i], b = pts[i + 1];
    const float len = distance(a, b);
    float t = step - carry;
    while (t <= len) {
      out.push_back(a + (b - a) * (t / len));
      t += step;
    }
    carry = len - (t - step);
  }
  if (distance(out.back(), pts.back()) > 0.3f * step) out.push_back(pts.back());
  else out.back() = pts.back();
  return out;
}

// Meandering: each point moved across the way, by swings that grow over the
// plains (little in the mountains), none at its ends (`keep_start`,
// `keep_end`: meters held straight there).
std::vector<Vec2> meandering(const std::vector<Vec2>& pts, uint64_t seed, float keep_start, float keep_end) {
  const size_t n = pts.size();
  std::vector<float> s(n, 0.0f);
  for (size_t i = 1; i < n; ++i) s[i] = s[i - 1] + distance(pts[i - 1], pts[i]);
  std::vector<Vec2> out(pts);
  for (size_t i = 1; i + 1 < n; ++i) {
    const Vec2 t = normalized(pts[i + 1] - pts[i - 1]);
    const Vec2 across{-t.z, t.x};
    const float plain = 1 - mountains_share(pts[i].x, pts[i].z);
    const float ends = smooth(0.0f, keep_start, s[i]) * smooth(0.0f, keep_end, s[n - 1] - s[i]);
    const float swing = 0.7f * (2 * value_noise(seed, s[i], 0, 75.0f) - 1) + 0.3f * (2 * value_noise(seed + 1, s[i], 0, 27.0f) - 1);
    out[i] = pts[i] + across * ((5.0f + 26.0f * plain) * ends * swing * 1.5f);
  }
  return out;
}

void box_up(River& r) {
  r.boxes.clear();
  for (size_t i = 0; i + 1 < r.points.size(); i += rules::kRiverChunk) {
    std::array<float, 4> b{1e30f, 1e30f, -1e30f, -1e30f};
    for (size_t k = i; k <= std::min(i + rules::kRiverChunk, r.points.size() - 1); ++k) {
      b[0] = std::min(b[0], r.points[k].x);
      b[1] = std::min(b[1], r.points[k].z);
      b[2] = std::max(b[2], r.points[k].x);
      b[3] = std::max(b[3], r.points[k].z);
    }
    r.boxes.push_back(b);
  }
  r.groups.clear();
  for (size_t i = 0; i < r.boxes.size(); i += rules::kRiverChunk) {
    std::array<float, 4> g{1e30f, 1e30f, -1e30f, -1e30f};
    for (size_t k = i; k < std::min(i + rules::kRiverChunk, r.boxes.size()); ++k) {
      g[0] = std::min(g[0], r.boxes[k][0]);
      g[1] = std::min(g[1], r.boxes[k][1]);
      g[2] = std::max(g[2], r.boxes[k][2]);
      g[3] = std::max(g[3], r.boxes[k][3]);
    }
    r.groups.push_back(g);
  }
}

// The springs, up in the mountains (meters).
// (On their south side, the ridges behind them: down to the plains.)
constexpr Vec2 kWestSpring{960.0f, -1260.0f};
constexpr Vec2 kCapitalSpring{1620.0f, -1300.0f};

bool in_pond(Vec2 p) {
  for (const Pond& q : continent_ponds()) {
    if (pond_shape(q, p.x, p.z) > -0.1f) return true;
  }
  return false;
}

// Where the first river meets the playable area's stream (its spring, just
// beyond the relief's east edge).
Vec2 west_junction(uint64_t seed) {
  return {rules::kReliefHalfSize + 6.0f, -560.0f + 300.0f * hash01(seed, 0x4A4F494EULL)};  // "JOIN"
}

// Near the playable area's relief (its own lands, its own stream).
bool by_relief(Vec2 p, float margin) { return rules::relief_inset(p.x, p.z) > -margin; }

}  // namespace

Vec2 river_junction(uint64_t seed) { return west_junction(seed); }

std::vector<River> route_rivers(const Heightmap& g, float join_level) {
  std::vector<River> rivers;
  if (g.empty() || g.land.empty()) return rivers;
  const std::shared_ptr<const Continent> continent = continent_of(g.seed);
  const Continent& k = *continent;
  // The ground on the nodes: the continent's, raised or lowered by the
  // relief's rim near it (Heightmap::land_height: as the tiles have it).
  std::vector<float> heights(static_cast<size_t>(kNodesX) * kNodesZ);
  for (int i = 0; i < kNodesX; ++i) {
    for (int j = 0; j < kNodesZ; ++j) {
      const Vec2 p = node_at({i, j});
      heights[static_cast<size_t>(i) * kNodesZ + static_cast<size_t>(j)] =
          rules::relief_inset(p.x, p.z) > -720.0f ? g.land_height(p.x, p.z) : k.at(i * kStride, j * kStride);
    }
  }
  const float sea = rules::kSeaLevel - 1.5f;
  // (The open sea: out beyond the island's coast.)
  const auto at_sea = [&](Vec2 p, float h) { return h < sea && !in_pond(p) && island_inland(k.seed, p.x, p.z) < 0.0f; };
  // (Inland: not along the coast, nor by the sea.)
  const auto coast = [&](Vec2 p, float h) { return h < rules::kSeaLevel + 2.0f && !in_pond(p); };
  // The first: down to the stream's spring, round the relief (not into it).
  {
    const Vec2 J = west_junction(k.seed);
    const auto goal = [&](Vec2 p, float) { return distance(p, J) < kNode; };
    // (Never lower than the stream it goes into: water goes down all the way.)
    const auto forbidden = [&](Vec2 p, float h) {
      return coast(p, h) || h < join_level - 1.0f || (by_relief(p, 60.0f) && distance(p, J) > 150.0f);
    };
    std::vector<Vec2> way = lowest_way(heights, k.seed, kWestSpring, goal, forbidden, 0x5257ULL);
    if (way.size() < 2) {
      way = lowest_way(heights, k.seed, kWestSpring, goal, [&](Vec2 p, float h) { return coast(p, h) || (by_relief(p, 60.0f) && distance(p, J) > 150.0f); }, 0x5257ULL);
    }
    if (way.size() > 1) {
      way.back() = J;
      River r;
      r.points = resampled(meandering(resampled(rounded(way, 3), 4.0f), k.seed + 0x4D41ULL, 40.0f, 140.0f), kRiverStep);
      rivers.push_back(std::move(r));
    }
  }
  // The second: through the capital, the Lac Miroir, then to the sea.
  {
    const Vec2 capital = continent_landmarks()[9].at, lake = continent_ponds()[0].center;
    // (Down to the lake, never lower than its water: nowhere to go from there.)
    const float above = continent_ponds()[0].level + 0.5f;
    const auto forbidden = [&](Vec2 p, float h) { return coast(p, h) || by_relief(p, 110.0f) || (h < above && !in_pond(p)); };
    const auto anyhow = [&](Vec2 p, float h) { return coast(p, h) || by_relief(p, 110.0f); };
    const auto to_capital = [&](Vec2 p, float) { return distance(p, capital) < kNode; };
    const auto into_lake = [&](Vec2 p, float) { return distance(p, lake) < kNode; };
    std::vector<Vec2> way = lowest_way(heights, k.seed, kCapitalSpring, to_capital, forbidden, 0x5243ULL);
    if (way.size() < 2) way = lowest_way(heights, k.seed, kCapitalSpring, to_capital, anyhow, 0x5243ULL);
    std::vector<Vec2> to_lake = lowest_way(heights, k.seed, capital, into_lake, forbidden, 0x524CULL);
    if (to_lake.size() < 2) to_lake = lowest_way(heights, k.seed, capital, into_lake, anyhow, 0x524CULL);
    const std::vector<Vec2> to_sea = lowest_way(heights, k.seed, lake, at_sea, [&](Vec2 p, float) { return by_relief(p, 110.0f); }, 0x5253ULL);
    if (way.size() > 1 && to_lake.size() > 1 && to_sea.size() > 1) {
      way.insert(way.end(), to_lake.begin() + 1, to_lake.end());
      way.insert(way.end(), to_sea.begin() + 1, to_sea.end());
      River r;
      r.points = resampled(meandering(resampled(rounded(way, 3), 4.0f), k.seed + 0x4D42ULL, 40.0f, 60.0f), kRiverStep);
      rivers.push_back(std::move(r));
    }
  }
  // Their widths: narrow at the spring, wider down (the second wide by the sea).
  for (size_t ri = 0; ri < rivers.size(); ++ri) {
    River& r = rivers[ri];
    const size_t n = r.points.size();
    r.width.assign(n, 0);
    const float total = static_cast<float>(n - 1) * kRiverStep;
    for (size_t i = 0; i < n; ++i) {
      const float s = static_cast<float>(i) * kRiverStep, t = s / std::max(total, 1.0f);
      float w = ri == 0 ? 0.8f + 0.6f * std::sqrt(t) : 1.0f + 4.5f * std::sqrt(t) + 3.0f * smooth(total - 200.0f, total, s);
      w *= 0.85f + 0.3f * value_noise(k.seed + 0x5749ULL + ri, s, 0, 60.0f);
      r.width[i] = w;
    }
    box_up(r);
  }
  return rivers;
}

void fit_rivers(Heightmap& g, const Stream* stream) {
  const bool joined = stream && stream->points.size() > 20 && stream->level.size() == stream->points.size();
  const float join_level = joined ? stream->level.front() : 0.0f;
  g.rivers.clear();
  if (g.empty() || g.land.empty()) return;
  g.rivers = route_rivers(g, join_level);
  g.tiles = TileCache{};  // (made before: dug again)
  for (size_t ri = 0; ri < g.rivers.size(); ++ri) {
    River& r = g.rivers[ri];
    const size_t n = r.points.size();
    if (n < 2) continue;
    // The ground along it, as made (its tiles: their folds; within reach of
    // the relief, as it blends into it): its water just under it, going down.
    std::vector<float> L(n);
    std::vector<int> pond(n, -1);
    for (size_t i = 0; i < n; ++i) {
      const Vec2 p = r.points[i];
      for (size_t q = 0; q < continent_ponds().size(); ++q) {
        if (pond_shape(continent_ponds()[q], p.x, p.z) > -0.02f) pond[i] = static_cast<int>(q);
      }
      float G = rules::relief_inset(p.x, p.z) > -rules::kOuterBlend - 4.0f ? g.height(p.x, p.z)
                                                                         : outer_ground(g.seed, p.x, p.z, g.land_height(p.x, p.z), 0.0f);
      if (pond[i] >= 0) G = continent_ponds()[static_cast<size_t>(pond[i])].level + 0.35f;
      L[i] = std::min(i > 0 ? L[i - 1] : 1e9f, G - 0.35f);
    }
    // Never under the lake it goes into (nor the sea).
    float need = rules::kSeaLevel;
    for (size_t i = n; i-- > 0;) {
      if (pond[i] >= 0) need = std::max(need, continent_ponds()[static_cast<size_t>(pond[i])].level);
      L[i] = std::max(L[i], need);
    }
    // The first ends at the stream's level, where it goes on.
    if (ri == 0 && joined) {
      for (float& v : L) v = std::max(v, join_level);
      const float excess = L[n - 1] - join_level, end = static_cast<float>(n - 1) * kRiverStep;
      for (size_t i = 0; i < n; ++i) L[i] -= excess * smooth(end - 120.0f, end, static_cast<float>(i) * kRiverStep);
    }
    // Steps where it goes down steeply: a pool, flat, then a waterfall into
    // the next one (a lip, from where the ground is that high; the pool dug
    // into the ground down to it: a little gorge below each fall).
    // (Not across a lake: its water flat.)
    std::vector<float> level = L;
    r.falls.clear();
    size_t i = 0;
    constexpr size_t kWithin = static_cast<size_t>(150.0f / kRiverStep);
    while (i + 1 < n) {
      const float drop = 2.5f + 7.0f * hash01(g.seed + ri, i);
      size_t j = i + 1;
      while (j < n && j - i <= kWithin && L[i] - L[j] < drop && pond[j] < 0) ++j;
      if (j < n && j - i <= kWithin && L[i] - L[j] >= drop && pond[j] < 0) {
        for (size_t q = i + 1; q <= j; ++q) level[q] = L[j] + 0.0015f * static_cast<float>(j - q) * kRiverStep;
        r.falls.push_back(static_cast<uint32_t>(i));
        i = j;
      } else {
        ++i;
      }
    }
    r.level = std::move(level);
    // The first going on a few meters along the stream, over the relief's
    // edge (their waters meet: no gap between them).
    if (ri == 0 && joined) {
      size_t from = 0;
      for (size_t q = 0; q < stream->points.size() / 4; ++q) {
        if (distance(stream->points[q], r.points.back()) < distance(stream->points[from], r.points.back())) from = q;
      }
      for (size_t q = from + 2; q < stream->points.size() && q <= from + 12; q += 2) {
        r.points.push_back(stream->points[q]);
        r.level.push_back(std::min(r.level.back(), stream->level[q]));
        r.width.push_back(stream->width[q]);
      }
      box_up(r);
    }
  }
}

bool nearest_river(const std::vector<River>& rivers, float x, float z, float limit, RiverSpot& spot) {
  float best2 = limit * limit;
  bool found = false;
  for (size_t ri = 0; ri < rivers.size(); ++ri) {
    const River& r = rivers[ri];
    if (r.level.size() != r.points.size()) continue;
    const auto far = [&](const std::array<float, 4>& b) {
      const float dx = std::max({b[0] - x, 0.0f, x - b[2]}), dz = std::max({b[1] - z, 0.0f, z - b[3]});
      return dx * dx + dz * dz >= best2;
    };
    for (size_t c = 0; c < r.boxes.size(); ++c) {
      if (c % rules::kRiverChunk == 0 && c / rules::kRiverChunk < r.groups.size() && far(r.groups[c / rules::kRiverChunk])) {
        c += rules::kRiverChunk - 1;  // (none of its boxes near)
        continue;
      }
      if (far(r.boxes[c])) continue;
      const size_t first = c * rules::kRiverChunk, last = std::min(first + rules::kRiverChunk, r.points.size() - 1);
      for (size_t i = first; i < last; ++i) {
        const Vec2 a = r.points[i], e = r.points[i + 1];
        const float abx = e.x - a.x, abz = e.z - a.z;
        const float len2 = abx * abx + abz * abz;
        const float t = len2 > 0 ? std::clamp(((x - a.x) * abx + (z - a.z) * abz) / len2, 0.0f, 1.0f) : 0.0f;
        const float px = a.x + abx * t - x, pz = a.z + abz * t - z;
        const float d2 = px * px + pz * pz;
        if (d2 < best2) {
          best2 = d2;
          found = true;
          spot.river = static_cast<int>(ri);
          spot.segment = i;
          spot.along = t;
        }
      }
    }
  }
  if (!found) return false;
  const River& r = rivers[static_cast<size_t>(spot.river)];
  const size_t i = spot.segment;
  const float t = spot.along;
  spot.distance = std::sqrt(best2);
  spot.width = r.width[i] + (r.width[i + 1] - r.width[i]) * t;
  spot.level = r.level[i] + (r.level[i + 1] - r.level[i]) * t;
  spot.direction = normalized(r.points[i + 1] - r.points[i]);
  return true;
}

float dig_rivers(const std::vector<River>& rivers, float x, float z, float h) {
  RiverSpot s;
  if (rivers.empty() || !nearest_river(rivers, x, z, rules::kRiverReach, s)) return h;
  const float w = s.width, d = s.distance, L = s.level;
  const float depth = 0.45f + 0.16f * w;
  if (d < w) {
    // Its bed: deepest in the middle, up to its water's edge.
    const float k = d / w;
    return std::min(h, L - depth * (1 - k * k));
  }
  // Its banks: rising from the water back to the ground (steeper where it
  // is high: the gorges), at least a little above the water near it.
  const float bank = L + 0.3f + 0.75f * (d - w) + 0.02f * (d - w) * (d - w);
  float out = std::min(h, bank);
  float hold = 1 - smooth(w + 3.0f, w + 8.0f, d);
  // (Not where it comes into the lake or the sea at their level: their water
  // is its own there, no bank across it.)
  if (hold > 0 && L < rules::kSeaLevel + 0.6f) hold = 0;
  for (const Pond& p : continent_ponds()) {
    if (hold > 0 && L < p.level + 0.6f && pond_shape(p, x, z) > -0.15f) hold = 0;
  }
  if (hold > 0 && out < L + 0.4f) out += (L + 0.4f - out) * hold;
  return out;
}

bool in_river(const std::vector<River>& rivers, float x, float z, float ground, float& level) {
  RiverSpot s;
  if (rivers.empty() || !nearest_river(rivers, x, z, 12.0f, s)) return false;
  if (s.distance > s.width + 0.5f || ground > s.level) return false;
  level = s.level;
  return true;
}

}  // namespace monstres
