// Knocked out monsters (no hit points come back by themselves), spells,
// the healer, coming back within reach, and stepping up onto low things.

#include "monstres/core/battle.hpp"
#include "monstres/core/debug.hpp"
#include "monstres/core/quests.hpp"
#include "monstres/core/save.hpp"
#include "test_helpers.hpp"

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

// Flat ground: the companion behind the player, a slime 3 m ahead (index 1);
// a second team member, in the crystal.
World arena() {
  World w = empty_world();
  Creature companion;
  companion.species = Species::Companion;
  companion.body.pos = {0, 0, 1};
  add_creature(w, companion);
  Creature wild;
  wild.species = Species::Slime;
  wild.level = 2;
  wild.body.pos = {0, 0, -3};
  wild.home = {0, -3};
  wild.timer = 1000;
  add_creature(w, wild);
  w.team = {starting_companion({}), Monster{Species::Slime, {}, 3, 0, 30}};
  return w;
}

uint16_t max_hp(const Monster& m) { return monster_fight_stats(m.species, m.level).hp; }

}  // namespace

TEST_CASE("in a wild battle, a knocked out companion is replaced; none left, the battle is lost") {
  World w = arena();
  step(w, command(Command::Engage, 1));
  REQUIRE(w.battle.phase == BattlePhase::Fight);
  w.team[0].hp = 0;
  step(w, Input{});
  CHECK(w.battle.phase == BattlePhase::Fight);
  CHECK(w.active == -1);
  step(w, command(Command::Summon, 0));
  CHECK(w.active == -1);  // not the knocked out one
  step(w, command(Command::Summon, 1));
  REQUIRE(w.active == 1);
  w.team[1].hp = 0;
  step(w, Input{});
  CHECK(w.battle.phase == BattlePhase::None);
  CHECK(w.battle.outcome == BattleOutcome::Defeat);
  CHECK(w.active == 1);
  CHECK(w.creatures[0].mind == Mind::KnockedOut);  // it lies there
}

TEST_CASE("hit points do not come back by themselves") {
  World w = arena();
  w.team[0].hp = 5;
  w.team[1].hp = 7;
  run(w, Input{}, 10 * rules::kTicksPerSecond);
  CHECK(w.team[0].hp == 5);
  CHECK(w.team[1].hp == 7);
}

TEST_CASE("a knocked out monster comes out only out of battles: it lies there, healed it gets up") {
  World w = arena();
  w.team[1].hp = 0;
  step(w, command(Command::Summon, 1));
  REQUIRE(w.active == 1);
  CHECK(w.creatures[0].mind == Mind::KnockedOut);
  const Vec3 at = w.creatures[0].body.pos;
  run(w, move(0, 127), 30);  // the player walks off: it does not follow
  CHECK(std::hypot(w.creatures[0].body.pos.x - at.x, w.creatures[0].body.pos.z - at.z) < 0.01f);
  // Heal (adventure spell 0): 10 hit points, up again; then it waits 5 s.
  w.player.pos = {0, 0, 0};
  REQUIRE(cannot_cast(w, 0) == nullptr);
  step(w, command(Command::Cast, 0));
  CHECK(w.team[1].hp == rules::kHealPoints);
  CHECK(w.creatures[0].mind != Mind::KnockedOut);
  CHECK(w.spell_cast == SpellId::Heal);
  CHECK(std::string(cannot_cast(w, 0)) == "cooldown");
  run(w, Input{}, rules::kHealCooldown);
  CHECK(cannot_cast(w, 0) == nullptr);
  w.team[1].hp = max_hp(w.team[1]);
  CHECK(std::string(cannot_cast(w, 0)) == "full");
  CHECK(std::string(cannot_cast(w, 1)) == "none");  // no second adventure spell
}

TEST_CASE("a knocked out monster left behind goes back into the crystal") {
  World w = arena();
  w.team[0].hp = 0;
  w.creatures[0].mind = Mind::KnockedOut;
  run(w, move(0, 127), 2 * rules::kTicksPerSecond);  // 10 m
  CHECK(w.active == 0);
  run(w, move(0, 127), 2 * rules::kTicksPerSecond);  // 20 m
  CHECK(w.active == -1);
}

TEST_CASE("the shield: for a second, no hit lands; then 5 s before the next one") {
  World w = arena();
  step(w, command(Command::Engage, 1));
  w.creatures[1].body.pos = {0, 0, 0.3f};  // in reach
  Foe& foe = w.battle.foes[0];
  foe.fighter.cooldown = {};
  step(w, command(Command::Cast, 1));  // combat spell 1: the shield
  CHECK(w.battle.ally.shield_ticks > 0);
  CHECK(std::string(cannot_cast(w, 1)) == "cooldown");
  const uint16_t hp = w.team[0].hp;
  for (int i = 0; i < rules::kShieldTicks - 2; ++i) step(w, Input{});
  CHECK(w.team[0].hp == hp);  // its bites were stopped
  CHECK(w.battle.ally.parried_tick > 0);
  run(w, Input{}, 3 * rules::kTicksPerSecond);
  CHECK(w.team[0].hp < hp);  // no shield any more
  CHECK(battle_json(w).find("\"shield\":0") != std::string::npos);
}

TEST_CASE("spells are chosen out of battles: combat ones, adventure ones") {
  World w = arena();
  CHECK(w.combat_spells == std::array<SpellId, kSpellSlots>{SpellId::Heal, SpellId::Shield});
  CHECK(w.adventure_spells == std::array<SpellId, kSpellSlots>{SpellId::Heal, SpellId::None});
  // The shield is not an adventure spell.
  step(w, command(Command::EquipSpell, 3 * 16 + static_cast<uint16_t>(SpellId::Shield)));
  CHECK(w.adventure_spells[1] == SpellId::None);
  // Heal in the second adventure slot: it moves there (no twice the same).
  step(w, command(Command::EquipSpell, 3 * 16 + static_cast<uint16_t>(SpellId::Heal)));
  CHECK(w.adventure_spells == std::array<SpellId, kSpellSlots>{SpellId::None, SpellId::Heal});
  // Swapping the combat ones.
  step(w, command(Command::EquipSpell, 0 * 16 + static_cast<uint16_t>(SpellId::Shield)));
  CHECK(w.combat_spells == std::array<SpellId, kSpellSlots>{SpellId::Shield, SpellId::Heal});
  // Not during a battle; training uses the combat spells.
  step(w, command(Command::Train));
  REQUIRE(w.battle.phase == BattlePhase::Training);
  CHECK(current_spells(w) == w.combat_spells);
  step(w, command(Command::EquipSpell, 0 * 16 + static_cast<uint16_t>(SpellId::Heal)));
  CHECK(w.combat_spells[0] == SpellId::Shield);
  CHECK(spells_json(w).find("\"inCombat\":true") != std::string::npos);
  // Saved.
  step(w, command(Command::Flee));
  const auto loaded = deserialize(serialize(w));
  REQUIRE(loaded);
  CHECK(loaded->combat_spells == w.combat_spells);
  CHECK(loaded->adventure_spells == w.adventure_spells);
}

TEST_CASE("training: the magician can be aimed at, or not") {
  World w = arena();
  w.creatures.resize(1);
  step(w, command(Command::Train));
  float fx, fz;
  CHECK(battle_forward(w, fx, fz));
  step(w, command(Command::Target, kNoTarget));
  CHECK_FALSE(battle_forward(w, fx, fz));
  CHECK(battle_json(w).find("\"target\":-1") != std::string::npos);
  step(w, command(Command::Target, 0));
  CHECK(battle_forward(w, fx, fz));
}

TEST_CASE("too far from its foes: 5 seconds to come back before the battle ends") {
  World w = arena();
  w.creatures[1].species = Species::Mushroom;  // slow: it does not follow
  step(w, command(Command::Engage, 1));
  w.battle.foes[0].fighter.cooldown = {9999, 9999, 9999};
  w.creatures[0].body.pos = {0, 0, 60};  // far away
  run(w, Input{}, 2 * rules::kTicksPerSecond);
  CHECK(w.battle.phase == BattlePhase::Fight);
  CHECK(battle_json(w).find("\"away\":") != std::string::npos);
  w.creatures[0].body.pos = w.creatures[1].body.pos;  // back within reach: it counts again from zero
  step(w, Input{});
  CHECK(w.battle.away_ticks == 0);
  w.creatures[0].body.pos = {0, 0, 60};
  run(w, Input{}, rules::kAwayTicks - 5);
  CHECK(w.battle.phase == BattlePhase::Fight);
  run(w, Input{}, 10);
  CHECK(w.battle.phase == BattlePhase::None);
  CHECK(w.battle.outcome == BattleOutcome::Fled);

  // Training too.
  World t = arena();
  t.creatures.resize(1);
  step(t, command(Command::Train));
  t.creatures[0].body.pos = {0, 0, 30};
  run(t, Input{}, rules::kAwayTicks - 5);
  CHECK(t.battle.phase == BattlePhase::Training);
  run(t, Input{}, 10);
  CHECK(t.battle.phase == BattlePhase::None);
}

TEST_CASE("Odette, the healer, heals the whole team and wakes the knocked out") {
  World w = new_game(31);
  w.team.push_back({Species::Slime, {}, 3, 0, 0});
  w.team[0].hp = 0;
  w.creatures[0].mind = Mind::KnockedOut;
  const int i = npc_index(w, npc::kOdette);
  REQUIRE(i > 0);
  CHECK_FALSE(can_heal_team(w, npc::kOdette));  // too far
  const Vec3 p = w.creatures[static_cast<size_t>(i)].body.pos;
  w.player.pos = {p.x + 1.5f, w.ground.height(p.x + 1.5f, p.z), p.z};
  w.creatures[0].body.pos = w.player.pos;
  REQUIRE(can_heal_team(w, npc::kOdette));
  CHECK(creature_json(w, i).find("\"healer\":true") != std::string::npos);
  CHECK_FALSE(can_heal_team(w, npc::kGaspard));  // not him
  step(w, command(Command::HealTeam, npc::kOdette));
  for (const Monster& m : w.team) CHECK(m.hp == max_hp(m));
  CHECK(w.creatures[0].mind != Mind::KnockedOut);
  CHECK(w.healed_tick == w.tick);
}

TEST_CASE("walking into something low steps up onto it; something higher stops") {
  World w = empty_world();
  add_box(w, {0, 0, -3}, {1, 0.35f, 0.5f});  // 70 cm: a log, a rock
  add_box(w, {6, 0, -3}, {1, 0.45f, 0.5f});  // 90 cm: a fence, a barrel
  run(w, move(0, 127), 50);
  CHECK(w.player.pos.z < -3.5f);  // over it
  w.player.pos = {6, 0, 0};
  run(w, move(0, 127), 50);
  CHECK(w.player.pos.z > -2.6f);  // stopped (a jump is needed)
}
