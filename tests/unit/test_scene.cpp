#include "monstres/render/scene.hpp"
#include "test_helpers.hpp"

#include <cmath>

using namespace monstres;
using doctest::Approx;

namespace {

// Applies a column-major matrix to a point, returns normalized device coordinates.
Vec3 project(const Mat4& m, Vec3 p) {
  const float x = m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12];
  const float y = m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13];
  const float z = m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14];
  const float w = m.m[3] * p.x + m.m[7] * p.y + m.m[11] * p.z + m.m[15];
  return {x / w, y / w, z / w};
}

const std::vector<Instance>& batch(const DrawList& list, MeshId id) {
  return list.batches[static_cast<size_t>(id)];
}

}  // namespace

TEST_CASE("each object is drawn with the mesh of its kind") {
  World w = empty_world();
  w.objects.push_back(make_object(ObjectKind::Tree, {5, 0, 5}, {1.2f, 1.2f, 1.2f}));
  w.objects.push_back(make_object(ObjectKind::Pine, {-5, 0, 5}, {1, 1, 1}));
  w.objects.push_back(make_object(ObjectKind::Log, {0, 0, 8}, {2, 0.3f, 0}, 16384));
  w.objects.push_back(make_object(ObjectKind::Rock, {8, 0, 0}, {1, 0.5f, 0.8f}));
  w.objects.back().variant = 6;  // -> Rock2
  add_box(w, {-8, 0, 0}, {0.5f, 0.5f, 0.5f});

  DrawList list;
  list.batches.assign(kMeshCount, std::vector<Instance>(3));  // also checks it is cleared first
  build_scene(w, list);
  REQUIRE(list.batches.size() == kMeshCount);

  CHECK(batch(list, MeshId::Cube).size() == 3);  // ground + player body and nose
  CHECK(batch(list, MeshId::Tree).size() == 1);
  CHECK(batch(list, MeshId::Pine).size() == 1);
  CHECK(batch(list, MeshId::Log).size() == 1);
  CHECK(batch(list, MeshId::Rock2).size() == 1);
  CHECK(batch(list, MeshId::Crate).size() == 1);
  CHECK(batch(list, MeshId::Bush).empty());

  const Instance& tree = batch(list, MeshId::Tree)[0];
  CHECK(tree.sy == Approx(1.2f));
  CHECK(tree.y == 0.0f);

  const Instance& log = batch(list, MeshId::Log)[0];
  CHECK(log.yaw == Approx(kPi / 2));
  CHECK(log.sx == Approx(2));
  CHECK(log.sy == Approx(0.3f));

  const Instance& crate = batch(list, MeshId::Crate)[0];
  CHECK(crate.y == Approx(0.5f));  // the crate mesh is centered
}

TEST_CASE("the ground and the player are cubes") {
  World w = empty_world();
  DrawList list;
  build_scene(w, list);
  const auto& cubes = batch(list, MeshId::Cube);
  REQUIRE(cubes.size() == 3);
  CHECK(cubes[0].pattern == 1.0f);  // checkerboard ground
  CHECK(cubes[0].y + cubes[0].sy == Approx(0));
  CHECK(cubes[1].y == Approx(rules::kPlayerHalfWidth));
  CHECK(cubes[1].yaw == Approx(0));  // facing -Z
}

TEST_CASE("the player cube turns to face its direction") {
  World w = empty_world();
  w.player.facing_x = -1;
  w.player.facing_z = 0;
  DrawList list;
  build_scene(w, list);
  const auto& cubes = batch(list, MeshId::Cube);
  CHECK(cubes[2].x < w.player.pos.x);  // the nose is on the -X side
  CHECK(cubes[1].yaw == Approx(kPi / 2));
}

TEST_CASE("the camera looks at the player from behind") {
  World w = empty_world();
  w.player.pos = {3, 0, -4};
  for (float yaw : {0.0f, 1.0f, 2.5f, -2.0f}) {
    CAPTURE(yaw);
    Camera cam;
    cam.yaw = yaw;
    const View v = build_view(w, cam, 16.0f / 9.0f);
    // The point the camera aims at is in the middle of the screen...
    const Vec3 center = project(v.view_projection, {3, 1, -4});
    CHECK(center.x == Approx(0).epsilon(1e-4));
    CHECK(center.y == Approx(0).epsilon(1e-4));
    // ...and a point ahead of the player (in the camera direction) is further away.
    const Vec3 ahead = project(v.view_projection, {3 - std::sin(yaw) * 5, 1, -4 - std::cos(yaw) * 5});
    CHECK(ahead.z > center.z);
    CHECK(v.camera_y > 0.1f);
  }
}
