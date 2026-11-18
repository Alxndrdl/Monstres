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

  if (ruins.radius > 0) {
    // A knoll with a flat top where the tower stands (the enclosure, ±9 m, stands level).
    h = add_knoll(h, ruins.center, x, z, knoll_top, kKnollHeight, 11.0f, ruins.radius + 17);
  }
  if (places.castle.radius > 0) {
    // A higher one for the castle: its walls (±14 m) stand level.
    h = add_knoll(h, places.castle.center, x, z, castle_top, kCastleKnollHeight, rules::kCastleHalf + 5.0f,
                  places.castle.radius + 32);
  }
  if (const CaveLayout& cave = places.cave; cave.radius > 0) {
    // The rocky hill: cliffs all around, a lumpy top; in front of the mouth,
    // a passage stays at the level of the ground outside.
    const float cx = x - cave.center.x, cz = z - cave.center.z;
    const float d = std::sqrt(cx * cx + cz * cz);
    const float rise = (rules::kCaveHillHeight + 2.5f * value_noise(seed + 43, x, z, 7.0f)) *
                       (1 - smooth(cave.radius - 8.0f, cave.radius, d));
    const float mx = x - cave.mouth.x, mz = z - cave.mouth.z;
    const float along = mx * cave.facing.x + mz * cave.facing.z;  // outwards
    const float across = std::fabs(-mx * cave.facing.z + mz * cave.facing.x);
    const float passage = (1 - smooth(2.6f, 3.6f, across)) * (1 - smooth(5.5f, 6.5f, -along));
    h += rise * (1 - passage);
  }

  // The stream runs down a gentle valley of its own: the hills flatten around it.
  if (places.west && places.west->valid()) {
    const float d = distance_to_stream(places.west->stream, x, z, 40.0f);
    h *= 0.15f + 0.85f * smooth(8.0f, 40.0f, d);
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

// The stream's water goes down from its spring (never up): its level at each
// point is below the ground all along before it, then its bed is dug under it,
// its banks sloping back up to the ground around.
void carve_stream(Heightmap& m, WestLayout& west) {
  Stream& s = west.stream;
  s.level.assign(s.points.size(), 0.0f);
  float level = 1e9f;
  for (size_t i = 0; i < s.points.size(); ++i) {
    level = std::min(level, m.height(s.points[i].x, s.points[i].z) - 0.7f);
    s.level[i] = std::max(level, rules::kSeaLevel - 0.6f);
  }
  if (s.points.empty()) return;
  float x0 = 1e9f, x1 = -1e9f, z0 = 1e9f, z1 = -1e9f;
  for (const Vec2& p : s.points) {
    x0 = std::min(x0, p.x);
    x1 = std::max(x1, p.x);
    z0 = std::min(z0, p.z);
    z1 = std::max(z1, p.z);
  }
  constexpr float kBank = 0.6f;  // the banks' steepest slope
  const float reach = rules::kStreamChannel + 30.0f;  // the banks rise back to the highest ground around
  const auto index = [](float v) {
    return std::clamp(static_cast<int>(std::floor((v + rules::kReliefHalfSize) / Heightmap::kCellSize)), 0,
                      Heightmap::kPoints - 1);
  };
  for (int i = index(x0 - reach); i <= std::min(index(x1 + reach) + 1, Heightmap::kPoints - 1); ++i) {
    for (int j = index(z0 - reach); j <= std::min(index(z1 + reach) + 1, Heightmap::kPoints - 1); ++j) {
      const float x = static_cast<float>(i) * Heightmap::kCellSize - rules::kReliefHalfSize;
      const float z = static_cast<float>(j) * Heightmap::kCellSize - rules::kReliefHalfSize;
      float water = 0;
      const float d = distance_to_stream(s, x, z, reach, &water);
      if (d >= reach) continue;
      float& h = m.heights[static_cast<size_t>(i * Heightmap::kPoints + j)];
      const float bed = water - rules::kStreamDepth;
      if (h <= bed) continue;
      // Its bed, then banks no steeper than kBank up to the ground.
      const float t = std::max(0.0f, d - (rules::kStreamWater - 0.3f));
      h = std::min(h, bed + kBank * t);
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
  if (west) carve_stream(m, *west);
  return m;
}

float natural_height(uint64_t seed, float x, float z) {
  const float edge = std::max(std::fabs(x), std::fabs(z));
  return hills(seed, x, z) * (1 - smooth(rules::kReliefHalfSize - 80, rules::kReliefHalfSize - 8, edge)) +
         continent_height(seed, x, z);
}

}  // namespace monstres
