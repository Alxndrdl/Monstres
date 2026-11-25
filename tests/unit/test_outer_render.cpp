// Drawing the continent beyond the relief (outer_mesh.hpp, far_terrain.hpp):
// the ground near the player in tiles that follow the World's ground and
// meet one another, a band down along the relief's edge only, the far
// terrain with the same ground further on, quick enough.

#include "monstres/core/continent.hpp"
#include "monstres/core/heightmap.hpp"
#include "monstres/render/far_terrain.hpp"
#include "monstres/render/outer_mesh.hpp"
#include "test_helpers.hpp"

#include <chrono>
#include <cmath>
#include <map>
#include <set>

using namespace monstres;

namespace {

const Continent& continent_for(uint64_t seed) { return *monstres::continent_of(seed); }

bool in_relief_rect(float x, float z) { return rules::relief_inset(x, z) > 1e-3f; }

}  // namespace

TEST_CASE("the continent's ground near the player: on the World's ground, tile against tile, none on the relief") {
  const World& w = cached_game(3);
  const Continent& k = continent_for(3);
  const auto tiles = outer_tiles_around(560.0f, -100.0f);
  REQUIRE(tiles.size() > 10);
  std::map<std::pair<float, float>, float> heights;  // every vertex's height, by where it is
  for (const auto& [i, j] : tiles) {
    const MeshData m = build_outer_tile(w, k, i, j);
    REQUIRE_FALSE(m.indices.empty());
    for (size_t v = 0; v < m.vertices.size(); v += MeshData::kFloatsPerVertex) {
      const float x = m.vertices[v], y = m.vertices[v + 1], z = m.vertices[v + 2];
      CHECK_FALSE(in_relief_rect(x, z));
      if (m.vertices[v + 4] < 0.5f) continue;  // the band down along the relief's edge (its normals across)
      CHECK(y == doctest::Approx(w.ground.height(x, z)).epsilon(1e-5));
      const auto key = std::make_pair(x, z);
      if (const auto it = heights.find(key); it != heights.end()) CHECK(it->second == y);  // a shared edge: the same
      heights[key] = y;
    }
  }
}

TEST_CASE("its band down: only where a tile meets the relief") {
  const World& w = cached_game(3);
  const Continent& k = continent_for(3);
  const float east = rules::kReliefHalfSize;
  int with_band = 0, without = 0;
  for (const auto& [i, j] : outer_tiles_around(560.0f, 200.0f)) {
    const MeshData m = build_outer_tile(w, k, i, j);
    bool band = false, next_to_relief = false;
    float min_x = 1e9f;
    for (size_t v = 0; v < m.vertices.size(); v += MeshData::kFloatsPerVertex) {
      band = band || m.vertices[v + 4] < 0.5f;
      min_x = std::min(min_x, m.vertices[v]);
    }
    next_to_relief = std::fabs(min_x - east) < 1e-3f;
    CHECK(band == next_to_relief);
    (band ? with_band : without) += 1;
  }
  CHECK(with_band > 0);
  CHECK(without > 0);
}

TEST_CASE("the far terrain has the same ground as the tiles, further on") {
  const World& w = cached_game(4);
  const Continent& k = continent_for(4);
  int checked = 0;
  for (float x = 800; x < 1280; x += 10) {
    for (float z = -1200; z < 1200; z += 130) {
      const float far = far_vertex_height(k, 0, x, z, &w.ground);
      CHECK(far == doctest::Approx(outer_ground(k.seed, x, z, k.at_point(x, z) + w.ground.rise(x, z), 10.0f)));
      // Against the tiles' own (all the folds): only the finest left out
      // (and the rivers' beds: dug into the tiles only).
      if (RiverSpot r; nearest_river(w.ground.rivers, x, z, rules::kRiverReach + 2, r)) continue;
      CHECK(std::fabs(far - w.ground.height(x, z)) < 2.0f);
      ++checked;
    }
  }
  CHECK(checked > 500);
  // By the relief: exactly the World's ground.
  for (float z = -1200; z < 600; z += 37) CHECK(far_vertex_height(k, 0, rules::kReliefHalfSize + 20, std::round(z / 10) * 10, &w.ground) ==
                                                 w.ground.height(rules::kReliefHalfSize + 20, std::round(z / 10) * 10));
}

TEST_CASE("the far terrain with its folds stays within its budget") {
  const World& w = cached_game(11);
  const Continent& k = continent_for(11);
  const auto start = std::chrono::steady_clock::now();
  const MeshData ground = build_far_ground_mesh(k, &w.ground);
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  MESSAGE("far ground with its folds: " << ms << " ms, " << ground.triangle_count() << " triangles");
  CHECK(ground.triangle_count() < 140000);
  if (!kSanitized) CHECK(ms < 1500.0);
}
