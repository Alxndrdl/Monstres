#include "monstres/core/world.hpp"
#include "monstres/render/meshes.hpp"
#include "monstres/render/scene.hpp"

#include <doctest.h>

#include <algorithm>
#include <cmath>

using namespace monstres;

namespace {

struct Bounds {
  float min[3] = {1e9f, 1e9f, 1e9f};
  float max[3] = {-1e9f, -1e9f, -1e9f};
};

Bounds bounds(const MeshData& m) {
  Bounds b;
  for (size_t i = 0; i < m.vertices.size(); i += MeshData::kFloatsPerVertex) {
    for (int a = 0; a < 3; ++a) {
      b.min[a] = std::min(b.min[a], m.vertices[i + static_cast<size_t>(a)]);
      b.max[a] = std::max(b.max[a], m.vertices[i + static_cast<size_t>(a)]);
    }
  }
  return b;
}

const char* name(size_t id) {
  static const char* names[] = {"cube", "tree", "pine", "rock0", "rock1", "rock2", "rock3",
                                "log", "stump", "bush", "crate"};
  return names[id];
}

}  // namespace

TEST_CASE("every mesh is a valid list of triangles") {
  const std::vector<MeshData> meshes = build_meshes();
  REQUIRE(meshes.size() == kMeshCount);
  for (size_t id = 0; id < meshes.size(); ++id) {
    CAPTURE(name(id));
    const MeshData& m = meshes[id];
    REQUIRE(m.vertex_count() > 0);
    CHECK(m.vertices.size() % MeshData::kFloatsPerVertex == 0);
    CHECK(m.vertex_count() % 3 == 0);
    for (size_t i = 0; i < m.vertices.size(); i += MeshData::kFloatsPerVertex) {
      const float* v = &m.vertices[i];
      const float normal_length = std::sqrt(v[3] * v[3] + v[4] * v[4] + v[5] * v[5]);
      REQUIRE(normal_length == doctest::Approx(1).epsilon(1e-3));
      for (int c = 6; c < 9; ++c) REQUIRE((v[c] >= 0 && v[c] <= 1.2f));
    }
  }
}

TEST_CASE("meshes have the sizes the collision boxes expect") {
  const std::vector<MeshData> meshes = build_meshes();
  auto check = [&](MeshId id, float x0, float y0, float z0, float x1, float y1, float z1) {
    CAPTURE(name(static_cast<size_t>(id)));
    const Bounds b = bounds(meshes[static_cast<size_t>(id)]);
    const float tolerance = 0.02f;
    CHECK(b.min[0] == doctest::Approx(x0).epsilon(tolerance));
    CHECK(b.min[1] == doctest::Approx(y0).epsilon(tolerance));
    CHECK(b.min[2] == doctest::Approx(z0).epsilon(tolerance));
    CHECK(b.max[0] == doctest::Approx(x1).epsilon(tolerance));
    CHECK(b.max[1] == doctest::Approx(y1).epsilon(tolerance));
    CHECK(b.max[2] == doctest::Approx(z1).epsilon(tolerance));
  };
  check(MeshId::Cube, -1, -1, -1, 1, 1, 1);
  check(MeshId::Crate, -1, -1, -1, 1, 1, 1);
  for (size_t i = 0; i < kRockVariants; ++i) {
    check(static_cast<MeshId>(static_cast<size_t>(MeshId::Rock0) + i), -1, 0, -1, 1, 1, 1);
  }
  check(MeshId::Log, -1, 0, -1, 1, 2, 1);
  check(MeshId::Stump, -1, 0, -1, 1, 1, 1);
  check(MeshId::Bush, -1, 0, -1, 1, 1, 1);
}

TEST_CASE("trees stand on the ground and are taller than the player can jump") {
  const std::vector<MeshData> meshes = build_meshes();
  for (MeshId id : {MeshId::Tree, MeshId::Pine}) {
    CAPTURE(name(static_cast<size_t>(id)));
    const Bounds b = bounds(meshes[static_cast<size_t>(id)]);
    CHECK(b.min[1] == doctest::Approx(0));
    CHECK(b.max[1] > 4.0f);
    CHECK(b.max[1] < 5.0f);
    // The trunk collision box (radius 0.28 or 0.25) is inside the crown.
    CHECK(b.max[0] > 0.9f);
  }
}

TEST_CASE("the four rock variants have different shapes") {
  const std::vector<MeshData> meshes = build_meshes();
  for (size_t a = 0; a < kRockVariants; ++a) {
    for (size_t b = a + 1; b < kRockVariants; ++b) {
      CHECK(meshes[static_cast<size_t>(MeshId::Rock0) + a].vertices !=
            meshes[static_cast<size_t>(MeshId::Rock0) + b].vertices);
    }
  }
}
