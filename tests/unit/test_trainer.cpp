// Battles against several monsters, aiming and moving freely, how foes
// fight (same delays, dodges, flight), and trainers' battles (Bastien).

#include "monstres/core/battle.hpp"
#include "monstres/core/debug.hpp"
#include "monstres/core/quests.hpp"
#include "monstres/core/save.hpp"
#include "test_helpers.hpp"

#include <algorithm>
#include <cmath>
#include <string>

using namespace monstres;

namespace {

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

size_t add_wild(World& w, Species species, Vec3 at, uint8_t level = 2) {
  Creature c;
  c.species = species;
  c.level = level;
  c.body.pos = at;
  c.home = {at.x, at.z};
  c.timer = 1000;
  return add_creature(w, c);
}

// Flat ground: the companion behind the player, three slimes ahead (close
// together), a mushroom and a far slime.
World group() {
  World w = empty_world();
  Creature companion;
  companion.species = Species::Companion;
  companion.body.pos = {0, 0, 1};
  add_creature(w, companion);
  add_wild(w, Species::Slime, {0, 0, -4});      // 1: engaged
  add_wild(w, Species::Slime, {2, 0, -5});      // 2: joins
  add_wild(w, Species::Mushroom, {-2, 0, -5});  // 3: another kind
  add_wild(w, Species::Slime, {-3, 0, -6});     // 4: joins
  add_wild(w, Species::Slime, {0, 0, -20});     // 5: too far
  w.team = {starting_companion({})};
  return w;
}

void go_to_bastien(World& w) {
  const int i = npc_index(w, npc::kBastien);
  REQUIRE(i > 0);
  const Vec3 p = w.creatures[static_cast<size_t>(i)].body.pos;
  w.player.pos = {p.x + 1.5f, w.ground.height(p.x + 1.5f, p.z), p.z};
  w.creatures[0].body.pos = w.player.pos;
}

int rival(const World& w) {
  for (size_t i = 1; i < w.creatures.size(); ++i) {
    if (w.creatures[i].tag == Tag::Rival) return static_cast<int>(i);
  }
  return -1;
}

}  // namespace

TEST_CASE("monsters of the same kind around the one engaged join the battle") {
  World w = group();
  step(w, command(Command::Engage, 1));
  REQUIRE(w.battle.phase == BattlePhase::Fight);
  REQUIRE(w.battle.foes.size() == 3);
  CHECK(w.battle.foes[0].index == 1);
  CHECK(w.battle.target == 0);
  for (const Foe& f : w.battle.foes) {
    CHECK(w.creatures[f.index].species == Species::Slime);
    CHECK(w.creatures[f.index].mind == Mind::Battle);
    CHECK(f.index != 5);
  }
  CHECK(w.creatures[3].mind != Mind::Battle);  // the mushroom stays out of it
}

TEST_CASE("aiming at a foe, or at no one: the companion then moves with the camera") {
  World w = group();
  step(w, command(Command::Engage, 1));
  float fx, fz;
  REQUIRE(battle_forward(w, fx, fz));
  step(w, command(Command::Target, 1));
  CHECK(w.battle.target == 1);
  CHECK(battle_target(w) == &w.battle.foes[1]);
  step(w, command(Command::Target, 9));  // no such foe: nothing changes
  CHECK(w.battle.target == 1);
  step(w, command(Command::Target, kNoTarget));
  CHECK(battle_target(w) == nullptr);
  CHECK_FALSE(battle_forward(w, fx, fz));
  // Free: forward on the joystick goes along the camera (yaw 0: -Z... here +X with a quarter turn).
  const Vec3 before = w.creatures[0].body.pos;
  Input right = move(0, 127, static_cast<Angle16>(-16384));  // camera facing +X
  run(w, right, 20);
  CHECK(w.creatures[0].body.pos.x > before.x + 1.0f);
  CHECK(battle_json(w).find("\"target\":-1") != std::string::npos);
  // No target, nothing to capture.
  CHECK_FALSE(can_capture(w));
}

TEST_CASE("beating one of a group: the companion aims at the next; all beaten, a victory") {
  World w = group();
  step(w, command(Command::Engage, 1));
  const size_t n = w.battle.foes.size();
  uint32_t xp = 0;
  for (const Foe& f : w.battle.foes) xp += f.xp;
  w.battle.dealt[0] = 10;
  for (size_t k = 0; k < n; ++k) {
    REQUIRE(w.battle.foes.size() == n - k);
    REQUIRE(battle_target(w) != nullptr);
    const uint16_t index = battle_target(w)->index;
    w.battle.foes[w.battle.target].hp = 0;
    step(w, Input{});
    CHECK(w.creatures[index].mind == Mind::KnockedOut);
    CHECK(w.creatures[index].beaten);
  }
  CHECK(w.battle.phase == BattlePhase::None);
  CHECK(w.battle.outcome == BattleOutcome::Victory);
  CHECK(w.battle.outcome_count == n);
  CHECK(w.battle.outcome_xp == xp);
}

TEST_CASE("a burst hits every foe around; moving freely, a throw goes straight ahead") {
  World w = group();
  step(w, command(Command::Engage, 1));
  REQUIRE(w.battle.foes.size() == 3);
  // All three close around the companion.
  w.creatures[0].body.pos = {0, 0, -5};
  for (size_t k = 0; k < 3; ++k) {
    w.creatures[w.battle.foes[k].index].body.pos = {static_cast<float>(k) - 1.0f, 0, -6.5f};
  }
  step(w, attack_input(3));
  for (const Foe& f : w.battle.foes) CHECK(f.hp < f.max_hp);
  // Free: the throw goes where the companion faces.
  step(w, command(Command::Target, kNoTarget));
  w.creatures[0].body.facing_x = 1;
  w.creatures[0].body.facing_z = 0;
  w.battle.ally.cooldown = {};
  w.battle.ally.state = FighterState::Ready;
  step(w, attack_input(2));
  REQUIRE_FALSE(w.battle.projectiles.empty());
  const Projectile& p = w.battle.projectiles.back();
  CHECK(p.dir_x > 0.99f);
}

TEST_CASE("foes attack as often as the companion, and show it first") {
  World w = group();
  w.creatures.resize(2);  // one slime
  step(w, command(Command::Engage, 1));
  Foe& f = w.battle.foes[0];
  w.creatures[1].body.pos = {0, 0, 0.2f};  // in reach
  f.fighter.cooldown = {};
  int windups = 0, attacks = 0;
  uint64_t last = 0;
  for (int i = 0; i < 4 * rules::kTicksPerSecond; ++i) {
    const bool was = w.battle.foes[0].fighter.state == FighterState::Windup;
    step(w, Input{});
    if (w.battle.phase != BattlePhase::Fight) break;
    const Fighter& g = w.battle.foes[0].fighter;
    windups += !was && g.state == FighterState::Windup;
    if (g.attack_tick == w.tick && g.attack_move == 0) {
      if (last) CHECK(w.tick - last >= species_moves(Species::Slime)[0].cooldown);
      last = w.tick;
      ++attacks;
    }
  }
  CHECK(windups >= attacks);
  CHECK(attacks >= 6);  // about every 1/3 s, the companion's quick attack's delay
}

TEST_CASE("foes move around the companion, and sometimes dodge what is thrown") {
  World w = group();
  w.creatures.resize(2);
  step(w, command(Command::Engage, 1));
  const Vec3 start = w.creatures[1].body.pos;
  w.battle.foes[0].fighter.cooldown = {9999, 9999, 9999};  // it only moves
  run(w, Input{}, 3 * rules::kTicksPerSecond);
  CHECK(std::hypot(w.creatures[1].body.pos.x - start.x, w.creatures[1].body.pos.z - start.z) > 1.0f);
  // Thrown at, again and again: some are dodged.
  int dodges = 0;
  for (int k = 0; k < 40 && dodges == 0; ++k) {
    w.battle.ally.cooldown = {};
    w.battle.ally.state = FighterState::Ready;
    w.team[0].hp = 200;
    w.battle.foes[0].hp = w.battle.foes[0].max_hp;  // it does not fall
    step(w, attack_input(2));
    for (int i = 0; i < 50; ++i) {
      step(w, Input{});
      dodges += w.battle.foes[0].fighter.state == FighterState::Dodge;
    }
  }
  CHECK(dodges > 0);
}

TEST_CASE("a crow companion climbs while the jump is held, and comes down after") {
  World w = group();
  w.creatures.resize(2);
  w.team = {Monster{Species::Crow, {}, 5, 0, 40}};
  w.creatures[0].species = Species::Crow;
  step(w, command(Command::Engage, 1));
  REQUIRE(w.battle.phase == BattlePhase::Fight);
  w.battle.foes[0].fighter.cooldown = {9999, 9999, 9999};
  const float low = w.creatures[0].body.pos.y;
  Input hold;
  hold.jump_held = true;
  run(w, hold, 90);
  CHECK(w.battle.ally.lift > 2.0f);
  CHECK(w.creatures[0].body.pos.y > low + 2.0f);
  CHECK(w.battle.ally.state != FighterState::Dodge);  // still: no dodge, it flies
  run(w, Input{}, 3 * rules::kTicksPerSecond);
  CHECK(w.battle.ally.lift == 0.0f);
}

TEST_CASE("the reason why a victory gave no experience") {
  World w = group();
  w.creatures.resize(2);
  w.creatures[1].beaten = true;
  step(w, command(Command::Engage, 1));
  w.battle.foes[0].hp = 0;
  step(w, Input{});
  CHECK(w.battle.outcome == BattleOutcome::Victory);
  CHECK(w.battle.outcome_xp == 0);
  CHECK(w.battle.outcome_no_xp == NoXp::Beaten);  // not "released"
  CHECK(battle_json(w).find("\"noXp\":\"beaten\"") != std::string::npos);
}

TEST_CASE("Bastien, the trainer, has his monster beside him; it cannot be fought in the wild") {
  World w = new_game(31);
  const int i = rival(w);
  REQUIRE(i > 0);
  const int b = npc_index(w, npc::kBastien);
  REQUIRE(b > 0);
  const Creature& m = w.creatures[static_cast<size_t>(i)];
  CHECK(m.species == Species::Mushroom);
  CHECK(std::hypot(m.body.pos.x - w.creatures[static_cast<size_t>(b)].body.pos.x,
                   m.body.pos.z - w.creatures[static_cast<size_t>(b)].body.pos.z) < 2.0f);
  CHECK(can_engage(w, static_cast<size_t>(i)) == EngageCheck::Owned);
  check_invariants(w);
}

TEST_CASE("a trainer's battle: places, rules, ready, the next monster, victory once worth experience") {
  World w = new_game(31);
  CHECK_FALSE(can_challenge(w, npc::kBastien));  // too far
  go_to_bastien(w);
  REQUIRE(can_challenge(w, npc::kBastien));
  CHECK(creature_json(w, npc_index(w, npc::kBastien)).find("\"challenge\":true") != std::string::npos);
  step(w, command(Command::Challenge, npc::kBastien));
  REQUIRE(w.battle.phase == BattlePhase::Prepare);
  const Rival r = w.battle.rival;
  CHECK(std::hypot(r.foe_spot.x - r.ally_spot.x, r.foe_spot.z - r.ally_spot.z) == doctest::Approx(10.0f));
  CHECK(zone_at(w, (r.ally_spot.x + r.foe_spot.x) / 2, (r.ally_spot.z + r.foe_spot.z) / 2) != Zone::Village);
  // They walk to their places; no fight yet, whatever the player presses.
  run(w, attack_input(1), 30);
  CHECK(w.battle.phase == BattlePhase::Prepare);
  CHECK(w.battle.foes[0].hp == w.battle.foes[0].max_hp);
  const std::string json = battle_json(w);
  CHECK(json.find("\"phase\":\"prepare\"") != std::string::npos);
  CHECK(json.find("\"seen\":false") != std::string::npos);  // his second one is not known yet
  step(w, command(Command::Ready));
  REQUIRE(w.battle.phase == BattlePhase::Fight);
  const int m = rival(w);
  CHECK(std::hypot(w.creatures[static_cast<size_t>(m)].body.pos.x - r.foe_spot.x,
                   w.creatures[static_cast<size_t>(m)].body.pos.z - r.foe_spot.z) < 0.3f);
  CHECK(std::hypot(w.creatures[0].body.pos.x - r.ally_spot.x, w.creatures[0].body.pos.z - r.ally_spot.z) < 0.3f);
  CHECK_FALSE(can_capture(w));  // a trainer's monster

  // His first one beaten: the next one comes out, where the first one stood.
  w.battle.dealt[0] = 10;
  w.battle.foes[0].hp = 0;
  step(w, Input{});
  CHECK(w.battle.phase == BattlePhase::Fight);
  CHECK(w.battle.foes.empty());
  run(w, Input{}, rules::kRivalNextTicks);
  REQUIRE(w.battle.foes.size() == 1);
  CHECK(w.creatures[static_cast<size_t>(m)].species == Species::Slime);
  CHECK(w.battle.rival.team[1].seen);
  CHECK(battle_json(w).find("\"ko\":true") != std::string::npos);
  // Beaten too: Bastien has no one left, the player wins (the first time, with experience).
  const uint16_t wizard = w.wizard.level;
  const uint32_t wizard_xp = w.wizard.xp;
  w.battle.foes[0].hp = 0;
  step(w, Input{});
  CHECK(w.battle.phase == BattlePhase::None);
  CHECK(w.battle.outcome == BattleOutcome::Victory);
  CHECK(w.battle.outcome_rival == npc::kBastien);
  CHECK(w.battle.outcome_xp == rules::kRivalXpPerLevel * (4 + 5));
  CHECK((w.wizard.level > wizard || w.wizard.xp > wizard_xp));
  CHECK(w.rival_wins == 1);
  // His monster stands again, the first of his team.
  run(w, Input{}, 2);
  CHECK(w.creatures[static_cast<size_t>(m)].species == Species::Mushroom);
  CHECK(w.creatures[static_cast<size_t>(m)].mind != Mind::KnockedOut);

  // Again: no experience this time.
  go_to_bastien(w);
  run(w, Input{}, 2);
  step(w, command(Command::Challenge, npc::kBastien));
  step(w, command(Command::Ready));
  REQUIRE(w.battle.phase == BattlePhase::Fight);
  for (int k = 0; k < 2; ++k) {
    run(w, Input{}, rules::kRivalNextTicks);
    REQUIRE(w.battle.foes.size() == 1);
    w.battle.foes[0].hp = 0;
    step(w, Input{});
  }
  CHECK(w.battle.outcome == BattleOutcome::Victory);
  CHECK(w.battle.outcome_xp == 0);
  CHECK(w.battle.outcome_no_xp == NoXp::Trainer);
  CHECK(w.rival_wins == 2);
  // Saved.
  const auto loaded = deserialize(serialize(w));
  REQUIRE(loaded);
  CHECK(loaded->rival_wins == 2);
}

TEST_CASE("a trainer's battle: a knocked out companion is replaced; none left, or forfeit, the trainer wins") {
  World w = new_game(31);
  w.team.push_back({Species::Slime, {}, 3, 0, 30});
  go_to_bastien(w);
  step(w, command(Command::Challenge, npc::kBastien));
  step(w, command(Command::Ready));
  REQUIRE(w.battle.phase == BattlePhase::Fight);
  w.team[0].hp = 0;
  step(w, Input{});
  CHECK(w.battle.phase == BattlePhase::Fight);  // another one can fight
  CHECK(w.active == -1);
  step(w, command(Command::Summon, 0));
  CHECK(w.active == -1);  // not the knocked out one
  step(w, command(Command::Summon, 1));
  CHECK(w.active == 1);
  w.team[1].hp = 0;
  step(w, Input{});
  CHECK(w.battle.phase == BattlePhase::None);
  CHECK(w.battle.outcome == BattleOutcome::Defeat);
  CHECK(w.team[0].hp >= 1);  // they get up
  CHECK(w.team[1].hp >= 1);
  CHECK(w.rival_wins == 0);

  // Forfeit: over at once, the trainer wins.
  step(w, command(Command::Summon, 0));
  go_to_bastien(w);
  run(w, Input{}, 2);
  step(w, command(Command::Challenge, npc::kBastien));
  REQUIRE(w.battle.phase == BattlePhase::Prepare);
  step(w, command(Command::Flee));
  CHECK(w.battle.phase == BattlePhase::None);
  CHECK(w.battle.outcome == BattleOutcome::Forfeit);
  CHECK(w.battle.outcome_xp == 0);
}

TEST_CASE("a game saved during a trainer's battle: his first monster is back beside him") {
  World w = new_game(31);
  go_to_bastien(w);
  step(w, command(Command::Challenge, npc::kBastien));
  step(w, command(Command::Ready));
  w.battle.foes[0].hp = 0;
  step(w, Input{});
  run(w, Input{}, rules::kRivalNextTicks);
  REQUIRE(w.creatures[static_cast<size_t>(rival(w))].species == Species::Slime);
  auto loaded = deserialize(serialize(w));
  REQUIRE(loaded);
  CHECK(loaded->battle.phase == BattlePhase::None);
  run(*loaded, Input{}, rules::kPopulationEveryTicks);
  CHECK(loaded->creatures[static_cast<size_t>(rival(*loaded))].species == Species::Mushroom);
}
