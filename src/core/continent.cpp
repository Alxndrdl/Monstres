#include "monstres/core/continent.hpp"

#include "monstres/core/heightmap.hpp"

#include <algorithm>
#include <cmath>

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
constexpr float kCapitalFlat = 110.0f, kCapitalTop = 20.0f;
constexpr float kMountainsFrom = -1050.0f, kMountainsFull = -1400.0f;  // z: the mountains rise northwards
constexpr float kDesertFrom = 1000.0f, kDesertFull = 1300.0f;          // z: the desert, southwards

constexpr std::array<Pond, 2> kPonds = {{
    {{1130.0f, 330.0f}, 270.0f, 175.0f, 5.0f},  // the lake
    {{1700.0f, 680.0f}, 380.0f, 250.0f, 1.0f},  // the marsh's water, between its tussocks
}};

// How far inside the island (x, z) is: 1 at its middle, 0 on its coast, negative at sea.
float inland(uint64_t seed, float x, float z) {
  // The coast wanders: the point is moved a little before measuring.
  const float wx = x + (value_noise(seed + 503, x, z, 700.0f) - 0.5f) * 420.0f;
  const float wz = z + (value_noise(seed + 504, x, z, 700.0f) - 0.5f) * 420.0f;
  const float dx = std::fabs(wx - kIslandCenter.x) / kIslandHalfX, dz = std::fabs(wz - kIslandCenter.z) / kIslandHalfZ;
  // Between an ellipse and a rounded rectangle (|x|^4 + |z|^4 = 1): sqrt is exact everywhere.
  const float r2 = std::sqrt(dx * dx + dz * dz), r4 = std::sqrt(std::sqrt(dx * dx * dx * dx + dz * dz * dz * dz));
  const float body = 1 - 0.5f * (r2 + r4);
  // The peninsula: distance to its segment.
  const float sx = kPeninsulaTo.x - kPeninsulaFrom.x, sz = kPeninsulaTo.z - kPeninsulaFrom.z;
  const float t = std::clamp(((x - kPeninsulaFrom.x) * sx + (z - kPeninsulaFrom.z) * sz) / (sx * sx + sz * sz), 0.0f, 1.0f);
  const float px = kPeninsulaFrom.x + sx * t - x, pz = kPeninsulaFrom.z + sz * t - z;
  const float cape = (1 - length2(px, pz) / kPeninsulaRadius) * 0.45f;
  // A ragged coast: bays and capes.
  const float ragged = (value_noise(seed + 501, x, z, 420.0f) - 0.5f) * 0.16f +
                       (value_noise(seed + 502, x, z, 130.0f) - 0.5f) * 0.06f;
  return std::max(body, cape) + ragged;
}

// A cone with a crater on top, added to h.
float volcano(float h, Vec2 c, float x, float z, float radius, float height, float crater, float depth) {
  const float r = length2(x - c.x, z - c.z);
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

float pond_factor(const Pond& p, float x, float z) {
  const float dx = (x - p.center.x) / p.radius_x, dz = (z - p.center.z) / p.radius_z;
  return 1 - std::sqrt(dx * dx + dz * dz);  // 1 in the middle, 0 on the shore
}

}  // namespace

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

Crater volcano_crater() {
  const float rim = kVolcanoHeight * (1 - kVolcanoCrater / kVolcanoRadius) * std::sqrt(1 - kVolcanoCrater / kVolcanoRadius);
  return {kVolcano, kVolcanoCrater, rim - kVolcanoCraterDepth * 0.35f};
}

float continent_height(uint64_t seed, float x, float z) {
  const float s = inland(seed, x, z);
  const float on_land = smooth(0.0f, 0.12f, s);
  // At sea: a shelf, then the deep ocean.
  const float sea = rules::kSeaLevel - 2.0f - 50.0f * smooth(0.0f, 0.25f, -s);
  // On land: rising from the beach, rolling hills.
  const float hills = (value_noise(seed + 510, x, z, 260.0f) - 0.5f) * 26.0f +
                      (value_noise(seed + 511, x, z, 90.0f) - 0.5f) * 9.0f +
                      (value_noise(seed + 512, x, z, 30.0f) - 0.5f) * 2.5f;
  float h = mix(sea, rules::kSeaLevel + 1.0f + 10.0f * smooth(0.0f, 0.3f, s) + hills * on_land, smooth(-0.03f, 0.04f, s));

  // The mountains in the north: ridges and peaks, higher towards the north.
  const float north = smooth(kMountainsFrom, kMountainsFull, z) * smooth(0.02f, 0.2f, s);
  if (north > 0) {
    const float ridge = 1 - std::fabs(2 * value_noise(seed + 520, x, z, 330.0f) - 1);
    const float ridge2 = 1 - std::fabs(2 * value_noise(seed + 521, x, z, 120.0f) - 1);
    h += north * (60.0f + ridge * ridge * 380.0f + ridge2 * ridge2 * 90.0f + value_noise(seed + 522, x, z, 45.0f) * 25.0f);
  }
  // The desert in the south: dunes, and a few flat-topped mesas.
  const float south = smooth(kDesertFrom, kDesertFull, z) * smooth(0.02f, 0.15f, s);
  if (south > 0) {
    const float dune = 1 - std::fabs(2 * value_noise(seed + 530, x * 0.6f, z * 1.8f, 90.0f) - 1);
    const float mesa = smooth(0.66f, 0.7f, value_noise(seed + 531, x, z, 300.0f));
    h = mix(h, rules::kSeaLevel + 8.0f + dune * dune * 14.0f + mesa * 45.0f, south);
  }
  // The volcanoes: the active one on the peninsula, the lone mountain in the plains.
  h = volcano(h, kVolcano, x, z, kVolcanoRadius, kVolcanoHeight, kVolcanoCrater, kVolcanoCraterDepth);
  h = volcano(h, kLoneMountain, x, z, kLoneRadius, kLoneHeight, kLoneCrater, kLoneCraterDepth);
  // The capital: a wide flat hilltop.
  {
    const float r = length2(x - kCapital.x, z - kCapital.z);
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

Region region_at(float x, float z) {
  if (std::max(std::fabs(x), std::fabs(z)) < rules::kStartHalfSize) return Region::Start;
  for (const Pond& p : kPonds) {
    if (pond_factor(p, x, z) > 0) return &p == &kPonds[0] ? Region::Lake : Region::Marsh;
  }
  if (length2(x - kCapital.x, z - kCapital.z) < kCapitalFlat * 1.5f) return Region::Capital;
  if (length2(x - kLoneMountain.x, z - kLoneMountain.z) < kLoneRadius * 0.8f) return Region::LoneMountain;
  if (length2(x - kVolcano.x, z - kVolcano.z) < kVolcanoRadius * 0.9f || x > 2700.0f) return Region::Volcano;
  const float fx = (x - kForest.x) / kForestRx, fz = (z - kForest.z) / kForestRz;
  if (fx * fx + fz * fz < 1) return Region::Forest;
  if (z < kMountainsFrom - 150.0f) return Region::Mountains;
  if (z > kDesertFrom + 150.0f) return Region::Desert;
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

}  // namespace monstres
