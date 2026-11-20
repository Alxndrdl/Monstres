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
  return std::max(w, 50.0f * (1 - smooth(25.0f, 55.0f, dp)));
}

float terrace_rise(uint64_t seed, const Path& path, const WestLayout& west, const CaveLayout& cave, float x, float z) {
  if (west.scarp.points.size() < 2) return 0;
  float sd = scarp_side(west, x, z);
  if (sd > 80.0f) return 0;  // far in the valley
  // Its edge wanders (not along a drawn line), the cave's spur too; held
  // only at the falls and the cave's mouth.
  const float by_cave = std::min(std::hypot(x - cave.center.x, z - cave.center.z), std::hypot(x - west.falls.x, z - west.falls.z));
  const float held = std::min(std::hypot(x - cave.mouth.x, z - cave.mouth.z), std::hypot(x - west.falls.x, z - west.falls.z));
  const float wander = smooth(7.0f, 26.0f, held);
  sd += wander * ((value_noise(seed + 70, x, z, 19.0f) - 0.5f) * 13.0f + (value_noise(seed + 71, x, z, 7.0f) - 0.5f) * 5.0f);
  const float w = cliff_width(seed, path, west, cave, x, z);
  const float height = rules::kTerraceHeight + 4.0f * (value_noise(seed + 73, x, z, 60.0f) - 0.5f) * smooth(20.0f, 60.0f, by_cave);
  return height * smooth(w * 0.5f, -w * 0.5f, sd);
}

namespace {

// knoll_top, castle_top: heights of the flat tops of the knolls (see make_heightmap);
// land: the continent's height there (0 in the valley).
float relief(uint64_t seed, const Path& path, const Places& places, float knoll_top, float castle_top,
             const WestTops& tops, float x, float z, float land) {
  const VillageLayout& village = places.village;
  const RuinsLayout& ruins = places.ruins;
  // Three octaves of hills; the smallest bumps fade out along the path.
  float near_path = 1 - smooth(1.5f, 7.0f, distance_to_path(path, x, z, 7.0f));
  if (places.west && places.west->valid()) {
    near_path = std::max(near_path, 1 - smooth(1.5f, 7.0f, distance_to_road(places.west->road, x, z, 7.0f)));
  }
  float h = hills(seed, x, z) + (value_noise(seed + 42, x, z, 6.0f) - 0.5f) * 0.8f * (1 - near_path);
  // The river runs down a gentle valley of its own: the hills (not the
  // places: the cave's hill by its cove) flatten around it.
  if (places.west && places.west->valid()) {
    const float d = distance_to_stream(places.west->stream, x, z, 40.0f);
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
    float passage = (1 - smooth(2.6f, 3.6f, across)) * (1 - smooth(5.5f, 6.5f, -along));
    if (west) {
      // The terrace inland, the cave in a spur of its edge; beside the
      // waterfall, at the cliff's foot, the hidden way in: a cleft.
      const Vec2 o = west->falls_out, e = west->hidden;
      const float in = -((x - e.x) * o.x + (z - e.z) * o.z);
      const float side = std::fabs(-(x - e.x) * o.z + (z - e.z) * o.x);
      passage = std::max(passage, (1 - smooth(1.4f, 2.2f, side)) * (1 - smooth(4.0f, 5.0f, in)));
      // (Where the continent's own ground already rises as high, it is that.)
      h += std::max(0.0f, terrace_rise(seed, path, *west, cave, x, z) - std::max(land, 0.0f)) * (1 - passage);
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
  return h;
}

}  // namespace

namespace {

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
  float level = 1e9f;
  for (size_t i = 0; i < n; ++i) {
    level = std::min(level, m.height(s.points[i].x, s.points[i].z) - 0.7f);
    s.level[i] = std::max(level, rules::kSeaLevel + 0.03f);  // flush with the sea at its mouth
  }
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
      // Not at the relief's very edge (where the continent's ground takes
      // over): the river comes out of the ground a little before it.
      const float edge = std::max(std::fabs(x), std::fabs(z));
      const float k = 1 - smooth(rules::kReliefHalfSize - 40.0f, rules::kReliefHalfSize - 6.0f, edge);
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
  const Vec2 e = west.hidden, o = west.falls_out;
  for (int i = index(e.x - 8); i <= index(e.x + 8); ++i) {
    for (int j = index(e.z - 8); j <= index(e.z + 8); ++j) {
      const float x = static_cast<float>(i) * Heightmap::kCellSize - rules::kReliefHalfSize;
      const float z = static_cast<float>(j) * Heightmap::kCellSize - rules::kReliefHalfSize;
      const float in = -((x - e.x) * o.x + (z - e.z) * o.z);
      const float side = std::fabs(-(x - e.x) * o.z + (z - e.z) * o.x);
      if (side < 2.2f && in > -2.2f && in < 4.6f) {
        m.heights[static_cast<size_t>(i * Heightmap::kPoints + j)] = west.cove_level + 0.35f;
      }
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
    const auto natural = [&](Vec2 p) {
      const float edge = std::max(std::fabs(p.x), std::fabs(p.z));
      return hills(seed, p.x, p.z) * (1 - smooth(rules::kReliefHalfSize - 80, rules::kReliefHalfSize - 8, edge)) +
             land_at(p.x, p.z);
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
  for (int i = 0; i < Heightmap::kPoints; ++i) {
    for (int j = 0; j < Heightmap::kPoints; ++j) {
      const float x = static_cast<float>(i) * Heightmap::kCellSize - rules::kReliefHalfSize;
      const float z = static_cast<float>(j) * Heightmap::kCellSize - rules::kReliefHalfSize;
      m.heights[static_cast<size_t>(i * Heightmap::kPoints + j)] =
          relief(seed, path, with_tops, knoll_top, castle_top, tops, x, z, land_at(x, z));
    }
  }
  if (west) {
    carve_stream(m, *west, seed);
  }
  return m;
}

float natural_height(uint64_t seed, float x, float z) {
  const float edge = std::max(std::fabs(x), std::fabs(z));
  return hills(seed, x, z) * (1 - smooth(rules::kReliefHalfSize - 80, rules::kReliefHalfSize - 8, edge)) +
         continent_height(seed, x, z);
}

}  // namespace monstres
