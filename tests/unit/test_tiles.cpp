// The ground beyond the relief (heightmap.hpp: tiles): made around where it
// is looked at, meeting the relief's edge and one another exactly, the same
// whatever was asked first, true to the continent's map, forgotten and made
// again identically.

#include "monstres/core/continent.hpp"
#include "monstres/core/heightmap.hpp"
#include "test_helpers.hpp"

#include <chrono>
#include <cmath>

using namespace monstres;

namespace {

constexpr float kEps = 0.01f;

// A fresh copy of a game's relief: no tile made yet.
Heightmap fresh(uint64_t seed) {
  Heightmap m = cached_game(seed).ground;
  REQUIRE(m.tiles.size() == 0);
  return m;
}

}  // namespace

TEST_CASE("past the relief's edge the ground goes on, without a step") {
  for (uint64_t seed = 0; seed < 4; ++seed) {
    CAPTURE(seed);
    const Heightmap m = fresh(seed);
    const float east = Heightmap::kMinX + Heightmap::kCellSize * Heightmap::kCellsX;
    const float south = Heightmap::kMinZ + Heightmap::kCellSize * Heightmap::kCellsZ;
    for (float t = 0.37f; t < 1.0f; t += 0.05f) {
      const float z = Heightmap::kMinZ + (south - Heightmap::kMinZ) * t, x = Heightmap::kMinX + (east - Heightmap::kMinX) * t;
      CAPTURE(x);
      CAPTURE(z);
      CHECK(std::fabs(m.height(Heightmap::kMinX - kEps, z) - m.height(Heightmap::kMinX + kEps, z)) < 0.05f);  // west
      CHECK(std::fabs(m.height(east + kEps, z) - m.height(east - kEps, z)) < 0.05f);                          // east
      CHECK(std::fabs(m.height(x, Heightmap::kMinZ - kEps) - m.height(x, Heightmap::kMinZ + kEps)) < 0.05f);  // north
      CHECK(std::fabs(m.height(x, south + kEps) - m.height(x, south - kEps)) < 0.05f);                        // south
    }
    // And no wall beyond: walkable slopes most of the way out (the mountains aside).
    int steep = 0, n = 0;
    for (float x = east + 1; x < east + 400; x += 7) {
      for (float z = -400; z < 400; z += 13) {
        const Vec3 nrm = m.normal(x, z);
        steep += nrm.y < 0.5f;
        ++n;
      }
    }
    CHECK(steep * 10 < n);
  }
}

TEST_CASE("the tiles meet exactly, whatever was made first") {
  const float tile = rules::kTileCells * Heightmap::kCellSize;
  // Points along the lines between tiles, far east and far north.
  std::vector<Vec2> probes;
  for (int k = 0; k < 40; ++k) {
    const float x = Heightmap::kMinX + tile * static_cast<float>(14 + k % 9);
    const float z = Heightmap::kMinZ + tile * static_cast<float>(-6 + k / 9) + 17.3f * static_cast<float>(k % 7);
    probes.push_back({x, z});
    probes.push_back({x + 37.1f, Heightmap::kMinZ + tile * static_cast<float>(-6 + k / 9)});
  }
  Heightmap a = fresh(7), b = fresh(7);
  std::vector<float> first;
  for (const Vec2& p : probes) first.push_back(a.height(p.x, p.z));
  // The other way round (other tiles made first, the neighbours in another order).
  for (size_t k = probes.size(); k-- > 0;) {
    CAPTURE(probes[k].x);
    CAPTURE(probes[k].z);
    CHECK(b.height(probes[k].x, probes[k].z) == first[k]);
  }
  // Across a tile's edge: no step (half a millimeter either side: only the slope).
  constexpr float e = 0.0005f;
  for (const Vec2& p : probes) {
    CHECK(std::fabs(a.height(p.x - e, p.z) - a.height(p.x + e, p.z)) < 0.01f);
    CHECK(std::fabs(a.height(p.x, p.z - e) - a.height(p.x, p.z + e)) < 0.01f);
  }
}

TEST_CASE("the ground beyond is the continent's map, with its detail") {
  const Heightmap m = fresh(3);
  int checked = 0, sea_kept = 0, sea = 0;
  float off = 0;
  for (float x = 900; x < 4200; x += 61) {
    for (float z = -2300; z < 2400; z += 67) {
      const float land = m.land_height(x, z), h = m.height(x, z);
      const float lx = 10.0f * std::round(x / 10.0f), lz = 10.0f * std::round(z / 10.0f);  // (on its 10 m points: the continent's own)
      // (raised by the relief's rim: heightmap.hpp, rise)
      CHECK(m.land_height(lx, lz) == doctest::Approx(continent_height(3, lx, lz) + m.rise(lx, lz)).epsilon(1e-4));
      off += std::fabs(h - land);
      ++checked;
      if (land < rules::kSeaLevel - 6.0f) {
        ++sea;
        sea_kept += h < rules::kSeaLevel;  // the open sea stays the sea
      }
    }
  }
  REQUIRE(sea > 50);
  CHECK(sea_kept == sea);
  CHECK(off / static_cast<float>(checked) < 8.0f);  // the map's shape, a little rougher
}

TEST_CASE("tiles are kept a while, the oldest forgotten, made again the same") {
  const Heightmap m = fresh(2);
  const float tile = rules::kTileCells * Heightmap::kCellSize;
  const Vec2 first{1700.3f, 900.7f};
  const float h = m.height(first.x, first.z);
  for (int k = 0; k < static_cast<int>(rules::kTilesKept) + 60; ++k) {
    m.height(2000.0f + tile * static_cast<float>(k % 20), -2000.0f + tile * static_cast<float>(k / 20));
    CHECK(m.tiles.size() <= rules::kTilesKept);
  }
  CHECK(m.height(first.x, first.z) == h);
  // Copies start with none, and are equal all the same.
  const Heightmap copy = m;
  CHECK(copy.tiles.size() == 0);
  CHECK(copy == m);
}

TEST_CASE("tiles are quick to make: those around the player in a moment") {
  const Heightmap m = fresh(5);
  const float tile = rules::kTileCells * Heightmap::kCellSize;
  const auto start = std::chrono::steady_clock::now();
  for (int a = 0; a < 7; ++a) {
    for (int b = 0; b < 7; ++b) m.height(1500.0f + tile * static_cast<float>(a), 300.0f + tile * static_cast<float>(b));
  }
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  CHECK(m.tiles.size() == 49);
  if (!kSanitized) CHECK(ms < 1500.0);  // (49 tiles: a 900 m square)
  MESSAGE("49 tiles in " << ms << " ms");
}
