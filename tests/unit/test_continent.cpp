// The continent around the playable area: its regions, its relief on a
// grid, and the far terrain drawn from it (rings of cells without gaps,
// within a budget of triangles and time).

#include "monstres/core/continent.hpp"
#include "monstres/core/west.hpp"
#include "monstres/render/far_terrain.hpp"
#include "monstres/render/models.hpp"
#include "test_helpers.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

using namespace monstres;
using doctest::Approx;

namespace {

const Continent& cached_continent() {
  static const Continent c = make_continent(3);
  return c;
}

double ms_since(std::chrono::steady_clock::time_point start) {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

}  // namespace

TEST_CASE("the continent: the start in the west, mountains north, the volcano east, desert south, plains in the middle") {
  const uint64_t seed = 3;
  CHECK(continent_height(seed, 0, 0) == 0);  // the playable area has its own relief
  CHECK(continent_height(seed, 169, -100) == 0);
  // The ocean all around.
  for (const Vec2 sea : {Vec2{-1100, -2400}, Vec2{-1100, 2450}, Vec2{1150, -2420}, Vec2{1150, 2480}, Vec2{4250, -2400}, Vec2{4250, 2450}, Vec2{4200, 1500}}) {
    CHECK(continent_height(seed, sea.x, sea.z) < rules::kSeaLevel - 10);
  }
  // A coast west of the start, not far.
  bool coast = false;
  for (float x = -200; x > -1000; x -= 10) coast = coast || continent_height(seed, x, 0) < rules::kSeaLevel;
  CHECK(coast);
  // Snowy peaks in the north, the active volcano in the east, the lone mountain in the plains.
  float north = 0;
  for (float x = 0; x < 2400; x += 40) north = std::max(north, continent_height(seed, x, -1550));
  CHECK(north > rules::kSnowLine + 50);
  const Crater crater = volcano_crater();
  const float rim = continent_height(seed, crater.center.x + crater.radius, crater.center.z);
  CHECK(rim > 400);
  CHECK(continent_height(seed, crater.center.x, crater.center.z) < crater.lava_level);  // the lava fills it
  CHECK(continent_height(seed, 1950, -330 + 200) > 100);
  // The desert: dry land above the sea; the lake: under its water.
  CHECK(continent_height(seed, 1150, 1500) > rules::kSeaLevel + 3);
  const Pond& lake = continent_ponds()[0];
  CHECK(continent_height(seed, lake.center.x, lake.center.z) < lake.level - 3);
  CHECK(region_at(0, 0) == Region::Start);
  CHECK(region_at(1150, -1600) == Region::Mountains);
  CHECK(region_at(crater.center.x, crater.center.z) == Region::Volcano);
  CHECK(region_at(1150, 1600) == Region::Desert);
  CHECK(region_at(lake.center.x, lake.center.z) == Region::Lake);
  CHECK(region_at(1520, 140) == Region::Capital);
  CHECK(region_at(1300, -100) == Region::Plains);
}

TEST_CASE("the continent: an island all within its grid, its outline lobed, its regions' edges winding") {
  for (uint64_t seed = 0; seed < 6; ++seed) {
    CAPTURE(seed);
    // The ocean all along the grid's edge (the continent not cut off by it).
    float highest = -1e9f;
    for (float x = Continent::kMinX; x <= Continent::kMaxX; x += 20) {
      highest = std::max({highest, continent_height(seed, x, Continent::kMinZ + 100), continent_height(seed, x, Continent::kMaxZ - 100)});
    }
    for (float z = Continent::kMinZ; z <= Continent::kMaxZ; z += 20) {
      highest = std::max({highest, continent_height(seed, Continent::kMinX + 100, z), continent_height(seed, Continent::kMaxX - 100, z)});
    }
    CHECK(highest < rules::kSeaLevel - 2);
  }
  // Never a box: going round the island from its middle, the coast's
  // distance changes a lot (lobes, bays, the peninsula).
  std::vector<float> reach;
  for (int a = 0; a < 72; ++a) {
    const float t = 6.2831853f * static_cast<float>(a) / 72.0f;
    float r = 0;
    for (float d = 200; d < 3200; d += 20) {
      if (continent_height(3, 1300 + std::cos(t) * d, std::sin(t) * d) > rules::kSeaLevel) r = d;
    }
    reach.push_back(r);
  }
  CHECK(*std::max_element(reach.begin(), reach.end()) - *std::min_element(reach.begin(), reach.end()) > 900);
  // The mountains' and the desert's edges wind: where they begin goes back and forth across the island.
  const auto begins = [](auto share, float x, float from, float to, float step) {
    for (float z = from; step > 0 ? z < to : z > to; z += step) {
      if (share(x, z) > 0.5f) return z;
    }
    return to;
  };
  float mountains_lo = 1e9f, mountains_hi = -1e9f, desert_lo = 1e9f, desert_hi = -1e9f;
  for (float x = 900; x < 2600; x += 50) {
    const float m = begins(mountains_share, x, -600, -2000, -10), d = begins(desert_share, x, 600, 2000, 10);
    mountains_lo = std::min(mountains_lo, m);
    mountains_hi = std::max(mountains_hi, m);
    desert_lo = std::min(desert_lo, d);
    desert_hi = std::max(desert_hi, d);
  }
  CHECK(mountains_hi - mountains_lo > 250);
  CHECK(desert_hi - desert_lo > 250);
}

TEST_CASE("the continent's grid: the heights computed at its points, 0 along the playable square") {
  const Continent& c = cached_continent();
  REQUIRE(c.heights.size() == static_cast<size_t>(Continent::kPointsX) * Continent::kPointsZ);
  for (const Vec2 p : {Vec2{-170, 50}, Vec2{1000, -1500}, Vec2{3420, -400}, Vec2{-1170, -2470}}) {
    CHECK(c.at_point(p.x, p.z) == continent_height(3, p.x, p.z));
  }
  for (float t = -170; t <= 170; t += 10) {
    CHECK(c.at_point(t, 170) == 0);
    CHECK(c.at_point(-170, t) == 0);
  }
  CHECK(c.height(5000, 0) == Continent::kDeep);  // beyond the grid: deep ocean
  CHECK(c.height(-165, 200) == Approx(0.5f * (c.at_point(-170, 200) + c.at_point(-160, 200))).epsilon(1e-4));
}

TEST_CASE("the far ground: rings without gaps between them, meeting the playable ground at the relief's edge") {
  const Continent& c = cached_continent();
  using namespace far_terrain;
  // Along each ring's outer edge, the finer ring follows the coarser one's straight edges.
  for (int level = 0; level + 1 < kLevels; ++level) {
    const float e = kOuter[level], fine = kCells[level], coarse = kCells[level + 1];
    for (float t = -e; t < e; t += coarse) {
      const float a = far_vertex_height(c, level + 1, e, t), b = far_vertex_height(c, level + 1, e, t + coarse);
      CHECK(far_vertex_height(c, level, e, t) == a);
      CHECK(far_vertex_height(c, level, e, t + fine) == Approx(0.5f * (a + b)));
      const float d = far_vertex_height(c, level + 1, t, -e), f = far_vertex_height(c, level + 1, t + coarse, -e);
      CHECK(far_vertex_height(c, level, t + fine, -e) == Approx(0.5f * (d + f)));
    }
  }
  const MeshData indexed = build_far_ground_mesh(c);
  CHECK(indexed.vertex_count() * 3 < indexed.draw_count());  // each point shared by several triangles
  const MeshData ground = expanded(indexed);
  CHECK_FALSE(ground.transparent);
  const World& world = cached_game(3);
  bool up = true, seam = true;
  size_t inside = 0;
  for (size_t t = 0; t < ground.vertices.size(); t += 3 * MeshData::kFloatsPerVertex) {
    const float* a = &ground.vertices[t];
    const float* b = a + MeshData::kFloatsPerVertex;
    const float* d = b + MeshData::kFloatsPerVertex;
    const float ny = (d[0] - a[0]) * (b[2] - a[2]) - (b[0] - a[0]) * (d[2] - a[2]);
    if (std::fabs(ny) < 1e-6f) continue;  // the band down along the inner edge
    up = up && ny > 0;
    for (const float* v : {a, b, d}) {
      const float edge = std::max(std::fabs(v[0]), std::fabs(v[2]));
      inside += edge < rules::kReliefHalfSize - 1e-3f;
      // (But where the stream comes in: its bed dug up to the edge, the continent's river going on into it.)
      if (std::fabs(edge - rules::kReliefHalfSize) < 1e-3f && distance_to_stream(world.west.stream, v[0], v[2], 20.0f) > 14.0f) {
        seam = seam && std::fabs(v[1] - world.ground.height(v[0], v[2])) < 1e-3f;
      }
    }
  }
  CHECK(up);
  CHECK(seam);        // the playable relief has the same heights there
  CHECK(inside == 0);  // the playable area is left to its own ground
}

TEST_CASE("the continent stays within its budget: triangles, memory, time") {
  const auto start = std::chrono::steady_clock::now();
  const Continent c = make_continent(11);
  const double grid_ms = ms_since(start);
  const auto meshes = std::chrono::steady_clock::now();
  const MeshData ground = build_far_ground_mesh(c), water = build_far_water_mesh(c), lava = build_lava_mesh(c);
  const double mesh_ms = ms_since(meshes);
  const size_t triangles = ground.triangle_count() + water.triangle_count() + lava.triangle_count();
  const double megabytes = static_cast<double>((ground.vertices.size() + water.vertices.size()) * sizeof(float) +
                                               (ground.indices.size() + water.indices.size()) * sizeof(uint32_t)) / 1e6;
  std::printf("continent: grid %.0f ms, meshes %.0f ms, %zu triangles (ground %zu, water %zu), %.1f MB of vertices\n",
              grid_ms, mesh_ms, triangles, ground.triangle_count(), water.triangle_count(), megabytes);
  CHECK(triangles < 140000);  // drawn every frame, on phones too
  CHECK(megabytes < 18);
  if (!kSanitized) CHECK(grid_ms + mesh_ms < 1500);  // once per world (natively; WebAssembly is a bit slower)
}
