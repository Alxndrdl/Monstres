#include "monstres/core/world.hpp"

#include <algorithm>
#include <cstdlib>

namespace monstres {

namespace {

Vec2i direction(Action a) {
  switch (a) {
    case Action::Up:    return {0, -1};
    case Action::Down:  return {0, 1};
    case Action::Left:  return {-1, 0};
    case Action::Right: return {1, 0};
    default:            return {0, 0};
  }
}

Vec2i add(Vec2i a, Vec2i b) { return {a.x + b.x, a.y + b.y}; }

bool is_free(const World& w, Vec2i p) {
  return inside(p) && p != w.player.pos && monster_at(w, p) == nullptr;
}

void spawn_monster(World& w) {
  // A bounded number of tries keeps step() O(1) even on a crowded map.
  for (int attempt = 0; attempt < 32; ++attempt) {
    const Vec2i p{static_cast<int32_t>(w.rng.below(rules::kWidth)),
                  static_cast<int32_t>(w.rng.below(rules::kHeight))};
    if (is_free(w, p) && manhattan(p, w.player.pos) >= rules::kMinSpawnDistance) {
      w.monsters.push_back({w.next_monster_id++, p, rules::kMonsterMaxHp});
      return;
    }
  }
}

void player_turn(World& w, Action action) {
  if (action == Action::Attack) {
    for (Monster& m : w.monsters) {
      if (manhattan(m.pos, w.player.pos) == 1) {
        m.hp -= 1 + static_cast<int32_t>(w.rng.below(2));
      }
    }
    const auto before = w.monsters.size();
    std::erase_if(w.monsters, [](const Monster& m) { return m.hp <= 0; });
    w.player.score += static_cast<uint32_t>(before - w.monsters.size()) * rules::kKillScore;
    return;
  }
  const Vec2i target = add(w.player.pos, direction(action));
  if (target != w.player.pos && is_free(w, target)) {
    w.player.pos = target;
  }
}

void monsters_turn(World& w) {
  static constexpr Action kDirs[] = {Action::Up, Action::Down, Action::Left, Action::Right};
  for (Monster& m : w.monsters) {
    const Vec2i target = add(m.pos, direction(kDirs[w.rng.below(4)]));
    if (is_free(w, target)) {
      m.pos = target;
    }
    if (manhattan(m.pos, w.player.pos) == 1) {
      w.player.hp = std::max(0, w.player.hp - 1);
    }
  }
}

}  // namespace

bool inside(Vec2i p) {
  return p.x >= 0 && p.y >= 0 && p.x < rules::kWidth && p.y < rules::kHeight;
}

int32_t manhattan(Vec2i a, Vec2i b) { return std::abs(a.x - b.x) + std::abs(a.y - b.y); }

const Monster* monster_at(const World& w, Vec2i p) {
  for (const Monster& m : w.monsters) {
    if (m.pos == p) return &m;
  }
  return nullptr;
}

World new_game(uint64_t seed) {
  World w;
  w.rng = Rng(seed);
  w.player.pos = {rules::kWidth / 2, rules::kHeight / 2};
  w.player.hp = rules::kPlayerMaxHp;
  for (uint32_t i = 0; i < rules::kInitialMonsters; ++i) {
    spawn_monster(w);
  }
  return w;
}

void step(World& w, Action action) {
  if (w.game_over()) return;
  ++w.tick;
  player_turn(w, action);
  if (w.tick % rules::kMonsterMoveEvery == 0) {
    monsters_turn(w);
  }
  if (w.tick % rules::kSpawnEvery == 0 && w.monsters.size() < rules::kMaxMonsters) {
    spawn_monster(w);
  }
}

}  // namespace monstres
