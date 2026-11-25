#include "monstres/core/continent.hpp"

#include "monstres/core/heightmap.hpp"

#include <algorithm>
#include <cmath>
#include <mutex>

namespace monstres {

namespace {

float smooth(float e0, float e1, float x) {
  const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
  return t * t * (3 - 2 * t);
}

float mix(float a, float b, float t) { return a + (b - a) * t; }

float length2(float x, float z) { return std::sqrt(x * x + z * z); }

// The places (meters). Island: a rounded rectangle; the peninsula: a
// thick segment towards the east.
constexpr Vec2 kIslandCenter{1300.0f, 0.0f};  // its west coast a few hundred meters from the start
constexpr float kIslandHalfX = 1800.0f, kIslandHalfZ = 2000.0f;
constexpr Vec2 kPeninsulaFrom{2550.0f, -150.0f}, kPeninsulaTo{3550.0f, -420.0f};
constexpr float kPeninsulaRadius = 430.0f;
constexpr Vec2 kVolcano{3420.0f, -400.0f};
constexpr float kVolcanoRadius = 640.0f, kVolcanoHeight = 520.0f, kVolcanoCrater = 95.0f, kVolcanoCraterDepth = 60.0f;
constexpr Vec2 kLoneMountain{1950.0f, -330.0f};
constexpr float kLoneRadius = 430.0f, kLoneHeight = 360.0f, kLoneCrater = 70.0f, kLoneCraterDepth = 45.0f;
constexpr Vec2 kForest{850.0f, -470.0f};
constexpr float kForestRx = 430.0f, kForestRz = 300.0f;
constexpr Vec2 kCapital{1520.0f, 140.0f};
constexpr float kCapitalFlat = 110.0f, kCapitalTop = 9.0f;  // low, by its river
constexpr float kMountainsFrom = -1050.0f, kMountainsFull = -1400.0f;  // z: the mountains rise northwards
constexpr float kDesertFrom = 1000.0f, kDesertFull = 1300.0f;          // z: the desert, southwards

constexpr std::array<Pond, 2> kPonds = {{
    {{1130.0f, 330.0f}, 270.0f, 175.0f, 5.0f},  // the lake
    {{1700.0f, 680.0f}, 380.0f, 250.0f, 1.0f},  // the marsh's water, between its tussocks
}};

// How far from the playable area's relief (x, z) is, as a share: 0 on it
// and up to 150 m around (its coast, its mountains: as they were made for
// it), 1 from 600 m on (the continent's own shapes, free to wander).
float free_of_relief(float x, float z) { return smooth(150.0f, 600.0f, -rules::relief_inset(x, z)); }

// The shapes' own noise (the regions' edges: fixed, as the places are).
constexpr uint64_t kShape = 0x5348415045ULL;  // "SHAPE"
float wander(uint64_t salt, float x, float z, float cell) { return 2 * value_noise(kShape + salt, x, z, cell) - 1; }

// How far inside the island (x, z) is: 1 at its middle, 0 on its coast, negative at sea.
float inland(uint64_t seed, float x, float z) {
  const float free = free_of_relief(x, z);
  // The coast wanders: the point is moved a little before measuring (far
  // from the relief, a lot: wide bays, long capes).
  const float wx = x + (value_noise(seed + 503, x, z, 700.0f) - 0.5f) * 420.0f + free * 260.0f * wander(11, x, z, 1500.0f);
  const float wz = z + (value_noise(seed + 504, x, z, 700.0f) - 0.5f) * 420.0f + free * 260.0f * wander(12, x, z, 1500.0f);
  const float sx = (wx - kIslandCenter.x) / kIslandHalfX, sz = (wz - kIslandCenter.z) / kIslandHalfZ;
  const float dx = std::fabs(sx), dz = std::fabs(sz);
  // Between an ellipse and a rounded rectangle (|x|^4 + |z|^4 = 1): sqrt is
  // exact everywhere. By the relief, as it was; beyond, rounder, and lobed
  // (its outline swelling and sinking all round: never a box).
  const float r2 = std::sqrt(dx * dx + dz * dz), r4 = std::sqrt(std::sqrt(dx * dx * dx * dx + dz * dz * dz * dz));
  const float square = 0.5f - 0.35f * free;
  const float a = std::atan2(sz, sx);
  const float swell = 0.12f * std::sin(2 * a + 4.44f) + 0.09f * std::sin(3 * a + 4.1f) + 0.06f * std::sin(5 * a + 2.2f) +
                      0.035f * std::sin(7 * a + 5.0f) + 0.02f * std::sin(11 * a + 0.7f);
  // (Northwards and southwards, hardly out: bays rather, the continent's grid ends not far beyond.)
  const float lobes = 1 + free * std::min(swell, 0.12f - 0.17f * std::fabs(std::sin(a)));
  const float body = 1 - ((1 - square) * r2 + square * r4) / lobes;
  // The peninsula: distance to its segment.
  const float px0 = kPeninsulaTo.x - kPeninsulaFrom.x, pz0 = kPeninsulaTo.z - kPeninsulaFrom.z;
  const float t = std::clamp(((x - kPeninsulaFrom.x) * px0 + (z - kPeninsulaFrom.z) * pz0) / (px0 * px0 + pz0 * pz0), 0.0f, 1.0f);
  const float px = kPeninsulaFrom.x + px0 * t - x, pz = kPeninsulaFrom.z + pz0 * t - z;
  const float cape = (1 - length2(px, pz) / (kPeninsulaRadius * (1 + 0.25f * wander(13, x, z, 300.0f)))) * 0.45f;
  // A ragged coast: bays and capes.
  const float ragged = (value_noise(seed + 501, x, z, 420.0f) - 0.5f) * 0.16f +
                       (value_noise(seed + 502, x, z, 130.0f) - 0.5f) * 0.06f;
  return std::max(body, cape) + ragged;
}

// Where a region's edge is, moved back and forth (meters): far from the
// relief only, winding over a few hundred meters, with smaller bends.
float edge_bend(uint64_t salt, float x, float z, float amount) {
  const float free = smooth(60.0f, 420.0f, -rules::relief_inset(x, z));
  return free * amount * (0.6f * wander(salt, x, z, 520.0f) + 0.3f * wander(salt + 1, x, z, 210.0f) + 0.1f * wander(salt + 2, x, z, 80.0f));
}

// A wavy outline: the radius at an angle, as a share (a few waves of their
// own, each turned its own way by `salt`).
float wavy(uint64_t salt, float a, float amount = 1.0f) {
  const auto phase = [&](uint64_t k) {
    uint64_t h = (salt + 1) * 0x9E3779B97F4A7C15ULL + k * 0xBF58476D1CE4E5B9ULL;
    h ^= h >> 31;
    h *= 0x94D049BB133111EBULL;
    h ^= h >> 29;
    return static_cast<float>(h >> 40) / static_cast<float>(1ULL << 24) * 6.2831853f;
  };
  return 1 + amount * (0.11f * std::sin(2 * a + phase(1)) + 0.075f * std::sin(3 * a + phase(2)) +
                       0.045f * std::sin(5 * a + phase(3)) + 0.03f * std::sin(7 * a + phase(4)) + 0.018f * std::sin(11 * a + phase(5)));
}

// 1 in the middle of a wavy outline around c (radii rx, rz), 0 on it, negative outside.
float blob(Vec2 c, float rx, float rz, uint64_t salt, float x, float z) {
  const float dx = (x - c.x) / rx, dz = (z - c.z) / rz;
  return 1 - std::sqrt(dx * dx + dz * dz) / wavy(salt, std::atan2(dz, dx));
}

// A cone's distance, its foot wavy (its crater kept round).
float cone_distance(Vec2 c, float x, float z, float crater, uint64_t salt) {
  const float r = length2(x - c.x, z - c.z);
  const float a = std::atan2(z - c.z, x - c.x);
  return r / wavy(salt, a, 0.9f * smooth(crater, crater * 2.5f, r));
}

// A cone with a crater on top, added to h.
float volcano(float h, Vec2 c, float x, float z, float radius, float height, float crater, float depth, uint64_t salt) {
  const float r = cone_distance(c, x, z, crater, salt);
  const auto cone = [&](float d) {
    const float t = std::max(0.0f, 1 - d / radius);
    return height * t * std::sqrt(t);  // concave slopes
  };
  float v = cone(r);
  if (r < crater) {
    const float k = r / crater;
    v = cone(crater) - depth * (1 - k * k);
  }
  return h + v;
}

float pond_factor(const Pond& p, float x, float z) { return pond_shape(p, x, z); }

constexpr float kVolcanoFrom = 2650.0f, kVolcanoFull = 2950.0f;  // x: the basalt land, eastwards


}  // namespace

float mountains_share(float x, float z) {
  const float b = edge_bend(21, x, z, 640.0f);
  return smooth(kMountainsFrom + b, kMountainsFull + b, z);
}
float desert_share(float x, float z) {
  const float b = edge_bend(23, x, z, 440.0f);
  return smooth(kDesertFrom + b, kDesertFull + b, z);
}
float volcano_share(float x, float z) {
  const float b = edge_bend(25, x, z, 260.0f);
  return smooth(kVolcanoFrom + b, kVolcanoFull + b, x);
}

const std::array<Landmark, 10>& continent_landmarks() {
  static const std::array<Landmark, 10> kLandmarks = {{
      {Region::Start, {0, 0}, 170},
      {Region::Mountains, {1150, -1550}, 700},
      {Region::Volcano, kVolcano, 450},
      {Region::Desert, {1150, 1550}, 700},
      {Region::Plains, {1150, -50}, 500},
      {Region::Forest, kForest, 330},
      {Region::Marsh, kPonds[1].center, 300},
      {Region::Lake, kPonds[0].center, 220},
      {Region::LoneMountain, kLoneMountain, 300},
      {Region::Capital, kCapital, 110},
  }};
  return kLandmarks;
}

const std::array<Pond, 2>& continent_ponds() { return kPonds; }

float pond_outline(const Pond& p, float angle) {
  const bool lake = std::fabs(p.center.x - kPonds[0].center.x) < 1 && std::fabs(p.center.z - kPonds[0].center.z) < 1;
  return wavy(lake ? 21 : 22, angle);
}

float pond_shape(const Pond& p, float x, float z) {
  const float dx = (x - p.center.x) / p.radius_x, dz = (z - p.center.z) / p.radius_z;
  return 1 - std::sqrt(dx * dx + dz * dz) / pond_outline(p, std::atan2(dz, dx));
}

float place_shape(Region r, float x, float z) {
  switch (r) {
    case Region::Forest: return blob(kForest, kForestRx, kForestRz, 34, x, z);
    case Region::Marsh: return blob(kPonds[1].center, kPonds[1].radius_x * 1.07f, kPonds[1].radius_z * 1.08f, 35, x, z);
    case Region::Capital: return blob(kCapital, kCapitalFlat, kCapitalFlat, 33, x, z);
    default: return 0.0f;
  }
}

Crater volcano_crater() {
  const float rim = kVolcanoHeight * (1 - kVolcanoCrater / kVolcanoRadius) * std::sqrt(1 - kVolcanoCrater / kVolcanoRadius);
  return {kVolcano, kVolcanoCrater, rim - kVolcanoCraterDepth * 0.35f};
}

float continent_height(uint64_t seed, float x, float z) {
  const float s = inland(seed, x, z);
  const float on_land = smooth(0.0f, 0.12f, s);
  // At sea: a shelf, then the deep ocean.
  const float sea = rules::kSeaLevel - 2.0f - 50.0f * smooth(0.0f, 0.25f, -s);
  // (Out at sea, away from the volcano's slopes going down into it: nothing
  // else there, only the ocean's floor; the places are all inland.)
  if (s < -0.3f && length2(x - kVolcano.x, z - kVolcano.z) > kVolcanoRadius * 1.4f &&
      std::max(std::fabs(x), std::fabs(z)) > rules::kStartHalfSize + 260.0f) {
    return sea;
  }
  // On land: rising from the beach, rolling hills.
  const float hills = (value_noise(seed + 510, x, z, 260.0f) - 0.5f) * 26.0f +
                      (value_noise(seed + 511, x, z, 90.0f) - 0.5f) * 9.0f +
                      (value_noise(seed + 512, x, z, 30.0f) - 0.5f) * 2.5f;
  float h = mix(sea, rules::kSeaLevel + 1.0f + 10.0f * smooth(0.0f, 0.3f, s) + hills * on_land, smooth(-0.03f, 0.04f, s));
  // Inland, never below the sea (no sea shut in a hollow of the hills: a low meadow there).
  // (The playable area's own: left as made for it.)
  if (s > 0.02f) h += (std::max(h, rules::kSeaLevel + 1.2f) - h) * smooth(0.02f, 0.1f, s) * smooth(5.0f, 80.0f, -rules::relief_inset(x, z));

  // The mountains in the north: ridges and peaks, higher towards the north.
  const float north = s > 0.02f ? mountains_share(x, z) * smooth(0.02f, 0.2f, s) : 0.0f;
  if (north > 0) {
    // (Read through a warped field, each octave turned its own way: ridges
    // that wind, never along the noise's square cells.)
    const float wx = x + 150.0f * (value_noise(seed + 523, x, z, 420.0f) - 0.5f) + 45.0f * (value_noise(seed + 525, x, z, 130.0f) - 0.5f);
    const float wz = z + 150.0f * (value_noise(seed + 524, x, z, 420.0f) - 0.5f) + 45.0f * (value_noise(seed + 526, x, z, 130.0f) - 0.5f);
    const float ax = 0.857f * wx - 0.515f * wz, az = 0.515f * wx + 0.857f * wz;   // turned by 31°
    const float bx = 0.612f * wx + 0.791f * wz, bz = -0.791f * wx + 0.612f * wz;  // and by -52°
    // (Their crests rounded a little: a sharp fold drawn on the 10 m grid would show its steps.)
    const auto fold = [](float v) { const float t = 2 * v - 1; return 1 - std::sqrt(t * t + 0.012f) + 0.11f; };
    const float ridge = std::clamp(fold(value_noise(seed + 520, ax, az, 330.0f)), 0.0f, 1.0f);
    const float ridge2 = std::clamp(fold(value_noise(seed + 521, bx, bz, 120.0f)), 0.0f, 1.0f);
    h += north * (60.0f + ridge * ridge * 380.0f + ridge2 * ridge2 * 90.0f + value_noise(seed + 522, x, z, 45.0f) * 25.0f);
  }
  // The desert in the south: dunes, and a few flat-topped mesas.
  const float south = s > 0.02f ? desert_share(x, z) * smooth(0.02f, 0.15f, s) : 0.0f;
  if (south > 0) {
    // (The dunes along the wind, bending: their crests do not run straight.)
    const float dx = x + 70.0f * (value_noise(seed + 532, x, z, 260.0f) - 0.5f), dz = z + 70.0f * (value_noise(seed + 533, x, z, 260.0f) - 0.5f);
    const float dt = 2 * value_noise(seed + 530, (0.94f * dx - 0.34f * dz) * 0.6f, (0.34f * dx + 0.94f * dz) * 1.8f, 90.0f) - 1;
    const float dune = std::clamp(1.11f - std::sqrt(dt * dt + 0.012f), 0.0f, 1.0f);
    const float mesa = smooth(0.66f, 0.7f, value_noise(seed + 531, x, z, 300.0f));
    h = mix(h, rules::kSeaLevel + 8.0f + dune * dune * 14.0f + mesa * 45.0f, south);
  }
  // The volcanoes: the active one on the peninsula, the lone mountain in the plains.
  h = volcano(h, kVolcano, x, z, kVolcanoRadius, kVolcanoHeight, kVolcanoCrater, kVolcanoCraterDepth, 31);
  h = volcano(h, kLoneMountain, x, z, kLoneRadius, kLoneHeight, kLoneCrater, kLoneCraterDepth, 32);
  // The capital: a wide flat hilltop (its edge wavy).
  {
    const float r = (1 - blob(kCapital, kCapitalFlat, kCapitalFlat, 33, x, z)) * kCapitalFlat;
    h = mix(h, kCapitalTop, 1 - smooth(kCapitalFlat, kCapitalFlat * 2.2f, r));
  }
  // The lake: a bowl under its water; the marsh: low, lumpy ground around its water.
  {
    const Pond& lake = kPonds[0];
    const float e = pond_factor(lake, x, z);
    h = mix(h, lake.level + 1.5f, smooth(-0.35f, 0.0f, e));
    if (e > 0) h = lake.level - 9.0f * smooth(0.0f, 0.6f, e);
    const Pond& marsh = kPonds[1];
    const float m = smooth(-0.25f, 0.15f, pond_factor(marsh, x, z));
    h = mix(h, marsh.level + (value_noise(seed + 540, x, z, 28.0f) - 0.52f) * 3.0f, m);
  }

  // Around the playable area: down to 0 at its square, where its own relief takes over.
  const float edge = std::max(std::fabs(x), std::fabs(z));
  return h * smooth(rules::kStartHalfSize, rules::kStartHalfSize + 260.0f, edge);
}

float snow_line(uint64_t seed, float x, float z) {
  const float waver = (value_noise(seed + 601, x, z, 60.0f) - 0.5f) * 50.0f + (value_noise(seed + 602, x, z, 260.0f) - 0.5f) * 40.0f;
  const float b = edge_bend(21, x, z, 640.0f);  // (along the mountains' edge)
  return rules::kSnowLine - 150.0f * smooth(-950.0f + b, -1350.0f + b, z) + waver;
}

float island_inland(uint64_t seed, float x, float z) { return inland(seed, x, z); }

Region region_at(float x, float z) {
  if (std::max(std::fabs(x), std::fabs(z)) < rules::kStartHalfSize) return Region::Start;
  for (const Pond& p : kPonds) {
    if (pond_factor(p, x, z) > 0) return &p == &kPonds[0] ? Region::Lake : Region::Marsh;
  }
  if (place_shape(Region::Capital, x, z) > -0.5f) return Region::Capital;
  if (cone_distance(kLoneMountain, x, z, kLoneCrater, 32) < kLoneRadius * 0.8f) return Region::LoneMountain;
  if (cone_distance(kVolcano, x, z, kVolcanoCrater, 31) < kVolcanoRadius * 0.9f || volcano_share(x, z) > 0.15f) return Region::Volcano;
  if (place_shape(Region::Forest, x, z) > 0) return Region::Forest;
  if (mountains_share(x, z) > 0.4f) return Region::Mountains;
  if (desert_share(x, z) > 0.5f) return Region::Desert;
  return Region::Plains;
}

float Continent::at_point(float x, float z) const {
  const float fi = (x - kMinX) / kCell, fj = (z - kMinZ) / kCell;
  const int i = static_cast<int>(std::lround(fi)), j = static_cast<int>(std::lround(fj));
  if (empty() || i < 0 || j < 0 || i >= kPointsX || j >= kPointsZ) return kDeep;
  return at(i, j);
}

float Continent::height(float x, float z) const {
  const float fx = (x - kMinX) / kCell, fz = (z - kMinZ) / kCell;
  if (empty() || fx < 0 || fz < 0 || fx > kPointsX - 1 || fz > kPointsZ - 1) return kDeep;
  const int i = std::min(static_cast<int>(fx), kPointsX - 2), j = std::min(static_cast<int>(fz), kPointsZ - 2);
  const float tx = fx - static_cast<float>(i), tz = fz - static_cast<float>(j);
  const float a = at(i, j), b = at(i + 1, j), c = at(i, j + 1), d = at(i + 1, j + 1);
  return mix(mix(a, b, tx), mix(c, d, tx), tz);
}

Continent make_continent(uint64_t seed) {
  Continent c;
  c.seed = seed;
  c.heights.resize(static_cast<size_t>(Continent::kPointsX) * Continent::kPointsZ);
  for (int i = 0; i < Continent::kPointsX; ++i) {
    const float x = Continent::kMinX + Continent::kCell * static_cast<float>(i);
    for (int j = 0; j < Continent::kPointsZ; ++j) {
      const float z = Continent::kMinZ + Continent::kCell * static_cast<float>(j);
      c.heights[static_cast<size_t>(i * Continent::kPointsZ + j)] = continent_height(seed, x, z);
    }
  }
  return c;
}

std::shared_ptr<const Continent> continent_of(uint64_t seed) {
  static std::mutex lock;
  static std::vector<std::shared_ptr<const Continent>> kept;  // the last asked first
  std::lock_guard<std::mutex> guard(lock);
  for (size_t i = 0; i < kept.size(); ++i) {
    if (kept[i]->seed == seed) {
      std::rotate(kept.begin(), kept.begin() + static_cast<long>(i), kept.begin() + static_cast<long>(i) + 1);
      return kept.front();
    }
  }
  kept.insert(kept.begin(), std::make_shared<const Continent>(make_continent(seed)));
  if (kept.size() > 3) kept.pop_back();
  return kept.front();
}

}  // namespace monstres
