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
                            const ForestLayout& forest, const CaveLayout& cave, const CastleLayout&) {
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

  // ---- The terrace and the river -------------------------------------------------
  // Inland (north and east) the land is higher: a terrace, its edge a line of
  // cliffs here, slopes there (the relief draws it: heightmap.cpp), the cave in
  // a spur of it. The river (the west's stream) comes from the Forêt Profonde
  // in the east along the terrace, falls over its edge by the cave into a pool,
  // then runs west, south under the road's bridge, and out to the sea.
  const Vec2 east{1, 0};
  const Vec2 cave_at = cave.radius > 0 ? cave.center : Vec2{path_x_at(path, -110.0f) - 62.0f, -110.0f};
  const bool cave_west = cave_at.x < path_x_at(path, cave_at.z);
  {
    const Vec2 C = cave_at;
    const auto j = [&](uint64_t k, float a) { return (hash01(seed, 900 + k) - 0.5f) * 2 * a; };
    std::vector<Vec2> c{{C.x - 175 + j(1, 20), -rules::kReliefHalfSize - 90.0f},
                        {C.x - 150 + j(8, 20), std::max(C.z - 430.0f, -rules::kReliefHalfSize + 20.0f)},
                        {C.x - 118 + j(2, 15), C.z - 230 + j(3, 20)},
                        {C.x - 95 + j(4, 8), C.z - 115 + j(5, 10)},
                        {C.x - 62 + j(6, 5), C.z - 55 + j(7, 5)}};
    // The spur around the cave: its steep face just before the mouth (on the
    // side the cave looks to), its long gentle side on the other.
    const float k = cave.radius > 0 && cave.facing.x < 0 ? -1.0f : 1.0f;
    std::vector<Vec2> spur{{-38, -14}, {-24, 10}, {-4, 23}, {16, 12}, {21, -4}, {24, -24}, {44, -36}};
    if (k < 0) std::reverse(spur.begin(), spur.end());
    for (const Vec2& p : spur) c.push_back({C.x + k * p.x, C.z + p.z});
    for (float x = C.x + 92; x < rules::kReliefHalfSize + 90; x += 55) {
      c.push_back({x, std::min(C.z - 40.0f + j(static_cast<uint64_t>(x + 2000), 15), -128.0f)});
    }
    w.scarp.points = spline(c, 2.0f);
    make_chunks(w.scarp.points, w.scarp.chunks);
    float best = 1e30f;
    for (const Vec2& p : w.scarp.points) {
      const float d = distance_to_path(path, p.x, p.z, best);
      if (d < best) {
        best = d;
        w.ramp = p;
      }
    }
  }
  // The waterfall: along the edge westwards from the cave's mouth, well away
  // from it (the spur between them); the pool below, the hidden cleft at the
  // cliff's foot right behind the falling water.
  {
    const Vec2 mouth = cave.radius > 0 ? cave.mouth : cave_at;
    const auto& sp = w.scarp.points;
    size_t best = 1;
    for (size_t i = 1; i + 1 < sp.size(); ++i) {
      if (distance(sp[i], mouth) < distance(sp[best], mouth)) best = i;
    }
    const auto westwards = [&](size_t i, float meters) {
      float gone = 0;
      while (i > 3 && gone < meters) {
        gone += distance(sp[i], sp[i - 1]);
        --i;
      }
      return i;
    };
    best = westwards(best, rules::kFallsFromMouth);
    if (!cave_west) {
      // The cave east of the path, little room between them: the falls on
      // the other side of the path, past where it goes up the terrace.
      size_t ramp = 1;
      for (size_t i = 1; i + 1 < sp.size(); ++i) {
        if (distance(sp[i], w.ramp) < distance(sp[ramp], w.ramp)) ramp = i;
      }
      best = std::min(best, westwards(ramp, 110.0f));
    }
    const Vec2 d = normalized(sp[std::min(best + 2, sp.size() - 1)] - sp[best - 1]);
    w.falls = sp[best];
    w.falls_out = {-d.z, d.x};  // the valley's side
    w.cove = w.falls + w.falls_out * 9.0f;
    w.hidden = w.falls + w.falls_out * rules::kHiddenOut;
  }
  {
    const float x_cross = std::min(w.farm.x + rules::kFarmRadius + 30.0f, -154.0f);
    const Vec2 F = w.falls, out = w.falls_out, lat{-out.z, out.x};
    std::vector<Vec2> pins;  // where the river is held (no bends added there)
    // Up on the terrace: its way in on the relief's edge, towards the Forêt Profonde.
    Vec2 deep{850.0f, -470.0f};
    for (const Landmark& l : continent_landmarks()) {
      if (l.region == Region::Forest) deep = l.at;
    }
    std::vector<Vec2> anchors;  // from upstream, down to the falls
    Vec2 up_cross{};
    const bool falls_west = F.x < path_x_at(path, F.z);
    if (falls_west) {  // across the path up on the terrace (north of the falls)
      float z = std::min(cave_at.z - 75.0f, F.z - 45.0f);
      while (z > -rules::kReliefHalfSize + 80 && scarp_side(w, path_x_at(path, z), z) > -25.0f) z -= 10.0f;
      up_cross = {path_x_at(path, z), z};
      // Across it and well away from it (not along it on the other side).
      Vec2 away = up_cross - east * 30.0f;  // (on the terrace, not by its edge)
      for (int k = 0; k < 12 && scarp_side(w, away.x, away.z) > -22.0f; ++k) away = away - out * 4.0f;
      anchors = {up_cross + east * 12.0f, up_cross - east * 12.0f, away};
      if (cave_west) anchors.push_back({cave_at.x + 12, cave_at.z - 54});  // behind the cave
      pins.push_back(up_cross);
    } else {
      anchors = {{cave_at.x + 30, cave_at.z - 64}, {cave_at.x + 4, cave_at.z - 52}};
    }
    // Straight to the lip over its last meters (less of them by the path).
    const float back = std::clamp(distance_to_path(path, F.x, F.z, 60.0f) - 14.0f, 12.0f, 26.0f);
    anchors.push_back(F - out * back + lat * (0.15f * back));
    anchors.push_back(F - out * (0.42f * back));
    const Vec2 to_deep = normalized(deep - anchors.front());
    const float edge = rules::kReliefHalfSize + 6.0f;
    Vec2 spring = anchors.front() + to_deep * ((edge - anchors.front().x) / std::max(to_deep.x, 0.2f));
    spring.z = std::clamp(spring.z + (hash01(seed, 90) - 0.5f) * 60.0f, -rules::kReliefHalfSize + 60.0f, -200.0f);
    std::vector<Vec2> c{spring + east * 14.0f, spring};
    pins.push_back(spring);
    // Along the lowest ground of the terrace (away from its edge), a bit at random.
    Vec2 at = spring;
    for (int step = 0; step < 80 && distance(at, anchors.front()) > 50.0f; ++step) {
      const Vec2 to = normalized(anchors.front() - at);
      float best = 1e30f;
      Vec2 next = at + to * 32.0f;
      for (int k = -3; k <= 3; ++k) {
        const float a = 0.2f * static_cast<float>(k);
        const Vec2 dd{to.x * std::cos(a) - to.z * std::sin(a), to.x * std::sin(a) + to.z * std::cos(a)};
        const Vec2 p = at + dd * 32.0f;
        float cost = natural_height(seed, p.x, p.z) + 1.2f * std::fabs(a) + 2.5f * hash01(seed, 700 + static_cast<uint64_t>(step * 8 + k + 3));
        if (scarp_side(w, p.x, p.z) > -30.0f) cost += 1000.0f;  // on the terrace, away from its edge
        if (forest.radius_x > 0 && outline_distance(forest.center, forest.radius_x, forest.radius_z, kForestOutline, p.x, p.z) < 1.3f) {
          cost += 1000.0f;
        }
        if (in_village(village, p.x, p.z, 30.0f) || in_cave_hill(cave, p.x, p.z, 15.0f)) cost += 1000.0f;
        if (cost < best) {
          best = cost;
          next = p;
        }
      }
      c.push_back(next);
      at = next;
    }
    for (const Vec2& a : anchors) c.push_back(a);
    // Over the edge, the pool, then down the valley.
    c.push_back(F);
    pins.push_back(F);
    c.push_back(w.cove);
    pins.push_back(w.cove);
    c.push_back(w.cove + out * 11.0f + lat * 2.0f);
    if (!falls_west) {  // back west under the path
      const float z = w.cove.z + 16.0f;
      const Vec2 low{path_x_at(path, z), z};
      c.push_back(low + east * 10.0f);
      c.push_back(low - east * 10.0f);
      pins.push_back(low);
    }
    const Vec2 last = c.back();
    c.push_back({0.55f * last.x + 0.45f * x_cross, last.z + 10.0f + 10.0f * hash01(seed, 101)});
    constexpr float kSouth = 205.0f;
    for (float z = last.z + 45.0f; z <= kSouth; z += 30.0f) {
      float x = x_cross + (hash01(seed, 100 + static_cast<uint64_t>(z + 1000)) - 0.5f) * 10.0f;
      if (std::fabs(z - road_line) < 34.0f) x = x_cross;
      if (std::fabs(z - w.farm.z) < rules::kFarmRadius + 24.0f) x = std::max(x, w.farm.x + rules::kFarmRadius + 18.0f);
      c.push_back({x, z});
    }
    pins.push_back({x_cross, road_line});
    // Its mouth: where the sea comes closest, south of the woods.
    float mouth_z = kSouth + 15.0f, nearest = -1e30f;
    for (float z = kSouth + 5.0f; z <= kSouth + 125.0f; z += 10.0f) {
      const float x = coast_x(seed, z);
      if (x > nearest) {
        nearest = x;
        mouth_z = z;
      }
    }
    const Vec2 turn = c.back();
    c.push_back({turn.x - 16.0f, 0.5f * (turn.z + mouth_z) + 6.0f});  // a bend, not a corner
    for (int k = 1; k < 24; ++k) {
      const float x = turn.x - 30.0f * static_cast<float>(k) - 10.0f;
      c.push_back({x, mouth_z});
      if (continent_height(seed, x, mouth_z) < rules::kSeaLevel - 3.0f || x < -rules::kReliefHalfSize + 40.0f) break;
    }
    {  // its very end where the sea is deepest around there (not on a shoal)
      Vec2& end = c.back();
      float deepest = continent_height(seed, end.x, end.z);
      const float z0 = end.z;
      for (float z = z0 - 40.0f; z <= z0 + 40.0f; z += 5.0f) {
        const float h = continent_height(seed, end.x, z);
        if (h < deepest - 0.3f) {
          deepest = h;
          end.z = z;
        }
      }
    }
    pins.push_back(c.back());
    std::vector<Vec2> pts = spline(c, 1.0f);

    // Meanders: the line swings from side to side, in bends of changing size,
    // held still at the pins (the falls, the pool, the bridges, its ends).
    {
      const size_t n = pts.size();
      std::vector<float> s(n, 0.0f), pin_s;
      for (size_t i = 1; i < n; ++i) s[i] = s[i - 1] + distance(pts[i], pts[i - 1]);
      for (const Vec2& p : pins) {
        size_t best = 0;
        for (size_t i = 0; i < n; ++i) {
          if (distance(pts[i], p) < distance(pts[best], p)) best = i;
        }
        pin_s.push_back(s[best]);
      }
      const float falls_s = pin_s[falls_west ? 2 : 1];
      std::vector<Vec2> moved(pts);
      float phase = 6.2831853f * hash01(seed, 102);
      for (size_t i = 0; i < n; ++i) {
        if (i > 0) phase += 6.2831853f * (s[i] - s[i - 1]) / (70.0f + 80.0f * value_noise(seed + 120, s[i] / 260.0f, 0, 1.0f));
        float held = 1e9f;
        for (const float p : pin_s) held = std::min(held, std::fabs(s[i] - p));
        const float e = [&] {
          const float t = std::clamp((held - 12.0f) / 30.0f, 0.0f, 1.0f);
          return t * t * (3 - 2 * t);
        }();
        const float amp = 5.0f + 14.0f * value_noise(seed + 121, s[i] / 210.0f, 0.5f, 1.0f);
        float off = e * amp * (std::sin(phase) + 0.35f * std::sin(2.3f * phase + 1.1f));
        const Vec2 t = normalized(pts[std::min(i + 15, n - 1)] - pts[i >= 15 ? i - 15 : 0]);
        const Vec2 nrm{-t.z, t.x};
        for (int tries = 0; tries < 3; ++tries) {
          const Vec2 p = pts[i] + nrm * off;
          const bool upstream = s[i] < falls_s;
          if ((upstream && scarp_side(w, p.x, p.z) > -18.0f) || (!upstream && scarp_side(w, p.x, p.z) < 6.0f && s[i] > falls_s + 25) ||
              in_farm(w, p.x, p.z, 14.0f) || in_village(village, p.x, p.z, 20.0f) ||
              (held > 25.0f && distance_to_path(path, p.x, p.z, 14.0f) < 14.0f)) {  // not along the path
            off *= 0.3f;
          }
        }
        moved[i] = pts[i] + nrm * off;
      }
      pts = std::move(moved);
    }
    w.stream.points = std::move(pts);
    make_chunks(w.stream.points, w.stream.chunks);
    // Its shape along it: narrow up on the terrace, wider down the valley,
    // a pool below the falls, narrower under the bridges, an estuary at the sea.
    {
      Stream& st = w.stream;
      const size_t n = st.points.size();
      std::vector<float> s(n, 0.0f);
      for (size_t i = 1; i < n; ++i) s[i] = s[i - 1] + distance(st.points[i], st.points[i - 1]);
      const auto closest = [&](Vec2 p) {
        size_t best = 0;
        for (size_t i = 0; i < n; ++i) {
          if (distance(st.points[i], p) < distance(st.points[best], p)) best = i;
        }
        return best;
      };
      st.falls = closest(F);
      const float sF = s[st.falls], sK = s[closest(w.cove)], sEnd = s[n - 1];
      std::vector<float> narrow;
      for (const Vec2& p : pins) narrow.push_back(s[closest(p)]);
      const auto smoothstep = [](float e0, float e1, float x) {
        const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
        return t * t * (3 - 2 * t);
      };
      st.width.assign(n, 0);
      st.depth.assign(n, 0);
      st.bend.assign(n, 0);
      for (size_t i = 0; i < n; ++i) {
        const Vec2 a = normalized(st.points[i] - st.points[i >= 7 ? i - 7 : 0]);
        const Vec2 b = normalized(st.points[std::min(i + 7, n - 1)] - st.points[i]);
        st.bend[i] = std::clamp(std::atan2(a.x * b.z - a.z * b.x, a.x * b.x + a.z * b.z) / 0.5f, -1.0f, 1.0f);
        float W = s[i] < sF ? 1.4f + 0.9f * s[i] / std::max(sF, 1.0f) : 2.4f + 0.8f * std::min(1.0f, (s[i] - sF) / 900.0f);
        W *= 0.78f + 0.44f * value_noise(seed + 130, s[i] / 70.0f, 0, 1.0f);
        W += 0.5f * (value_noise(seed + 131, s[i] / 11.0f, 0, 1.0f) - 0.5f);
        W += 0.35f * std::fabs(st.bend[i]);
        W += 4.3f * std::exp(-((s[i] - sK) / 7.5f) * ((s[i] - sK) / 7.5f)) * (0.8f + 0.4f * value_noise(seed + 132, s[i] / 5.0f, 0, 1.0f));
        for (const float p : narrow) {
          if (std::fabs(s[i] - p) < 14.0f && std::fabs(p - sK) > 1.0f && std::fabs(p - sF) > 1.0f) W = std::min(W, 2.7f);
        }
        if (s[i] <= sF && s[i] > sF - 5.0f) W = std::min(W, 2.2f);  // gathered at the lip
        if (s[i] > sF && s[i] < sK) W = std::max(W, 5.0f);          // the pool, wide from the cliff's foot
        W *= 1 + 2.3f * smoothstep(sEnd - 120.0f, sEnd, s[i]);  // opening into the sea
        float D = 0.42f + 0.35f * value_noise(seed + 133, s[i] / 45.0f, 0, 1.0f) + 0.25f * std::fabs(st.bend[i]);
        D += 1.1f * std::exp(-((s[i] - sF - 6.0f) / 5.0f) * ((s[i] - sF - 6.0f) / 5.0f));  // the plunge pool
        D *= 1 - 0.45f * smoothstep(sEnd - 120.0f, sEnd, s[i]);
        if (s[i] < 160.0f) D *= 0.8f;
        st.width[i] = W;
        st.depth[i] = D;
      }
    }
  }

  // The bridges: where the road crosses the river, and where the path does;
  // each from the way's point a bridge's half length before the crossing to
  // the one as far after it (both its ends on the way, even where it bends).
  {
    const auto span = [](const std::vector<Vec2>& pts, size_t at, Vec2& middle, Vec2& along) {
      size_t a = at, b = at;
      float before = 0, after = 0;
      while (a > 0 && before < rules::kBridgeHalf) before += distance(pts[a], pts[a - 1]), --a;
      while (b + 1 < pts.size() && after < rules::kBridgeHalf) after += distance(pts[b], pts[b + 1]), ++b;
      along = normalized(pts[b] - pts[a]);  // its axis: from end to end
      middle = pts[at];                     // over the river, on the way
    };
    float best = 1e30f;
    size_t at = 0;
    for (size_t i = 1; i + 1 < w.road.points.size(); ++i) {
      const Vec2 p = w.road.points[i];
      const float d = distance_to_stream(w.stream, p.x, p.z, best);
      if (d < best) {
        best = d;
        at = i;
      }
    }
    span(w.road.points, at, w.bridge, w.bridge_along);
    best = 1e30f;
    at = 0;
    for (size_t i = 1; i + 1 < path.points.size(); ++i) {
      const Vec2 p = path.points[i];
      if (p.z > -40.0f) continue;  // north of the start
      const float d = distance_to_stream(w.stream, p.x, p.z, best);
      if (d < best) {
        best = d;
        at = i;
      }
    }
    span(path.points, at, w.path_bridge, w.path_bridge_along);
  }
  // The water mill: up the river from the path's bridge (above the falls), on
  // a straight reach; its wheel in the water by the southern bank, the house
  // beside it on the bank.
  {
    const Stream& st = w.stream;
    size_t bridge_at = 0;
    float near = 1e30f;
    for (size_t i = 0; i < st.points.size(); ++i) {
      const float d = distance(st.points[i], w.path_bridge);
      if (d < near) {
        near = d;
        bridge_at = i;
      }
    }
    size_t i = std::min(bridge_at, st.falls > 60 ? st.falls - 60 : size_t{0});
    float gone = 0;
    while (i > 30 && gone < rules::kWaterMillUpstream) {
      gone += distance(st.points[i], st.points[i - 1]);
      --i;
    }
    for (size_t k = i > 25 ? i - 25 : 0; k < std::min(i + 25, st.points.size()); ++k) {
      if (std::fabs(st.bend[k]) < std::fabs(st.bend[i])) i = k;
    }
    i = std::clamp<size_t>(i, 3, st.points.size() - 4);
    const Vec2 along = normalized(st.points[i + 3] - st.points[i - 3]);
    Vec2 side{-along.z, along.x};
    if (side.z < 0) side = side * -1.0f;
    w.water_wheel = st.points[i] + side * (st.width[i] - 0.45f);
    w.water_mill = w.water_wheel + side * 3.0f;
    w.water_mill_along = along;
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
      size_t seg = 0;
      float t = 0;  // (the river as wide as it is there: its estuary is)
      const float d = stream_segment(w.stream, c.x, c.z, r + 40.0f, seg, t);
      const float water = seg < w.stream.width.size() ? w.stream.width[seg] : rules::kStreamWater;
      return distance_to_road(w.road, c.x, c.z, r + 4.0f) >= r + 4.0f && d >= r + 5.0f + water;
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
         distance(p, west.cart) < 5.0f + margin || distance(p, west.bridge) < rules::kBridgeHalf + 2.0f + margin ||
         distance(p, west.path_bridge) < rules::kBridgeHalf + 2.0f + margin ||
         distance(p, west.cove) < 18.0f + margin || distance(p, west.hidden) < 6.0f + margin ||
         distance(p, west.water_mill) < 13.0f + margin;
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

  // The bridges, the road's and the path's (their height set after the others settle).
  const size_t bridge_index = w.objects.size();
  add(make(ObjectKind::Bridge, west.bridge, yaw_along(west.bridge_along), 0, {rules::kBridgeHalf, 0.5f, 1.4f}));
  add(make(ObjectKind::Bridge, west.path_bridge, yaw_along(west.path_bridge_along), 1, {rules::kBridgeHalf, 0.5f, 1.4f}));
  // The water mill on its bank, its door away from the river (its height set after too).
  const size_t mill_index = w.objects.size();
  {
    Vec2 to_river{west.water_mill_along.z, -west.water_mill_along.x};
    if (distance_to_stream(west.stream, west.water_mill.x + to_river.x * 3.0f, west.water_mill.z + to_river.z * 3.0f, 30.0f) >
        distance_to_stream(west.stream, west.water_mill.x, west.water_mill.z, 30.0f)) {
      to_river = to_river * -1.0f;
    }
    // Its front (-Z) away from the river: local +Z towards it.
    const float yaw = std::atan2(to_river.x, to_river.z);  // local +Z along to_river
    Object mill = make(ObjectKind::WaterMill, west.water_mill, static_cast<Angle16>(std::lround(yaw / 6.2831853f * 65536.0f) & 0xFFFF), v8(),
                       {3.0f, 2.4f, 2.5f});
    add(mill);
  }
  // Behind the waterfall, the hidden way into the cave, looking out (north).
  {
    Object cleft = make(ObjectKind::HiddenCave, west.hidden, 0, 0);
    cleft.yaw = static_cast<Angle16>(std::lround(std::atan2(-west.falls_out.x, -west.falls_out.z) / 6.2831853f * 65536.0f) & 0xFFFF);
    add(cleft);
  }

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
  // The bridges' decks: a little above the banks at both of their ends.
  for (size_t k = bridge_index; k < bridge_index + 2; ++k) {
    Object& bridge = w.objects[k];
    const Vec2 middle{bridge.pos.x, bridge.pos.z};
    const Vec2 along = k == bridge_index ? west.bridge_along : west.path_bridge_along;
    const Box b = object_box(bridge);
    float lowest = 1e30f, banks = -1e30f;
    for (float x = b.min.x; x <= b.max.x + 0.01f; x += 0.5f) {
      for (float z = b.min.z; z <= b.max.z + 0.01f; z += 0.5f) lowest = std::min(lowest, w.ground.height(x, z));
    }
    for (const float t : {-1.0f, 1.0f}) {
      const Vec2 end = middle + axis_of(along) * (t * (rules::kBridgeHalf - 0.2f));
      banks = std::max(banks, w.ground.height(end.x, end.z));
    }
    const float deck = banks + 0.22f;
    bridge.pos.y = lowest - 0.1f;
    bridge.size.y = std::max(0.3f, (deck - bridge.pos.y) * 0.5f);
  }
  // The water mill's floor: a meter above the river (its stone base goes down the bank).
  {
    Object& mill = w.objects[mill_index];
    float level = 0;
    distance_to_stream(west.stream, mill.pos.x, mill.pos.z, 40.0f, &level);
    mill.pos.y = std::max(level + 1.1f, w.ground.height(mill.pos.x, mill.pos.z) - 1.0f);
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
      {npc::kMathurin, front_of_object(ObjectKind::WaterMill, west.water_mill, 4.2f)},
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

Vec2 wheel_spot(const WestLayout& west) {
  return west.water_wheel - west.water_mill_along * 6.0f;  // in the river by the wheel, a little upstream
}

float stream_segment(const Stream& s, float x, float z, float limit, size_t& seg, float& t) {
  if (s.points.size() < 2) return limit;
  return polyline_distance(s.points, s.chunks, x, z, limit, &seg, &t);
}

float river_surface(const WestLayout& west, float x, float z, float ground, float limit, float& level, float& width,
                    RiverPart part, size_t* segment) {
  const Stream& s = west.stream;
  const size_t n = s.points.size();
  if (n < 2 || s.level.size() != n || s.width.size() != n) return limit;
  // Beyond the playable area it runs under the ground (it springs there).
  if (std::max(std::fabs(x), std::fabs(z)) > rules::kWorldHalfSize + 12.0f) return limit;
  size_t seg = 0;
  float t = 0;
  float d = polyline_distance(s.points, s.chunks, x, z, limit, &seg, &t);
  const size_t f = s.falls;
  if (f > 0 && f + 1 < n && (part != RiverPart::Any || distance(Vec2{x, z}, west.falls) < 40.0f)) {
    // By the waterfall: above its lip (on the terrace, the ground up there),
    // the river up to its lip; anywhere else, the pool (under the cliff's
    // rock where that stands higher).
    const float mid = 0.5f * (s.level[f] + s.level[f + 1]);
    const bool above = part == RiverPart::Any ? ground > mid && scarp_side(west, x, z) < -rules::kFallsLip
                                              : part == RiverPart::Above;
    const bool near = distance(Vec2{x, z}, west.falls) < 40.0f;  // else: all of that part
    const size_t first = above ? (near && f > 60 ? f - 60 : 0) : f + 1;
    const size_t last = above ? f : (near ? std::min(n - 1, f + 61) : n - 1);
    float best2 = limit * limit;
    bool found = false;
    for (size_t i = first; i < last; ++i) {
      const Vec2 a = s.points[i], e = s.points[i + 1];
      const float abx = e.x - a.x, abz = e.z - a.z;
      const float len2 = abx * abx + abz * abz;
      const float u = len2 > 0 ? std::clamp(((x - a.x) * abx + (z - a.z) * abz) / len2, 0.0f, 1.0f) : 0.0f;
      const float px = a.x + abx * u - x, pz = a.z + abz * u - z;
      if (px * px + pz * pz < best2) {
        best2 = px * px + pz * pz;
        seg = i;
        t = u;
        found = true;
      }
    }
    if (!found) return limit;
    d = std::sqrt(best2);
  }
  if (d >= limit) return limit;
  if (segment) *segment = seg;
  level = s.level[seg] + (s.level[seg + 1] - s.level[seg]) * t;
  width = s.width[seg] + (s.width[seg + 1] - s.width[seg]) * t;
  return d;
}

float scarp_side(const WestLayout& west, float x, float z) {
  const auto& pts = west.scarp.points;
  if (pts.size() < 2) return 1e9f;
  size_t seg = 0;
  float t = 0;
  const float d = polyline_distance(pts, west.scarp.chunks, x, z, 1e9f, &seg, &t);
  const Vec2 a = pts[seg], b = pts[seg + 1];
  const float qx = a.x + (b.x - a.x) * t, qz = a.z + (b.z - a.z) * t;
  const float cross = (b.x - a.x) * (z - qz) - (b.z - a.z) * (x - qx);
  return cross >= 0 ? d : -d;
}

uint32_t wheel_raiders_left(const World& w) {
  const QuestState& q = w.quests[static_cast<size_t>(QuestId::WaterWheel)];
  if (q.stage != QuestStage::Active) return 0;
  return q.progress >= rules::kWheelRaiders ? 0 : rules::kWheelRaiders - q.progress;
}

bool wheel_turning(const World& w) {
  const QuestState& q = w.quests[static_cast<size_t>(QuestId::WaterWheel)];
  return q.stage == QuestStage::Done || (q.stage == QuestStage::Active && q.progress >= rules::kWheelRaiders);
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
    if (c.tag == Tag::Raider && distance(c.home, orchard) < 1.0f) return;  // they are there
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

// The Ploufis that jammed the water mill's wheel: aggressive, in the river by it.
void keep_wheel_raiders(World& w) {
  const uint32_t left = wheel_raiders_left(w);
  const Vec2 spot = wheel_spot(w.west);
  if (left == 0 || !lived_in(w, spot)) return;
  for (const Creature& c : w.creatures) {
    if (c.tag == Tag::Raider && distance(c.home, spot) < 1.0f) return;  // they are there
  }
  Rng rng(w.seed ^ 0x574845454CULL, visits(w, spot));  // "WHEEL"
  for (uint32_t k = 0; k < left; ++k) {
    Creature c;
    c.species = Species::Axolotl;
    c.genes = roll_genes(Species::Axolotl, Zone::River, rng);
    c.genes.chromatic = c.genes.elemental = false;
    const bool leader = k == 0 && left == rules::kWheelRaiders;
    c.genes.form = leader ? Form::Alpha : Form::Normal;
    c.level = leader ? rules::kWheelLeaderLevel : rules::kWheelRaiderLevel;
    c.tag = Tag::Raider;
    free_spot(w, c, spot + dir16(static_cast<Angle16>(k * 21845)) * 2.5f);
    c.home = spot;
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
  keep_wheel_raiders(w);
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
    if (c.tag != Tag::Raider || c.mind == Mind::KnockedOut || c.mind == Mind::Flee) continue;  // broke free: it runs first
    if (flat2(c.body.pos, w.player.pos) > rules::kAggroDistance * rules::kAggroDistance ||
        std::fabs(c.body.pos.y - w.player.pos.y) > 2.5f) {
      continue;
    }
    if (c.species == Species::Villager) force_challenge(w, c.npc);  // an aggressive trainer
    else force_battle(w, i);
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

void go_home(World& w) {
  if (inside(w)) leave_place(w);
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
  if (!w.creatures.empty()) {  // the companion comes along, beside the player
    Body& c = w.creatures[0].body;
    const float x = s.x + west.farm_front.z * 1.2f, z = s.z - west.farm_front.x * 1.2f;
    c.pos = {x, w.ground.height(x, z), z};
    c.velocity_y = 0;
  }
  update_population(w);
}

bool companion_guide(const World& w, Vec2& at) {
  if (inside(w)) {
    if (w.place.kind != PlaceKind::Cave && w.place.kind != PlaceKind::Burrow) return false;
    return way_out_ahead(w, rules::kCompanionGuideDistance, at);
  }
  if (!w.west.valid() || w.ground.empty()) return false;
  const Vec3 p = w.player.pos;
  float best = 1e18f;
  const auto offer = [&](Vec2 q) {
    const float d2 = (q.x - p.x) * (q.x - p.x) + (q.z - p.z) * (q.z - p.z);
    if (d2 < best) {
      best = d2;
      at = q;
    }
  };
  bool back = false;  // all found: back to where they belong (when nothing is left to look for)
  Vec2 home{};
  const auto near = [&](Vec2 q, float r) { return (q.x - p.x) * (q.x - p.x) + (q.z - p.z) * (q.z - p.z) < r * r; };
  const QuestState& shepherd = w.quests[static_cast<size_t>(QuestId::Shepherd)];
  if (shepherd.stage == QuestStage::Active && near(w.west.pasture, rules::kLambGuideRange)) {
    std::array<bool, rules::kLambs> following{};
    std::array<bool, rules::kLambs> seen{};
    for (const Creature& c : w.creatures) {
      if (c.tag != Tag::Lamb || c.variant >= rules::kLambs || (shepherd.progress & (1u << c.variant))) continue;
      seen[c.variant] = true;
      if (c.mind == Mind::Follow) following[c.variant] = true;
      else offer({c.body.pos.x, c.body.pos.z});
    }
    std::array<Vec2, rules::kLambs> spots{};
    bool spotted = false;
    for (uint32_t k = 0; k < rules::kLambs; ++k) {
      if ((shepherd.progress & (1u << k)) || seen[k]) continue;
      if (!spotted) spots = lamb_spots(w);  // not around yet: where it waits
      spotted = true;
      offer(spots[k]);
    }
    for (uint32_t k = 0; k < rules::kLambs; ++k) {
      if (following[k]) {
        back = true;
        home = w.west.pasture;
      }
    }
  }
  const QuestState& merchant = w.quests[static_cast<size_t>(QuestId::Merchant)];
  if (merchant.stage == QuestStage::Active && near(w.west.cart, rules::kGoodsGuideRange)) {
    if ((merchant.progress & kGoodsFound) != kGoodsFound) {
      const auto spots = goods_spots(w);
      for (uint32_t k = 0; k < rules::kGoods; ++k) {
        if (!(merchant.progress & (1u << k))) offer(spots[k]);
      }
    } else if (!back) {
      back = true;
      home = w.west.cart;
    }
  }
  if (best < 1e17f) return true;
  if (back) at = home;
  return back;
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

bool far_from_safety(const World& w, float x, float z) {
  const float d2 = rules::kRaiderSafeDistance * rules::kRaiderSafeDistance;
  const auto far = [&](Vec2 p) { return (p.x - x) * (p.x - x) + (p.z - z) * (p.z - z) > d2; };
  if (w.west.valid() && !far(farm_rescue_spot(w.west))) return false;
  if (in_village(w.village, x, z, rules::kRaiderSafeDistance)) return false;
  for (const Object& o : w.objects) {
    if (o.kind == ObjectKind::Teleporter && !far({o.pos.x, o.pos.z})) return false;
  }
  return true;
}

bool roaming_raider(const World& w, float x, float z, uint32_t roll) {
  if (roll >= rules::kRoamingChance || w.quests[static_cast<size_t>(QuestId::Orchard)].stage != QuestStage::Done) {
    return false;
  }
  uint32_t roaming = 0;
  for (const Creature& c : w.creatures) roaming += c.tag == Tag::Raider && c.species != Species::Villager ? 1 : 0;
  return roaming < rules::kRoamingRaiders && far_from_safety(w, x, z);
}

void raider_down(World& w, const Creature& c) {
  if (c.tag != Tag::Raider) return;
  if (w.west.valid() && distance(c.home, wheel_spot(w.west)) < 1.0f) {  // one of the wheel's
    QuestState& q = w.quests[static_cast<size_t>(QuestId::WaterWheel)];
    if (q.stage == QuestStage::Active && q.progress < rules::kWheelRaiders) ++q.progress;
    return;
  }
  QuestState& q = w.quests[static_cast<size_t>(QuestId::Orchard)];
  if (q.stage == QuestStage::Active && q.progress < rules::kRaiders) ++q.progress;
}

void heal_farm(World& w) {
  for (Monster& m : w.farm) m.hp = monster_fight_stats(m).hp;
}

void send_to_farm(World& w, const Monster& m) {
  if (w.farm.size() < rules::kMaxFarm) w.farm.push_back(m);
  heal_farm(w);
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
      heal_farm(w);  // those taken back come out healed
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
      heal_farm(w);  // and those left there are cared for
      break;
    }
    default: break;
  }
}

}  // namespace monstres
