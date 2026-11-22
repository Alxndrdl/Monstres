#include "monstres/core/roads.hpp"

#include "monstres/core/continent.hpp"
#include "monstres/core/heightmap.hpp"
#include "monstres/core/north.hpp"
#include "monstres/core/quests.hpp"
#include "monstres/core/sites.hpp"
#include "monstres/core/west.hpp"

#include <algorithm>
#include <cmath>
#include <queue>

namespace monstres {

namespace {

Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.z + b.z}; }
Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.z - b.z}; }
Vec2 operator*(Vec2 a, float k) { return {a.x * k, a.z * k}; }
float length(Vec2 v) { return std::sqrt(v.x * v.x + v.z * v.z); }
float distance(Vec2 a, Vec2 b) { return length(a - b); }
Vec2 normalized(Vec2 v, Vec2 fallback = {1, 0}) {
  const float l = length(v);
  return l > 1e-6f ? v * (1 / l) : fallback;
}
Vec2 axis_of(Vec2 d) {
  return std::fabs(d.x) >= std::fabs(d.z) ? Vec2{d.x >= 0 ? 1.0f : -1.0f, 0} : Vec2{0, d.z >= 0 ? 1.0f : -1.0f};
}
Vec2 dir16(Angle16 a) {
  const SinCos sc = sincos16(a);
  return {sc.cos, sc.sin};
}

uint64_t mix(uint64_t z) {
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}
float hash01(uint64_t seed, uint64_t salt) {
  return static_cast<float>(mix(seed ^ (0x524F414453ULL + salt * 0x9E3779B97F4A7C15ULL)) >> 40) /
         static_cast<float>(1ULL << 24);
}

// Where the path is at z (its points go by increasing z).
float path_x_at(const Path& path, float z) {
  const auto& pts = path.points;
  if (pts.empty()) return 0;
  const auto it = std::lower_bound(pts.begin(), pts.end(), z, [](const Vec2& p, float v) { return p.z < v; });
  if (it == pts.begin()) return pts.front().x;
  if (it == pts.end()) return pts.back().x;
  const Vec2 b = *it, a = *(it - 1);
  const float t = b.z - a.z > 1e-6f ? (z - a.z) / (b.z - a.z) : 0.0f;
  return a.x + (b.x - a.x) * t;
}

// The nearest point of a polyline to p (and its distance).
Vec2 nearest_on(const std::vector<Vec2>& pts, Vec2 p, float& best) {
  Vec2 at = pts.empty() ? p : pts.front();
  best = 1e30f;
  for (size_t i = 0; i + 1 < pts.size(); ++i) {
    const Vec2 a = pts[i], ab = pts[i + 1] - a;
    const float len2 = ab.x * ab.x + ab.z * ab.z;
    const float t = len2 > 0 ? std::clamp(((p.x - a.x) * ab.x + (p.z - a.z) * ab.z) / len2, 0.0f, 1.0f) : 0.0f;
    const Vec2 q = a + ab * t;
    const float d = distance(p, q);
    if (d < best) {
      best = d;
      at = q;
    }
  }
  return at;
}

void make_lane_chunks(Road& r) {
  r.chunks.boxes.clear();
  for (size_t i = 0; i + 1 < r.points.size(); i += Chunks::kChunk) {
    std::array<float, 4> b{1e30f, 1e30f, -1e30f, -1e30f};
    for (size_t k = i; k <= std::min(i + Chunks::kChunk, r.points.size() - 1); ++k) {
      b[0] = std::min(b[0], r.points[k].x);
      b[1] = std::min(b[1], r.points[k].z);
      b[2] = std::max(b[2], r.points[k].x);
      b[3] = std::max(b[3], r.points[k].z);
    }
    r.chunks.boxes.push_back(b);
  }
}

float lane_distance(const Road& r, float x, float z, float limit) {
  float best2 = limit * limit;
  for (size_t c = 0; c < r.chunks.boxes.size(); ++c) {
    const auto& b = r.chunks.boxes[c];
    const float dx = std::max({b[0] - x, 0.0f, x - b[2]}), dz = std::max({b[1] - z, 0.0f, z - b[3]});
    if (dx * dx + dz * dz >= best2) continue;
    const size_t first = c * Chunks::kChunk, last = std::min(first + Chunks::kChunk, r.points.size() - 1);
    for (size_t i = first; i < last; ++i) {
      const Vec2 a = r.points[i], ab = r.points[i + 1] - a;
      const float len2 = ab.x * ab.x + ab.z * ab.z;
      const float t = len2 > 0 ? std::clamp(((x - a.x) * ab.x + (z - a.z) * ab.z) / len2, 0.0f, 1.0f) : 0.0f;
      const float ex = a.x + ab.x * t - x, ez = a.z + ab.z * t - z;
      best2 = std::min(best2, ex * ex + ez * ez);
    }
  }
  return std::sqrt(best2);
}

// Smoothed (corners cut, a few times over), then a point every meter or so.
std::vector<Vec2> smoothed(std::vector<Vec2> c) {
  for (int round = 0; round < 3 && c.size() > 2; ++round) {
    std::vector<Vec2> out{c.front()};
    for (size_t i = 0; i + 1 < c.size(); ++i) {
      out.push_back(c[i] * 0.75f + c[i + 1] * 0.25f);
      out.push_back(c[i] * 0.25f + c[i + 1] * 0.75f);
    }
    out.push_back(c.back());
    c = std::move(out);
  }
  // A point every meter along it (the last one at its end).
  std::vector<Vec2> pts{c.front()};
  float gone = 0;
  for (size_t i = 0; i + 1 < c.size(); ++i) {
    const float d = distance(c[i], c[i + 1]);
    float t = 1.0f - gone;
    while (t <= d) {
      pts.push_back(c[i] + (c[i + 1] - c[i]) * (t / std::max(d, 1e-6f)));
      t += 1.0f;
    }
    gone = d - (t - 1.0f);
  }
  if (distance(pts.back(), c.back()) > 0.3f) pts.push_back(c.back());
  return pts;
}

// The land for the lanes: a grid of kCell meters over the world, each cell
// its height (as near as can be known before the relief is made) and
// whether it is closed (water, cliffs' foot, places).
struct Land {
  static constexpr float kCell = 7.0f;
  static constexpr float kMinX = -rules::kWorldHalfSize + 10.0f, kMinZ = -rules::kWorldNorth + 10.0f;
  static constexpr int kNx = static_cast<int>((2 * rules::kWorldHalfSize - 20.0f) / kCell);
  static constexpr int kNz = static_cast<int>((rules::kWorldHalfSize + rules::kWorldNorth - 20.0f) / kCell);
  std::vector<float> height, extra;  // extra: cost added per meter (1e9: closed)
  std::vector<float> wander;         // a little noise: the cheapest way winds (not along the grid's lines)
  std::vector<uint8_t> target;       // on the path, the road or a lane already made
  std::vector<uint8_t> wet;          // in the water, or by it: closed even where a lane starts
  static float x_of(int i) { return kMinX + kCell * (static_cast<float>(i) + 0.5f); }
  static float z_of(int j) { return kMinZ + kCell * (static_cast<float>(j) + 0.5f); }
  static int i_of(float x) { return std::clamp(static_cast<int>((x - kMinX) / kCell), 0, kNx - 1); }
  static int j_of(float z) { return std::clamp(static_cast<int>((z - kMinZ) / kCell), 0, kNz - 1); }
  static size_t at(int i, int j) { return static_cast<size_t>(i) * kNz + static_cast<size_t>(j); }
};
constexpr float kClosed = 1e9f;

// The cheapest way from `from` to the path, the road or a lane (Dijkstra
// on the grid): its cells' middles, from `from` on. Empty: none.
std::vector<Vec2> cheapest_way(const Land& land, Vec2 from) {
  const int nx = Land::kNx, nz = Land::kNz;
  std::vector<float> cost(static_cast<size_t>(nx) * nz, 1e30f);
  std::vector<int32_t> came(static_cast<size_t>(nx) * nz, -1);
  using Item = std::pair<float, int32_t>;
  std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
  const int si = Land::i_of(from.x), sj = Land::j_of(from.z);
  const size_t start = Land::at(si, sj);
  cost[start] = 0;
  open.push({0, static_cast<int32_t>(start)});
  int32_t found = -1;
  static constexpr int kDi[8] = {1, -1, 0, 0, 1, 1, -1, -1}, kDj[8] = {0, 0, 1, -1, 1, -1, 1, -1};
  while (!open.empty()) {
    const auto [c, k] = open.top();
    open.pop();
    if (c > cost[static_cast<size_t>(k)]) continue;
    if (land.target[static_cast<size_t>(k)] && static_cast<size_t>(k) != start) {
      found = k;
      break;
    }
    const int i = k / nz, j = k % nz;
    for (int d = 0; d < 8; ++d) {
      const int a = i + kDi[d], b = j + kDj[d];
      if (a < 0 || b < 0 || a >= nx || b >= nz) continue;
      const size_t n = Land::at(a, b);
      // Close to where it starts, the place itself is open (its lane leaves it).
      const bool near_start = std::abs(a - si) <= 2 && std::abs(b - sj) <= 2;
      const float extra = near_start && !land.wet[n] ? 0.0f : land.extra[n];
      if (extra >= kClosed && !land.target[n]) continue;
      const float len = d < 4 ? Land::kCell : Land::kCell * 1.41421356f;
      const float slope = std::fabs(land.height[n] - land.height[static_cast<size_t>(k)]) / len;
      const float steep = slope > 0.4f && !near_start ? 40.0f : 0.0f;  // too steep for a lane: only if there is no other way
      const float step = len * (1.0f + land.wander[n] + std::min(extra, 50.0f) + 30.0f * slope * slope + steep);
      if (c + step < cost[n]) {
        cost[n] = c + step;
        came[n] = k;
        open.push({c + step, static_cast<int32_t>(n)});
      }
    }
  }
  std::vector<Vec2> way;
  for (int32_t k = found; k >= 0; k = came[static_cast<size_t>(k)]) {
    way.push_back({Land::x_of(k / nz), Land::z_of(k % nz)});
  }
  std::reverse(way.begin(), way.end());  // from `from`'s cell on
  return way;
}

}  // namespace

RoadsLayout make_roads(uint64_t seed, const Path& path, const VillageLayout& village, const RuinsLayout& ruins,
                       const ForestLayout& forest, const CaveLayout& cave, const CastleLayout& castle, const WestLayout& west,
                       const NorthLayout& north, SitesLayout& sites) {
  RoadsLayout r;
  sites = {};
  if (path.points.size() < 2 || !west.valid() || !north.valid()) return r;
  const auto ground = [&](float x, float z) {
    return natural_height(seed, x, z) + terrace_rise(seed, path, west, cave, x, z);
  };
  // (Far from the terrace's edge, which side is enough: its rise is all there, or none of it.)
  const auto quick_ground = [&](float x, float z, float edge) {
    if (std::fabs(edge) > 80.0f) return natural_height(seed, x, z) + (edge < 0 ? terrace_rise(seed, path, west, cave, x, z) : 0.0f);
    return ground(x, z);
  };
  // Firm ground all over a disc, beside the path (not on it), away from the
  // other places, the terrace's edge and the water.
  const auto firm = [&](Vec2 c, float rad) {
    if (!rules::in_world(c.x, c.z, rad + 14.0f)) return false;
    if (distance_to_path(path, c.x, c.z, rad + 5.0f) < rad + 4.0f) return false;
    if (std::fabs(scarp_side(west, c.x, c.z)) < rad + 30.0f) return false;
    if (distance_to_stream(west.stream, c.x, c.z, rad + 20.0f) < rad + 18.0f) return false;
    if (in_west_place(west, c.x, c.z, rad + 10.0f) || in_north_place(north, c.x, c.z, rad + 10.0f)) return false;
    float lowest = 1e9f, highest = -1e9f;
    for (int k = 0; k <= 8; ++k) {
      const Vec2 p = k == 8 ? c : c + dir16(static_cast<Angle16>(k * 8192)) * rad;
      const float h = ground(p.x, p.z);
      lowest = std::min(lowest, h);
      highest = std::max(highest, h);
    }
    return lowest > rules::kSeaLevel + 4.0f && highest - lowest < 0.35f * rad + 2.0f;
  };
  const auto beside = [&](float z0, float spread, float off0, float off_more, float rad, uint64_t salt, Vec2& at) {
    const float side0 = hash01(seed, salt + 999) < 0.5f ? -1.0f : 1.0f;
    for (int k = 0; k < 80; ++k) {
      const float z = z0 + (hash01(seed, salt + static_cast<uint64_t>(k)) - 0.5f) * spread;
      const float off = off0 + off_more * hash01(seed, salt + 200 + static_cast<uint64_t>(k)) + 0.5f * static_cast<float>(k);
      for (const float s : {side0, -side0}) {
        const Vec2 p{path_x_at(path, z) + s * off, z};
        if (firm(p, rad)) {
          at = p;
          return true;
        }
      }
    }
    return false;
  };
  const auto facing_path = [&](Vec2 c) { return axis_of(Vec2{path_x_at(path, c.z), c.z} - c); };
  // The places along the path.
  if (!beside(-515.0f, 60.0f, 22.0f, 10.0f, rules::kInnRadius, 10, r.inn)) r.inn = {path_x_at(path, -515.0f) + 24.0f, -515.0f};
  r.inn_front = facing_path(r.inn);
  if (!beside(-380.0f, 70.0f, 50.0f, 30.0f, rules::kFarmsteadRadius, 300, r.farmstead)) {
    r.farmstead = {path_x_at(path, -380.0f) - 60.0f, -380.0f};
  }
  r.farmstead_front = facing_path(r.farmstead);
  if (!beside(-800.0f, 50.0f, 55.0f, 30.0f, rules::kSawmillRadius, 600, r.sawmill)) {
    r.sawmill = {path_x_at(path, -800.0f) + 60.0f, -800.0f};
  }
  r.sawmill_front = facing_path(r.sawmill);

  // The land, for the lanes' ways.
  Land land;
  const size_t cells = static_cast<size_t>(Land::kNx) * Land::kNz;
  land.height.resize(cells);
  land.extra.resize(cells);
  land.target.assign(cells, 0);
  land.wander.resize(cells);
  land.wet.assign(cells, 0);
  std::vector<uint8_t> low(cells, 0);  // low ground by the sea: avoided (below)
  std::vector<float> edges(cells);       // the side of the terrace's edge (for the sites)
  for (int i = 0; i < Land::kNx; ++i) {
    for (int j = 0; j < Land::kNz; ++j) {
      const float x = Land::x_of(i), z = Land::z_of(j);
      const size_t k = Land::at(i, j);
      const float edge_side = scarp_side(west, x, z);
      edges[k] = edge_side;
      float h = quick_ground(x, z, edge_side);
      float extra = 0;
      // The north's valley up to the col: its floor (the relief cuts the mountains back).
      if (z < rules::kFrostFrom + 50.0f) h = std::min(h, north.hamlet_level + 0.15f * std::max(0.0f, rules::kFrostFrom - z) +
                                                             0.55f * std::max(0.0f, distance_to_path(path, x, z, 200.0f) - 24.0f));
      if (in_hamlet(north, x, z)) h = north.hamlet_level;
      land.height[k] = h;
      const bool on_path = distance_to_path(path, x, z, 4.0f) < 3.6f;
      const bool on_road = distance_to_road(west.road, x, z, 4.0f) < 3.6f;
      // (Not off a bridge: away from the river.)
      if ((on_path || on_road) && !in_village(village, x, z, 6.0f) && distance_to_stream(west.stream, x, z, 14.0f) >= 13.0f) {
        land.target[k] = 1;
      }
      // (The terrace lifts the continent's low ground up north.)
      if (h < rules::kSeaLevel + 0.25f) extra = kClosed;
      else if (h < rules::kSeaLevel + 1.2f) low[k] = 1;
      if (distance_to_stream(west.stream, x, z, 10.0f) < 9.0f || in_lake(village.lake, x, z, 4.0f)) {
        extra = kClosed;
        land.wet[k] = 1;
      }
      if (in_village(village, x, z, 3.0f) || in_lake(village.lake, x, z, 4.0f) || in_ruins(ruins, x, z, 2.0f) ||
          in_castle(castle, x, z, 2.0f) || in_cave_hill(cave, x, z, 6.0f)) {
        extra = kClosed;
      }
      if (!on_road && in_west_place(west, x, z, 2.0f)) extra = kClosed;
      if (in_north_place(north, x, z, 2.0f)) {
        // (Rochebrune's ground is open, not its buildings.)
        const bool open = in_hamlet(north, x, z) && distance(Vec2{x, z}, north.arena) > rules::kArenaRadius + 1.5f &&
                          !in_paddock(north, x, z, 1.5f) && distance(Vec2{x, z}, north.ranch) > 7.0f;
        if (!open) extra = kClosed;
      }
      for (const Vec2 p : north.houses) {
        if (distance(Vec2{x, z}, p) < 5.5f) extra = kClosed;
      }
      for (const auto& [c, rad] : {std::pair{r.inn, rules::kInnRadius}, std::pair{r.farmstead, rules::kFarmsteadRadius},
                                   std::pair{r.sawmill, rules::kSawmillRadius}}) {
        if (distance(Vec2{x, z}, c) < rad - 2.0f) extra = kClosed;
      }
      if (extra < kClosed) {
        if (in_forest(forest, x, z) || in_woods(west, x, z)) extra += 0.7f;  // round the woods if it can
        // Not over the terrace's edge (its cliffs) but where the path goes up it; not along it.
        const float edge = std::fabs(edge_side);
        if (edge < 9.0f && distance(Vec2{x, z}, west.ramp) > 70.0f) extra = kClosed;
        else if (edge < 14.0f) extra += 1.5f;
      }
      land.extra[k] = extra;
      land.wander[k] = 1.1f * value_noise(seed + 990, x, z, 24.0f) + 0.5f * value_noise(seed + 991, x, z, 9.0f);
    }
  }

  // Where the lanes can go from the path and the road (the low ground by the
  // sea too): the sites are placed there (make_sites), then closed.
  {
    std::vector<uint8_t> reached(cells, 0);
    std::vector<int32_t> todo;
    for (size_t k = 0; k < cells; ++k) {
      if (land.target[k]) {
        reached[k] = 1;
        todo.push_back(static_cast<int32_t>(k));
      }
    }
    while (!todo.empty()) {
      const int32_t k = todo.back();
      todo.pop_back();
      const int i = k / Land::kNz, j = k % Land::kNz;
      static constexpr int kDi[4] = {1, -1, 0, 0}, kDj[4] = {0, 0, 1, -1};
      for (int d = 0; d < 4; ++d) {
        const int a = i + kDi[d], b = j + kDj[d];
        if (a < 0 || b < 0 || a >= Land::kNx || b >= Land::kNz) continue;
        const size_t n = Land::at(a, b);
        if (reached[n] || land.extra[n] >= kClosed) continue;
        reached[n] = 1;
        todo.push_back(static_cast<int32_t>(n));
      }
    }
    const auto reachable = [&](float x, float z) {
      return rules::in_world(x, z, 0.0f) && reached[Land::at(Land::i_of(x), Land::j_of(z))] != 0;
    };
    // Between the grid's middles.
    const auto between = [&](const std::vector<float>& v, float x, float z) {
      const float fx = std::clamp((x - Land::kMinX) / Land::kCell - 0.5f, 0.0f, static_cast<float>(Land::kNx - 1) - 0.001f);
      const float fz = std::clamp((z - Land::kMinZ) / Land::kCell - 0.5f, 0.0f, static_cast<float>(Land::kNz - 1) - 0.001f);
      const int i = static_cast<int>(fx), j = static_cast<int>(fz);
      const float tx = fx - static_cast<float>(i), tz = fz - static_cast<float>(j);
      const auto a = [&](int p, int q) { return v[Land::at(p, q)]; };
      const float top = a(i, j) + (a(i + 1, j) - a(i, j)) * tx, bottom = a(i, j + 1) + (a(i + 1, j + 1) - a(i, j + 1)) * tx;
      return top + (bottom - top) * tz;
    };
    SiteHints hints;
    hints.reachable = reachable;
    hints.ground = [&](float x, float z) { return between(land.height, x, z); };
    hints.edge = [&](float x, float z) { return between(edges, x, z); };
    hints.keep_off = {{r.inn, rules::kInnRadius}, {r.farmstead, rules::kFarmsteadRadius}, {r.sawmill, rules::kSawmillRadius}};
    sites = make_sites(seed, path, village, ruins, forest, cave, castle, west, north, hints);
    for (int i = 0; i < Land::kNx; ++i) {
      for (int j = 0; j < Land::kNz; ++j) {
        const float x = Land::x_of(i), z = Land::z_of(j);
        const size_t k = Land::at(i, j);
        const Vec2 p{x, z};
        // The low ground by the sea: avoided if there is another way.
        if (low[k] && land.extra[k] < kClosed) land.extra[k] += 2.0f;
        if (in_site(sites, x, z, 2.0f)) {
          // (The port's quay is open, not its houses; the camp's ground too.)
          const bool open = (in_port(sites, x, z) || distance(p, sites.camp) < rules::kSeaCampRadius - 3.0f) &&
                            land.height[k] > rules::kSeaLevel + 0.5f;
          if (!open) land.extra[k] = kClosed;
        }
        for (const Vec2 h : sites.port_houses) {
          if (distance(p, h) < 5.0f) land.extra[k] = kClosed;
        }
      }
    }
  }

  // The lanes, each from its place to the nearest of the path, the road or
  // a lane already made (it joins it there).
  const auto toward_ways = [&](Vec2 c) {
    float dp = 0, dr = 0;
    const Vec2 a = nearest_on(path.points, c, dp), b = nearest_on(west.road.points, c, dr);
    return normalized((dp < dr ? a : b) - c);
  };
  struct Goal {
    Vec2 at;
    LaneTo to;
  };
  std::vector<Goal> goals;
  if (castle.radius > 0) goals.push_back({castle.center + castle.facing * (rules::kCastleHalf + 6.0f), LaneTo::Castle});
  goals.push_back({west.pasture + axis_of(west.farm - west.pasture) * (rules::kPenHalf + 4.5f), LaneTo::Pen});
  goals.push_back({west.mill + toward_ways(west.mill) * (rules::kMillRadius + 4.0f), LaneTo::Windmill});
  goals.push_back({west.water_mill + normalized(west.water_mill - west.water_wheel) * 5.5f, LaneTo::WaterMill});
  goals.push_back({west.menhirs + toward_ways(west.menhirs) * (rules::kMenhirsRadius + 1.0f), LaneTo::Menhirs});
  goals.push_back({west.cabin + toward_ways(west.cabin) * (rules::kCabinClearing + 1.0f), LaneTo::Cabin});
  goals.push_back({r.inn + r.inn_front * (rules::kInnRadius - 3.0f), LaneTo::Inn});
  goals.push_back({r.farmstead + r.farmstead_front * 8.0f, LaneTo::Farmstead});
  goals.push_back({north.mine + north.mine_front * (rules::kMineYard + 3.5f), LaneTo::Mine});
  goals.push_back({north.refuge + north.refuge_front * 6.0f, LaneTo::Refuge});
  goals.push_back({north.springs + toward_ways(north.springs) * (rules::kSpringsRadius + 1.0f), LaneTo::Springs});
  goals.push_back({r.sawmill + r.sawmill_front * 6.5f, LaneTo::Sawmill});
  goals.push_back({north.arena + north.arena_front * (rules::kArenaRadius + 2.0f), LaneTo::Arena});
  goals.push_back({north.ranch + north.ranch_front * 5.5f, LaneTo::Ranch});
  goals.push_back({north.grotto + north.grotto_front * 5.0f, LaneTo::Grotto});
  if (sites.valid()) {
    if (sites.port.x != 0 || sites.port.z != 0) {
      goals.push_back({sites.port - sites.port_sea * (rules::kPortDepth + 3.0f), LaneTo::Port});
    }
    if (!sites.abbey_way.empty()) goals.push_back({sites.abbey_way.front(), LaneTo::Abbey});
    const auto placed = [](Vec2 p) { return p.x != 0 || p.z != 0; };
    if (placed(sites.camp)) goals.push_back({sites.camp + sites.camp_front * (rules::kSeaCampRadius + 2.0f), LaneTo::Camp});
    if (placed(sites.lookout)) goals.push_back({sites.lookout + sites.lookout_front * (rules::kLookoutRadius + 1.0f), LaneTo::Lookout});
    if (placed(sites.quarry)) {
      goals.push_back({sites.quarry + sites.quarry_front * (rules::kQuarryRadius + rules::kQuarryRim + 3.0f), LaneTo::Quarry});
    }
    if (placed(sites.chapel)) goals.push_back({sites.chapel + sites.chapel_front * (rules::kChapelRadius - 4.0f), LaneTo::Chapel});
    if (placed(sites.apiary)) goals.push_back({sites.apiary + sites.apiary_front * (rules::kApiaryRadius - 3.0f), LaneTo::Apiary});
  }
  for (const Goal& g : goals) {
    std::vector<Vec2> way = cheapest_way(land, g.at);
    // (A site shut in where it starts: from a little further out, towards the
    // nearest way, never over the water.)
    const Vec2 out = toward_ways(g.at);
    for (int k = 1; k <= 5 && way.size() < 2 && g.to >= LaneTo::Port; ++k) {
      const Vec2 from = g.at + out * (7.0f * static_cast<float>(k));
      bool dry = true;
      for (float t = 0; t <= 1.0f && dry; t += 0.1f) {
        const Vec2 q = g.at + (from - g.at) * t;
        dry = distance_to_stream(west.stream, q.x, q.z, 10.0f) >= 9.0f && !in_lake(village.lake, q.x, q.z, 4.0f);
      }
      if (!dry) break;
      way = cheapest_way(land, from);
      if (way.size() >= 2) way.insert(way.begin(), g.at);
    }
    if (way.size() < 2) continue;
    // Its ends exactly: at the place, and on the line it joins.
    way.front() = g.at;
    float dp = 0, dr = 0, dl = 1e30f;
    const Vec2 end = way.back();
    Vec2 join = nearest_on(path.points, end, dp);
    const Vec2 on_road = nearest_on(west.road.points, end, dr);
    if (dr < dp) join = on_road;
    for (const Road& lane : r.lanes) {
      float d = 0;
      const Vec2 q = nearest_on(lane.points, end, d);
      if (d < std::min({dp, dr, dl})) {
        dl = d;
        join = q;
      }
    }
    way.push_back(join);
    std::reverse(way.begin(), way.end());  // from where it leaves, to its place
    Road lane;
    lane.points = smoothed(way);
    if (lane.points.size() < 6) continue;
    // Gentle swings from side to side (held at both ends, never into the water).
    {
      auto& pts = lane.points;
      const size_t n = pts.size();
      std::vector<Vec2> moved(pts);
      const float phase = 50.0f * hash01(seed, 800 + static_cast<uint64_t>(g.to));
      float along = 0;
      for (size_t i = 1; i + 1 < n; ++i) {
        along += distance(pts[i], pts[i - 1]);
        const float held = std::min(static_cast<float>(i), static_cast<float>(n - 1 - i));
        const float k = std::min(1.0f, held / 12.0f);
        const Vec2 t = normalized(pts[std::min(i + 4, n - 1)] - pts[i >= 4 ? i - 4 : 0]);
        const float off = k * 3.2f * (value_noise(seed + 995, along / 23.0f + phase, 0.5f, 1.0f) - 0.5f) * 2.0f;
        const Vec2 q = pts[i] + Vec2{-t.z, t.x} * off;
        if (distance_to_stream(west.stream, q.x, q.z, 8.0f) > 7.5f) moved[i] = q;
      }
      pts = smoothed(moved);
    }
    make_lane_chunks(lane);
    // The cells along it: a lane to join, for the next ones.
    for (size_t k = 0; k < lane.points.size(); k += 2) {
      const Vec2 p = lane.points[k];
      land.target[Land::at(Land::i_of(p.x), Land::j_of(p.z))] = 1;
    }
    r.lanes.push_back(std::move(lane));
    r.lane_to.push_back(g.to);
  }
  // The abbey's own way up its slope, from the foot its lane leads to.
  if (sites.abbey_way.size() >= 6) {
    Road way;
    way.points = sites.abbey_way;
    make_lane_chunks(way);
    r.lanes.push_back(std::move(way));
    r.lane_to.push_back(LaneTo::AbbeyWay);
  }
  // Shrines at some forks (where a lane leaves the path or the road).
  for (size_t k = 0; k < r.lanes.size(); ++k) {
    if (hash01(seed, 700 + k) < 0.4f && r.lanes[k].points.size() > 20) {
      const Vec2 a = r.lanes[k].points[0], b = r.lanes[k].points[6];
      const Vec2 d = normalized(b - a);
      r.shrines.push_back(a + d * 4.0f + Vec2{-d.z, d.x} * 2.6f);
    }
  }
  return r;
}

float distance_to_lanes(const RoadsLayout& r, float x, float z, float limit) {
  float best = limit;
  for (const Road& lane : r.lanes) best = std::min(best, lane_distance(lane, x, z, best));
  return best;
}

bool in_roadside_place(const RoadsLayout& r, float x, float z, float margin) {
  if (!r.valid()) return false;
  const Vec2 p{x, z};
  if (distance(p, r.inn) < rules::kInnRadius + margin || distance(p, r.farmstead) < rules::kFarmsteadRadius + margin ||
      distance(p, r.sawmill) < rules::kSawmillRadius + margin) {
    return true;
  }
  for (const Vec2& s : r.shrines) {
    if (distance(p, s) < 2.5f + margin) return true;
  }
  return false;
}

// ---- The objects ----------------------------------------------------------------------

namespace {

Object make(ObjectKind kind, Vec2 pos, Angle16 yaw = 0, uint8_t variant = 0, Vec3 size = {1, 1, 1}) {
  Object o;
  o.kind = kind;
  o.pos = {pos.x, 0, pos.z};
  o.size = size;
  o.yaw = yaw;
  o.variant = variant;
  return o;
}

// The quarter turn whose front (local -Z) looks along `dir` (an axis).
Angle16 yaw_facing(Vec2 dir) {
  if (std::fabs(dir.x) >= std::fabs(dir.z)) return dir.x > 0 ? Angle16{49152} : Angle16{16384};
  return dir.z > 0 ? Angle16{32768} : Angle16{0};
}

Vec2 local_to_world(const Object& o, float lx, float lz) {
  const SinCos sc = sincos16(o.yaw);
  return {o.pos.x + lx * sc.cos + lz * sc.sin, o.pos.z - lx * sc.sin + lz * sc.cos};
}

bool nature(ObjectKind k) { return k <= ObjectKind::Crate; }

}  // namespace

void clear_lanes(World& w) {
  const RoadsLayout& r = w.roads;
  if (!r.valid() || w.ground.empty()) return;
  // The valley's own trees and rocks (saved) on a lane, or in a place along the roads, go.
  {
    std::vector<Object> kept;
    kept.reserve(w.objects.size());
    bool dropping_log = false;
    for (const Object& o : w.objects) {
      if (o.kind == ObjectKind::Collider && dropping_log && o.variant == 0) continue;  // the pieces of a log gone
      dropping_log = false;
      if (nature(o.kind)) {
        const Box b = object_box(o);
        const float reach = std::max({b.max.x - b.min.x, b.max.z - b.min.z, o.kind == ObjectKind::Log ? 2 * o.size.x : 0.0f}) * 0.5f;
        if (distance_to_lanes(r, o.pos.x, o.pos.z, reach + 2.0f) < reach + rules::kLaneHalfWidth + 0.4f ||
            in_roadside_place(r, o.pos.x, o.pos.z, reach)) {
          dropping_log = o.kind == ObjectKind::Log;
          continue;
        }
      }
      kept.push_back(o);
    }
    w.objects = std::move(kept);
  }
}

void build_roads(World& w) {
  const RoadsLayout& r = w.roads;
  if (!r.valid() || w.ground.empty()) return;
  Rng rng(w.seed ^ 0x524F414453504345ULL, 53);  // "ROADSPCE": its own stream
  const auto v8 = [&]() { return static_cast<uint8_t>(rng.below(256)); };
  const auto add = [&](const Object& o) { w.objects.push_back(o); };
  // Only on ground flat enough, where nothing of the roads' stands yet, off the ways.
  const auto add_if_free = [&](const Object& o) {
    Box b = object_box(o);
    {
      const float center = w.ground.height(o.pos.x, o.pos.z);
      float lowest = center;
      for (float cx : {b.min.x, b.max.x}) {
        for (float cz : {b.min.z, b.max.z}) lowest = std::min(lowest, w.ground.height(cx, cz));
      }
      if (center - lowest > 0.6f * (b.max.y - b.min.y) + 0.1f) return false;
    }
    const float reach = 0.5f * std::max(b.max.x - b.min.x, b.max.z - b.min.z);
    if (distance_to_path(w.path, o.pos.x, o.pos.z, reach + 2.0f) < reach + rules::kPathHalfWidth + 0.3f) return false;
    if (distance_to_lanes(r, o.pos.x, o.pos.z, reach + 2.0f) < reach + rules::kLaneHalfWidth + 0.3f) return false;
    if (w.west.valid() && distance_to_road(w.west.road, o.pos.x, o.pos.z, reach + 2.0f) < reach + rules::kRoadHalfWidth + 0.3f) {
      return false;
    }
    b.min = b.min - Vec3{0.3f, 0, 0.3f};
    b.max = b.max + Vec3{0.3f, 0, 0.3f};
    b.min.y = -1e9f;
    b.max.y = 1e9f;
    for (size_t k = 0; k < w.objects.size(); ++k) {  // (all: the valley's, the west's, the north's, its own)
      if (std::fabs(w.objects[k].pos.x - o.pos.x) > 16.0f || std::fabs(w.objects[k].pos.z - o.pos.z) > 16.0f) continue;
      Box other = object_box(w.objects[k]);
      other.min.y = -1e9f;
      other.max.y = 1e9f;
      if (overlaps(b, other)) return false;
    }
    w.objects.push_back(o);
    return true;
  };

  // A signpost at each fork, on the lane's side; the shrines.
  for (const Road& lane : r.lanes) {
    if (lane.points.size() < 8) continue;
    const Vec2 a = lane.points[0], d = normalized(lane.points[5] - a);
    const Vec2 side{-d.z, d.x};
    for (const float s : {1.0f, -1.0f}) {
      if (add_if_free(make(ObjectKind::Sign, a + d * 3.2f + side * (s * 1.9f), yaw_facing(d * -1.0f)))) break;
    }
  }
  for (const Vec2& s : r.shrines) {
    if (add_if_free(make(ObjectKind::Shrine, s, static_cast<Angle16>(rng.below(4) << 14), v8()))) {
      const Vec2 b = s + dir16(static_cast<Angle16>(rng.below(65536))) * 2.2f;
      add_if_free(make(ObjectKind::Bench, b, static_cast<Angle16>(rng.below(2) << 14), v8()));
    }
  }

  // The inn: its house, tables and benches outside, barrels, a lantern, a woodpile behind.
  {
    const Object inn = make(ObjectKind::HouseChimney, r.inn, yaw_facing(r.inn_front), v8());
    add(inn);
    for (const float u : {-2.4f, 2.4f}) {
      const Vec2 t = local_to_world(inn, u, -5.4f);
      add_if_free(make(ObjectKind::Table, t, inn.yaw, v8()));
      add_if_free(make(ObjectKind::Bench, local_to_world(inn, u, -4.5f), inn.yaw, v8()));
      add_if_free(make(ObjectKind::Bench, local_to_world(inn, u, -6.3f), inn.yaw, v8()));
    }
    add_if_free(make(ObjectKind::Barrel, local_to_world(inn, 3.1f, -1.0f), 0, v8()));
    add_if_free(make(ObjectKind::Barrel, local_to_world(inn, 3.2f, 0.0f), 0, v8()));
    add_if_free(make(ObjectKind::Barrel, local_to_world(inn, -3.1f, -1.4f), 0, v8()));
    add_if_free(make(ObjectKind::Lamp, local_to_world(inn, 1.6f, -3.2f), 0));
    add_if_free(make(ObjectKind::Woodpile, local_to_world(inn, 0, 3.0f), inn.yaw, v8()));
    add_if_free(make(ObjectKind::Well, local_to_world(inn, -6.0f, -3.0f), 0, v8()));
  }
  // The farmstead: its house, its barn, fields, haystacks, a few apple trees, fences.
  {
    const Object house = make(ObjectKind::HouseCottage, r.farmstead, yaw_facing(r.farmstead_front), v8());
    add(house);
    add_if_free(make(ObjectKind::Barn, local_to_world(house, 9.5f, 2.0f), house.yaw, v8()));
    for (int k = 0; k < 3; ++k) {
      const Vec2 at = local_to_world(house, -9.0f - 6.5f * static_cast<float>(k % 2), 3.0f + 6.0f * static_cast<float>(k / 2) - 4.0f);
      add_if_free(make(ObjectKind::Crops, at, house.yaw, static_cast<uint8_t>(v8() | (k == 1 ? 1 : 0)), {2.6f, 1, 2.2f}));
    }
    for (int k = 0; k < 3; ++k) {
      const Vec2 at = local_to_world(house, 6.0f + 2.4f * static_cast<float>(k), -5.5f - 1.2f * static_cast<float>(k % 2));
      add_if_free(make(ObjectKind::Haystack, at, 0, v8()));
    }
    for (int k = 0; k < 4; ++k) {
      const Vec2 at = local_to_world(house, -4.0f + 4.5f * static_cast<float>(k % 2), 9.0f + 4.0f * static_cast<float>(k / 2));
      const float sc = 0.85f + 0.3f * static_cast<float>(rng.below(100)) / 100.0f;
      add_if_free(make(ObjectKind::FruitTree, at, static_cast<Angle16>(rng.below(65536)), v8(), {sc, sc, sc}));
    }
    for (int k = 0; k < 6; ++k) {  // fences along the fields' front
      add_if_free(make(ObjectKind::Fence, local_to_world(house, -6.0f - 2.0f * static_cast<float>(k), -1.8f), house.yaw, v8()));
    }
    add_if_free(make(ObjectKind::Well, local_to_world(house, 4.2f, -2.8f), 0, v8()));
  }
  // The sawmill: the cabin, logs stacked, woodpiles, stumps around.
  {
    const Object cabin = make(ObjectKind::Cabin, r.sawmill, yaw_facing(r.sawmill_front), v8());
    add(cabin);
    for (int k = 0; k < 4; ++k) {
      Object log = make(ObjectKind::Log, local_to_world(cabin, -6.5f, -2.0f + 1.0f * static_cast<float>(k)),
                        static_cast<Angle16>((cabin.yaw + 16384) & 0xC000), v8(), {2.2f, 0.32f, 1});
      add_if_free(log);
    }
    add_if_free(make(ObjectKind::Woodpile, local_to_world(cabin, 0, 3.0f), cabin.yaw, v8()));
    add_if_free(make(ObjectKind::Woodpile, local_to_world(cabin, 4.6f, -1.0f), static_cast<Angle16>((cabin.yaw + 16384) & 0xFFFF), v8()));
    for (int k = 0; k < 8; ++k) {
      const Vec2 at = r.sawmill + dir16(static_cast<Angle16>(rng.below(65536))) * (6.0f + 6.0f * static_cast<float>(rng.below(100)) / 100.0f);
      const float s = 0.3f + 0.15f * static_cast<float>(rng.below(100)) / 100.0f;
      add_if_free(make(ObjectKind::Stump, at, 0, v8(), {s, 0.5f, s}));
    }
  }
  // By the path, here and there beyond the valley: benches, woodpiles,
  // haystacks, barrels; a lantern by the forks.
  {
    const auto& pts = w.path.points;
    float gone = 0;
    float next = 40.0f + 40.0f * static_cast<float>(rng.below(100)) / 100.0f;
    for (size_t i = 1; i + 1 < pts.size(); ++i) {
      gone += distance(pts[i], pts[i - 1]);
      if (gone < next) continue;
      gone = 0;
      next = 55.0f + 70.0f * static_cast<float>(rng.below(100)) / 100.0f;
      const Vec2 p = pts[i];
      const uint32_t what = rng.below(100);
      const float s = rng.below(2) ? 1.0f : -1.0f;
      const uint8_t variant = v8();
      if (std::max(std::fabs(p.x), std::fabs(p.z)) < rules::kValleyHalfSize + 10.0f || !rules::in_world(p.x, p.z, 10.0f)) continue;
      if (in_west_place(w.west, p.x, p.z, 6.0f) || in_north_place(w.north, p.x, p.z, 6.0f) || in_roadside_place(r, p.x, p.z, 6.0f)) continue;
      if (p.z < rules::kFrostFrom) continue;  // the mountains: nothing
      const Vec2 d = normalized(pts[i + 1] - pts[i - 1]);
      const Vec2 side{-d.z, d.x};
      const Vec2 at = p + side * (s * (rules::kPathHalfWidth + 1.8f));
      if (what < 30) {
        add_if_free(make(ObjectKind::Bench, at, static_cast<Angle16>((yaw_facing(axis_of(d)) + 16384) & 0xFFFF), variant));
      } else if (what < 50) {
        add_if_free(make(ObjectKind::Woodpile, p + side * (s * (rules::kPathHalfWidth + 2.2f)), yaw_facing(axis_of(side)), variant));
      } else if (what < 62 && p.z > rules::kLarchesFrom) {
        add_if_free(make(ObjectKind::Haystack, p + side * (s * (rules::kPathHalfWidth + 3.0f)), 0, variant));
      } else if (what < 72) {
        add_if_free(make(ObjectKind::Barrel, at, 0, variant));
        add_if_free(make(ObjectKind::Barrel, at + d * 0.8f, 0, variant));
      } else if (what < 80) {
        add_if_free(make(ObjectKind::Shrine, p + side * (s * (rules::kPathHalfWidth + 2.0f)), static_cast<Angle16>(rng.below(4) << 14), variant));
      }
    }
  }
}

void add_road_npcs(World& w) {
  const RoadsLayout& r = w.roads;
  if (!r.valid() || w.ground.empty() || w.place.kind != PlaceKind::Outside) return;
  struct Who {
    uint8_t npc;
    Vec2 at;
  };
  const Who people[] = {
      {npc::kAubin, r.inn + r.inn_front * 4.4f + Vec2{r.inn_front.z, r.inn_front.x} * 4.6f},
      {npc::kBertille, r.farmstead + r.farmstead_front * 4.0f + Vec2{r.farmstead_front.z, r.farmstead_front.x} * 2.4f},
  };
  const BodyShape& shape = species_info(Species::Villager).shape;
  for (const Who& who : people) {
    if (npc_index(w, who.npc) >= 0) continue;
    Creature c;
    c.species = Species::Villager;
    c.npc = who.npc;
    c.variant = static_cast<uint8_t>(mix(w.seed ^ (0x4E50430000ULL + who.npc)) & 0xFF);
    for (int k = 0; k < 40; ++k) {
      const Vec2 d = dir16(static_cast<Angle16>(k * 10923));
      const float rad = k == 0 ? 0.0f : 0.8f + 0.35f * static_cast<float>(k / 6);
      const Vec2 p = who.at + d * rad;
      c.body.pos = {p.x, w.ground.height(p.x, p.z), p.z};
      if (!inside_any_object(w, c.body, shape)) break;
    }
    c.home = {c.body.pos.x, c.body.pos.z};
    add_creature(w, c);
  }
}

}  // namespace monstres
