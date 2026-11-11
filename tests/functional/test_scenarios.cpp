// Functional tests: whole game situations driven by player inputs, checked
// through what the player would see.

#include "monstres/core/save.hpp"
#include "replay.hpp"
#include "test_helpers.hpp"

using namespace monstres;
using namespace monstres::test;
using doctest::Approx;

TEST_CASE("walking around an object: blocked, side-step, then past it") {
  World w = empty_world();
  add_box(w, {0, 0, -4}, {1, 1.5f, 1});            // too high to jump over
  run(w, forward(), rules::kTicksPerSecond);       // walk into it
  CHECK(w.player.pos.z == Approx(-3 + rules::kPlayerHalfWidth));
  run(w, move(127, 0), rules::kTicksPerSecond / 2);  // side-step 2.5 m to the right
  run(w, forward(), rules::kTicksPerSecond);       // now nothing in the way
  CHECK(w.player.pos.z < -5.0f);
  check_invariants(w);
}

TEST_CASE("climbing a staircase of boxes by jumping") {
  World w = empty_world();
  add_box(w, {0, 0, -3}, {1, 0.4f, 1});            // step 1: top at 0.8 m
  add_box(w, {0, 0, -7}, {1, 0.8f, 3});            // step 2: top at 1.6 m, long
  // Keep walking and press jump twice a second (presses in the air are ignored).
  for (int i = 0; i < 4; ++i) {
    step(w, move(0, 127, 0, true));
    run(w, forward(), 24);
  }
  run(w, move(0, 0), rules::kTicksPerSecond);      // stop and let the player land
  CHECK(w.player.on_ground);
  CHECK(w.player.pos.y == Approx(1.6f));
  check_invariants(w);
}

TEST_CASE("the same seed and inputs always give the same game") {
  const Replay r = parse_replay("seed 99\nrandom 5 3000");
  CHECK(play(r) == play(r));
}

TEST_CASE("saving and loading in the middle of a game changes nothing") {
  const Replay r = parse_replay("seed 11\nrandom 77 3000");
  const World uninterrupted = play(r);

  World w = new_game(r.seed);
  for (size_t i = 0; i < r.inputs.size(); ++i) {
    if (i % 250 == 0) {
      auto loaded = deserialize(serialize(w));
      REQUIRE(loaded.has_value());
      w = std::move(*loaded);
    }
    step(w, r.inputs[i]);
  }
  CHECK(w == uninterrupted);
}

TEST_CASE("game rules hold during long random games") {
  for (uint64_t seed = 1; seed <= 8; ++seed) {
    CAPTURE(seed);
    World w = new_game(seed);
    const Replay r = parse_replay("random " + std::to_string(seed * 31) + " 4000");
    for (size_t t = 0; t < r.inputs.size(); ++t) {
      step(w, r.inputs[t]);
      if (t % 5 == 0) check_invariants(w);
    }
  }
}
