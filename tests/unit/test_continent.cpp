// The continent around the playable area: its regions, its relief on a
// grid, and the far terrain drawn from it (rings of cells without gaps,
// within a budget of triangles and time).

#include "monstres/core/continent.hpp"
#include "monstres/render/far_terrain.hpp"
#include "monstres/render/models.hpp"
#include "test_helpers.hpp"

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
  for (const Vec2 sea : {Vec2{-1100, 0}, Vec2{1150, -2400}, Vec2{1150, 2450}, Vec2{4200, 1500}}) {
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
  const MeshData ground = build_far_ground_mesh(c);
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
      if (std::fabs(edge - rules::kReliefHalfSize) < 1e-3f) {
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
  const size_t triangles = (ground.vertex_count() + water.vertex_count() + lava.vertex_count()) / 3;
  const double megabytes = static_cast<double>((ground.vertices.size() + water.vertices.size()) * sizeof(float)) / 1e6;
  std::printf("continent: grid %.0f ms, meshes %.0f ms, %zu triangles (ground %zu, water %zu), %.1f MB of vertices\n",
              grid_ms, mesh_ms, triangles, ground.vertex_count() / 3, water.vertex_count() / 3, megabytes);
  CHECK(triangles < 140000);  // drawn every frame, on phones too
  CHECK(megabytes < 18);
  if (!kSanitized) CHECK(grid_ms + mesh_ms < 1500);  // once per world (natively; WebAssembly is a bit slower)
}
