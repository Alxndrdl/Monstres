#include "monstres/core/heightmap.hpp"

#include "monstres/core/continent.hpp"
#include "monstres/core/west.hpp"
#include "monstres/core/world.hpp"

#include <algorithm>
#include <cmath>

namespace monstres {

namespace {

uint64_t mix64(uint64_t z) {
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}

float lattice(uint64_t seed, int64_t x, int64_t z) {
  const uint64_t h = mix64(seed * 0x9E3779B97F4A7C15ULL + static_cast<uint64_t>(x) * 0xC2B2AE3D27D4EB4FULL +
                           static_cast<uint64_t>(z) * 0x165667B19E3779F9ULL);
  return static_cast<float>(h >> 40) / static_cast<float>(1ULL << 24);
}

float smooth(float e0, float e1, float x) {
  const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
  return t * t * (3 - 2 * t);
}

constexpr float kLakeDepth = 1.4f;

// The big hills only (no small bumps).
float hills(uint64_t seed, float x, float z) {
  return (value_noise(seed + 40, x, z, 34.0f) - 0.5f) * 8.0f + (value_noise(seed + 41, x, z, 15.0f) - 0.5f) * 3.0f;
}

constexpr float kKnollHeight = 3.0f;  // the tower's knoll, above the hills around it

constexpr float kCastleKnollHeight = 4.5f;

// A knoll with a flat top of radius `flat_radius` at height `top`, gentle slopes out to `outer`.
float add_knoll(float h, Vec2 center, float x, float z, float top, float height, float flat_radius, float outer) {
  const float rx = x - center.x, rz = z - center.z;
  const float d = std::sqrt(rx * rx + rz * rz);
  const float flat = 1 - smooth(flat_radius, outer, d);
  const float knoll = height * (1 - smooth(4.0f, outer, d));
  return h + knoll + (top - (h + knoll)) * flat;
}

// The heights the west's places are leveled at (see make_heightmap).
struct WestTops {
  float farm = 0, mill = 0, pasture = 0, burrow = 0, lighthouse = 0, menhirs = 0, crater = 0, cart = 0;
  float camp = 0, hollow = 0, cabin = 0;
};

// Leveled ground of radius `flat` at `top`, blending back into h out to `outer`.
float level_at(float h, Vec2 c, float x, float z, float top, float flat, float outer) {
  const float dx = x - c.x, dz = z - c.z;
  const float d2 = dx * dx + dz * dz;
  if (d2 >= outer * outer) return h;
  const float k = 1 - smooth(flat, outer, std::sqrt(d2));
  return h + (top - h) * k;
}

float shape_west(const WestLayout& west, const WestTops& t, float x, float z, float h) {
  h = level_at(h, west.cart, x, z, t.cart, 4.0f, 10.0f);
  h = level_at(h, west.farm, x, z, t.farm, rules::kFarmRadius, rules::kFarmRadius + 18.0f);
  h = level_at(h, west.mill, x, z, t.mill, rules::kMillRadius, rules::kMillKnoll + 6.0f);
  h = level_at(h, west.pasture, x, z, t.pasture, rules::kPastureRadius - 4.0f, rules::kPastureRadius + 10.0f);
  h = level_at(h, west.burrow, x, z, t.burrow, 5.0f, rules::kBurrowRadius + 6.0f);
  h = level_at(h, west.camp, x, z, t.camp, rules::kCampRadius - 2.0f, rules::kCampRadius + 6.0f);
  h = level_at(h, west.hollow_tree, x, z, t.hollow, 6.0f, rules::kHollowClearing + 6.0f);
  h = level_at(h, west.cabin, x, z, t.cabin, 5.0f, rules::kCabinClearing + 6.0f);
  h = level_at(h, west.lighthouse, x, z, t.lighthouse, 6.5f, rules::kLighthouseRadius + 24.0f);
  h = level_at(h, west.menhirs, x, z, t.menhirs, rules::kMenhirsRadius - 2.0f, rules::kMenhirsRadius + 20.0f);
  // The crater: its rim around a bowl, the ground leveled around it.
  h = level_at(h, west.crater, x, z, t.crater, rules::kCraterRim, rules::kCraterRim + 14.0f);
  const float cx = x - west.crater.x, cz = z - west.crater.z;
  const float r2 = cx * cx + cz * cz;
  if (r2 < (rules::kCraterRim + 2) * (rules::kCraterRim + 2)) {
    const float r = std::sqrt(r2), R = rules::kCraterRadius;
    if (r < R) h -= rules::kCraterDepth * (1 - (r / R) * (r / R)) - 1.4f * smooth(R * 0.6f, R, r);
    else h += 1.4f * (1 - smooth(R, rules::kCraterRim, r));  // the rim
  }
  return h;
}

}  // namespace

float cliff_width(uint64_t seed, const Path& path, const WestLayout& west, const CaveLayout& cave, float x, float z) {
  // Here and there a cliff, elsewhere a slope; cliffs by the cave and the
  // falls; a gentle ramp where the path goes up.
  float w = 3.0f + 20.0f * smooth(0.42f, 0.68f, value_noise(seed + 72, x, z, 85.0f));
  const float by_cave = std::min(std::hypot(x - cave.center.x, z - cave.center.z), std::hypot(x - west.falls.x, z - west.falls.z));
  w += (2.2f - w) * (1 - smooth(30.0f, 60.0f, by_cave));
  (void)path;
  const float dp = std::hypot(x - west.ramp.x, z - west.ramp.z);
  return std::max(w, 80.0f * (1 - smooth(40.0f, 80.0f, dp)));
}

namespace {

// Distances to the lines of the land (the path, the west's road and river,
// the terrace's edge) every kStep meters: far from a line, its distance at
// the nearest of these points tells enough (it changes by at most a meter
// per meter), without measuring it again at every point of the relief.
struct Coarse {
  static constexpr float kStep = 16.0f;
  static constexpr float kSlack = 11.4f;  // farthest from the nearest point (half a diagonal)
  int n = 0;
  std::vector<float> path, road, stream, scarp;
  std::vector<float> lowest_land;  // the continent's lowest ground around (by the sea: below its level)
  float at(const std::vector<float>& v, float x, float z) const {
    const int i = std::clamp(static_cast<int>(std::lround((x + rules::kReliefHalfSize) / kStep)), 0, n - 1);
    const int j = std::clamp(static_cast<int>(std::lround((z + rules::kReliefHalfSize) / kStep)), 0, n - 1);
    return v[static_cast<size_t>(i * n + j)];
  }
  // Between its points (smooth, where the exact value is not needed).
  float between(const std::vector<float>& v, float x, float z) const {
    const float fx = std::clamp((x + rules::kReliefHalfSize) / kStep, 0.0f, static_cast<float>(n - 1) - 0.001f);
    const float fz = std::clamp((z + rules::kReliefHalfSize) / kStep, 0.0f, static_cast<float>(n - 1) - 0.001f);
    const int i = static_cast<int>(fx), j = static_cast<int>(fz);
    const float tx = fx - static_cast<float>(i), tz = fz - static_cast<float>(j);
    const auto a = [&](int p, int q) { return v[static_cast<size_t>(p * n + q)]; };
    const float top = a(i, j) + (a(i + 1, j) - a(i, j)) * tx, bottom = a(i, j + 1) + (a(i + 1, j + 1) - a(i, j + 1)) * tx;
    return top + (bottom - top) * tz;
  }
};

// Beyond this from the terrace's edge (its widest slope, half, and how far
// it wanders), the terrace is all there or not at all.
constexpr float kTerraceReach = 25.0f + 9.0f + 0.5f;

float terrace_rise_at(uint64_t seed, const Path& path, const WestLayout& west, const CaveLayout& cave, float x, float z, float sd) {
  if (sd > 80.0f) return 0;  // far in the valley
  // Its edge wanders (not along a drawn line), the cave's spur too; held
  // only at the falls and the cave's mouth.
  const float by_cave = std::min(std::hypot(x - cave.center.x, z - cave.center.z), std::hypot(x - west.falls.x, z - west.falls.z));
  const float held = std::min(std::hypot(x - cave.mouth.x, z - cave.mouth.z), std::hypot(x - west.falls.x, z - west.falls.z));
  const float wander = smooth(7.0f, 26.0f, held);
  sd += wander * ((value_noise(seed + 70, x, z, 19.0f) - 0.5f) * 13.0f + (value_noise(seed + 71, x, z, 7.0f) - 0.5f) * 5.0f);
  const float w = cliff_width(seed, path, west, cave, x, z);
  const float height = rules::kTerraceHeight + 3.0f * (value_noise(seed + 73, x, z, 60.0f) - 0.5f) * smooth(20.0f, 60.0f, by_cave);
  return height * smooth(w * 0.5f, -w * 0.5f, sd);
}

}  // namespace

float terrace_rise(uint64_t seed, const Path& path, const WestLayout& west, const CaveLayout& cave, float x, float z) {
  if (west.scarp.points.size() < 2) return 0;
  return terrace_rise_at(seed, path, west, cave, x, z, scarp_side(west, x, z));
}

namespace {

// knoll_top, castle_top: heights of the flat tops of the knolls (see make_heightmap);
// land: the continent's height there (0 in the valley).
float relief(uint64_t seed, const Path& path, const Places& places, float knoll_top, float castle_top,
             const WestTops& tops, float x, float z, float land, const Coarse& coarse) {
  const VillageLayout& village = places.village;
  const RuinsLayout& ruins = places.ruins;
  // A line's distance, measured only where it matters (closer than `limit`).
  const auto near = [&](const std::vector<float>& v, float limit, auto&& measure) {
    return coarse.n == 0 || coarse.at(v, x, z) - Coarse::kSlack < limit ? measure() : limit;
  };
  // Three octaves of hills; the smallest bumps fade out along the path.
  float near_path = 1 - smooth(1.5f, 7.0f, near(coarse.path, 7.0f, [&] { return distance_to_path(path, x, z, 7.0f); }));
  if (places.west && places.west->valid()) {
    const float d = near(coarse.road, 7.0f, [&] { return distance_to_road(places.west->road, x, z, 7.0f); });
    near_path = std::max(near_path, 1 - smooth(1.5f, 7.0f, d));
  }
  float h = hills(seed, x, z) + (value_noise(seed + 42, x, z, 6.0f) - 0.5f) * 0.8f * (1 - near_path);
  // The river runs down a gentle valley of its own: the hills (not the
  // places: the cave's hill by its cove) flatten around it.
  if (places.west && places.west->valid()) {
    const float d = near(coarse.stream, 40.0f, [&] { return distance_to_stream(places.west->stream, x, z, 40.0f); });
    h *= 0.15f + 0.85f * smooth(8.0f, 40.0f, d);
  }

  if (ruins.radius > 0) {
    // A knoll with a flat top where the tower stands (the enclosure, ±9 m, stands level).
    h = add_knoll(h, ruins.center, x, z, knoll_top, kKnollHeight, 11.0f, ruins.radius + 17);
  }
  if (places.castle.radius > 0) {
    // A higher one for the castle: its walls (±14 m) stand level.
    h = add_knoll(h, places.castle.center, x, z, castle_top, kCastleKnollHeight, rules::kCastleHalf + 5.0f,
                  places.castle.radius + 32);
  }
  const WestLayout* west = places.west && places.west->valid() ? places.west : nullptr;
  if (const CaveLayout& cave = places.cave; cave.radius > 0) {
    // In front of the cave's mouth a passage stays at the level of the ground
    // outside (a ravine cut into the rock up to it).
    const float mx = x - cave.mouth.x, mz = z - cave.mouth.z;
    const float along = mx * cave.facing.x + mz * cave.facing.z;  // outwards
    const float across = std::fabs(-mx * cave.facing.z + mz * cave.facing.x);
    // It opens out (a notch in the cliff, not a corridor), its walls uneven,
    // as far as the cliff's foot.
    const float half = 2.6f + 0.55f * std::max(0.0f, along + 1.0f) + 1.4f * (value_noise(seed + 74, x, z, 3.5f) - 0.5f);
    float passage = (1 - smooth(half, half + 1.2f, across)) * (1 - smooth(5.5f, 6.5f, -along)) * (1 - smooth(12.0f, 18.0f, along));
    if (west) {
      // The terrace inland, the cave in a spur of its edge; beside the
      // waterfall's water, at the cliff's foot, the hidden way in: a cleft (not up to its lip).
      const Vec2 o = west->falls_out, e = west->hidden;
      const float in = -((x - e.x) * o.x + (z - e.z) * o.z);
      const float across_cleft = std::fabs(-(x - e.x) * o.z + (z - e.z) * o.x);
      passage = std::max(passage, (1 - smooth(1.5f, 2.3f, across_cleft)) * (1 - smooth(1.1f, 1.5f, in)));
      // (Where the continent's own ground already rises as high, it is that.)
      float sd = coarse.n ? coarse.at(coarse.scarp, x, z) : 0.0f;  // far from the edge: which side is enough
      if (coarse.n == 0 || std::fabs(sd) - Coarse::kSlack < kTerraceReach) {
        sd = scarp_side(*west, x, z);
      } else {
        sd = sd > 0 ? 81.0f : -std::max(std::fabs(sd) - Coarse::kSlack, kTerraceReach);
      }
      const float rise = west->scarp.points.size() < 2 ? 0.0f : terrace_rise_at(seed, path, *west, cave, x, z, sd);
      // (Over a hollow of the continent's ground, its top still that high.)
      const float on_terrace = std::min(1.0f, rise / (rules::kTerraceHeight - 1.5f));
      h += (std::max(0.0f, rise - std::max(land, 0.0f)) + on_terrace * std::max(0.0f, -land)) * (1 - passage);
    } else {
      // Hand-made worlds: a rocky hill, cliffs all around, a lumpy top.
      const float cx = x - cave.center.x, cz = z - cave.center.z;
      const float d = std::sqrt(cx * cx + cz * cz);
      const float rise = (rules::kCaveHillHeight + 2.5f * value_noise(seed + 43, x, z, 7.0f)) *
                         (1 - smooth(cave.radius - 8.0f, cave.radius, d));
      h += rise * (1 - passage);
    }
  }

  // The hills fade out towards the edge of the relief, where the continent's
  // ground (drawn far away from its own grid) takes over: both then meet exactly.
  const float edge = std::max(std::fabs(x), std::fabs(z));
  h = h * (1 - smooth(rules::kReliefHalfSize - 80, rules::kReliefHalfSize - 8, edge)) + land;
  if (places.west && places.west->valid()) h = shape_west(*places.west, tops, x, z, h);

  if (village.lake.radius_x > 0) {
    // Flat village and shore, at y = 0.
    const float vx = x - village.center.x, vz = z - village.center.z;
    const float to_village = std::sqrt(vx * vx + vz * vz);
    const Lake& lake = village.lake;
    const float lake_depth = 1 - lake_distance(lake, x, z);  // 0 at the shore, 1 in the middle
    h *= std::min(smooth(rules::kVillageRadius + 3, rules::kVillageRadius + 15, to_village),
                  smooth(0.1f, 1.3f, -lake_depth));
    if (lake_depth > 0) h = -kLakeDepth * std::min(1.0f, lake_depth * 2.5f);
  }
  // The path and the road stay out of the sea: by the coast, where the
  // ground dips a little below it, they run on a strip of land just above it.
  // (Only by the coast, and not over the deeper sea: no causeway.)
  const float lowest = coarse.n ? coarse.between(coarse.lowest_land, x, z) : land;
  const float coast = smooth(rules::kSeaLevel + 0.5f, rules::kSeaLevel - 0.5f, lowest) *
                      smooth(rules::kSeaLevel - 7.0f, rules::kSeaLevel - 1.0f, h);
  if (h < rules::kSeaLevel + 0.8f && coast > 0) {
    float d = near(coarse.path, 14.0f, [&] { return distance_to_path(path, x, z, 14.0f); });
    if (places.west && places.west->valid()) {
      d = std::min(d, near(coarse.road, 14.0f, [&] { return distance_to_road(places.west->road, x, z, 14.0f); }));
    }
    const float dry = rules::kSeaLevel + 0.8f;
    h += (dry - h) * (1 - smooth(4.0f, 14.0f, d)) * coast;
  }
  return h;
}

}  // namespace

namespace {

// The bridges' ends: the ground there raised to the higher bank's (the
// river's valley dug lower on one side), going down to the ground around
// over a few meters: the way meets the deck at both ends.
void embank_bridges(Heightmap& m, const WestLayout& west) {
  const auto index = [](float v) {
    return std::clamp(static_cast<int>(std::floor((v + rules::kReliefHalfSize) / Heightmap::kCellSize)), 0,
                      Heightmap::kPoints - 1);
  };
  const std::pair<Vec2, Vec2> bridges[] = {{west.bridge, west.bridge_along}, {west.path_bridge, west.path_bridge_along}};
  constexpr float L = rules::kBridgeHalf, kRamp = 12.0f;
  for (const auto& [c, a] : bridges) {
    if (a.x == 0 && a.z == 0) continue;
    const float top = std::max(m.height(c.x + a.x * (L - 0.2f), c.z + a.z * (L - 0.2f)),
                               m.height(c.x - a.x * (L - 0.2f), c.z - a.z * (L - 0.2f)));
    const float r = L + kRamp + 6.0f;
    for (int i = index(c.x - r); i <= std::min(index(c.x + r) + 1, Heightmap::kPoints - 1); ++i) {
      for (int j = index(c.z - r); j <= std::min(index(c.z + r) + 1, Heightmap::kPoints - 1); ++j) {
        const float x = static_cast<float>(i) * Heightmap::kCellSize - rules::kReliefHalfSize;
        const float z = static_cast<float>(j) * Heightmap::kCellSize - rules::kReliefHalfSize;
        const float u = std::fabs((x - c.x) * a.x + (z - c.z) * a.z);
        const float v = std::fabs(-(x - c.x) * a.z + (z - c.z) * a.x);
        if (u < L - 2.0f || u > L + kRamp || v > 6.0f) continue;
        const float k = (1 - smooth(L, L + kRamp, u)) * (1 - smooth(3.5f, 6.0f, v));
        float& h = m.heights[static_cast<size_t>(i * Heightmap::kPoints + j)];
        if (h < top) h += (top - h) * k;
      }
    }
  }
}

constexpr float kSpringEdge = rules::kWorldHalfSize;  // beyond it, the river runs under the ground

// The river's water goes down from its spring (never up): its level at each
// point is below the ground all along before it (over the terrace's edge it
// drops: its waterfall). Then its bed is dug under it: deepest by the outer
// bank of a bend (a steep cut bank there), shallow by the inner one (a gravel
// bar, a gentle slope up), its banks rising back to the ground around. Not
// into the cliff below the falls; a deeper race under the mill's wheel; the
// hidden cleft's floor and its ledge a little above the pool.
void carve_stream(Heightmap& m, WestLayout& west, uint64_t seed) {
  Stream& s = west.stream;
  const size_t n = s.points.size();
  s.level.assign(n, 0.0f);
  // The waterfall: one step, at the cliff's lip. Its last point up on the
  // terrace (behind the lip), its first one past the cliff's foot; those
  // between, on the cliff's face, take the pool's level.
  size_t up = 0, foot = 0;
  if (s.falls > 0 && s.falls + 1 < n) {
    up = s.falls - 1;
    for (size_t i = s.falls > 10 ? s.falls - 10 : 0; i < std::min(n, s.falls + 10); ++i) {
      if (scarp_side(west, s.points[i].x, s.points[i].z) < -rules::kFallsLip) up = i;
    }
    foot = up + 1;
    while (foot + 1 < n && foot < up + 12 && scarp_side(west, s.points[foot].x, s.points[foot].z) < rules::kFallsLip + 0.6f) ++foot;
    s.falls = up;
  }
  // Its level only goes down, but not from where it comes in, beyond the
  // playable area (the relief there goes down to the continent's ground far
  // away): it would set the river's level all along the terrace.
  float level = 1e9f;
  for (size_t i = 0; i < n; ++i) {
    if (i > up && i < foot) continue;
    const float here = m.height(s.points[i].x, s.points[i].z) - 0.7f;
    const bool spring = i < n / 4 && std::max(std::fabs(s.points[i].x), std::fabs(s.points[i].z)) > kSpringEdge;
    if (spring) continue;
    level = std::min(level, here);
    s.level[i] = std::max(level, rules::kSeaLevel + 0.03f);  // flush with the sea at its mouth
  }
  size_t first = 0;  // there: as just after
  while (first + 1 < n && std::max(std::fabs(s.points[first].x), std::fabs(s.points[first].z)) > kSpringEdge) ++first;
  for (size_t i = 0; i < first && first < n / 4; ++i) s.level[i] = s.level[first];
  // A high fall, whatever the valley's ground below: where it is high, the
  // pool and the river after it lower (a little gorge below the falls).
  if (foot > up && foot < n && s.level[up] - s.level[foot] < rules::kFallsLeast) {
    const float pool = std::max(s.level[up] - rules::kFallsLeast, rules::kSeaLevel + 0.03f);
    for (size_t i = up + 1; i < n; ++i) s.level[i] = std::min(s.level[i], pool);
  }
  for (size_t i = up + 1; i < foot; ++i) s.level[i] = s.level[foot];
  if (n < 2) return;
  if (s.width.size() != n) {
    s.width.assign(n, rules::kStreamWater);
    s.depth.assign(n, 0.55f);
    s.bend.assign(n, 0.0f);
  }
  float x0 = 1e9f, x1 = -1e9f, z0 = 1e9f, z1 = -1e9f, widest = 0;
  for (size_t i = 0; i < n; ++i) {
    x0 = std::min(x0, s.points[i].x);
    x1 = std::max(x1, s.points[i].x);
    z0 = std::min(z0, s.points[i].z);
    z1 = std::max(z1, s.points[i].z);
    widest = std::max(widest, s.width[i]);
  }
  const float reach = widest + 60.0f;  // the banks rise back to the highest ground around
  const auto index = [](float v) {
    return std::clamp(static_cast<int>(std::floor((v + rules::kReliefHalfSize) / Heightmap::kCellSize)), 0,
                      Heightmap::kPoints - 1);
  };
  const auto lerp = [](const std::vector<float>& v, size_t i, float t) { return v[i] + (v[i + 1] - v[i]) * t; };
  for (int i = index(x0 - reach); i <= std::min(index(x1 + reach) + 1, Heightmap::kPoints - 1); ++i) {
    for (int j = index(z0 - reach); j <= std::min(index(z1 + reach) + 1, Heightmap::kPoints - 1); ++j) {
      const float x = static_cast<float>(i) * Heightmap::kCellSize - rules::kReliefHalfSize;
      const float z = static_cast<float>(j) * Heightmap::kCellSize - rules::kReliefHalfSize;
      float& h = m.heights[static_cast<size_t>(i * Heightmap::kPoints + j)];
      // Every stretch of it nearby digs (two arms of a bend at different levels: the lower one wins).
      float target = 1e9f;
      float side_of_scarp = 1e9f;  // looked up once, if needed
      const float rough = value_noise(seed + 140, x, z, 6.0f);
      const auto& ch = s.chunks.boxes;
      for (size_t c = 0; c < ch.size(); ++c) {
        const auto& box = ch[c];
        if (x < box[0] - reach || x > box[2] + reach || z < box[1] - reach || z > box[3] + reach) continue;
        const size_t first = c * Chunks::kChunk, last = std::min(first + Chunks::kChunk, n - 1);
        for (size_t seg = first; seg < last; ++seg) {
          const Vec2 a = s.points[seg], b = s.points[seg + 1];
          const float abx = b.x - a.x, abz = b.z - a.z;
          const float len2 = abx * abx + abz * abz;
          const float t = len2 > 0 ? std::clamp(((x - a.x) * abx + (z - a.z) * abz) / len2, 0.0f, 1.0f) : 0.0f;
          const float qx = a.x + abx * t, qz = a.z + abz * t;
          const float d = std::hypot(x - qx, z - qz);
          if (d >= reach) continue;
          const float water = lerp(s.level, seg, t), W = lerp(s.width, seg, t), D = lerp(s.depth, seg, t), B = lerp(s.bend, seg, t);
          if (h <= water - D) continue;
          // Below the falls, not into the cliff (it stands over the pool).
          if (seg + 1 > s.falls) {
            if (side_of_scarp > 1e8f) side_of_scarp = scarp_side(west, x, z);
            if (side_of_scarp < -0.5f) continue;
          }
          // Across: which side of the flow (positive towards the inside of a bend turning that way), how far.
          const float across = -abz * (x - qx) + abx * (z - qz) >= 0 ? d : -d;
          const float deep = -B * 0.4f * W;
          if (std::fabs(across) < W) {
            const float u = (across - deep) / W;
            target = std::min(target, water - 0.05f - D * std::max(0.0f, 1 - u * u));
          } else {
            const bool outer = across * B < 0;
            const float slope = std::max(0.18f, 0.5f + (outer ? 0.6f : -0.32f) * std::fabs(B) + 0.35f * (rough - 0.5f));
            // Farther out, the sides of its valley: gentle (where it cut deep, a wide valley, no gorge).
            const float out = std::fabs(across) - W;
            target = std::min(target, water - 0.05f + slope * std::min(out, 6.0f) + 0.42f * std::max(0.0f, out - 6.0f));
          }
        }
      }
      if (target >= h) continue;
      // Not beyond the playable area (where the relief goes down to the
      // continent's ground): the river comes out of the ground there.
      const float edge = std::max(std::fabs(x), std::fabs(z));
      const float k = 1 - smooth(kSpringEdge + 4.0f, kSpringEdge + 16.0f, edge);
      h += (std::min(h, target) - h) * k;
    }
  }
  // The pool below the falls: its water's level.
  size_t pool = 0;
  for (size_t i = 0; i < n; ++i) {
    if (std::hypot(s.points[i].x - west.cove.x, s.points[i].z - west.cove.z) <
        std::hypot(s.points[pool].x - west.cove.x, s.points[pool].z - west.cove.z)) {
      pool = i;
    }
  }
  west.cove_level = s.level[pool];
  // The mill's race, and the cleft's floor with its ledge.
  for (int i = index(west.water_wheel.x - 4); i <= index(west.water_wheel.x + 4); ++i) {
    for (int j = index(west.water_wheel.z - 4); j <= index(west.water_wheel.z + 4); ++j) {
      const float x = static_cast<float>(i) * Heightmap::kCellSize - rules::kReliefHalfSize;
      const float z = static_cast<float>(j) * Heightmap::kCellSize - rules::kReliefHalfSize;
      float lvl = 0;
      distance_to_stream(s, x, z, 20.0f, &lvl);
      if (std::hypot(x - west.water_wheel.x, z - west.water_wheel.z) < 2.6f) {
        float& h = m.heights[static_cast<size_t>(i * Heightmap::kPoints + j)];
        h = std::min(h, lvl - 1.1f);
      }
    }
  }
  // Behind the falling water, the cleft's ledge at the cliff's foot, a
  // little above the pool; along the foot from it, a ledge out to the
  // nearer bank (the way in).
  const Vec2 F = west.falls, o = west.falls_out, lat{-o.z, o.x};
  const float ledge = west.cove_level + 0.35f;
  float banks[2] = {14.0f, 14.0f};
  for (int k = 0; k < 2; ++k) {
    const float way = k == 0 ? 1.0f : -1.0f;
    for (float l = 3.0f; l < 14.0f; l += 0.5f) {
      const float px = F.x + o.x * 1.6f + lat.x * way * l, pz = F.z + o.z * 1.6f + lat.z * way * l;
      if (m.height(px, pz) > ledge + 0.1f) {
        banks[k] = l + 1.0f;
        break;
      }
    }
  }
  const float sgn = banks[0] <= banks[1] ? 1.0f : -1.0f, walk = std::min(banks[0], banks[1]);
  for (int i = index(F.x - 16); i <= index(F.x + 16); ++i) {
    for (int j = index(F.z - 16); j <= index(F.z + 16); ++j) {
      const float x = static_cast<float>(i) * Heightmap::kCellSize - rules::kReliefHalfSize;
      const float z = static_cast<float>(j) * Heightmap::kCellSize - rules::kReliefHalfSize;
      const float out = (x - F.x) * o.x + (z - F.z) * o.z, l = (-(x - F.x) * o.z + (z - F.z) * o.x) * sgn;
      const bool cleft = std::fabs(l) < 2.6f && out > -0.3f && out < rules::kFallsLanding - 0.6f;
      const bool way = l > 0 && l < walk && out > -0.2f && out < 2.2f;
      if (cleft || way) m.heights[static_cast<size_t>(i * Heightmap::kPoints + j)] = ledge;
    }
  }
}

}  // namespace

float value_noise(uint64_t seed, float x, float z, float cell) {
  const float fx = x / cell, fz = z / cell;
  const float x0 = std::floor(fx), z0 = std::floor(fz);
  const float tx = fx - x0, tz = fz - z0;
  const float sx = tx * tx * (3 - 2 * tx), sz = tz * tz * (3 - 2 * tz);
  const auto ix = static_cast<int64_t>(x0), iz = static_cast<int64_t>(z0);
  const float a = lattice(seed, ix, iz), b = lattice(seed, ix + 1, iz);
  const float c = lattice(seed, ix, iz + 1), d = lattice(seed, ix + 1, iz + 1);
  return (a + (b - a) * sx) + ((c + (d - c) * sx) - (a + (b - a) * sx)) * sz;
}

float Heightmap::height(float x, float z) const {
  if (heights.empty()) return 0;
  const float fx = std::clamp((x + rules::kReliefHalfSize) / kCellSize, 0.0f, static_cast<float>(kCells));
  const float fz = std::clamp((z + rules::kReliefHalfSize) / kCellSize, 0.0f, static_cast<float>(kCells));
  const int i = std::min(static_cast<int>(fx), kCells - 1);
  const int j = std::min(static_cast<int>(fz), kCells - 1);
  const float tx = fx - static_cast<float>(i), tz = fz - static_cast<float>(j);
  const float h00 = at(i, j), h10 = at(i + 1, j), h01 = at(i, j + 1), h11 = at(i + 1, j + 1);
  if (tz >= tx) return h00 + (h11 - h01) * tx + (h01 - h00) * tz;  // triangle (x0,z0) (x0,z1) (x1,z1)
  return h00 + (h10 - h00) * tx + (h11 - h10) * tz;                 // triangle (x0,z0) (x1,z1) (x1,z0)
}

Vec3 Heightmap::normal(float x, float z) const {
  constexpr float k = kCellSize / 2;
  const float gx = (height(x + k, z) - height(x - k, z)) / kCellSize;
  const float gz = (height(x, z + k) - height(x, z - k)) / kCellSize;
  const float len = std::sqrt(gx * gx + 1 + gz * gz);
  return {-gx / len, 1 / len, -gz / len};
}

float Heightmap::land_height(float x, float z) const {
  if (land.empty()) return 0;
  const float fx = std::clamp((x + rules::kReliefHalfSize) / kLandCell, 0.0f, static_cast<float>(kLandPoints - 1));
  const float fz = std::clamp((z + rules::kReliefHalfSize) / kLandCell, 0.0f, static_cast<float>(kLandPoints - 1));
  const int i = std::min(static_cast<int>(fx), kLandPoints - 2), j = std::min(static_cast<int>(fz), kLandPoints - 2);
  const float tx = fx - static_cast<float>(i), tz = fz - static_cast<float>(j);
  const auto at = [&](int a, int b) { return land[static_cast<size_t>(a * kLandPoints + b)]; };
  const float top = at(i, j) + (at(i + 1, j) - at(i, j)) * tx;
  const float bottom = at(i, j + 1) + (at(i + 1, j + 1) - at(i, j + 1)) * tx;
  return top + (bottom - top) * tz;
}

Heightmap make_heightmap(uint64_t seed, const Path& path, const Places& places) {
  Heightmap m;
  m.heights.resize(static_cast<size_t>(Heightmap::kPoints * Heightmap::kPoints));
  const float knoll_top = hills(seed, places.ruins.center.x, places.ruins.center.z) + kKnollHeight;
  const float castle_top = hills(seed, places.castle.center.x, places.castle.center.z) + kCastleKnollHeight;
  // The continent's ground on its 10 m grid (the same points as Continent's),
  // between them in a straight line: as the far terrain draws it.
  constexpr float kLandCell = Heightmap::kLandCell;
  constexpr int kLand = Heightmap::kLandPoints;
  std::vector<float>& land = m.land;
  land.resize(static_cast<size_t>(kLand * kLand));
  for (int i = 0; i < kLand; ++i) {
    for (int j = 0; j < kLand; ++j) {
      land[static_cast<size_t>(i * kLand + j)] =
          continent_height(seed, -rules::kReliefHalfSize + kLandCell * static_cast<float>(i),
                           -rules::kReliefHalfSize + kLandCell * static_cast<float>(j));
    }
  }
  const auto land_at = [&](float x, float z) {
    const float fx = (x + rules::kReliefHalfSize) / kLandCell, fz = (z + rules::kReliefHalfSize) / kLandCell;
    const int i = std::min(static_cast<int>(fx), kLand - 2), j = std::min(static_cast<int>(fz), kLand - 2);
    const float tx = fx - static_cast<float>(i), tz = fz - static_cast<float>(j);
    const float a = land[static_cast<size_t>(i * kLand + j)], b = land[static_cast<size_t>((i + 1) * kLand + j)];
    const float c = land[static_cast<size_t>(i * kLand + j + 1)], d = land[static_cast<size_t>((i + 1) * kLand + j + 1)];
    return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * tz;
  };
  // The west's places are leveled at the height of their middle.
  WestTops tops;
  WestLayout* west = places.west && places.west->valid() ? places.west : nullptr;
  if (west) {
    // (Above the sea, by the coast too: a hollow of the hills there is no
    // place for a camp or a burrow.)
    const auto natural = [&](Vec2 p) {
      const float edge = std::max(std::fabs(p.x), std::fabs(p.z));
      return std::max(hills(seed, p.x, p.z) * (1 - smooth(rules::kReliefHalfSize - 80, rules::kReliefHalfSize - 8, edge)) +
                          land_at(p.x, p.z),
                      rules::kSeaLevel + 1.0f);
    };
    tops.cart = natural(west->cart);
    tops.farm = natural(west->farm);
    tops.mill = natural(west->mill) + rules::kMillRise;
    tops.pasture = natural(west->pasture);
    tops.burrow = natural(west->burrow);
    tops.camp = natural(west->camp);
    tops.hollow = natural(west->hollow_tree);
    tops.cabin = natural(west->cabin);
    tops.lighthouse = std::max(natural(west->lighthouse), rules::kSeaLevel + 1.0f) + rules::kLighthouseRise;
    tops.menhirs = natural(west->menhirs);
    tops.crater = natural(west->crater);
  }
  const Places with_tops{places.village, places.ruins, places.cave, places.castle, west};
  Coarse coarse;
  coarse.n = static_cast<int>(2 * rules::kReliefHalfSize / Coarse::kStep) + 1;
  for (auto* v : {&coarse.path, &coarse.road, &coarse.stream, &coarse.scarp, &coarse.lowest_land}) {
    v->assign(static_cast<size_t>(coarse.n * coarse.n), 1e9f);
  }
  for (int i = 0; i < coarse.n; ++i) {
    for (int j = 0; j < coarse.n; ++j) {
      const float x = static_cast<float>(i) * Coarse::kStep - rules::kReliefHalfSize;
      const float z = static_cast<float>(j) * Coarse::kStep - rules::kReliefHalfSize;
      const auto k = static_cast<size_t>(i * coarse.n + j);
      coarse.path[k] = distance_to_path(path, x, z, 1e9f);
      for (float dx = -24; dx <= 24; dx += 12) {
        for (float dz = -24; dz <= 24; dz += 12) coarse.lowest_land[k] = std::min(coarse.lowest_land[k], land_at(x + dx, z + dz));
      }
      if (west) {
        coarse.road[k] = distance_to_road(west->road, x, z, 1e9f);
        coarse.stream[k] = distance_to_stream(west->stream, x, z, 1e9f);
        coarse.scarp[k] = west->scarp.points.size() < 2 ? 1e9f : scarp_side(*west, x, z);
      }
    }
  }
  for (int i = 0; i < Heightmap::kPoints; ++i) {
    for (int j = 0; j < Heightmap::kPoints; ++j) {
      const float x = static_cast<float>(i) * Heightmap::kCellSize - rules::kReliefHalfSize;
      const float z = static_cast<float>(j) * Heightmap::kCellSize - rules::kReliefHalfSize;
      m.heights[static_cast<size_t>(i * Heightmap::kPoints + j)] =
          relief(seed, path, with_tops, knoll_top, castle_top, tops, x, z, land_at(x, z), coarse);
    }
  }
  if (west) {
    carve_stream(m, *west, seed);
    embank_bridges(m, *west);
  }
  return m;
}

float natural_height(uint64_t seed, float x, float z) {
  const float edge = std::max(std::fabs(x), std::fabs(z));
  return hills(seed, x, z) * (1 - smooth(rules::kReliefHalfSize - 80, rules::kReliefHalfSize - 8, edge)) +
         continent_height(seed, x, z);
}

}  // namespace monstres
