#include "monstres/core/terrain.hpp"
#include "monstres/render/terrain_mesh.hpp"
#include "test_helpers.hpp"

#include <algorithm>
#include <cmath>

using namespace monstres;
using doctest::Approx;

namespace {

// y component of the normal of each triangle: > 0 when it faces up.
template <typename F>
void for_each_triangle(const MeshData& m, F f) {
  constexpr size_t k = MeshData::kFloatsPerVertex;
  for (size_t t = 0; t < m.vertices.size(); t += 3 * k) {
    const float* a = &m.vertices[t];
    const float* b = &m.vertices[t + k];
    const float* c = &m.vertices[t + 2 * k];
    f(a, b, c, (c[0] - a[0]) * (b[2] - a[2]) - (b[0] - a[0]) * (c[2] - a[2]));
  }
}

}  // namespace

TEST_CASE("the path crosses the world and passes by the spawn point") {
  for (uint64_t seed = 0; seed < kManySeeds; ++seed) {
    CAPTURE(seed);
    const Path path = make_path(seed);
    REQUIRE(path.points.size() > 50);
    CHECK(path.points.front().z < -rules::kWorldHalfSize);
    CHECK(path.points.back().z > rules::kWorldHalfSize);
    CHECK(distance_to_path(path, 0, 0) <= 1.0f + 1e-4f);
    for (size_t i = 0; i < path.points.size(); ++i) {
      CHECK(std::fabs(path.points[i].x) <= rules::kPathMaxX + 0.5f);
      if (i > 0) CHECK(path.points[i].z > path.points[i - 1].z);  // by increasing z
      if (i > 0) {
        const float dx = path.points[i].x - path.points[i - 1].x;
        const float dz = path.points[i].z - path.points[i - 1].z;
        CHECK(std::sqrt(dx * dx + dz * dz) < 1.2f);
      }
    }
  }
}

TEST_CASE("the path depends on the seed only") {
  CHECK(make_path(5) == make_path(5));
  CHECK_FALSE(make_path(5) == make_path(6));
}

TEST_CASE("distance_to_path measures to the center line") {
  Path path;
  path.points = {{0, -10}, {0, 10}};
  CHECK(distance_to_path(path, 3, 0) == Approx(3));
  CHECK(distance_to_path(path, -2, 5) == Approx(2));
  CHECK(distance_to_path(path, 0, 13) == Approx(3));  // past the end
}

TEST_CASE("no object is placed on the path") {
  for (uint64_t seed = 0; seed < kManySeeds; ++seed) {
    CAPTURE(seed);
    const World& w = cached_game(seed);
    CHECK(w.path == make_path(seed));
    for (const Object& o : w.objects) {
      CHECK(distance_to_path(w.path, o.pos.x, o.pos.z) > rules::kPathHalfWidth + 0.3f);
    }
  }
}

TEST_CASE("the ground mesh covers the world up to the horizon and faces up") {
  const World& w = cached_game(3);
  const MeshData ground = build_ground_mesh(w);
  CHECK_FALSE(ground.transparent);
  const auto cells = static_cast<size_t>(2 * terrain::kDetailHalfSize / Heightmap::kCellSize);
  CHECK(ground.vertex_count() == cells * cells * 6 + 24);
  float max_x = 0;
  bool all_up = true;
  for_each_triangle(ground, [&](const float* a, const float* b, const float* c, float ny) {
    all_up = all_up && ny > 0;
    max_x = std::max({max_x, std::fabs(a[0]), std::fabs(b[0]), std::fabs(c[0])});
  });
  CHECK(all_up);
  CHECK(max_x == Approx(terrain::kHorizonHalfSize));
}

TEST_CASE("the ground has grass and patches of earth, the same for a seed") {
  const World& w = cached_game(3);
  const MeshData a = build_ground_mesh(w);
  CHECK(a.vertices == build_ground_mesh(w).vertices);
  float min_green = 1, max_green = 0;
  for (size_t i = 0; i < a.vertices.size(); i += MeshData::kFloatsPerVertex) {
    const float g = a.vertices[i + 7];
    min_green = std::min(min_green, g);
    max_green = std::max(max_green, g);
  }
  CHECK(max_green - min_green > 0.15f);  // visible variation
}

TEST_CASE("the path mesh is a ribbon on the path, with soft edges") {
  const World& w = cached_game(3);
  const MeshData path = build_path_mesh(w);
  CHECK(path.transparent);
  REQUIRE(path.vertex_count() > 0);
  bool all_up = true;
  for_each_triangle(path, [&](const float*, const float*, const float*, float ny) { all_up = all_up && ny > 0; });
  CHECK(all_up);
  float min_alpha = 1, max_alpha = 0;
  for (size_t i = 0; i < path.vertices.size(); i += MeshData::kFloatsPerVertex) {
    const float* v = &path.vertices[i];
    CHECK(v[1] == Approx(ground_height(w, v[0], v[2]) + terrain::kPathHeight));  // just above the ground
    CHECK(distance_to_path(w.path, v[0], v[2]) <= rules::kPathHalfWidth * 1.5f);
    min_alpha = std::min(min_alpha, v[9]);
    max_alpha = std::max(max_alpha, v[9]);
  }
  CHECK(min_alpha == 0.0f);
  CHECK(max_alpha == 1.0f);
}

TEST_CASE("value_noise is smooth and within [0, 1]") {
  float previous = value_noise(1, 0, 2.0f, 5);
  for (int i = 1; i < 500; ++i) {
    const float v = value_noise(1, static_cast<float>(i) * 0.05f, 2.0f, 5);
    CHECK(v >= 0.0f);
    CHECK(v <= 1.0f);
    CHECK(std::fabs(v - previous) < 0.1f);
    previous = v;
  }
}
