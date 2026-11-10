// Functional tests: whole game scenarios driven by player actions, checked
// through the outcome a player would see.

#include "monstres/core/save.hpp"
#include "replay.hpp"
#include "test_helpers.hpp"

#include <sstream>

using namespace monstres;
using namespace monstres::test;

namespace {
Replay replay_from(const std::string& text) {
  std::istringstream in(text);
  return parse_replay(in);
}
}  // namespace

TEST_CASE("walking right stops at the edge of the map") {
  const World w = play(replay_from("seed 3\nactions right*30"));
  // Either the edge or a monster in the way stops the player.
  const bool at_edge = w.player.pos.x == rules::kWidth - 1;
  const bool blocked = monster_at(w, {w.player.pos.x + 1, w.player.pos.y}) != nullptr;
  CHECK((at_edge || blocked));
}

TEST_CASE("the player hunts down a monster") {
  World w = empty_world({2, 2});
  add_monster(w, {6, 2}, rules::kMonsterMaxHp);
  // Walk next to it (ticks 1-3), then attack (ticks 4-6): each hit does 1 or 2
  // damage, so 3 hits always kill it before monsters first act on tick 8.
  step(w, Action::Right);
  step(w, Action::Right);
  step(w, Action::Right);
  for (int i = 0; i < 3; ++i) step(w, Action::Attack);
  CHECK(w.monsters.empty());
  CHECK(w.player.score == rules::kKillScore);
  check_invariants(w);
}

TEST_CASE("the same seed and inputs always give the same game") {
  const Replay r = replay_from("seed 99\nrandom 5 3000");
  CHECK(play(r) == play(r));
}

TEST_CASE("saving and loading in the middle of a game changes nothing") {
  const Replay r = replay_from("seed 11\nrandom 77 2000");
  const World uninterrupted = play(r);

  World w = new_game(r.seed);
  for (size_t i = 0; i < r.actions.size(); ++i) {
    if (i % 250 == 0) {
      auto loaded = deserialize(serialize(w));
      REQUIRE(loaded.has_value());
      w = std::move(*loaded);
    }
    step(w, r.actions[i]);
  }
  CHECK(w == uninterrupted);
}

TEST_CASE("game rules hold during long random games") {
  for (uint64_t seed = 1; seed <= 20; ++seed) {
    CAPTURE(seed);
    World w = new_game(seed);
    Rng inputs(seed * 31);
    for (int t = 0; t < 2000 && !w.game_over(); ++t) {
      step(w, static_cast<Action>(inputs.below(6)));
      check_invariants(w);
    }
  }
}

TEST_CASE("an idle player eventually loses") {
  // Monsters wander randomly; standing still for long enough is fatal.
  World w = new_game(1);
  for (int t = 0; t < 200000 && !w.game_over(); ++t) step(w, Action::None);
  CHECK(w.game_over());
  CHECK(w.player.hp == 0);
}
