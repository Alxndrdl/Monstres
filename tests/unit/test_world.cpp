#include "monstres/core/world.hpp"
#include "test_helpers.hpp"

using namespace monstres;

TEST_CASE("new_game sets up a valid world") {
  for (uint64_t seed = 0; seed < 50; ++seed) {
    CAPTURE(seed);
    const World w = new_game(seed);
    CHECK(w.tick == 0);
    CHECK(w.player.pos == Vec2i{rules::kWidth / 2, rules::kHeight / 2});
    CHECK(w.player.hp == rules::kPlayerMaxHp);
    CHECK(w.player.score == 0);
    CHECK(w.monsters.size() == rules::kInitialMonsters);
    for (const Monster& m : w.monsters) {
      CHECK(manhattan(m.pos, w.player.pos) >= rules::kMinSpawnDistance);
    }
    check_invariants(w);
  }
}

TEST_CASE("new_game is deterministic") {
  CHECK(new_game(42) == new_game(42));
  CHECK_FALSE(new_game(42) == new_game(43));
}

TEST_CASE("player moves in the four directions") {
  World w = empty_world({5, 5});
  step(w, Action::Right);
  CHECK(w.player.pos == Vec2i{6, 5});
  step(w, Action::Down);
  CHECK(w.player.pos == Vec2i{6, 6});
  step(w, Action::Left);
  CHECK(w.player.pos == Vec2i{5, 6});
  step(w, Action::Up);
  CHECK(w.player.pos == Vec2i{5, 5});
  CHECK(w.tick == 4);
}

TEST_CASE("player cannot leave the map") {
  World w = empty_world({0, 0});
  step(w, Action::Left);
  step(w, Action::Up);
  CHECK(w.player.pos == Vec2i{0, 0});

  w.player.pos = {rules::kWidth - 1, rules::kHeight - 1};
  step(w, Action::Right);
  step(w, Action::Down);
  CHECK(w.player.pos == Vec2i{rules::kWidth - 1, rules::kHeight - 1});
}

TEST_CASE("player cannot walk into a monster") {
  World w = empty_world({5, 5});
  add_monster(w, {6, 5});
  step(w, Action::Right);
  CHECK(w.player.pos == Vec2i{5, 5});
}

TEST_CASE("attacking hurts adjacent monsters only") {
  World w = empty_world({5, 5});
  add_monster(w, {6, 5});  // adjacent
  add_monster(w, {7, 5});  // two cells away
  add_monster(w, {6, 6});  // diagonal: not adjacent
  step(w, Action::Attack);
  const Monster* near = monster_at(w, {6, 5});
  REQUIRE(near != nullptr);  // 1 or 2 damage cannot kill a full-health monster
  CHECK(near->hp < rules::kMonsterMaxHp);
  CHECK(monster_at(w, {7, 5})->hp == rules::kMonsterMaxHp);
  CHECK(monster_at(w, {6, 6})->hp == rules::kMonsterMaxHp);
}

TEST_CASE("killing a monster removes it and gives points") {
  World w = empty_world({5, 5});
  add_monster(w, {5, 4}, 1);
  step(w, Action::Attack);
  CHECK(w.monsters.empty());
  CHECK(w.player.score == rules::kKillScore);
}

TEST_CASE("monsters next to the player hurt it when they act") {
  // The monster at (1,0) acts first and is boxed in by the map edge, the
  // player and two other monsters, so it stays adjacent whatever it rolls.
  World w = empty_world({0, 0});
  add_monster(w, {1, 0});
  add_monster(w, {2, 0});
  add_monster(w, {1, 1});
  for (uint64_t i = 0; i < rules::kMonsterMoveEvery; ++i) step(w, Action::None);
  CHECK(w.player.hp < rules::kPlayerMaxHp);
}

TEST_CASE("a game over world no longer changes") {
  World w = empty_world();
  w.player.hp = 0;
  const World before = w;
  step(w, Action::Right);
  CHECK(w == before);
}

TEST_CASE("monsters spawn over time up to the limit") {
  World w = empty_world({0, 0});
  for (uint64_t i = 0; i < rules::kSpawnEvery * (rules::kMaxMonsters + 4); ++i) {
    step(w, Action::None);
    if (w.game_over()) break;
  }
  CHECK(w.monsters.size() > 0);
  CHECK(w.monsters.size() <= rules::kMaxMonsters);
}
