#include "monstres/core/debug.hpp"
#include "test_helpers.hpp"

using namespace monstres;

TEST_CASE("to_json describes the world") {
  World w = empty_world();
  add_box(w, {3, 0, 4}, {1, 1, 1});
  w.player.pos = {1.5f, 0.25f, -2};
  w.player.on_ground = false;
  CHECK(to_json(w) ==
        "{\"tick\":0,\"player\":{\"x\":1.5000,\"y\":0.2500,\"z\":-2.0000,\"ground\":0.0000,\"onGround\":false,"
        "\"facingX\":0.0000,\"facingZ\":-1.0000},\"objects\":1,\"creatures\":[]}");
}
