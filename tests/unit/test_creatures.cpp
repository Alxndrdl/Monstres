// The companion and the wild monsters: where they appear and how they behave.

#include "monstres/core/world.hpp"
#include "test_helpers.hpp"

#include <cmath>

using namespace monstres;
using doctest::Approx;

namespace {

float distance_xz(const Body& a, const Body& b) {
  const float dx = a.pos.x - b.pos.x, dz = a.pos.z - b.pos.z;
  return std::sqrt(dx * dx + dz * dz);
}

World world_with_companion() {
  World w = empty_world();
  spawn_creatures(w);
  w.creatures.resize(1);  // keep only the companion
  return w;
}

Creature& first_of(World& w, Species species) {
  for (Creature& c : w.creatures) {
    if (c.species == species) return c;
  }
  FAIL("no creature of that species");
  return w.creatures[0];
}

}  // namespace

TEST_CASE("a new game has the companion next to the player and wild monsters around") {
  for (uint64_t seed = 0; seed < 20; ++seed) {
    CAPTURE(seed);
    const World w = new_game(seed);
    REQUIRE(!w.creatures.empty());
    CHECK(w.creatures[0].species == Species::Companion);
    CHECK(distance_xz(w.creatures[0].body, w.player) < 2.0f);
    size_t wild = 0;
    for (size_t i = 1; i < w.creatures.size(); ++i) {
      const Creature& c = w.creatures[i];
      CHECK(c.species != Species::Companion);
      CHECK(distance_xz(c.body, w.player) >= rules::kMonsterSpawnMinDistance);
      if (c.species <= Species::Bird) {
        ++wild;
        CHECK(c.home.x == c.body.pos.x);
      }
    }
    CHECK(wild == rules::kWildMonsters);
    check_invariants(w);
  }
}

TEST_CASE("every wild species appears") {
  int counts[static_cast<int>(Species::Count)] = {};
  for (uint64_t seed = 0; seed < 5; ++seed) {
    for (const Creature& c : new_game(seed).creatures) ++counts[static_cast<int>(c.species)];
  }
  for (int count : counts) CHECK(count > 0);
}

TEST_CASE("creatures do not change the object layout") {
  World w = new_game(77);
  const auto objects = w.objects;
  spawn_creatures(w);
  CHECK(w.objects == objects);
}

TEST_CASE("the companion follows the player") {
  World w = world_with_companion();
  run(w, forward(), 5 * rules::kTicksPerSecond);  // the player walks 25 m
  CHECK(w.player.pos.z < -20.0f);
  run(w, move(0, 0), 2 * rules::kTicksPerSecond);  // and waits
  const Creature& c = w.creatures[0];
  CHECK(distance_xz(c.body, w.player) < 2.0f);
  CHECK(c.mind == Mind::Idle);
  // It looks at the player once arrived.
  const float dx = w.player.pos.x - c.body.pos.x, dz = w.player.pos.z - c.body.pos.z;
  const float d = std::sqrt(dx * dx + dz * dz);
  CHECK(c.body.facing_x * dx / d + c.body.facing_z * dz / d > 0.9f);
}

TEST_CASE("the companion catches up when left far behind") {
  World w = world_with_companion();
  w.player.pos = {20, 0, 20};
  step(w, move(0, 0));
  CHECK(distance_xz(w.creatures[0].body, w.player) < 2.0f);
}

TEST_CASE("the companion jumps to follow the player onto a crate") {
  World w = world_with_companion();
  add_box(w, {0, 0, -4}, {2, 0.3f, 2});  // top at 0.6 m
  w.player.pos = {0, 0.6f, -4};
  run(w, move(0, 0), 6 * rules::kTicksPerSecond);
  const Creature& c = w.creatures[0];
  CHECK(c.body.pos.y == Approx(0.6f));
  CHECK(distance_xz(c.body, w.player) < 2.0f);
}

TEST_CASE("wild monsters wander around their home") {
  World w = new_game(5);
  w.player.pos = {0, 0, 0};
  std::vector<Vec3> start;
  for (const Creature& c : w.creatures) start.push_back(c.body.pos);
  run(w, move(0, 0), 60 * rules::kTicksPerSecond);  // the player stays at the spawn point
  int moved = 0;
  for (size_t i = 1; i < w.creatures.size(); ++i) {
    const Creature& c = w.creatures[i];
    if (c.species > Species::Bird) continue;  // the village and the lake have their own tests
    CAPTURE(i);
    const float dx = c.body.pos.x - c.home.x, dz = c.body.pos.z - c.home.z;
    CHECK(std::sqrt(dx * dx + dz * dz) < rules::kWanderRadius * 1.5f + 1.0f);
    if (!(c.body.pos == start[i])) ++moved;
  }
  CHECK(moved >= static_cast<int>(rules::kWildMonsters) - 2);
  check_invariants(w);
}

TEST_CASE("slimes only move while hopping") {
  World w = new_game(5);
  Creature& slime = first_of(w, Species::Slime);
  const size_t index = static_cast<size_t>(&slime - w.creatures.data());
  int hops = 0;
  for (int t = 0; t < 20 * rules::kTicksPerSecond; ++t) {
    const Body before = w.creatures[index].body;
    step(w, move(0, 0));
    const Body& after = w.creatures[index].body;
    const bool moved = before.pos.x != after.pos.x || before.pos.z != after.pos.z;
    if (moved) CHECK_FALSE((before.on_ground && after.on_ground));
    if (before.on_ground && !after.on_ground) ++hops;
  }
  CHECK(hops > 3);
}

TEST_CASE("birds run away from the player") {
  World w = new_game(5);
  Creature& bird = first_of(w, Species::Bird);
  const size_t index = static_cast<size_t>(&bird - w.creatures.data());
  // Put the player right next to it.
  w.player.pos = {bird.body.pos.x + 1.5f, 0, bird.body.pos.z};
  w.player.on_ground = true;
  const float before = distance_xz(w.creatures[index].body, w.player);
  step(w, move(0, 0));
  CHECK(w.creatures[index].mind == Mind::Flee);
  run(w, move(0, 0), rules::kTicksPerSecond);
  CHECK(distance_xz(w.creatures[index].body, w.player) > before + 2.0f);
}

TEST_CASE("creatures keep the rules during long games") {
  World w = new_game(31);
  Rng inputs(8);
  for (int t = 0; t < 3000; ++t) {
    step(w, move(static_cast<int8_t>(static_cast<int>(inputs.below(255)) - 127), 100,
                  static_cast<Angle16>(inputs.below(65536)), inputs.below(30) == 0));
    if (t % 10 == 0) check_invariants(w);
  }
}
