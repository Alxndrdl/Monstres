#pragma once

#include "monstres/core/world.hpp"

#include <doctest.h>

#include <ostream>
#include <set>
#include <utility>

namespace monstres {

// Readable values in doctest failure messages.
inline std::ostream& operator<<(std::ostream& os, Vec2i v) {
  return os << '(' << v.x << ", " << v.y << ')';
}

// A world with no random monsters, for tests that place things by hand.
inline World empty_world(Vec2i player_pos = {rules::kWidth / 2, rules::kHeight / 2}) {
  World w;
  w.rng = Rng(1);
  w.player.pos = player_pos;
  w.player.hp = rules::kPlayerMaxHp;
  return w;
}

inline void add_monster(World& w, Vec2i pos, int32_t hp = rules::kMonsterMaxHp) {
  w.monsters.push_back({w.next_monster_id++, pos, hp});
}

// Rules that must hold after every tick of any game.
inline void check_invariants(const World& w) {
  CHECK(inside(w.player.pos));
  CHECK(w.player.hp >= 0);
  CHECK(w.player.hp <= rules::kPlayerMaxHp);
  CHECK(w.monsters.size() <= rules::kMaxMonsters);
  std::set<std::pair<int32_t, int32_t>> cells{{w.player.pos.x, w.player.pos.y}};
  std::set<uint32_t> ids;
  for (const Monster& m : w.monsters) {
    CHECK(inside(m.pos));
    CHECK(m.hp > 0);
    CHECK(m.hp <= rules::kMonsterMaxHp);
    CHECK(m.id < w.next_monster_id);
    CHECK_MESSAGE(cells.insert({m.pos.x, m.pos.y}).second, "two things on cell ", m.pos);
    CHECK_MESSAGE(ids.insert(m.id).second, "duplicate monster id ", m.id);
  }
}

}  // namespace monstres
