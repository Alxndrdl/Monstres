#include "monstres/core/west.hpp"

#include "monstres/core/battle.hpp"
#include "monstres/core/continent.hpp"
#include "monstres/core/heightmap.hpp"
#include "monstres/core/interior.hpp"
#include "monstres/core/quests.hpp"
#include "monstres/core/variants.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

namespace monstres {

namespace {

// Plain arithmetic only (identical on every platform): angles as Angle16,
// square roots, no sin or cos of the standard library.
Vec2 dir16(Angle16 a) {  // (cos, sin)
  const SinCos sc = sincos16(a);
  return {sc.cos, sc.sin};
}
float length(Vec2 v) { return std::sqrt(v.x * v.x + v.z * v.z); }
Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.z + b.z}; }
Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.z - b.z}; }
Vec2 operator*(Vec2 a, float k) { return {a.x * k, a.z * k}; }
float distance(Vec2 a, Vec2 b) { return length(a - b); }
Vec2 normalized(Vec2 v, Vec2 fallback = {1, 0}) {
  const float l = length(v);
  return l > 1e-6f ? v * (1.0f / l) : fallback;
}
// The axis (±X or ±Z) closest to v.
Vec2 axis_of(Vec2 v) {
  if (std::fabs(v.x) >= std::fabs(v.z)) return {v.x >= 0 ? 1.0f : -1.0f, 0};
  return {0, v.z >= 0 ? 1.0f : -1.0f};
}

uint64_t mix(uint64_t z) {
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}
// A number in [0, 1) for (seed, salt), the same each time.
float hash01(uint64_t seed, uint64_t salt) {
  return static_cast<float>(mix(seed ^ (salt * 0x9E3779B97F4A7C15ULL) ^ 0x5745535457455354ULL) >> 40) /
         static_cast<float>(1ULL << 24);
}

float path_x_at(const Path& path, float z) {
  float best_x = 0, best_d = 1e30f;
  for (const Vec2& p : path.points) {
    const float d = std::fabs(p.z - z);
    if (d < best_d) {
      best_d = d;
      best_x = p.x;
    }
  }
  return best_x;
}

float catmull_rom(float p0, float p1, float p2, float p3, float t) {
  const float t2 = t * t, t3 = t2 * t;
  return 0.5f * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 + (-p0 + 3 * p1 - 3 * p2 + p3) * t3);
}

// Through the control points, a point about every `step` meters.
std::vector<Vec2> spline(const std::vector<Vec2>& c, float step) {
  std::vector<Vec2> out;
  const size_t n = c.size();
  for (size_t i = 0; i + 1 < n; ++i) {
    const Vec2 p0 = c[i > 0 ? i - 1 : 0], p1 = c[i], p2 = c[i + 1], p3 = c[std::min(i + 2, n - 1)];
    const int samples = std::max(1, static_cast<int>(distance(p1, p2) / step));
    for (int s = 0; s < samples; ++s) {
      const float t = static_cast<float>(s) / static_cast<float>(samples);
      out.push_back({catmull_rom(p0.x, p1.x, p2.x, p3.x, t), catmull_rom(p0.z, p1.z, p2.z, p3.z, t)});
    }
  }
  if (n > 0) out.push_back(c.back());
  return out;
}

void make_chunks(const std::vector<Vec2>& pts, Chunks& ch) {
  ch.boxes.clear();
  for (size_t i = 0; i + 1 < pts.size(); i += Chunks::kChunk) {
    std::array<float, 4> b{1e30f, 1e30f, -1e30f, -1e30f};
    for (size_t k = i; k <= std::min(i + Chunks::kChunk, pts.size() - 1); ++k) {
      b[0] = std::min(b[0], pts[k].x);
      b[1] = std::min(b[1], pts[k].z);
      b[2] = std::max(b[2], pts[k].x);
      b[3] = std::max(b[3], pts[k].z);
    }
    ch.boxes.push_back(b);
  }
}

// Distance to a line through points; the segment and where along it.
float polyline_distance(const std::vector<Vec2>& pts, const Chunks& ch, float x, float z, float limit, size_t* seg,
                        float* along) {
  float best2 = limit * limit;
  for (size_t c = 0; c < ch.boxes.size(); ++c) {
    const auto& b = ch.boxes[c];
    const float dx = std::max({b[0] - x, 0.0f, x - b[2]}), dz = std::max({b[1] - z, 0.0f, z - b[3]});
    if (dx * dx + dz * dz >= best2) continue;
    const size_t first = c * Chunks::kChunk, last = std::min(first + Chunks::kChunk, pts.size() - 1);
    for (size_t i = first; i < last; ++i) {
      const Vec2 a = pts[i], e = pts[i + 1];
      const float abx = e.x - a.x, abz = e.z - a.z;
      const float len2 = abx * abx + abz * abz;
      const float t = len2 > 0 ? std::clamp(((x - a.x) * abx + (z - a.z) * abz) / len2, 0.0f, 1.0f) : 0.0f;
      const float px = a.x + abx * t - x, pz = a.z + abz * t - z;
      const float d2 = px * px + pz * pz;
      if (d2 < best2) {
        best2 = d2;
        if (seg) *seg = i;
        if (along) *along = t;
      }
    }
  }
  return std::sqrt(best2);
}

// Where the land ends going west from the valley, at z (the last meters of land).
float coast_x(uint64_t seed, float z) {
  for (float x = -rules::kStartHalfSize; x > -630.0f; x -= 5.0f) {
    if (continent_height(seed, x, z) < rules::kSeaLevel + 0.5f) return x + 5.0f;
  }
  return -630.0f;
}

// Solid ground all over a disc (its middle and eight points around).
bool on_land(uint64_t seed, Vec2 c, float r) {
  const float limit = rules::kWorldHalfSize - 12.0f;
  if (std::fabs(c.x) + r > limit || std::fabs(c.z) + r > limit) return false;
  for (int k = 0; k <= 8; ++k) {
    const Vec2 p = k == 8 ? c : c + dir16(static_cast<Angle16>(k * 8192)) * r;
    if (continent_height(seed, p.x, p.z) < rules::kSeaLevel + 1.0f) return false;
  }
  return true;
}

struct Disc {
  Vec2 at;
  float r;
};

Vec2 farm_axis_right(Vec2 front) { return {front.z, -front.x}; }

}  // namespace

// ---- Layout ----------------------------------------------------------------------

float distance_to_road(const Road& road, float x, float z, float limit) {
  if (road.points.size() < 2) return limit;
  return polyline_distance(road.points, road.chunks, x, z, limit, nullptr, nullptr);
}

float distance_to_stream(const Stream& s, float x, float z, float limit, float* level) {
  if (s.points.size() < 2) return limit;
  size_t seg = 0;
  float t = 0;
  const float d = polyline_distance(s.points, s.chunks, x, z, limit, &seg, &t);
  if (level && d < limit && seg + 1 < s.level.size()) *level = s.level[seg] + (s.level[seg + 1] - s.level[seg]) * t;
  return d;
}

WestLayout make_west_layout(uint64_t seed, const Path& path, const VillageLayout& village, const RuinsLayout&,
                            const ForestLayout& forest, const CaveLayout&, const CastleLayout&) {
  WestLayout w;
  if (village.lake.radius_x <= 0 || path.points.empty()) return w;  // hand-made worlds
  std::vector<Disc> taken;
  const auto free_of = [&](Vec2 c, float r, float gap) {
    for (const Disc& d : taken) {
      if (distance(d.at, c) < d.r + r + gap) return false;
    }
    return true;
  };
  // Beyond the valley's square, and out of its forest (which reaches past it).
  const auto clear_of_valley = [&](Vec2 c, float r) {
    if (std::max(std::fabs(c.x), std::fabs(c.z)) - r < rules::kValleyHalfSize + 4.0f) return false;
    if (forest.radius_x > 0) {
      const float k = r / std::min(forest.radius_x, forest.radius_z);
      if (outline_distance(forest.center, forest.radius_x, forest.radius_z, kForestOutline, c.x, c.z) < 1.0f + k + 0.05f) {
        return false;
      }
    }
    return true;
  };
  // The first free spot around `want`, going round and out.
  const auto settle = [&](Vec2 want, float r, auto&& extra) {
    for (int k = 0; k < 700; ++k) {
      const float d = 3.0f * std::sqrt(static_cast<float>(k));
      const Vec2 c = want + dir16(static_cast<Angle16>(k * 40503)) * d;
      if (!on_land(seed, c, r) || !clear_of_valley(c, r) || !free_of(c, r, 6.0f) || !extra(c)) continue;
      return c;
    }
    return want;
  };

  // The lighthouse, where the land ends west of the start; the sea beyond it.
  const float jitter_z = (hash01(seed, 1) - 0.5f) * 20.0f;
  const float lz = -12.0f + jitter_z;
  const float coast = coast_x(seed, lz);
  w.lighthouse = {coast + 9.0f, lz};
  {
    float lowest = 1e30f;
    Vec2 sea{-1, 0};
    for (int k = 0; k < 16; ++k) {
      const Vec2 d = dir16(static_cast<Angle16>(k * 4096));
      const Vec2 p = w.lighthouse + d * 30.0f;
      const float h = continent_height(seed, p.x, p.z);
      if (h < lowest) {
        lowest = h;
        sea = d;
      }
    }
    w.sea = axis_of(sea);
  }
  taken.push_back({w.lighthouse, rules::kLighthouseRadius + 4.0f});

  // The fork, where the road leaves the path.
  const Vec2 fork{path_x_at(path, rules::kForkZ), rules::kForkZ};

  // The farm: north of the road (or south), well away from the coast and the stream to come.
  const float road_line = (fork.z + lz) * 0.5f;
  const float farm_r = rules::kFarmRadius + 6.0f;
  w.farm = settle(Vec2{-215.0f, road_line - 52.0f}, farm_r, [&](Vec2 c) {
    return c.x <= -208.0f && c.x >= w.lighthouse.x + farm_r + 24.0f && std::fabs(c.z - road_line) > farm_r + 6.0f;
  });
  w.farm_front = {0, w.farm.z < road_line ? 1.0f : -1.0f};
  taken.push_back({w.farm, farm_r});

  // The road: from the fork west, along the farm's front, to the lighthouse's door.
  {
    const Vec2 right = farm_axis_right(w.farm_front);
    const float front_line = w.farm.z + w.farm_front.z * (rules::kFarmRadius + 4.0f);
    std::vector<Vec2> c{fork, {fork.x - 18.0f, fork.z}};
    const float east_end = w.farm.x + std::fabs(right.x) * (rules::kFarmRadius + 14.0f);
    const float start = fork.x - 18.0f;
    const int legs = std::max(1, static_cast<int>((start - east_end) / 28.0f));
    for (int k = 1; k < legs; ++k) {
      const float t = static_cast<float>(k) / static_cast<float>(legs);
      const float x = start + (east_end - start) * t;
      const float wobble = x < -40.0f ? (hash01(seed, 10 + static_cast<uint64_t>(k)) - 0.5f) * 10.0f : 0.0f;
      c.push_back({x, fork.z + (front_line - fork.z) * t + wobble});
    }
    c.push_back({east_end, front_line});
    c.push_back({w.farm.x, front_line + 0.5f});
    const Vec2 door = w.lighthouse - w.sea * (rules::kLighthouseRadius + 4.0f);
    const float west_end = w.farm.x - (rules::kFarmRadius + 14.0f);
    c.push_back({west_end, front_line});
    const int more = std::max(1, static_cast<int>((west_end - door.x) / 28.0f));
    for (int k = 1; k < more; ++k) {
      const float t = static_cast<float>(k) / static_cast<float>(more);
      const float wobble = (hash01(seed, 40 + static_cast<uint64_t>(k)) - 0.5f) * 8.0f;
      c.push_back({west_end + (door.x - west_end) * t, front_line + (door.z - front_line) * t + wobble});
    }
    c.push_back(door);
    w.road.points = spline(c, 1.0f);
    make_chunks(w.road.points, w.road.chunks);
  }

  // The stream: from the hills in the north, east of the farm, under the
  // road, down south, then west to the sea.
  {
    const float x_cross = std::min(w.farm.x + rules::kFarmRadius + 30.0f, -154.0f);
    std::vector<Vec2> c;
    constexpr float kSouth = 205.0f;
    for (int k = 0; k <= 15; ++k) {
      const float z = -240.0f + 30.0f * static_cast<float>(k);
      if (z > kSouth) break;
      float x = x_cross + (hash01(seed, 100 + static_cast<uint64_t>(k)) - 0.5f) * 26.0f;
      if (std::fabs(z - road_line) < 34.0f) x = x_cross;
      if (std::fabs(z - w.farm.z) < rules::kFarmRadius + 24.0f) x = std::max(x, w.farm.x + rules::kFarmRadius + 16.0f);
      c.push_back({x, z});
    }
    // Its mouth: where the sea comes closest, south of the woods.
    float mouth_z = kSouth + 15.0f, nearest = -1e30f;
    for (float z = kSouth + 5.0f; z <= kSouth + 125.0f; z += 10.0f) {
      const float x = coast_x(seed, z);
      if (x > nearest) {
        nearest = x;
        mouth_z = z;
      }
    }
    for (int k = 1; k < 24; ++k) {
      const float x = x_cross - 30.0f * static_cast<float>(k);
      const float z = mouth_z + (hash01(seed, 200 + static_cast<uint64_t>(k)) - 0.5f) * 14.0f;
      c.push_back({x, z});
      // Well out at sea (its last stretch under the water), or at the world's edge.
      if (continent_height(seed, x, z) < rules::kSeaLevel - 3.0f || x < -rules::kWorldHalfSize + 30.0f) break;
    }
    w.stream.points = spline(c, 1.0f);
    make_chunks(w.stream.points, w.stream.chunks);
  }

  // The bridge: where the road crosses the stream.
  {
    float best = 1e30f;
    for (size_t i = 1; i + 1 < w.road.points.size(); ++i) {
      const Vec2 p = w.road.points[i];
      const float d = distance_to_stream(w.stream, p.x, p.z, best);
      if (d < best) {
        best = d;
        w.bridge = p;
        w.bridge_along = normalized(w.road.points[i + 1] - w.road.points[i - 1]);
      }
    }
  }
  // The cart: by the road, on the side of the start.
  {
    const size_t i = std::min(static_cast<size_t>(rules::kCartFromFork), w.road.points.size() - 2);
    const Vec2 along = normalized(w.road.points[i + 1] - w.road.points[i]);
    Vec2 side{-along.z, along.x};
    if (side.z < 0) side = side * -1.0f;
    w.cart = w.road.points[i] + side * (rules::kRoadHalfWidth + 2.6f);
    w.cart_along = along;
  }

  const auto away_from_ways = [&](float r) {
    return [&, r](Vec2 c) {
      return distance_to_road(w.road, c.x, c.z, r + 4.0f) >= r + 4.0f &&
             distance_to_stream(w.stream, c.x, c.z, r + 5.0f) >= r + 5.0f;
    };
  };
  const Vec2 right = farm_axis_right(w.farm_front);
  const Vec2 back = w.farm_front * -1.0f;

  w.mill = settle(w.farm + back * (rules::kFarmRadius + 26.0f) + right * 12.0f, rules::kMillKnoll,
                  away_from_ways(rules::kMillKnoll));
  taken.push_back({w.mill, rules::kMillKnoll});
  w.pasture = settle(w.farm + back * (rules::kFarmRadius + 62.0f) - right * 28.0f, rules::kPastureRadius + 10.0f,
                     away_from_ways(rules::kPastureRadius + 10.0f));
  taken.push_back({w.pasture, rules::kPastureRadius + 10.0f});
  {
    const float z = w.farm.z + back.z * 85.0f;
    w.menhirs = settle(Vec2{coast_x(seed, z) + 55.0f, z}, rules::kMenhirsRadius + 6.0f,
                       away_from_ways(rules::kMenhirsRadius + 6.0f));
    taken.push_back({w.menhirs, rules::kMenhirsRadius + 6.0f});
  }
  const float woods_r = std::max(rules::kWoodsRadiusX, rules::kWoodsRadiusZ) + 2.0f;
  w.woods = settle(w.farm + w.farm_front * (rules::kFarmRadius + 78.0f) - right * 20.0f, woods_r, away_from_ways(woods_r));
  w.woods_rx = rules::kWoodsRadiusX;
  w.woods_rz = rules::kWoodsRadiusZ;
  taken.push_back({w.woods, woods_r});
  // In the woods: the cabin by its edge towards the road, the camp, the hollow tree.
  const float towards_road = w.farm_front.z > 0 ? -1.0f : 1.0f;
  w.cabin = w.woods + Vec2{0.5f * w.woods_rx, towards_road * 0.5f * w.woods_rz};
  w.camp = w.woods + Vec2{-0.3f * w.woods_rx, -towards_road * 0.3f * w.woods_rz};
  w.hollow_tree = w.woods + Vec2{0.12f * w.woods_rx, towards_road * 0.05f * w.woods_rz};
  {
    const float z = w.woods.z + w.farm_front.z * 85.0f;
    w.crater = settle(Vec2{coast_x(seed, z) + 60.0f, z}, rules::kCraterRim + 8.0f, away_from_ways(rules::kCraterRim + 8.0f));
    taken.push_back({w.crater, rules::kCraterRim + 8.0f});
  }
  w.burrow = settle(Vec2{w.woods.x + 70.0f, w.woods.z + w.farm_front.z * 45.0f}, rules::kBurrowRadius + 4.0f,
                    away_from_ways(rules::kBurrowRadius + 4.0f));
  // The burrow's hole looks towards the stream (east).
  w.burrow_front = {1, 0};
  return w;
}

// ---- Where things are ----------------------------------------------------------------

namespace {
constexpr uint64_t kWoodsOutline = 4;
}

bool in_farm(const WestLayout& west, float x, float z, float margin) {
  if (!west.valid()) return false;
  return distance(west.farm, {x, z}) < rules::kFarmRadius + margin;
}

float woods_distance(const WestLayout& west, float x, float z) {
  if (!west.valid()) return 1e9f;
  return outline_distance(west.woods, west.woods_rx, west.woods_rz, kWoodsOutline, x, z);
}

bool in_woods(const WestLayout& west, float x, float z, float margin) {
  if (!west.valid()) return false;
  return woods_distance(west, x, z) < 1.0f + margin / std::min(west.woods_rx, west.woods_rz);
}

bool in_woods_clearing(const WestLayout& west, float x, float z, float margin) {
  if (!west.valid()) return false;
  const Vec2 p{x, z};
  return distance(p, west.camp) < rules::kCampRadius + margin ||
         distance(p, west.hollow_tree) < rules::kHollowClearing + margin ||
         distance(p, west.cabin) < rules::kCabinClearing + margin;
}

bool in_crater(const WestLayout& west, float x, float z, float margin) {
  if (!west.valid()) return false;
  return distance(west.crater, {x, z}) < rules::kCraterRim + margin;
}

bool in_west_place(const WestLayout& west, float x, float z, float margin) {
  if (!west.valid()) return false;
  const Vec2 p{x, z};
  return distance_to_road(west.road, x, z, rules::kRoadHalfWidth + 1.0f + margin) < rules::kRoadHalfWidth + 0.8f + margin ||
         distance_to_stream(west.stream, x, z, rules::kStreamChannel + margin + 1.0f) < rules::kStreamChannel + margin ||
         in_farm(west, x, z, margin + 2.0f) || distance(p, west.mill) < rules::kMillRadius + 5.0f + margin ||
         distance(p, west.pasture) < rules::kPastureRadius + margin ||
         distance(p, west.burrow) < rules::kBurrowRadius + margin || in_woods_clearing(west, x, z, margin) ||
         distance(p, west.lighthouse) < rules::kLighthouseRadius + 2.0f + margin ||
         distance(p, west.menhirs) < rules::kMenhirsRadius + margin || in_crater(west, x, z, margin) ||
         distance(p, west.cart) < 5.0f + margin || distance(p, west.bridge) < 7.0f + margin;
}

Vec2 farm_point(const WestLayout& west, float u, float v) {
  const Vec2 right = farm_axis_right(west.farm_front);
  return west.farm + right * u + west.farm_front * v;
}
Vec2 paddock_center(const WestLayout& west) { return farm_point(west, 16.0f, -14.0f); }
Vec2 orchard_center(const WestLayout& west) { return farm_point(west, -17.0f, 3.0f); }
Vec2 farm_rescue_spot(const WestLayout& west) { return farm_point(west, -1.0f, 19.0f); }

// ---- The objects -------------------------------------------------------------------------

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
// The quarter turn whose local +X runs along `dir` (fences, walls, the bridge).
Angle16 yaw_along(Vec2 dir) { return std::fabs(dir.x) >= std::fabs(dir.z) ? Angle16{0} : Angle16{16384}; }

Vec2 local_to_world(const Object& o, float lx, float lz) {
  const SinCos sc = sincos16(o.yaw);
  return {o.pos.x + lx * sc.cos + lz * sc.sin, o.pos.z - lx * sc.sin + lz * sc.cos};
}

// An invisible solid box in the space of `o`, from `bottom` meters up.
Object collider(const Object& o, float lx, float lz, Vec3 half, float bottom) {
  Object c = make(ObjectKind::Collider, local_to_world(o, lx, lz), o.yaw, static_cast<uint8_t>(std::lround(bottom * 10)));
  c.size = half;
  return c;
}

bool nature(ObjectKind k) { return k <= ObjectKind::Crate; }

// The valley's own trees and rocks (saved) in the way of the west's road and places go.
void make_way(World& w) {
  std::vector<Object> kept;
  kept.reserve(w.objects.size());
  bool dropping_log = false;
  for (const Object& o : w.objects) {
    if (o.kind == ObjectKind::Collider && dropping_log && o.variant == 0) continue;  // the pieces of a log gone
    dropping_log = false;
    if (nature(o.kind)) {
      const Box b = object_box(o);
      const float reach = std::max({b.max.x - b.min.x, b.max.z - b.min.z, o.kind == ObjectKind::Log ? 2 * o.size.x : 0.0f}) * 0.5f;
      if (in_west_place(w.west, o.pos.x, o.pos.z, reach + 0.3f)) {
        dropping_log = o.kind == ObjectKind::Log;
        continue;
      }
    }
    kept.push_back(o);
  }
  w.objects = std::move(kept);
}

}  // namespace

size_t build_west(World& w) {
  const WestLayout& west = w.west;
  if (!west.valid() || w.ground.empty()) return w.objects.size();
  make_way(w);
  const size_t first = w.objects.size();
  Rng rng(w.seed ^ 0x5745535450414345ULL, 43);  // "WESTPACE": its own stream
  // One draw per statement: the order in which a call's arguments are
  // evaluated differs between compilers (the same world everywhere).
  const auto v8 = [&]() { return static_cast<uint8_t>(rng.below(256)); };
  const auto any_yaw = [&]() { return static_cast<Angle16>(rng.below(65536)); };
  const auto add = [&](const Object& o) { w.objects.push_back(o); };
  // Stones, crystals, standing stones: only where nothing else stands.
  const auto add_if_free = [&](const Object& o) {
    Box b = object_box(o);
    b.min = b.min - Vec3{0.3f, 0, 0.3f};
    b.max = b.max + Vec3{0.3f, 0, 0.3f};
    b.min.y = -1e9f;
    b.max.y = 1e9f;
    for (size_t k = first; k < w.objects.size(); ++k) {
      Box other = object_box(w.objects[k]);
      other.min.y = -1e9f;
      other.max.y = 1e9f;
      if (overlaps(b, other)) return;
    }
    w.objects.push_back(o);
  };

  // The fork's signpost, and the cart by the road.
  if (west.road.points.size() > 12) {
    const Vec2 p = west.road.points[10];
    const Vec2 along = normalized(west.road.points[11] - west.road.points[9]);
    add(make(ObjectKind::Sign, p + Vec2{along.z, -along.x} * (rules::kRoadHalfWidth + 0.9f), 0));
  }
  add(make(ObjectKind::Cart, west.cart, yaw_along(west.cart_along), v8()));
  Object crate = make(ObjectKind::Crate, west.cart + Vec2{west.cart_along.x * 2.6f, west.cart_along.z * 2.6f}, 0, v8(),
                      {0.35f, 0.35f, 0.35f});
  add(crate);

  // The bridge (its height set after the others settle).
  const size_t bridge_index = w.objects.size();
  add(make(ObjectKind::Bridge, west.bridge, yaw_along(west.bridge_along), 0, {5.4f, 0.5f, 1.4f}));

  // The farm.
  const Vec2 f = west.farm_front;
  const Vec2 right = farm_axis_right(f);
  const Angle16 front_yaw = yaw_facing(f);
  const auto at = [&](float u, float v) { return farm_point(west, u, v); };
  add(make(ObjectKind::Farmhouse, at(0, 10), front_yaw, v8()));
  add(make(ObjectKind::Barn, at(16, 6), front_yaw, v8()));
  add(make(ObjectKind::Well, at(-7, 17), 0, v8()));
  for (const auto& [u, v] : {std::pair{24.0f, 12.0f}, std::pair{25.5f, 17.0f}, std::pair{8.0f, 1.0f}}) {
    const Angle16 yaw = any_yaw();
    const uint8_t variant = v8();
    add(make(ObjectKind::Haystack, at(u, v), yaw, variant));
  }
  add(make(ObjectKind::Barrel, at(10.6f, 3.5f), 0, v8()));
  add(make(ObjectKind::Barrel, at(10.6f, 4.4f), 0, v8()));
  add(make(ObjectKind::Barrel, at(-4.4f, 14.2f), 0, v8()));
  Object box = make(ObjectKind::Crate, at(21.6f, 3.2f), 0, v8(), {0.4f, 0.4f, 0.4f});
  add(box);
  // The paddock: fences around, its gate towards the yard.
  {
    const Vec2 c = paddock_center(west);
    constexpr float hu = 9.0f, hv = 7.0f;
    for (int k = 0; k < 9; ++k) {
      const float u = -hu + 1.0f + 2.0f * static_cast<float>(k);
      if (k != 4) add(make(ObjectKind::Fence, c + right * u + f * hv, yaw_along(right)));  // the gate: one left out
      add(make(ObjectKind::Fence, c + right * u - f * hv, yaw_along(right)));
    }
    for (int k = 0; k < 7; ++k) {  // the sides just outside the ends of the front and back
      const float v = -hv + 1.0f + 2.0f * static_cast<float>(k);
      add(make(ObjectKind::Fence, c + right * (hu + 0.08f) + f * v, yaw_along(f)));
      add(make(ObjectKind::Fence, c - right * (hu + 0.08f) + f * v, yaw_along(f)));
    }
  }
  // The orchard: four rows of apple trees.
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      const float ju = (rng.below(161) - 80.0f) / 100.0f, jv = (rng.below(161) - 80.0f) / 100.0f;
      const float scale = 0.9f + static_cast<float>(rng.below(31)) / 100.0f;
      const Vec2 p = orchard_center(west) + right * (-9.0f + 6.0f * static_cast<float>(i) + ju) + f * (-9.0f + 6.0f * static_cast<float>(j) + jv);
      const Angle16 yaw = any_yaw();
      const uint8_t variant = v8();
      add(make(ObjectKind::FruitTree, p, yaw, variant, {scale, scale, scale}));
    }
  }
  // The fields: wheat and cabbages, behind.
  const auto field = [&](float u, float v, float hu, float hv, bool cabbages) {
    const bool across = std::fabs(right.x) < 0.5f;  // half extents along the world's X and Z
    Object o = make(ObjectKind::Crops, at(u, v), 0, static_cast<uint8_t>((v8() & 0xFE) | (cabbages ? 1 : 0)),
                    across ? Vec3{hv, 0.5f, hu} : Vec3{hu, 0.5f, hv});
    add(o);
  };
  field(-20.0f, -18.0f, 6.0f, 4.5f, false);
  field(-6.0f, -18.0f, 5.0f, 4.5f, true);
  field(-13.0f, -28.5f, 9.0f, 3.5f, false);
  // Its gate on the road: two lanterns, a signpost; the teleporter in the yard.
  add(make(ObjectKind::Lamp, at(-3.0f, rules::kFarmRadius - 4.0f), 0));
  add(make(ObjectKind::Lamp, at(3.0f, rules::kFarmRadius - 4.0f), 0));
  add(make(ObjectKind::Sign, at(5.0f, rules::kFarmRadius - 3.0f), front_yaw));
  add(make(ObjectKind::Teleporter, at(-12.0f, 24.0f), 0, static_cast<uint8_t>(TeleporterId::Farm)));

  // The mill, its sails towards the farm.
  {
    const Vec2 to_farm = axis_of(west.farm - west.mill);
    const Object mill = make(ObjectKind::Windmill, west.mill, yaw_facing(to_farm), v8());
    add(mill);
    add(make(ObjectKind::Haystack, local_to_world(mill, 4.2f, 1.5f), 0, v8()));
    add(make(ObjectKind::Barrel, local_to_world(mill, -3.4f, -2.6f), 0, v8()));
  }

  // The shepherd's pen, its gate towards the farm.
  {
    const Vec2 c = west.pasture;
    const Vec2 g = axis_of(west.farm - c);
    const Vec2 s{g.z, -g.x};
    const float h = rules::kPenHalf;
    for (int k = 0; k < 5; ++k) {
      const float t = -h + 1.0f + 2.0f * static_cast<float>(k);
      if (k != 2) add(make(ObjectKind::Fence, c + s * t + g * h, yaw_along(s)));
      add(make(ObjectKind::Fence, c + s * t - g * h, yaw_along(s)));
      add(make(ObjectKind::Fence, c + g * t + s * (h + 0.08f), yaw_along(g)));
      add(make(ObjectKind::Fence, c + g * t - s * (h + 0.08f), yaw_along(g)));
    }
    add(make(ObjectKind::Haystack, c + s * (h + 2.5f), 0, v8()));
    add(make(ObjectKind::Barrel, c + s * (h + 1.8f) + g * 2.4f, 0, v8()));
  }

  // The burrow's mound, a few stones and bushes around.
  {
    const Object mound = make(ObjectKind::Burrow, west.burrow, yaw_facing(west.burrow_front), v8());
    add(mound);
    for (int k = 0; k < 4; ++k) {
      const Vec2 d = dir16(static_cast<Angle16>(16384 + k * 9000 + static_cast<int>(rng.below(3000))));
      const Angle16 yaw = k % 2 ? any_yaw() : Angle16{0};
      const uint8_t variant = v8();
      Object o = make(k % 2 ? ObjectKind::Bush : ObjectKind::Rock, west.burrow - d * 4.6f, yaw, variant,
                      k % 2 ? Vec3{0.7f, 0.7f, 0} : Vec3{0.6f, 0.5f, 0.7f});
      add_if_free(o);
    }
  }

  // The woods: the cabin and its woodpile, the camp, the hollow tree.
  {
    const Vec2 out = axis_of(west.cabin - west.woods);
    const Object cabin = make(ObjectKind::Cabin, west.cabin, yaw_facing(out), v8());
    add(cabin);
    add(make(ObjectKind::Woodpile, local_to_world(cabin, 0, 3.4f), cabin.yaw, v8()));
    for (const auto& [lx, lz] : {std::pair{4.6f, -2.2f}, std::pair{-4.4f, -1.4f}}) {
      const Angle16 yaw = any_yaw();
      const uint8_t variant = v8();
      Object o = make(ObjectKind::Stump, local_to_world(cabin, lx, lz), yaw, variant, {0.45f, 0.5f, 0});
      add(o);
    }
    const Object tent = make(ObjectKind::Tent, west.camp + Vec2{0, -4.5f}, yaw_facing({0, 1}), v8());
    add(tent);
    add(make(ObjectKind::Campfire, west.camp, 0, v8()));
    add(make(ObjectKind::Barrel, west.camp + Vec2{3.5f, -3.8f}, 0, v8()));
    Object c2 = make(ObjectKind::Crate, west.camp + Vec2{-3.6f, -3.6f}, 0, v8(), {0.4f, 0.4f, 0.4f});
    add(c2);
    const Vec2 door = axis_of(west.camp - west.hollow_tree);
    add(make(ObjectKind::HollowTree, west.hollow_tree, yaw_facing(door), v8()));
  }

  // The lighthouse, its door towards the land; its lantern stands on the gallery.
  {
    const Object house = make(ObjectKind::Lighthouse, west.lighthouse, yaw_facing(west.sea * -1.0f), v8());
    add(house);
    add(collider(house, 0, 0, {1.35f, 1.3f, 1.35f}, rules::kLighthouseGallery));
    for (int k = 0; k < 5; ++k) {
      const Vec2 d = dir16(static_cast<Angle16>(rng.below(65536)));
      const float r = 5.0f + static_cast<float>(rng.below(300)) / 100.0f;
      const auto yaw = static_cast<Angle16>(rng.below(4) << 14);
      const uint8_t variant = v8();
      const float sx = 0.5f + static_cast<float>(rng.below(60)) / 100.0f;
      const float sy = 0.4f + static_cast<float>(rng.below(50)) / 100.0f;
      const float sz = 0.5f + static_cast<float>(rng.below(60)) / 100.0f;
      Object o = make(ObjectKind::Rock, west.lighthouse + d * r, yaw, variant, {sx, sy, sz});
      if (distance(Vec2{o.pos.x, o.pos.z}, west.lighthouse - west.sea * 5.0f) < 3.5f) continue;  // the door
      add_if_free(o);
    }
  }

  // The circle of standing stones, a teleporter in its middle.
  for (int k = 0; k < 10; ++k) {
    const Vec2 d = dir16(static_cast<Angle16>(k * 6554 + static_cast<int>(rng.below(1200))));
    const float height = 2.6f + static_cast<float>(rng.below(180)) / 100.0f;
    const Angle16 yaw = any_yaw();
    const uint8_t variant = v8();
    add_if_free(make(ObjectKind::Menhir, west.menhirs + d * rules::kMenhirRing, yaw, variant, {1, height, 1}));
  }
  add(make(ObjectKind::Teleporter, west.menhirs, 0, static_cast<uint8_t>(TeleporterId::Menhirs)));

  // The crater: the meteorite in its middle, glowing crystals around it, stones thrown out.
  {
    const Angle16 yaw = any_yaw();
    const uint8_t variant = v8();
    add(make(ObjectKind::Meteorite, west.crater, yaw, variant, {1.6f, 1.6f, 1.6f}));
  }
  for (int k = 0; k < 9; ++k) {
    const Vec2 d = dir16(static_cast<Angle16>(k * 7282 + static_cast<int>(rng.below(2000))));
    const float r = 5.0f + static_cast<float>(rng.below(700)) / 100.0f;
    const float s = 0.5f + static_cast<float>(rng.below(70)) / 100.0f;
    const Angle16 yaw = any_yaw();
    const uint8_t variant = v8();
    add_if_free(make(ObjectKind::Crystal, west.crater + d * r, yaw, variant, {s, s, s}));
  }
  settle_objects(w, first);
  // The bridge's deck: a little above the banks at both of its ends.
  Object& bridge = w.objects[bridge_index];
  {
    const Box b = object_box(bridge);
    float lowest = 1e30f, banks = -1e30f;
    for (float x = b.min.x; x <= b.max.x + 0.01f; x += 0.5f) {
      for (float z = b.min.z; z <= b.max.z + 0.01f; z += 0.5f) lowest = std::min(lowest, w.ground.height(x, z));
    }
    for (const float t : {-1.0f, 1.0f}) {
      const Vec2 end = west.bridge + west.bridge_along * (t * 5.2f);
      banks = std::max(banks, w.ground.height(end.x, end.z));
    }
    const float deck = banks + 0.22f;
    bridge.pos.y = lowest - 0.1f;
    bridge.size.y = std::max(0.3f, (deck - bridge.pos.y) * 0.5f);
  }
  return first;
}

// ---- The villagers of the west ------------------------------------------------------------

namespace {

bool farm_spot_free(const World& w, Body& b, Vec2 want) {
  const BodyShape& shape = species_info(Species::Villager).shape;
  for (int k = 0; k < 40; ++k) {
    const Vec2 d = dir16(static_cast<Angle16>(k * 10923));
    const float r = k == 0 ? 0.0f : 0.8f + 0.35f * static_cast<float>(k / 6);
    const Vec2 p = want + d * r;
    b.pos = {p.x, w.ground.height(p.x, p.z), p.z};
    if (!inside_any_object(w, b, shape)) return true;
  }
  b.pos = {want.x, w.ground.height(want.x, want.z), want.z};
  return false;
}

}  // namespace

void add_west_npcs(World& w) {
  const WestLayout& west = w.west;
  if (!west.valid() || w.ground.empty() || inside(w)) return;
  const auto object_at = [&](ObjectKind kind, Vec2 near) -> const Object* {
    for (size_t k = w.objects.size() - std::min<size_t>(w.wild_objects, w.objects.size()); k < w.objects.size(); ++k) {
      const Object& o = w.objects[k];
      if (o.kind == kind && distance({o.pos.x, o.pos.z}, near) < 3.0f) return &o;
    }
    return nullptr;
  };
  const auto front_of_object = [&](ObjectKind kind, Vec2 near, float d) {
    const Object* o = object_at(kind, near);
    if (!o) return near;
    return local_to_world(*o, 0, -d);
  };
  struct Who {
    uint8_t npc;
    Vec2 at;
  };
  const Who people[] = {
      {npc::kAnselme, west.cart + west.cart_along * -2.6f},
      {npc::kCapucine, farm_point(west, 3.0f, 19.0f)},
      {npc::kViolette, farm_point(west, -4.5f, 15.5f)},
      {npc::kBasile, west.pasture + axis_of(west.farm - west.pasture) * (rules::kPenHalf + 2.5f)},
      {npc::kFirmin, front_of_object(ObjectKind::Cabin, west.cabin, 4.0f)},
      {npc::kRomane, west.camp + Vec2{2.4f, 1.5f}},
      {npc::kLucien, front_of_object(ObjectKind::Windmill, west.mill, 4.5f)},
      {npc::kYvon, front_of_object(ObjectKind::Lighthouse, west.lighthouse, 4.0f)},
  };
  for (const Who& who : people) {
    if (npc_index(w, who.npc) >= 0) continue;
    Creature c;
    c.species = Species::Villager;
    c.npc = who.npc;
    c.variant = static_cast<uint8_t>(mix(w.seed ^ (0x4E50430000ULL + who.npc)) & 0xFF);
    farm_spot_free(w, c.body, who.at);
    c.home = {c.body.pos.x, c.body.pos.z};
    add_creature(w, c);
  }
}

// ---- Spots -------------------------------------------------------------------------------

std::array<Vec2, rules::kGoods> goods_spots(const World& w) {
  std::array<Vec2, rules::kGoods> spots{};
  const WestLayout& west = w.west;
  for (uint32_t k = 0; k < rules::kGoods; ++k) {
    Vec2 best = west.cart;
    for (int t = 0; t < 16; ++t) {
      const auto a = static_cast<Angle16>(static_cast<int>(hash01(w.seed, 300 + k * 32 + static_cast<uint32_t>(t)) * 65536.0f));
      const float r = 9.0f + 13.0f * hash01(w.seed, 400 + k * 32 + static_cast<uint32_t>(t));
      const Vec2 p = west.cart + dir16(static_cast<Angle16>(a + k * 21845)) * r;
      if (distance_to_road(west.road, p.x, p.z, 4.0f) < 3.0f) continue;
      if (in_lake(w.village.lake, p.x, p.z, 3.0f)) continue;
      if (!w.ground.empty() && distance_to_stream(west.stream, p.x, p.z, 6.0f) < 5.0f) continue;
      Body b;
      b.pos = {p.x, w.ground.height(p.x, p.z), p.z};
      if (!inside(w) && inside_any_object(w, b, {0.5f, 0.6f})) continue;
      best = p;
      break;
    }
    spots[k] = best;
  }
  return spots;
}

std::array<Vec2, rules::kLambs> lamb_spots(const World& w) {
  std::array<Vec2, rules::kLambs> spots{};
  const WestLayout& west = w.west;
  for (uint32_t k = 0; k < rules::kLambs; ++k) {
    Vec2 best = west.pasture;
    for (int t = 0; t < 24; ++t) {
      const auto a = static_cast<Angle16>(k * 21845 + static_cast<int>(hash01(w.seed, 500 + k * 32 + static_cast<uint32_t>(t)) * 16000.0f));
      const float r = 34.0f + 26.0f * hash01(w.seed, 600 + k * 32 + static_cast<uint32_t>(t));
      const Vec2 p = west.pasture + dir16(a) * r;
      if (w.ground.empty()) break;
      if (std::fabs(p.x) > rules::kWorldHalfSize - 10 || std::fabs(p.z) > rules::kWorldHalfSize - 10) continue;
      if (w.ground.height(p.x, p.z) < rules::kSeaLevel + 0.8f) continue;
      if (distance_to_stream(west.stream, p.x, p.z, 7.0f) < 6.0f) continue;
      best = p;
      break;
    }
    spots[k] = best;
  }
  return spots;
}

bool mill_sails(const World& w, Vec3& hub, Vec2& facing) {
  if (!w.west.valid()) return false;
  const std::vector<Object>& objects = inside(w) ? w.outdoors.objects : w.objects;
  for (size_t k = objects.size(); k-- > 0;) {
    const Object& o = objects[k];
    if (o.kind != ObjectKind::Windmill) continue;
    const SinCos sc = sincos16(o.yaw);
    facing = {-sc.sin, -sc.cos};
    hub = {o.pos.x + facing.x * 2.9f, o.pos.y + rules::kMillHub, o.pos.z + facing.z * 2.9f};
    return true;
  }
  return false;
}

bool mill_turning(const World& w) {
  return (w.quests[static_cast<size_t>(QuestId::Mill)].progress & kMillTurning) != 0;
}

uint32_t raiders_left(const World& w) {
  const QuestState& q = w.quests[static_cast<size_t>(QuestId::Orchard)];
  if (q.stage != QuestStage::Active) return 0;
  return q.progress >= rules::kRaiders ? 0 : rules::kRaiders - q.progress;
}

bool farm_open(const World& w) {
  return w.quests[static_cast<size_t>(QuestId::Delivery)].stage == QuestStage::Done;
}

std::vector<Vec3> owl_perches(const World& w) {
  std::vector<Vec3> perches;
  for (size_t k = w.objects.size() - std::min<size_t>(w.wild_objects, w.objects.size()); k < w.objects.size(); ++k) {
    const Object& o = w.objects[k];
    if (o.kind == ObjectKind::HollowTree) {
      for (int a = 0; a < 5; ++a) {
        const Vec2 d = dir16(static_cast<Angle16>(a * 13107 + 3000));
        perches.push_back({o.pos.x + d.x * 3.4f, o.pos.y + rules::kHollowTreeTop, o.pos.z + d.z * 3.4f});
      }
    } else if (o.kind == ObjectKind::Cabin) {
      perches.push_back({o.pos.x, o.pos.y + 4.15f, o.pos.z});
    }
  }
  return perches;
}

// ---- Keeping creatures --------------------------------------------------------------------

namespace {

int cell_of(Vec2 p) {
  const int n = rules::kPopulationCells;
  const int i = std::clamp(static_cast<int>((p.x + rules::kWorldHalfSize) / rules::kPopulationCell), 0, n - 1);
  const int j = std::clamp(static_cast<int>((p.z + rules::kWorldHalfSize) / rules::kPopulationCell), 0, n - 1);
  return i * n + j;
}
bool lived_in(const World& w, Vec2 p) { return w.population.active[static_cast<size_t>(cell_of(p))] != 0; }
uint16_t visits(const World& w, Vec2 p) { return w.population.visits[static_cast<size_t>(cell_of(p))]; }

// On free ground around `c` (rings going out).
bool free_spot(const World& w, Creature& c, Vec2 around, int start = 0) {
  const BodyShape& shape = species_info(c.species).shape;
  for (int k = start; k < start + 64; ++k) {
    const Vec2 d = dir16(static_cast<Angle16>(k * 10923 + (k / 6) * 3000));
    const float r = k == 0 ? 0.0f : 1.4f + 0.9f * static_cast<float>(k / 6);
    const Vec2 p = around + d * r;
    c.body.pos = {p.x, w.ground.height(p.x, p.z), p.z};
    if (!inside_any_object(w, c.body, shape)) return true;
  }
  c.body.pos = {around.x, w.ground.height(around.x, around.z), around.z};
  return false;
}

void keep_flock(World& w) {
  const WestLayout& west = w.west;
  if (!lived_in(w, west.pasture)) return;
  const uint8_t home = w.quests[static_cast<size_t>(QuestId::Shepherd)].progress & kLambsHome;
  const auto wanted = static_cast<size_t>(rules::kFlock + std::popcount(home));
  size_t have = 0;
  for (const Creature& c : w.creatures) have += c.tag == Tag::Flock ? 1 : 0;
  Rng rng(w.seed ^ 0x464C4F434BULL, visits(w, west.pasture));  // "FLOCK"
  for (size_t k = have; k < wanted; ++k) {
    Creature c;
    c.species = Species::Sheep;
    c.genes = roll_genes(Species::Sheep, Zone::Hills, rng);
    c.genes.chromatic = c.genes.elemental = false;
    c.genes.form = Form::Normal;
    c.level = rules::kFlockLevel;
    c.tag = Tag::Flock;
    free_spot(w, c, west.pasture, static_cast<int>(k) * 3 + 1);
    c.home = west.pasture;
    c.timer = static_cast<uint16_t>(rng.below(3 * rules::kTicksPerSecond));
    add_creature(w, c);
  }
}

void keep_lambs(World& w) {
  const QuestState& q = w.quests[static_cast<size_t>(QuestId::Shepherd)];
  if (q.stage != QuestStage::Active) return;
  const auto spots = lamb_spots(w);
  for (uint32_t k = 0; k < rules::kLambs; ++k) {
    if (q.progress & (1u << k)) continue;
    bool there = false;
    for (const Creature& c : w.creatures) there = there || (c.tag == Tag::Lamb && c.variant == k);
    if (there || !lived_in(w, spots[k])) continue;
    Rng rng(w.seed ^ 0x4C414D42ULL, k * 1000 + visits(w, spots[k]));  // "LAMB"
    Creature c;
    c.species = Species::Sheep;
    c.genes = roll_genes(Species::Sheep, Zone::Hills, rng);
    c.genes.chromatic = c.genes.elemental = false;
    c.genes.form = Form::Micro;
    c.genes.allele[0] = 1;  // only females are micro
    c.level = rules::kLambLevel;
    c.tag = Tag::Lamb;
    c.variant = static_cast<uint8_t>(k);
    free_spot(w, c, spots[k]);
    c.home = {c.body.pos.x, c.body.pos.z};
    add_creature(w, c);
  }
}

void keep_raiders(World& w) {
  const uint32_t left = raiders_left(w);
  const Vec2 orchard = orchard_center(w.west);
  if (left == 0 || !lived_in(w, orchard)) return;
  for (const Creature& c : w.creatures) {
    if (c.tag == Tag::Raider) return;  // they are there
  }
  Rng rng(w.seed ^ 0x5241494445525321ULL, visits(w, orchard));  // "RAIDERS!"
  for (uint32_t k = 0; k < left; ++k) {
    Creature c;
    c.species = Species::Raccoon;
    c.genes = roll_genes(Species::Raccoon, Zone::Farm, rng);
    c.genes.chromatic = c.genes.elemental = false;
    const bool leader = k == 0 && left == rules::kRaiders;
    c.genes.form = leader ? Form::Alpha : Form::Normal;
    c.level = leader ? rules::kRaiderLeaderLevel : rules::kRaiderLevel;
    c.tag = Tag::Raider;
    free_spot(w, c, orchard + dir16(static_cast<Angle16>(k * 13107)) * 3.0f);
    c.home = orchard;
    c.timer = static_cast<uint16_t>(rng.below(2 * rules::kTicksPerSecond));
    add_creature(w, c);
  }
}

void keep_boarders(World& w) {
  const Vec2 paddock = paddock_center(w.west);
  const size_t wanted = lived_in(w, paddock) ? std::min<size_t>(w.farm.size(), rules::kBoardersShown) : 0;
  // As they are, or drawn again.
  bool same = true;
  size_t have = 0;
  for (const Creature& c : w.creatures) {
    if (c.tag != Tag::Boarder) continue;
    ++have;
    same = same && c.variant < w.farm.size() && w.farm[c.variant].species == c.species && w.farm[c.variant].genes == c.genes;
  }
  if (same && have == wanted) return;
  for (size_t i = w.creatures.size(); i-- > 1;) {
    if (w.creatures[i].tag == Tag::Boarder && foe_slot(w, i) < 0) remove_creature(w, i);
  }
  for (size_t k = 0; k < wanted; ++k) {
    const Monster& m = w.farm[k];
    Creature c;
    c.species = m.species;
    c.genes = m.genes;
    c.level = static_cast<uint8_t>(std::min<uint16_t>(m.level, 255));
    c.tag = Tag::Boarder;
    c.variant = static_cast<uint8_t>(k);
    free_spot(w, c, paddock, static_cast<int>(k) * 2);
    c.home = paddock;
    c.timer = static_cast<uint16_t>(k * 20);
    add_creature(w, c);
  }
}

}  // namespace

void keep_west(World& w) {
  if (!w.west.valid() || w.ground.empty() || inside(w)) return;
  keep_flock(w);
  keep_lambs(w);
  keep_raiders(w);
  keep_boarders(w);
}

// ---- Each tick -----------------------------------------------------------------------------

namespace {

constexpr uint16_t kAggroRest = 3 * rules::kTicksPerSecond;  // after a rescue, a moment of peace

float flat2(Vec3 a, Vec3 b) {
  const float dx = a.x - b.x, dz = a.z - b.z;
  return dx * dx + dz * dz;
}

}  // namespace

void west_step(World& w) {
  if (!w.west.valid() || inside(w) || w.ground.empty()) return;
  // The merchant's bundles, picked up as the player walks by.
  QuestState& merchant = w.quests[static_cast<size_t>(QuestId::Merchant)];
  if (merchant.stage == QuestStage::Active && (merchant.progress & kGoodsFound) != kGoodsFound && w.tick % 6 == 0) {
    const auto spots = goods_spots(w);
    for (uint32_t k = 0; k < rules::kGoods; ++k) {
      if (merchant.progress & (1u << k)) continue;
      const Vec3 at{spots[k].x, w.ground.height(spots[k].x, spots[k].z), spots[k].z};
      if (flat2(at, w.player.pos) < rules::kGoodsReach * rules::kGoodsReach && std::fabs(at.y - w.player.pos.y) < 1.5f) {
        merchant.progress = static_cast<uint8_t>(merchant.progress | (1u << k));
        ++w.bag[static_cast<size_t>(Item::Goods)];
      }
    }
  }
  // The lambs: found, they follow; in the pen, they join the flock.
  QuestState& shepherd = w.quests[static_cast<size_t>(QuestId::Shepherd)];
  for (Creature& c : w.creatures) {
    if (c.tag != Tag::Lamb) continue;
    if (c.mind != Mind::Follow && flat2(c.body.pos, w.player.pos) < rules::kLambFollow * rules::kLambFollow) {
      c.mind = Mind::Follow;
      c.timer = 0;
    }
    const float dx = c.body.pos.x - w.west.pasture.x, dz = c.body.pos.z - w.west.pasture.z;
    if (std::fabs(dx) < rules::kPenHalf - 0.3f && std::fabs(dz) < rules::kPenHalf - 0.3f) {
      c.tag = Tag::Flock;
      c.home = w.west.pasture;
      c.mind = Mind::Idle;
      c.timer = rules::kTicksPerSecond;
      if (c.variant < rules::kLambs) shepherd.progress = static_cast<uint8_t>(shepherd.progress | (1u << c.variant));
    }
  }
  // An aggressive monster close by attacks.
  if (w.battle.phase != BattlePhase::None || (w.rescue_tick > 0 && w.tick - w.rescue_tick < kAggroRest)) return;
  for (size_t i = 1; i < w.creatures.size(); ++i) {
    const Creature& c = w.creatures[i];
    if (c.tag != Tag::Raider || c.mind == Mind::KnockedOut) continue;
    if (flat2(c.body.pos, w.player.pos) > rules::kAggroDistance * rules::kAggroDistance ||
        std::fabs(c.body.pos.y - w.player.pos.y) > 2.5f) {
      continue;
    }
    force_battle(w, i);
    return;
  }
}

void rescue(World& w) {
  const WestLayout& west = w.west;
  if (!west.valid() || inside(w)) return;
  const Vec2 s = farm_rescue_spot(west);
  w.teleport_from = w.player.pos;
  w.teleport_tick = w.tick;
  w.player.pos = {s.x, w.ground.height(s.x, s.z), s.z};
  w.player.velocity_y = 0;
  w.player.on_ground = true;
  w.player.facing_x = -west.farm_front.x;
  w.player.facing_z = -west.farm_front.z;
  if (w.active >= 0 && !w.creatures.empty()) {
    const Monster& m = w.team[static_cast<size_t>(w.active)];
    w.went_in = {w.tick, false, m.species, m.genes, w.creatures[0].body};
    w.active = -1;
    w.creatures[0].mind = Mind::Idle;
  }
  if (!w.creatures.empty()) w.creatures[0].body.pos = w.player.pos;
  w.rescue_tick = w.tick;
  update_population(w);
}

bool push_sails(World& w, Vec3 p) {
  QuestState& q = w.quests[static_cast<size_t>(QuestId::Mill)];
  if (q.stage != QuestStage::Active || (q.progress & kMillTurning)) return false;
  Vec3 hub;
  Vec2 facing;
  if (!mill_sails(w, hub, facing)) return false;
  const Vec3 d = p - hub;
  const float along = d.x * facing.x + d.z * facing.z;
  const float all2 = d.x * d.x + d.y * d.y + d.z * d.z;
  const float across = std::sqrt(std::max(0.0f, all2 - along * along));
  if (std::fabs(along) > 1.0f || across > rules::kSailReach + 0.4f) return false;
  q.progress = static_cast<uint8_t>(q.progress | kMillTurning);
  return true;
}

void raider_down(World& w, const Creature& c) {
  if (c.tag != Tag::Raider) return;
  QuestState& q = w.quests[static_cast<size_t>(QuestId::Orchard)];
  if (q.stage == QuestStage::Active && q.progress < rules::kRaiders) ++q.progress;
}

void send_to_farm(World& w, const Monster& m) {
  if (w.farm.size() < rules::kMaxFarm) w.farm.push_back(m);
  QuestState& q = w.quests[static_cast<size_t>(QuestId::Transfer)];
  if (q.stage == QuestStage::Active) q.progress = static_cast<uint8_t>(q.progress | kSentToFarm);
}

const ItemInfo& item_info(Item item) {
  static const ItemInfo kItems[] = {
      {"coin", false, 0},
      {"manaPotion", false, rules::kPotionPrice},
      {"strangeStone", true, 0},
      {"goods", true, 0},
  };
  static_assert(std::size(kItems) == kItemCount);
  const auto i = static_cast<size_t>(item);
  return kItems[i < kItemCount ? i : 0];
}

void west_command(World& w, const Input& in) {
  switch (in.command) {
    case Command::UseItem: {
      if (in.target != static_cast<uint16_t>(Item::ManaPotion)) break;
      uint32_t& n = w.bag[static_cast<size_t>(Item::ManaPotion)];
      const uint16_t most = wizard_stats(w.wizard).mana;
      if (n == 0 || w.wizard.mana >= most) break;
      --n;
      w.wizard.mana = static_cast<uint16_t>(std::min<uint32_t>(most, uint32_t{w.wizard.mana} + rules::kPotionMana));
      break;
    }
    case Command::Discard: {
      const size_t item = in.target % 256;
      if (item >= kItemCount || item_info(static_cast<Item>(item)).quest) break;
      const uint32_t count = in.target / 256;
      uint32_t& n = w.bag[item];
      n = count == 0 ? 0 : n - std::min(n, count);
      break;
    }
    case Command::Buy: {
      if (in.target >= kItemCount || w.battle.phase != BattlePhase::None) break;
      const ItemInfo& info = item_info(static_cast<Item>(in.target));
      uint32_t& coins = w.bag[static_cast<size_t>(Item::Coin)];
      if (info.price == 0 || coins < info.price || !near_npc(w, npc::kAnselme) ||
          w.quests[static_cast<size_t>(QuestId::Merchant)].stage != QuestStage::Done) {
        break;
      }
      coins -= info.price;
      ++w.bag[in.target];
      break;
    }
    case Command::FarmSwap: {
      if (!farm_open(w) || !near_npc(w, npc::kCapucine) || w.battle.phase != BattlePhase::None) break;
      const size_t slot = in.target % 8, kept = in.target / 8;
      QuestState& q = w.quests[static_cast<size_t>(QuestId::Transfer)];
      const auto took = [&]() {
        if (q.stage == QuestStage::Active) q.progress = static_cast<uint8_t>(q.progress | kTookFromFarm);
      };
      // The one out of the crystal goes back in first.
      const auto recall = [&](size_t s) {
        if (static_cast<int>(s) != w.active || w.creatures.empty()) return;
        const Monster& m = w.team[s];
        w.went_in = {w.tick, false, m.species, m.genes, w.creatures[0].body};
        w.active = -1;
        w.creatures[0].mind = Mind::Idle;
      };
      if (slot < w.team.size() && kept < w.farm.size()) {  // swapped
        recall(slot);
        std::swap(w.team[slot], w.farm[kept]);
        took();
      } else if (slot == rules::kTeamNone && kept < w.farm.size() && w.team.size() < rules::kTeamSize) {  // taken back
        w.team.push_back(w.farm[kept]);
        w.farm.erase(w.farm.begin() + static_cast<std::ptrdiff_t>(kept));
        took();
      } else if (slot < w.team.size() && kept == rules::kFarmEmpty && w.team.size() > 1 && w.farm.size() < rules::kMaxFarm) {
        recall(slot);  // left at the farm
        w.farm.push_back(w.team[slot]);
        w.team.erase(w.team.begin() + static_cast<std::ptrdiff_t>(slot));
        if (w.active > static_cast<int>(slot)) --w.active;
      }
      break;
    }
    default: break;
  }
}

}  // namespace monstres
