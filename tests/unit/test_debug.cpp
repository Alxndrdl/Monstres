#include "monstres/core/debug.hpp"
#include "test_helpers.hpp"

using namespace monstres;

TEST_CASE("to_json describes the world") {
  World w = empty_world();
  add_box(w, {3, 0, 4}, {1, 1, 1});
  w.player.pos = {1.5f, 0.25f, -2};
  w.player.on_ground = false;
  CHECK(to_json(w) ==
        "{\"tick\":0,\"clock\":28800,\"hour\":8.0000,\"player\":{\"x\":1.5000,\"y\":0.2500,\"z\":-2.0000,\"ground\":0.0000,\"zone\":\"meadow\",\"onGround\":false,"
        "\"facingX\":0.0000,\"facingZ\":-1.0000},\"objects\":1,\"creatures\":[]}");
}

TEST_CASE("creature_json describes one creature for the info panel") {
  World w = empty_world();
  Creature c;
  c.species = Species::Bird;
  c.mind = Mind::Flee;
  c.variant = 3;
  c.body.pos = {3, 0, -4};
  w.creatures.push_back(c);
  CHECK(creature_json(w, 0) ==
        "{\"index\":0,\"species\":\"bird\",\"mind\":\"flee\",\"variant\":3,"
        "\"family\":\"bird\",\"elements\":[\"air\"],\"look\":0,"
        "\"variants\":[\"sex.male\",\"plumage.yellow\",\"crest.none\",\"wings.small\"],\"chromatic\":false,\"elemental\":false,"
        "\"level\":1,\"engage\":\"notMonster\",\"released\":false,\"beaten\":false,\"train\":false,\"x\":3.0000,\"y\":0.0000,\"z\":-4.0000,\"distance\":5.0000,\"onGround\":true}");
  CHECK(creature_json(w, 1) == "null");
  CHECK(creature_json(w, -1) == "null");
}
