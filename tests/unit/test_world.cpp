#include "monstres/core/world.hpp"
#include "test_helpers.hpp"

using namespace monstres;
using doctest::Approx;

TEST_CASE("new_game builds a valid world") {
  for (uint64_t seed = 0; seed < 30; ++seed) {
    CAPTURE(seed);
    const World w = new_game(seed);
    CHECK(w.tick == 0);
    CHECK(w.player.pos == Vec3{0, w.ground.height(0, 0), 0});  // standing on the ground
    CHECK(w.player.on_ground);
    size_t nature = 0;
    for (const Object& o : w.objects) nature += o.kind <= ObjectKind::Crate;
    CHECK(nature >= rules::kObjectCount * 9 / 10);  // random placement may not fit them all
    for (size_t i = 0; i < w.objects.size(); ++i) {
      const Object& o = w.objects[i];
      const Box b = object_box(o);
      CHECK(o.kind < ObjectKind::Count);
      // Resting on the ground: slightly sunk, but more than half of it out.
      const float height = b.max.y - b.min.y;
      CHECK(b.min.y == Approx(o.pos.y));
      CHECK(o.pos.y < w.ground.height(o.pos.x, o.pos.z));
      CHECK(o.pos.y >= w.ground.height(o.pos.x, o.pos.z) - 0.05f - 0.4f * height - 1e-4f);
      for (float x : {b.min.x, b.max.x}) {
        for (float z : {b.min.z, b.max.z}) CHECK(o.pos.y < w.ground.height(x, z) + 0.2f * height + 0.05f);
      }
      CHECK(b.max.x <= rules::kWorldHalfSize);
      CHECK(b.min.x >= -rules::kWorldHalfSize);
      if (!is_round(o.kind)) CHECK((o.yaw & 0x3FFF) == 0);  // boxy objects turn by quarters
      for (size_t j = i + 1; j < w.objects.size(); ++j) {
        CHECK_FALSE(overlaps(b, object_box(w.objects[j])));
      }
    }
    check_invariants(w);
  }
}

TEST_CASE("the world has every kind of object") {
  int counts[static_cast<int>(ObjectKind::Count)] = {};
  for (uint64_t seed = 0; seed < 10; ++seed) {
    for (const Object& o : new_game(seed).objects) ++counts[static_cast<int>(o.kind)];
  }
  for (int count : counts) CHECK(count > 0);
  // Trees are the most common.
  CHECK(counts[static_cast<int>(ObjectKind::Tree)] > counts[static_cast<int>(ObjectKind::Crate)]);
}

TEST_CASE("collision boxes follow each kind of object") {
  SUBCASE("a tree blocks only at its trunk, too high to jump over") {
    const Box b = object_box(make_object(ObjectKind::Tree, {2, 0, 3}, {1, 1, 1}));
    CHECK(b.max.x - b.min.x == Approx(0.56f));
    CHECK(b.max.y > rules::kPlayerHeight + 1.3f);
  }
  SUBCASE("a rock is as high as its size") {
    const Box b = object_box(make_object(ObjectKind::Rock, {0, 0, 0}, {1, 0.7f, 0.5f}));
    CHECK(b.max.y == Approx(0.7f));
    CHECK(b.max.x == Approx(0.85f));
    CHECK(b.max.z == Approx(0.425f));
  }
  SUBCASE("a log lies along X, or along Z when turned a quarter") {
    const Box along_x = object_box(make_object(ObjectKind::Log, {0, 0, 0}, {2, 0.3f, 0}));
    CHECK(along_x.max.x == Approx(2));
    CHECK(along_x.max.z == Approx(0.3f));
    CHECK(along_x.max.y == Approx(0.6f));
    const Box along_z = object_box(make_object(ObjectKind::Log, {0, 0, 0}, {2, 0.3f, 0}, 16384));
    CHECK(along_z.max.x == Approx(0.3f));
    CHECK(along_z.max.z == Approx(2));
  }
  SUBCASE("a crate box is its size, from its base") {
    const Box b = object_box(make_object(ObjectKind::Crate, {1, 0.5f, 1}, {0.5f, 0.5f, 0.5f}));
    CHECK(b.min.y == Approx(0.5f));
    CHECK(b.max.y == Approx(1.5f));
  }
}

TEST_CASE("the player can jump onto a fallen log") {
  World w = empty_world();
  w.objects.push_back(make_object(ObjectKind::Log, {0, 0, -2}, {3, 0.35f, 0}));  // top at 0.7 m
  step(w, move(0, 127, 0, true));
  run(w, forward(), 23);       // 0.4 s forward: above the log
  run(w, move(0, 0), 60);      // stop and land
  CHECK(w.player.on_ground);
  CHECK(w.player.pos.y == Approx(0.7f));
}

TEST_CASE("the player cannot walk through a tree trunk") {
  World w = empty_world();
  w.objects.push_back(make_object(ObjectKind::Tree, {0, 0, -3}, {1, 1, 1}));
  run(w, forward(), rules::kTicksPerSecond);
  CHECK(w.player.pos.z == Approx(-3 + 0.28f + rules::kPlayerHalfWidth));
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
