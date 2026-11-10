#include "monstres/core/debug.hpp"
#include "test_helpers.hpp"

using namespace monstres;

TEST_CASE("to_json describes the world") {
  World w = empty_world({3, 4});
  add_monster(w, {7, 8}, 2);
  w.player.score = 20;
  CHECK(to_json(w) ==
        "{\"tick\":0,\"gameOver\":false,\"player\":{\"x\":3,\"y\":4,\"hp\":10,\"score\":20},"
        "\"monsters\":[{\"id\":1,\"x\":7,\"y\":8,\"hp\":2}]}");
}

TEST_CASE("action names round-trip") {
  for (int i = 0; i <= static_cast<int>(Action::Attack); ++i) {
    const auto a = static_cast<Action>(i);
    CHECK(parse_action(action_name(a)) == a);
  }
  CHECK_FALSE(parse_action("jump"));
}
