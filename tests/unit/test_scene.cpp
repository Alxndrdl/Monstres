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

}  // namespace

TEST_CASE("the scene has the ground, every object and the player") {
  World w = empty_world();
  add_box(w, {5, 0, 5}, {1, 1, 1});
  add_box(w, {-5, 0, 5}, {1, 2, 1});
  std::vector<CubeInstance> cubes(100);  // also checks it is cleared first
  build_scene(w, cubes);
  REQUIRE(cubes.size() == 1 + 2 + 2);   // ground + objects + player body and nose

  CHECK(cubes[0].pattern == 1.0f);       // ground
  CHECK(cubes[0].y + cubes[0].hy == Approx(0));
  CHECK(cubes[2].hy == 2.0f);

  const CubeInstance& body = cubes[3];
  CHECK(body.y == Approx(rules::kPlayerHalfWidth));  // resting on the ground
  CHECK(body.yaw == Approx(0));                      // facing -Z
}

TEST_CASE("the player cube turns to face its direction") {
  World w = empty_world();
  w.player.facing_x = -1;
  w.player.facing_z = 0;
  std::vector<CubeInstance> cubes;
  build_scene(w, cubes);
  CHECK(cubes.back().x < w.player.pos.x);                    // the nose is on the -X side
  CHECK(cubes[cubes.size() - 2].yaw == Approx(kPi / 2));
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
    // The camera is above the ground.
    CHECK(v.camera_y > 0.1f);
  }
}
