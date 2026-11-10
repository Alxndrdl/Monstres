#include "monstres/render/scene.hpp"
#include "test_helpers.hpp"

using namespace monstres;

namespace {
bool same(Color a, Color b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; }
}

TEST_CASE("the scene has the board, each monster and the player") {
  World w = empty_world({2, 3});
  add_monster(w, {5, 5});
  add_monster(w, {6, 6});
  std::vector<QuadInstance> quads;
  build_scene(w, quads);

  const size_t tiles = static_cast<size_t>(rules::kWidth * rules::kHeight);
  // tiles + (body + hp bar) per monster + (body + hp bar) for the player
  CHECK(quads.size() == tiles + 2 * 2 + 2);

  const QuadInstance& player = quads[quads.size() - 2];
  CHECK(same(player.color, palette::kPlayer));
  CHECK(player.x > 2.0f);
  CHECK(player.x < 3.0f);
}

TEST_CASE("build_scene clears its output first") {
  World w = empty_world();
  std::vector<QuadInstance> quads(1000);
  build_scene(w, quads);
  CHECK(quads.size() == static_cast<size_t>(rules::kWidth * rules::kHeight) + 2);
}

TEST_CASE("the player turns grey on game over") {
  World w = empty_world();
  w.player.hp = 0;
  std::vector<QuadInstance> quads;
  build_scene(w, quads);
  CHECK(same(quads[quads.size() - 2].color, palette::kDead));
}
