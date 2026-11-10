#include "monstres/core/world.hpp"
#include "test_helpers.hpp"

using namespace monstres;
using doctest::Approx;

TEST_CASE("new_game builds a valid world") {
  for (uint64_t seed = 0; seed < 30; ++seed) {
    CAPTURE(seed);
    const World w = new_game(seed);
    CHECK(w.tick == 0);
    CHECK(w.player.pos == Vec3{0, 0, 0});
    CHECK(w.player.on_ground);
    REQUIRE(w.objects.size() == rules::kObjectCount);
    for (size_t i = 0; i < w.objects.size(); ++i) {
      const Object& o = w.objects[i];
      CHECK(o.center.y == o.half.y);  // resting on the ground
      CHECK(std::fabs(o.center.x) + o.half.x <= rules::kWorldHalfSize);
      CHECK(std::fabs(o.center.z) + o.half.z <= rules::kWorldHalfSize);
      for (size_t j = i + 1; j < w.objects.size(); ++j) {
        CHECK_FALSE(overlaps(object_box(o), object_box(w.objects[j])));
      }
    }
    check_invariants(w);
  }
}

TEST_CASE("new_game is deterministic") {
  CHECK(new_game(42) == new_game(42));
  CHECK_FALSE(new_game(42) == new_game(43));
}

TEST_CASE("the joystick moves the player relative to the camera") {
  const float one_second = rules::kWalkSpeed;

  SUBCASE("camera yaw 0: forward is -Z") {
    World w = empty_world();
    run(w, forward(0), rules::kTicksPerSecond);
    CHECK(w.player.pos.z == Approx(-one_second).epsilon(1e-4));
    CHECK(w.player.pos.x == Approx(0));
  }
  SUBCASE("camera yaw 0: right is +X") {
    World w = empty_world();
    run(w, move(127, 0), rules::kTicksPerSecond);
    CHECK(w.player.pos.x == Approx(one_second).epsilon(1e-4));
  }
  SUBCASE("camera turned 90° left: forward is -X") {
    World w = empty_world();
    run(w, forward(16384), rules::kTicksPerSecond);
    CHECK(w.player.pos.x == Approx(-one_second).epsilon(1e-4));
    CHECK(w.player.pos.z == Approx(0).epsilon(1e-4));
  }
  SUBCASE("diagonals are not faster") {
    World w = empty_world();
    run(w, move(127, 127), rules::kTicksPerSecond);
    const float distance = std::sqrt(w.player.pos.x * w.player.pos.x + w.player.pos.z * w.player.pos.z);
    CHECK(distance == Approx(one_second).epsilon(1e-3));
  }
  SUBCASE("half-way joystick walks at half speed") {
    World w = empty_world();
    run(w, move(0, 64), rules::kTicksPerSecond);
    CHECK(w.player.pos.z == Approx(-one_second * 64 / 127).epsilon(1e-3));
  }
}

TEST_CASE("the player faces where it walks, and keeps facing when it stops") {
  World w = empty_world();
  run(w, move(127, 0), 5);
  CHECK(w.player.facing_x == Approx(1));
  CHECK(w.player.facing_z == Approx(0));
  run(w, move(0, 0), 5);
  CHECK(w.player.facing_x == Approx(1));
}

TEST_CASE("objects block the player") {
  World w = empty_world();
  add_box(w, {0, 0, -3}, {1, 1, 1});  // front face at z = -2
  run(w, forward(), rules::kTicksPerSecond);
  CHECK(w.player.pos.z == Approx(-2 + rules::kPlayerHalfWidth));
  check_invariants(w);
}

TEST_CASE("the player slides along an object when walking diagonally into it") {
  World w = empty_world();
  add_box(w, {0, 0, -3}, {5, 1, 1});
  run(w, move(127, 127), rules::kTicksPerSecond);
  CHECK(w.player.pos.z == Approx(-2 + rules::kPlayerHalfWidth));
  CHECK(w.player.pos.x > 2.0f);  // kept moving sideways
}

TEST_CASE("the world has edges") {
  World w = empty_world();
  run(w, forward(), 20 * rules::kTicksPerSecond);
  CHECK(w.player.pos.z == Approx(-(rules::kWorldHalfSize - rules::kPlayerHalfWidth)));
}

TEST_CASE("jumping goes up about 1.2 m and comes back down") {
  World w = empty_world();
  step(w, jump_input());
  CHECK_FALSE(w.player.on_ground);
  float peak = 0;
  int ticks = 1;
  while (!w.player.on_ground && ticks < 200) {
    step(w, move(0, 0));
    peak = std::max(peak, w.player.pos.y);
    ++ticks;
  }
  CHECK(w.player.on_ground);
  CHECK(w.player.pos.y == 0.0f);
  CHECK(peak == Approx(1.2f).epsilon(0.05));
  CHECK(ticks == doctest::Approx(42).epsilon(0.1));  // ~0.7 s in the air
}

TEST_CASE("jumping in the air does nothing") {
  World w = empty_world();
  step(w, jump_input());
  run(w, move(0, 0), 10);
  const float velocity = w.player.velocity_y;
  step(w, jump_input());
  CHECK(w.player.velocity_y < velocity);  // still falling under gravity, no second jump
}

TEST_CASE("the player can jump onto a low object and stand on it") {
  World w = empty_world();
  add_box(w, {0, 0, -5}, {1, 0.4f, 4});  // top at 0.8 m, from z = -1 to z = -9
  step(w, move(0, 127, 0, true));
  run(w, forward(), rules::kTicksPerSecond);
  CHECK(w.player.on_ground);
  CHECK(w.player.pos.y == Approx(0.8f));
  check_invariants(w);
}

TEST_CASE("an object above stops a jump") {
  World w = empty_world();
  add_box(w, {0, 0, 0}, {2, 0.2f, 2}, 1.4f);  // underside at 1.4 m
  step(w, jump_input());
  float peak = 0;
  for (int i = 0; i < 60; ++i) {
    step(w, move(0, 0));
    peak = std::max(peak, w.player.pos.y);
  }
  CHECK(peak <= 1.4f - rules::kPlayerHeight + 1e-5f);
  CHECK(w.player.on_ground);
  check_invariants(w);
}
