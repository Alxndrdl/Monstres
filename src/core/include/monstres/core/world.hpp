#pragma once

#include "monstres/core/rng.hpp"

#include <cstdint>
#include <vector>

namespace monstres {

// Game rules use integers only (grid positions, hit points). Floating point
// results can differ between native and WebAssembly builds, integers cannot,
// so a replay gives the exact same state everywhere.

struct Vec2i {
  int32_t x = 0;
  int32_t y = 0;
  bool operator==(const Vec2i&) const = default;
};

enum class Action : uint8_t { None = 0, Up, Down, Left, Right, Attack };

namespace rules {
inline constexpr int32_t kWidth = 16;
inline constexpr int32_t kHeight = 12;
inline constexpr int32_t kTicksPerSecond = 10;
inline constexpr int32_t kPlayerMaxHp = 10;
inline constexpr int32_t kMonsterMaxHp = 3;
inline constexpr uint32_t kKillScore = 10;
inline constexpr uint64_t kMonsterMoveEvery = 8;   // ticks
inline constexpr uint64_t kSpawnEvery = 40;        // ticks
inline constexpr uint32_t kInitialMonsters = 3;
inline constexpr uint32_t kMaxMonsters = 8;
inline constexpr int32_t kMinSpawnDistance = 4;    // from the player
}  // namespace rules

struct Monster {
  uint32_t id = 0;
  Vec2i pos;
  int32_t hp = 0;
  bool operator==(const Monster&) const = default;
};

struct Player {
  Vec2i pos;
  int32_t hp = 0;
  uint32_t score = 0;
  bool operator==(const Player&) const = default;
};

struct World {
  uint64_t tick = 0;
  Rng rng;
  Player player;
  std::vector<Monster> monsters;
  uint32_t next_monster_id = 1;

  bool game_over() const { return player.hp <= 0; }
  bool operator==(const World&) const = default;
};

World new_game(uint64_t seed);

// Advances the world by exactly one fixed tick. Same world + same action
// always gives the same result.
void step(World& world, Action action);

bool inside(Vec2i p);
int32_t manhattan(Vec2i a, Vec2i b);
const Monster* monster_at(const World& world, Vec2i p);

}  // namespace monstres
