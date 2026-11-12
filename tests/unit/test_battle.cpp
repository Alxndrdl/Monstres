// Battles: the companion against one wild monster, the team, capture.

#include "monstres/core/battle.hpp"
#include "monstres/core/debug.hpp"
#include "monstres/core/save.hpp"
#include "monstres/core/variants.hpp"
#include "test_helpers.hpp"

#include <algorithm>
#include <cmath>
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
  add_creature(w, companion);
  Creature wild;
  wild.species = foe;
  wild.level = level;
  wild.body.pos = {0, 0, -3};
  wild.home = {0, -3};
  wild.timer = 1000;
  add_creature(w, wild);
  Creature villager;
  villager.species = Species::Villager;
  villager.body.pos = {5, 0, 0};
  villager.home = {5, 0};
  add_creature(w, villager);
  Creature crow;
  crow.species = Species::Crow;
  crow.body.pos = {-5, 0, 0};
  add_creature(w, crow);
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
  const World& w = cached_game(42);
  REQUIRE(w.team.size() == 1);
  CHECK(w.team[0].species == Species::Companion);
  CHECK(w.team[0].level == rules::kCompanionStartLevel);
  CHECK(w.team[0].hp == monster_fight_stats(Species::Companion, w.team[0].level).hp);
  CHECK(w.battle.phase == BattlePhase::None);
  // Wild monsters have a level, higher far from the start.
  for (size_t i = 1; i < w.creatures.size(); ++i) {
    const Creature& c = w.creatures[i];
    if (!monster_stats(c.species).is_monster) continue;
    int lowest = 1;
    switch (zone_at(w, c.home.x, c.home.z)) {
      case Zone::Forest: lowest = 2; break;
      case Zone::Ruins: lowest = 3; break;
      case Zone::Cave: lowest = 4; break;
      case Zone::Castle: lowest = 5; break;
      default: break;
    }
    CHECK(c.level >= lowest);
    CHECK(c.level <= lowest + 2);
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
  CHECK(can_engage(w, 3) == EngageCheck::Ok);          // a crow: from afar
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
  w.battle.dealt[0] = w.battle.enemy_max_hp - w.battle.enemy_hp;  // as if the companion had done it
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
  w.battle.dealt[0] = w.battle.enemy_max_hp - 1;  // it did all the work
  w.creatures[0].body.pos = {0, 0, -2.2f};
  const uint32_t wizard_xp = w.wizard.xp;
  step(w, attack_input(1));
  CHECK(w.battle.phase == BattlePhase::None);
  CHECK(w.battle.outcome == BattleOutcome::Victory);
  CHECK(w.battle.outcome_xp == 16u);
  CHECK(w.team[0].xp == 16u);
  REQUIRE(w.battle.outcome_gains.size() == 1);
  CHECK(w.battle.outcome_gains[0].xp == 16u);
  CHECK(w.battle.outcome_gains[0].xp_before == 0u);
  CHECK(w.wizard.xp == wizard_xp + 8u);
  // Beaten: it lies there for a while, then runs home.
  CHECK(w.creatures[1].mind == Mind::KnockedOut);
  CHECK(can_engage(w, 1) == EngageCheck::KnockedOut);
  run(w, Input{}, rules::kKnockedOutTicks - 2);
  CHECK(w.creatures[1].mind == Mind::KnockedOut);
  run(w, Input{}, 3);
  CHECK(w.creatures[1].mind != Mind::KnockedOut);  // up again (it was home already)

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
  w.battle.dealt[0] = 10;
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
  World w = cached_game(7);
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

namespace {
Input summon_input(uint16_t slot) { return command(Command::Summon, slot); }
}  // namespace

TEST_CASE("the companion goes back into the crystal, and comes out next to the player") {
  World w = arena();
  REQUIRE(active_monster(w) == &w.team[0]);
  step(w, summon_input(0));  // recall
  CHECK(w.active == -1);
  CHECK(active_monster(w) == nullptr);
  CHECK(w.went_in.tick == w.tick);
  CHECK_FALSE(w.went_in.captured);
  CHECK(w.went_in.species == Species::Companion);
  CHECK(can_engage(w, 1) == EngageCheck::NoCompanion);
  run(w, forward(), 60);
  CHECK(w.creatures[0].body.pos == w.player.pos);  // inside the crystal, it goes along

  step(w, summon_input(0));
  CHECK(w.active == 0);
  CHECK(w.came_out_tick == w.tick);
  const Body& c = w.creatures[0].body;
  const float d = std::hypot(c.pos.x - w.player.pos.x, c.pos.z - w.player.pos.z);
  CHECK(d > 0.8f);
  CHECK(d < 2.0f);
  step(w, summon_input(9));  // no such member: nothing happens
  CHECK(w.active == 0);
}

TEST_CASE("another team member takes the companion's place") {
  World w = arena();
  Genes genes{};
  genes.allele[1] = 2;
  w.team.push_back({Species::Mushroom, genes, 4, 0, 20});
  w.creatures[0].body.pos = {2, 0, 2};
  step(w, summon_input(1));
  CHECK(w.active == 1);
  CHECK(w.creatures[0].species == Species::Mushroom);
  CHECK(w.creatures[0].genes == genes);
  CHECK(w.creatures[0].level == 4);
  CHECK(w.creatures[0].body.pos.x == doctest::Approx(2).epsilon(0.05));  // where Pompon was
  CHECK(w.went_in.species == Species::Companion);
  CHECK(w.came_out_tick == w.tick);
  // It is the one fighting now.
  World fight = w;
  engage(fight);
  const uint16_t hp = fight.team[1].hp;
  fight.battle.foe.cooldown = {};
  fight.creatures[0].body.pos = {fight.creatures[1].body.pos.x, 0, fight.creatures[1].body.pos.z + 1.2f};
  for (int i = 0; i < 300 && fight.team[1].hp == hp; ++i) step(fight, Input{});
  CHECK(fight.team[1].hp < hp);
  CHECK(fight.team[0].hp == monster_fight_stats(Species::Companion, fight.team[0].level).hp);
  // Even a slow mushroom keeps up with the player.
  run(w, forward(), 4 * rules::kTicksPerSecond);
  const Body& m = w.creatures[0].body;
  CHECK(std::hypot(m.pos.x - w.player.pos.x, m.pos.z - w.player.pos.z) < 6.0f);  // like Pompon: a few steps behind
}

TEST_CASE("during a fight, a companion can be swapped but not recalled") {
  World w = arena();
  w.team.push_back({Species::Bird, {}, 3, 0, 15});
  engage(w);
  step(w, summon_input(0));  // recall: no
  CHECK(w.active == 0);
  step(w, attack_input(2));
  CHECK(w.battle.ally.cooldown[1] > 0);
  step(w, summon_input(1));  // swap: yes, with a fresh fighter
  CHECK(w.active == 1);
  CHECK(w.creatures[0].species == Species::Bird);
  CHECK(w.battle.phase == BattlePhase::Fight);
  CHECK(w.battle.ally.cooldown[1] == 0);
  CHECK(battle_json(w).find("\"ally\":{\"species\":\"bird\"") != std::string::npos);
}

TEST_CASE("a captured monster leaves a trace for the runes; the companion out is saved") {
  World w = arena();
  engage(w);
  w.battle.enemy_hp = 1;
  const Vec3 where = w.creatures[1].body.pos;
  step(w, command(Command::Capture));
  CHECK(w.went_in.captured);
  CHECK(w.went_in.species == Species::Slime);
  CHECK(w.went_in.body.pos.x == doctest::Approx(where.x));

  World g = cached_game(5);
  g.team.push_back({Species::Slime, {}, 2, 0, 10});
  step(g, summon_input(1));
  auto loaded = deserialize(serialize(g));
  REQUIRE(loaded.has_value());
  CHECK(loaded->active == 1);
  CHECK(loaded->creatures[0].species == Species::Slime);
  step(g, summon_input(1));  // recalled
  loaded = deserialize(serialize(g));
  REQUIRE(loaded.has_value());
  CHECK(loaded->active == -1);
}

TEST_CASE("a crow on a roof: out of reach of a bite, not of a throw; it stays there and throws back") {
  World w = arena();
  add_box(w, {-6, 0, 0}, {1, 2, 1});  // a "roof" 4 m high
  Creature& crow = w.creatures[3];
  crow.body.pos = {-6, 4, 0};
  crow.body.on_ground = true;
  w.creatures[0].body.pos = {-5.2f, 0, 0};  // right below it
  REQUIRE(can_engage(w, 3) == EngageCheck::Ok);
  engage(w, 3);
  const uint16_t hp = w.battle.enemy_hp;
  step(w, attack_input(1));
  CHECK(w.battle.enemy_hp == hp);  // a bite does not reach that high
  w.creatures[0].body.pos = {-1, 0, 0};
  step(w, attack_input(2));
  REQUIRE(w.battle.projectiles.size() == 1);
  CHECK(w.battle.projectiles[0].dir_y > 0.3f);  // aimed up at it
  for (int i = 0; i < 60 && !w.battle.projectiles.empty(); ++i) step(w, Input{});
  CHECK(w.battle.enemy_hp < hp);  // the roof did not stop it
  // It does not come down, and it throws at the companion.
  w.battle.foe.cooldown = {};
  const uint16_t mine = w.team[0].hp;
  for (int i = 0; i < 300 && w.team[0].hp == mine; ++i) step(w, Input{});
  CHECK(w.team[0].hp < mine);
  CHECK(w.creatures[3].body.pos == Vec3{-6, 4, 0});
  // Beaten, it flies away.
  w.battle.enemy_hp = 1;
  w.battle.ally.cooldown = {};
  step(w, attack_input(2));
  for (int i = 0; i < 60 && w.battle.phase == BattlePhase::Fight; ++i) step(w, Input{});
  CHECK(w.battle.outcome == BattleOutcome::Victory);
  step(w, Input{});
  CHECK(w.creatures[3].mind != Mind::Battle);
}

TEST_CASE("a fish can be fought from the shore: it stays in the water") {
  World w = arena();
  Creature fish;
  fish.species = Species::Fish;
  fish.level = 2;
  fish.body.pos = {4, 0, -4};
  fish.home = {4, -4};
  add_creature(w, fish);
  const auto index = static_cast<uint16_t>(w.creatures.size() - 1);
  engage(w, index);
  const uint16_t hp = w.battle.enemy_hp;
  step(w, attack_input(2));
  for (int i = 0; i < 60 && !w.battle.projectiles.empty(); ++i) step(w, Input{});
  CHECK(w.battle.enemy_hp < hp);
  run(w, Input{}, 120);
  CHECK(w.creatures[index].body.pos == Vec3{4, 0, -4});
}

TEST_CASE("a crow companion flies at the magician's side, a fish swims in the air") {
  World w = arena();
  w.team.push_back({Species::Crow, {}, 2, 0, 20});
  w.team.push_back({Species::Fish, {}, 2, 0, 20});
  step(w, command(Command::Summon, 1));
  run(w, forward(), 2 * rules::kTicksPerSecond);
  const Body& crow = w.creatures[0].body;
  CHECK(crow.pos.y == doctest::Approx(companion_hover(Species::Crow)).epsilon(0.05));
  CHECK_FALSE(crow.on_ground);  // it flaps its wings
  CHECK(std::hypot(crow.pos.x - w.player.pos.x, crow.pos.z - w.player.pos.z) < 6.0f);
  step(w, command(Command::Summon, 2));
  run(w, Input{}, rules::kTicksPerSecond);
  CHECK(w.creatures[0].body.pos.y == doctest::Approx(companion_hover(Species::Fish)).epsilon(0.05));
  // It fights flying too.
  engage(w);
  run(w, move(0, 127), 30);
  CHECK(w.creatures[0].body.pos.y == doctest::Approx(companion_hover(Species::Fish)).epsilon(0.05));
}

TEST_CASE("the victory's experience is shared by the hit points each team member dealt") {
  World w = arena(Species::Slime, 4);  // 32 XP to share
  w.team.push_back(starting_companion({}));
  engage(w);
  w.battle.dealt[0] = 30;  // Pompon did most of the work
  w.battle.dealt[1] = 10;  // the second one came in later
  w.battle.enemy_hp = 1;
  w.battle.dealt[0] -= 1;  // the last hit, below, brings it back to 30
  w.creatures[0].body.pos = {0, 0, -2.2f};
  step(w, attack_input(1));
  REQUIRE(w.battle.outcome == BattleOutcome::Victory);
  CHECK(w.battle.outcome_xp == 32u);
  REQUIRE(w.battle.outcome_gains.size() == 2);
  // 32 * 30 / 41 = 23.4 and 32 * 10 / 41 = 7.8, rounded.
  CHECK(w.battle.outcome_gains[0].xp == 23u);
  CHECK(w.battle.outcome_gains[1].xp == 8u);
  CHECK(w.team[1].xp == 8u);
  const std::string json = battle_json(w);
  CHECK(json.find("\"gains\":[{\"slot\":0,\"xp\":23") != std::string::npos);
  CHECK(json.find("\"steps\":[[") != std::string::npos);
}

TEST_CASE("a monster let go gives no experience when beaten or caught again") {
  World w = arena();
  w.creatures[1].released = true;
  engage(w);
  w.battle.dealt[0] = 20;
  w.battle.enemy_hp = 1;
  w.creatures[0].body.pos = {0, 0, -2.2f};
  step(w, attack_input(1));
  CHECK(w.battle.outcome == BattleOutcome::Victory);
  CHECK(w.battle.outcome_xp == 0u);
  CHECK(w.team[0].xp == 0u);
  CHECK(w.battle.outcome_gains.empty());
}

TEST_CASE("letting a monster go: out of the crystal, back to the wild") {
  World w = arena();
  w.team.push_back({Species::Bird, {}, 5, 3, 20});
  const size_t creatures = w.creatures.size();
  // One alone cannot be let go; nor during a battle.
  World alone = arena();
  step(alone, command(Command::Release, 0));
  CHECK(alone.team.size() == 1);

  step(w, command(Command::Release, 1));  // the bird, in the crystal
  REQUIRE(w.team.size() == 1);
  REQUIRE(w.creatures.size() == creatures + 1);
  const Creature& bird = w.creatures.back();
  CHECK(bird.species == Species::Bird);
  CHECK(bird.level == 5);
  CHECK(bird.released);
  CHECK(bird.id == w.released_id);
  CHECK(w.released_tick == w.tick);
  CHECK(w.active == 0);  // Pompon is still out
  const float d0 = std::hypot(bird.body.pos.x - w.player.pos.x, bird.body.pos.z - w.player.pos.z);
  CHECK(d0 < 2.0f);  // it came out next to the player
  run(w, Input{}, 3 * rules::kTicksPerSecond);
  const Creature& gone = w.creatures[static_cast<size_t>(find_creature(w, w.released_id))];
  CHECK(std::hypot(gone.body.pos.x - w.player.pos.x, gone.body.pos.z - w.player.pos.z) > d0 + 4.0f);  // and runs off

  // The companion out can be let go too: it simply leaves.
  w.team.push_back({Species::Slime, {}, 3, 0, 30});
  step(w, command(Command::Release, 0));
  REQUIRE(w.team.size() == 1);
  CHECK(w.team[0].species == Species::Slime);
  CHECK(w.active == -1);
  CHECK(w.creatures.back().species == Species::Companion);
  CHECK(w.creatures.back().released);
}

TEST_CASE("training: the companion attacks the magician's shield, no one is hurt") {
  World w = arena();
  w.creatures.resize(1);  // just the companion, behind the player
  REQUIRE(can_train(w));
  step(w, command(Command::Train));
  REQUIRE(w.battle.phase == BattlePhase::Training);
  float fx, fz;
  REQUIRE(battle_forward(w, fx, fz));
  CHECK(fz < -0.9f);  // towards the magician
  // The joystick moves the companion, not the player.
  const Vec3 player = w.player.pos;
  run(w, move(0, 127), 10);
  CHECK(w.player.pos == player);
  // A bite on the shield: blocked, its damage shown.
  w.creatures[0].body.pos = {0, 0, 0.7f};
  const uint16_t hp = w.team[0].hp;
  step(w, attack_input(1));
  CHECK(w.battle.foe.parried_tick == w.tick);
  CHECK(w.battle.foe.last_damage > 0);
  CHECK(w.team[0].hp == hp);
  CHECK(battle_json(w).find("\"phase\":\"training\"") != std::string::npos);
  CHECK(battle_json(w).find("\"shield\":{") != std::string::npos);
  // Flee stops it at once.
  step(w, command(Command::Flee));
  CHECK(w.battle.phase == BattlePhase::None);
  CHECK(w.battle.outcome == BattleOutcome::None);
  run(w, forward(), 10);
  CHECK(w.player.pos.z < player.z);  // the player walks again
}

TEST_CASE("a dodge without moving is a jump on the spot") {
  World w = arena();
  engage(w);
  const Vec3 start = w.creatures[0].body.pos;
  Input jump;
  jump.jump = true;
  step(w, jump);
  CHECK(w.battle.ally.state == FighterState::Dodge);
  CHECK_FALSE(w.creatures[0].body.on_ground);  // up in the air
  float highest = 0;
  for (int i = 0; i < 30; ++i) {
    step(w, Input{});
    highest = std::max(highest, w.creatures[0].body.pos.y);
  }
  CHECK(highest > 0.6f);
  CHECK(std::hypot(w.creatures[0].body.pos.x - start.x, w.creatures[0].body.pos.z - start.z) < 0.05f);  // not back
}
