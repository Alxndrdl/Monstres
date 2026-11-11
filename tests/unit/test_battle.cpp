// Battles: the companion against one wild monster, the team, capture.

#include "monstres/core/battle.hpp"
#include "monstres/core/debug.hpp"
#include "monstres/core/save.hpp"
#include "monstres/core/variants.hpp"
#include "test_helpers.hpp"

#include <string>

using namespace monstres;

namespace {

// Flat empty ground: the player at the origin, the companion just behind,
// a slime 3 m ahead (index 1), a villager and a crow further away.
World arena(Species foe = Species::Slime, uint8_t level = 2) {
  World w = empty_world();
  Creature companion;
  companion.species = Species::Companion;
  companion.body.pos = {0, 0, 1};
  w.creatures.push_back(companion);
  Creature wild;
  wild.species = foe;
  wild.level = level;
  wild.body.pos = {0, 0, -3};
  wild.home = {0, -3};
  wild.timer = 1000;
  w.creatures.push_back(wild);
  Creature villager;
  villager.species = Species::Villager;
  villager.body.pos = {5, 0, 0};
  villager.home = {5, 0};
  w.creatures.push_back(villager);
  Creature crow;
  crow.species = Species::Crow;
  crow.body.pos = {-5, 0, 0};
  w.creatures.push_back(crow);
  w.team = {starting_companion({})};
  return w;
}

Input command(Command c, uint16_t target = 0) {
  Input in;
  in.command = c;
  in.target = target;
  return in;
}

Input attack_input(uint8_t slot) {
  Input in;
  in.attack = slot;
  return in;
}

void engage(World& w, uint16_t target = 1) {
  step(w, command(Command::Engage, target));
  REQUIRE(w.battle.phase == BattlePhase::Fight);
}

}  // namespace

TEST_CASE("a new game starts with the companion alone in the team") {
  const World w = new_game(42);
  REQUIRE(w.team.size() == 1);
  CHECK(w.team[0].species == Species::Companion);
  CHECK(w.team[0].level == rules::kCompanionStartLevel);
  CHECK(w.team[0].hp == monster_fight_stats(Species::Companion, w.team[0].level).hp);
  CHECK(w.battle.phase == BattlePhase::None);
  // Wild monsters have a level, a little higher in the ruins.
  for (size_t i = 1; i < w.creatures.size(); ++i) {
    const Creature& c = w.creatures[i];
    if (!monster_stats(c.species).is_monster) continue;
    const bool ruins = zone_at(w, c.home.x, c.home.z) == Zone::Ruins;
    CHECK(c.level >= (ruins ? 3 : 1));
    CHECK(c.level <= (ruins ? 5 : 3));
  }
}

TEST_CASE("stats grow with the level, damage depends on attack and defense") {
  const FightStats l1 = monster_fight_stats(Species::Slime, 1);
  const FightStats l9 = monster_fight_stats(Species::Slime, 9);
  CHECK(l1.hp == monster_stats(Species::Slime).hp);
  CHECK(l9.hp == 2 * l1.hp);
  CHECK(l9.attack > l1.attack);
  const Moves& moves = species_moves(Species::Companion);
  CHECK(moves[0].shape == MoveShape::Melee);
  CHECK(moves[1].shape == MoveShape::Projectile);
  CHECK(moves[2].shape == MoveShape::Burst);
  CHECK(moves[1].cooldown > moves[0].cooldown);
  const FightStats pompon = monster_fight_stats(Species::Companion, 3);
  CHECK(move_damage(moves[0], pompon, l1) >= 1);
  CHECK(move_damage(moves[2], pompon, l1) > move_damage(moves[0], pompon, l1));
  CHECK(move_damage(moves[0], pompon, l9) <= move_damage(moves[0], pompon, l1));
}

TEST_CASE("a battle starts only against a wild monster on the ground, not too far") {
  World w = arena();
  CHECK(can_engage(w, 0) == EngageCheck::NotMonster);  // the companion
  CHECK(can_engage(w, 2) == EngageCheck::NotMonster);  // a villager
  CHECK(can_engage(w, 3) == EngageCheck::Unreachable); // a crow
  CHECK(can_engage(w, 99) == EngageCheck::NotMonster);
  CHECK(can_engage(w, 1) == EngageCheck::Ok);
  w.creatures[1].body.pos = {0, 0, -30};
  CHECK(can_engage(w, 1) == EngageCheck::TooFar);
  step(w, command(Command::Engage, 1));
  CHECK(w.battle.phase == BattlePhase::None);

  w = arena();
  step(w, command(Command::Engage, 2));
  CHECK(w.battle.phase == BattlePhase::None);
  engage(w);
  CHECK(w.creatures[1].mind == Mind::Battle);
  CHECK(w.battle.enemy == 1);
  CHECK(w.battle.enemy_hp == monster_fight_stats(Species::Slime, 2).hp);
  CHECK(can_engage(w, 1) == EngageCheck::InBattle);
}

TEST_CASE("in a battle the joystick moves the companion towards the enemy, the player waits") {
  World w = arena();
  engage(w);
  const Vec3 player = w.player.pos;
  const float before = w.creatures[0].body.pos.z;
  run(w, move(0, 127, 12345), 20);  // forward, whatever the camera
  CHECK(w.player.pos == player);
  CHECK(w.creatures[0].body.pos.z < before - 1.0f);  // the enemy is towards -Z
  float fx = 0, fz = 0;
  REQUIRE(battle_forward(w, fx, fz));
  CHECK(fz < -0.9f);
  // It faces its enemy, even when moving sideways.
  run(w, move(127, 0), 10);
  CHECK(w.creatures[0].body.facing_z < -0.5f);
}

TEST_CASE("the quick attack hits in reach and has a short cooldown") {
  World w = arena();
  engage(w);
  w.creatures[0].body.pos = {0, 0, -2.2f};  // next to the slime
  const uint16_t hp = w.battle.enemy_hp;
  step(w, attack_input(1));
  CHECK(w.battle.enemy_hp < hp);
  CHECK(w.battle.foe.hit_tick == w.tick);
  const uint16_t after_one = w.battle.enemy_hp;
  step(w, attack_input(1));  // too soon
  CHECK(w.battle.enemy_hp == after_one);
  CHECK(w.battle.ally.cooldown[0] > 0);
  run(w, Input{}, species_moves(Species::Companion)[0].cooldown);
  step(w, attack_input(1));
  CHECK(w.battle.enemy_hp < after_one);

  // Out of reach, the quick attack misses.
  World far = arena();
  engage(far);
  const uint16_t full = far.battle.enemy_hp;
  step(far, attack_input(1));
  CHECK(far.battle.enemy_hp == full);
}

TEST_CASE("a special attack throws a projectile that flies to the enemy") {
  World w = arena();
  engage(w);
  const uint16_t hp = w.battle.enemy_hp;
  step(w, attack_input(2));
  REQUIRE(w.battle.projectiles.size() == 1);
  CHECK(w.battle.projectiles[0].element == Element::Fire);
  CHECK(w.battle.projectiles[0].from_ally);
  for (int i = 0; i < 40 && !w.battle.projectiles.empty(); ++i) step(w, Input{});
  CHECK(w.battle.projectiles.empty());
  CHECK(w.battle.enemy_hp < hp);
  CHECK(w.battle.ally.cooldown[1] > 0);
}

TEST_CASE("the enemy shows its attack first: a dodge or a parry avoids it") {
  // Let the slime attack a companion that stands still.
  World w = arena();
  engage(w);
  w.creatures[0].body.pos = {0, 0, -2.2f};
  w.battle.foe.cooldown = {};
  int ticks = 0;
  while (w.battle.foe.state != FighterState::Windup && ticks++ < 200) step(w, Input{});
  REQUIRE(w.battle.foe.state == FighterState::Windup);
  World dodging = w, parrying = w, standing = w;

  const uint16_t full = standing.team[0].hp;
  while (standing.battle.foe.state == FighterState::Windup) step(standing, Input{});
  CHECK(standing.team[0].hp < full);
  CHECK(standing.battle.ally.hit_tick == standing.tick);

  // Dodge just before the hit: back, out of reach and untouchable.
  while (dodging.battle.foe.state_ticks > 3) step(dodging, Input{});
  step(dodging, jump_input());
  CHECK(dodging.battle.ally.state == FighterState::Dodge);
  while (dodging.battle.foe.state == FighterState::Windup) step(dodging, Input{});
  CHECK(dodging.team[0].hp == full);

  // Parry: no damage, and the enemy is stunned.
  while (parrying.battle.foe.state_ticks > 3) step(parrying, Input{});
  Input parry;
  parry.parry = true;
  step(parrying, parry);
  while (parrying.battle.foe.state == FighterState::Windup) step(parrying, Input{});
  CHECK(parrying.team[0].hp == full);
  CHECK(parrying.battle.ally.parried_tick > 0);
  if (parrying.battle.foe.move == 0) CHECK(parrying.battle.foe.state == FighterState::Stunned);
}

TEST_CASE("capture needs a weakened enemy; the monster then joins the team") {
  World w = arena();
  engage(w);
  CHECK_FALSE(can_capture(w));
  step(w, command(Command::Capture));
  CHECK(w.battle.phase == BattlePhase::Fight);
  CHECK(w.team.size() == 1);

  const Genes genes = w.creatures[1].genes;
  w.battle.enemy_hp = static_cast<uint16_t>(w.battle.enemy_max_hp * rules::kCaptureHpPercent / 100);
  CHECK(can_capture(w));
  const size_t creatures = w.creatures.size();
  const uint32_t xp = w.team[0].xp;
  step(w, command(Command::Capture));
  CHECK(w.battle.phase == BattlePhase::None);
  CHECK(w.battle.outcome == BattleOutcome::Captured);
  REQUIRE(w.team.size() == 2);
  CHECK(w.team[1].species == Species::Slime);
  CHECK(w.team[1].genes == genes);
  CHECK(w.team[1].level == 2);
  CHECK(w.creatures.size() == creatures - 1);
  CHECK(w.creatures[1].species == Species::Villager);
  CHECK(w.team[0].xp > xp);
  CHECK(w.dex[static_cast<size_t>(Species::Slime)][0] == Discovery::Captured);

  // A full team captures no more.
  World full = arena();
  full.team.resize(rules::kTeamSize, starting_companion({}));
  engage(full);
  full.battle.enemy_hp = 1;
  CHECK_FALSE(can_capture(full));
}

TEST_CASE("fleeing: the companion runs back to the player, then the battle ends") {
  World w = arena();
  engage(w);
  w.creatures[0].body.pos = {6, 0, -6};  // fighting away from the player
  step(w, command(Command::Flee));
  CHECK(w.battle.phase == BattlePhase::Flee);
  CHECK(w.creatures[1].mind != Mind::Battle);
  int ticks = 0;
  while (w.battle.phase != BattlePhase::None && ticks++ < 600) step(w, move(127, 0));  // the joystick does not matter
  CHECK(w.battle.phase == BattlePhase::None);
  CHECK(w.battle.outcome == BattleOutcome::Fled);
  const float dx = w.creatures[0].body.pos.x - w.player.pos.x, dz = w.creatures[0].body.pos.z - w.player.pos.z;
  CHECK(dx * dx + dz * dz <= rules::kFleeArrival * rules::kFleeArrival + 0.5f);
  // The player can walk again.
  const Vec3 p = w.player.pos;
  run(w, forward(), 10);
  CHECK(w.player.pos.z < p.z);
}

TEST_CASE("victory gives experience, a defeat leaves the companion weak") {
  World w = arena();
  engage(w);
  w.battle.enemy_hp = 1;
  w.creatures[0].body.pos = {0, 0, -2.2f};
  const uint32_t wizard_xp = w.wizard.xp;
  step(w, attack_input(1));
  CHECK(w.battle.phase == BattlePhase::None);
  CHECK(w.battle.outcome == BattleOutcome::Victory);
  CHECK(w.battle.outcome_xp == 16u);
  CHECK(w.team[0].xp == 16u);
  CHECK(w.wizard.xp == wizard_xp + 8u);
  CHECK(w.creatures[1].mind == Mind::Flee);

  World lost = arena();
  engage(lost);
  lost.team[0].hp = 0;
  step(lost, Input{});
  CHECK(lost.battle.outcome == BattleOutcome::Defeat);
  CHECK(lost.team[0].hp == 1);
  run(lost, Input{}, 3 * rules::kTicksPerSecond);  // it rests
  CHECK(lost.team[0].hp > 1);
}

TEST_CASE("levels go up with experience") {
  World w = arena();
  engage(w);
  w.team[0].xp = monster_xp_to_next(w.team[0].level) - 1;
  w.team[0].hp = 10;
  const uint16_t level = w.team[0].level;
  w.battle.enemy_hp = 1;
  w.creatures[0].body.pos = {0, 0, -2.2f};
  step(w, attack_input(1));
  CHECK(w.team[0].level == level + 1);
  CHECK(w.team[0].hp > 10);  // the hit points gained come at once
}

TEST_CASE("battles are deterministic") {
  World a = arena(), b = arena();
  for (World* w : {&a, &b}) {
    engage(*w);
    for (int i = 0; i < 400; ++i) {
      Input in = move(static_cast<int8_t>((i * 37) % 255 - 127), 90);
      in.attack = static_cast<uint8_t>(i % 7 == 0 ? 1 + (i / 7) % 3 : 0);
      in.jump = i % 53 == 0;
      in.parry = i % 41 == 0;
      step(*w, in);
    }
  }
  CHECK(a == b);
}

TEST_CASE("the team and the levels are saved, not the battle") {
  World w = new_game(7);
  w.team.push_back({Species::Bird, {}, 4, 12, 9});
  w.creatures[1].level = 3;
  const auto loaded = deserialize(serialize(w));
  REQUIRE(loaded.has_value());
  CHECK(loaded->team == w.team);
  CHECK(loaded->creatures == w.creatures);

  // A game saved during a battle goes on without it.
  World fighting = arena();
  fighting.seed = 3;
  engage(fighting);
  const auto during = deserialize(serialize(fighting));
  REQUIRE(during.has_value());
  CHECK(during->battle.phase == BattlePhase::None);
  CHECK(during->creatures[1].mind == Mind::Idle);
}

TEST_CASE("team and battle JSON for the interface") {
  World w = arena();
  const std::string team = team_json(w);
  CHECK(team.find("\"species\":\"companion\"") != std::string::npos);
  CHECK(team.find("\"active\":true") != std::string::npos);
  CHECK(team.find("\"id\":\"bite\"") != std::string::npos);
  CHECK(team.find("\"element\":\"fire\"") != std::string::npos);
  CHECK(battle_json(w).find("\"phase\":\"none\"") != std::string::npos);
  CHECK(creature_json(w, 1).find("\"engage\":\"ok\"") != std::string::npos);
  engage(w);
  const std::string b = battle_json(w);
  CHECK(b.find("\"phase\":\"fight\"") != std::string::npos);
  CHECK(b.find("\"capture\":\"notWeak\"") != std::string::npos);
  CHECK(b.find("\"enemy\":{\"index\":1,\"species\":\"slime\"") != std::string::npos);
}

TEST_CASE("the companion does not walk through its enemy") {
  World w = arena(Species::Mushroom);  // slow: it hardly moves
  engage(w);
  run(w, forward(), 120);
  const Body& a = w.creatures[0].body;
  const Body& e = w.creatures[1].body;
  CHECK(a.pos.z > e.pos.z);  // still on its side
  const float touching = species_info(Species::Companion).shape.half_width + species_info(Species::Mushroom).shape.half_width;
  CHECK(std::hypot(a.pos.x - e.pos.x, a.pos.z - e.pos.z) >= touching);
}
