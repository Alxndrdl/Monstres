#pragma once

#include "monstres/core/world.hpp"

#include <array>

namespace monstres {

// Battles: the companion fights one wild monster, controlled by the player
// (the player's magician waits). The victory's experience is shared by the
// team members who fought, by the hit points each dealt; the beaten
// monster lies knocked out for a while. Training (Command::Train) works the
// same way against the magician, who shields itself: no one is hurt. Started from the context menu (Command::
// Engage), ended by a victory, a defeat, a capture, or by fleeing (the
// companion runs back to the player first).
//
// During a fight the joystick moves the companion relative to the line
// between it and the enemy (forward: towards the enemy); the jump becomes a
// dodge, and the companion has three attacks: a quick one, and two special
// ones with a cooldown. Parrying blocks the next hit and stuns an enemy
// whose bite was parried. No elemental strengths or weaknesses yet.

namespace rules {
inline constexpr uint16_t kCompanionStartLevel = 3;
inline constexpr float kEngageDistance = 15.0f;     // from the player to the monster
inline constexpr float kBattleLeash = 25.0f;        // companion this far from the enemy: the battle ends
inline constexpr uint16_t kCaptureHpPercent = 25;   // weakened: at most this much of its hit points left
inline constexpr float kFleeArrival = 2.0f;         // the companion is back next to the player
inline constexpr uint16_t kFleeMaxTicks = 8 * rules::kTicksPerSecond;
inline constexpr uint16_t kDodgeTicks = 12;         // 0.2 s
inline constexpr float kDodgeSpeed = 13.0f;         // m/s
inline constexpr uint16_t kDodgeCooldown = 36;
inline constexpr uint16_t kParryTicks = 18;         // 0.3 s guard
inline constexpr uint16_t kParryCooldown = 42;
inline constexpr uint16_t kStunTicks = 70;          // an enemy whose attack was parried
inline constexpr float kProjectileSpeed = 13.0f;
inline constexpr uint16_t kRegenEveryTicks = rules::kTicksPerSecond;  // +1 hit point (out of battle)
inline constexpr float kCompanionMinRunSpeed = 6.0f;  // slow species keep up with the player
}  // namespace rules

enum class MoveShape : uint8_t {
  Melee,       // hits the enemy in reach, in front
  Projectile,  // thrown straight ahead (towards the enemy)
  Burst,       // around itself
};

// One attack. Names and texts in web/lore.js (moves), by identifier.
struct Move {
  const char* id;
  MoveShape shape;
  bool elemental;     // of the monster's element (else Normal)
  uint8_t power;
  uint16_t cooldown;  // ticks, for the companion (wild monsters wait longer)
  float range;        // Melee: reach beyond both bodies; Projectile: distance; Burst: radius
};
using Moves = std::array<Move, 3>;  // quick attack, then two special ones
const Moves& species_moves(Species species);

// Characteristics of a monster at a level.
struct FightStats {
  uint16_t hp, attack, defense, speed, magic;
};
FightStats monster_fight_stats(Species species, uint16_t level);
uint32_t monster_xp_to_next(uint16_t level);

// Hit points dealt by `move` of an attacker with these characteristics.
uint16_t move_damage(const Move& move, const FightStats& attacker, const FightStats& defender);

// The companion as a new team member (new game, old saves).
Monster starting_companion(const Genes& genes);

// Can a battle against creatures[index] start now?
enum class EngageCheck : uint8_t {
  Ok,
  NotMonster,   // villagers, the companion itself
  TooFar,
  InBattle,
  NoCompanion,  // no monster out of the crystal
  KnockedOut,   // already beaten, it lies there
};
EngageCheck can_engage(const World& world, size_t index);
const char* engage_check_name(EngageCheck check);

bool can_capture(const World& world);

// Can the companion start training against the magician now?
bool can_train(const World& world);

// Direction the companion faces the enemy (unit vector on the ground):
// the battle camera looks along it, forward on the joystick goes along it.
bool battle_forward(const World& world, float& fx, float& fz);

// How a body should move during this tick (world.cpp moves it).
struct Steer {
  float dir_x = 0, dir_z = 0;
  float speed = 0;
  bool jump = false;
};

// Wild crows and fish do not move during a battle (up on their perch or in
// the air, in the lake): they bite only what comes close, and throw.
bool holds_its_place(Species species);

// Summon team[slot] out of the staff's crystal, where the companion was (it
// goes back in), or next to the player. If it is the one out: recalled
// (not during a fight). During a fight, swapping is allowed.
void summon(World& world, size_t slot);

// Steps of a tick, called by step() in this order:
void battle_command(World& world, const Input& input);  // Engage, Flee, Capture, Summon, Release, Train
Steer battle_ally(World& world, const Input& input, float dir_x, float dir_z);  // Fight phase
Steer battle_foe(World& world);                         // Fight phase
void battle_resolve(World& world);  // projectiles, timers, victory or defeat (after moving)
void team_regen(World& world);      // hit points come back out of battles

}  // namespace monstres
