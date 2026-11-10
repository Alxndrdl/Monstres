#include "monstres/render/scene.hpp"

namespace monstres {

namespace {

float f(int32_t v) { return static_cast<float>(v); }

void hp_bar(std::vector<QuadInstance>& out, Vec2i pos, int32_t hp, int32_t max_hp) {
  const float ratio = max_hp > 0 ? f(hp) / f(max_hp) : 0.0f;
  out.push_back({f(pos.x) + 0.1f, f(pos.y) + 0.05f, 0.8f * ratio, 0.08f, palette::kHpBar});
}

}  // namespace

void build_scene(const World& w, std::vector<QuadInstance>& out) {
  out.clear();
  for (int32_t y = 0; y < rules::kHeight; ++y) {
    for (int32_t x = 0; x < rules::kWidth; ++x) {
      out.push_back({f(x), f(y), 1, 1, (x + y) % 2 ? palette::kTileB : palette::kTileA});
    }
  }
  for (const Monster& m : w.monsters) {
    out.push_back({f(m.pos.x) + 0.15f, f(m.pos.y) + 0.2f, 0.7f, 0.7f, palette::kMonster});
    hp_bar(out, m.pos, m.hp, rules::kMonsterMaxHp);
  }
  const Color player = w.game_over() ? palette::kDead : palette::kPlayer;
  out.push_back({f(w.player.pos.x) + 0.2f, f(w.player.pos.y) + 0.2f, 0.6f, 0.7f, player});
  hp_bar(out, w.player.pos, w.player.hp, rules::kPlayerMaxHp);
}

}  // namespace monstres
