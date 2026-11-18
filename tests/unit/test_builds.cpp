// The magician's mana, the Mercy and Detection spells, the monsters' builds
// (micro, alpha) and traits (gifts, size, weight, age), how each species
// fights, and a crow flying over the lake.

#include "monstres/core/battle.hpp"
#include "monstres/core/debug.hpp"
#include "monstres/core/quests.hpp"
#include "monstres/core/save.hpp"
#include "monstres/core/variants.hpp"
#include "test_helpers.hpp"

#include <cmath>
#include <map>
#include <utility>
#include <string>

using namespace monstres;

namespace {

Input command(Command c, uint16_t target = 0) {
  Input in;
  in.command = c;
  in.target = target;
  return in;
}

// Flat ground: the companion behind the player, a monster 3 m ahead (index 1).
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
  w.team = {starting_companion({})};
  return w;
}

}  // namespace

TEST_CASE("spells cost mana, which does not come back by itself; levels give more") {
  World w = arena();
  const uint16_t full = wizard_stats(w.wizard).mana;
  CHECK(w.wizard.mana == full);
  w.team[0].hp = 5;
  REQUIRE(w.adventure_spells[0] == SpellId::Heal);
  step(w, command(Command::Cast, 0));
  CHECK(w.wizard.mana == full - spell_info(SpellId::Heal).mana);
  run(w, Input{}, 20 * rules::kTicksPerSecond);
  CHECK(w.wizard.mana == full - spell_info(SpellId::Heal).mana);  // not back
  w.wizard.mana = spell_info(SpellId::Heal).mana - 1;
  CHECK(std::string(cannot_cast(w, 0)) == "mana");
  step(w, command(Command::Cast, 0));
  CHECK(w.wizard.mana == spell_info(SpellId::Heal).mana - 1);  // nothing cast
  // A new level: as much more mana as the most grew.
  const uint16_t before = w.wizard.mana;
  grant_wizard_xp(w.wizard, wizard_stats(w.wizard).xp_to_next);
  CHECK(w.wizard.level == 2);
  CHECK(wizard_stats(w.wizard).mana == full + rules::kWizardManaPerLevel);
  CHECK(w.wizard.mana == before + rules::kWizardManaPerLevel);
  CHECK(spells_json(w).find("\"manaMax\":25") != std::string::npos);
}

TEST_CASE("Mercy: the next blow that would beat a foe leaves it 1 hit point, once") {
  World w = arena(Species::Bird, 1);
  w.combat_spells = {SpellId::Mercy, SpellId::Shield};
  step(w, command(Command::Engage, 1));
  REQUIRE(w.battle.phase == BattlePhase::Fight);
  step(w, command(Command::Cast, 0));
  CHECK(w.battle.mercy);
  CHECK(std::string(cannot_cast(w, 0)) == "active");
  // Weakened, then a blow that would beat it (right next to it).
  const auto next_to_it = [&] {
    const Vec3 foe = w.creatures[1].body.pos;
    w.creatures[0].body.pos = {foe.x, foe.y, foe.z + 0.8f};
  };
  w.battle.foes[0].hp = 3;
  next_to_it();
  Input bite;
  bite.attack = 1;
  step(w, bite);
  run(w, Input{}, 2);
  REQUIRE(!w.battle.foes.empty());
  CHECK(w.battle.foes[0].hp == 1);
  CHECK_FALSE(w.battle.mercy);
  CHECK(w.battle.mercy_tick > 0);
  CHECK(can_capture(w));
  // Spent: the next one beats it.
  run(w, Input{}, 30);
  next_to_it();
  step(w, bite);
  run(w, Input{}, 2);
  CHECK(w.battle.phase == BattlePhase::None);
  CHECK(w.battle.outcome == BattleOutcome::Victory);
  CHECK(w.beaten_count[static_cast<size_t>(Species::Bird)] == 1);
}

TEST_CASE("Mercy lasts until the battle ends, and does nothing in training") {
  World w = arena();
  w.combat_spells = {SpellId::Mercy, SpellId::Shield};
  step(w, command(Command::Train));
  REQUIRE(w.battle.phase == BattlePhase::Training);
  CHECK(std::string(cannot_cast(w, 0)) == "training");
  step(w, command(Command::Flee));
  step(w, command(Command::Engage, 1));
  step(w, command(Command::Cast, 0));
  REQUIRE(w.battle.mercy);
  step(w, command(Command::Flee));
  run(w, Input{}, rules::kFleeMaxTicks + 1);
  CHECK(w.battle.phase == BattlePhase::None);
  CHECK_FALSE(w.battle.mercy);
}

TEST_CASE("Detection: for a minute, the closest very rare monster within 50 m") {
  World w = arena();
  w.adventure_spells = {SpellId::Detect, SpellId::None, SpellId::None, SpellId::None};
  Creature rare;
  rare.species = Species::Bird;
  rare.genes.form = Form::Alpha;
  rare.body.pos = {30, 0, 0};
  rare.home = {30, 0};
  rare.timer = 60000;
  const size_t far = add_creature(w, rare);
  rare.genes = {};
  rare.genes.allele[1] = 3;  // emerald: a very rare color
  rare.body.pos = {0, 0, 40};
  rare.home = {0, 40};
  const size_t near = add_creature(w, rare);
  rare.genes = {};
  rare.genes.chromatic = true;
  rare.body.pos = {60, 0, 0};  // too far
  rare.home = {60, 0};
  add_creature(w, rare);
  CHECK(detect_rare(w) == -1);  // not cast yet
  step(w, command(Command::Cast, 0));
  REQUIRE(w.detect_ticks > 0);
  CHECK(w.wizard.mana == wizard_stats(w.wizard).mana - spell_info(SpellId::Detect).mana);
  CHECK(detect_rare(w) == static_cast<int>(far));  // 30 m, closer than 40 m
  w.creatures[far].body.pos = {0, 0, 49};
  CHECK(detect_rare(w) == static_cast<int>(near));
  CHECK(std::string(cannot_cast(w, 0)) == "active");
  CHECK(spells_json(w).find("\"found\":{\"index\":" + std::to_string(near)) != std::string::npos);
  run(w, Input{}, rules::kDetectTicks);
  CHECK(w.detect_ticks == 0);
  CHECK(detect_rare(w) == -1);
  CHECK(cannot_cast(w, 0) == nullptr);
}

TEST_CASE("very rare: a very rare allele, a rare form, or a build") {
  CHECK_FALSE(very_rare(Species::Bird, {}));
  Genes g;
  g.allele[1] = 3;
  CHECK(very_rare(Species::Bird, g));
  g = {};
  g.allele[3] = 1;  // large wings: only rare
  CHECK_FALSE(very_rare(Species::Bird, g));
  g = {};
  g.elemental = true;
  CHECK(very_rare(Species::Slime, g));
  g = {};
  g.allele[0] = 1;
  g.form = Form::Micro;
  CHECK(very_rare(Species::Slime, g));
}

TEST_CASE("builds: micro only females, omega only males, both very rare in the wild") {
  Genes g;
  g.form = Form::Micro;
  CHECK_FALSE(valid_genes(Species::Slime, g));  // a male
  g.allele[0] = 1;
  CHECK(valid_genes(Species::Slime, g));
  g.form = Form::Omega;
  CHECK_FALSE(valid_genes(Species::Slime, g));
  g.allele[0] = 0;
  CHECK(valid_genes(Species::Slime, g));
  CHECK(genes_from_code(genes_code(g)) == g);
  CHECK_FALSE(valid_genes(Species::Villager, g));

  std::map<Form, int> seen;
  Rng rng(7);
  constexpr int kRolls = 256 * 200;
  for (int i = 0; i < kRolls; ++i) {
    const Genes r = roll_genes(Species::Slime, Zone::Meadow, rng);
    ++seen[r.form];
    CHECK(valid_genes(Species::Slime, r));
  }
  CHECK(seen[Form::Omega] == 0);  // not in the wild
  CHECK(seen[Form::Alpha] > 120);  // about 1 in 256
  CHECK(seen[Form::Alpha] < 300);
  CHECK(seen[Form::Micro] > 120);
  CHECK(seen[Form::Micro] < 300);
}

TEST_CASE("the bestiary lists the micro, alpha and omega builds after the rare forms") {
  const size_t n = variant_count(Species::Bird);
  CHECK(n <= kMaxVariants);
  CHECK(variant_id(Species::Bird, n - 3) == "micro");
  CHECK(variant_id(Species::Bird, n - 2) == "alpha");
  CHECK(variant_id(Species::Bird, n - 1) == "omega");
  CHECK(variant_id(Species::Bird, n - 4) == "elemental");
  const Genes micro = genes_with(Species::Bird, n - 3);
  CHECK(micro.form == Form::Micro);
  CHECK(valid_genes(Species::Bird, micro));  // a female
  CHECK(has_variant(Species::Bird, micro, n - 3));
  CHECK(valid_genes(Species::Bird, genes_with(Species::Bird, n - 1)));  // a male omega
  CHECK(bestiary_json(cached_game(3)).find("\"id\":\"alpha\",\"kind\":\"build\"") != std::string::npos);
}

TEST_CASE("builds change the gifts' bonuses only, not the fight stats (but the size the monster is drawn)") {
  const FightStats normal = monster_fight_stats(Species::Slime, 5);
  for (const Form form : {Form::Micro, Form::Alpha, Form::Omega}) {
    Genes g;
    g.form = form;
    g.allele[0] = form == Form::Micro ? 1 : 0;
    const FightStats f = monster_fight_stats(Species::Slime, 5, g);
    CHECK(f.hp == normal.hp);
    CHECK(f.attack == normal.attack);
    CHECK(f.defense == normal.defense);
    CHECK(f.speed == normal.speed);
    CHECK(f.magic == normal.magic);
  }
  CHECK(build_scale(Form::Omega) > build_scale(Form::Alpha));
  CHECK(build_scale(Form::Micro) < 1);
}

TEST_CASE("traits: gifts drawn from 5 to 15, moved by the build; sizes by build; the same for one monster") {
  for (uint32_t id = 1; id < 400; ++id) {
    for (const Form form : {Form::Normal, Form::Micro, Form::Alpha, Form::Omega}) {
      Genes g;
      g.form = form;
      g.allele[0] = form == Form::Micro ? 1 : 0;
      const Traits t = roll_traits(42, id, Species::Companion, g);
      for (size_t k = 0; k < kGiftCount; ++k) {
        CHECK(t.gifts[k] >= kGiftBaseMin);
        CHECK(t.gifts[k] <= kGiftBaseMax);
      }
      CHECK(t.age_days >= 20);
      const float scale = static_cast<float>(t.size_mm) / 450.0f;  // a normal male Pompon: 45 cm
      CHECK(scale > build_scale(form) * 0.8f);
      CHECK(scale < build_scale(form) * 1.15f);
    }
  }
  const auto range = [](Form form, size_t gift) {
    Genes g;
    g.form = form;
    g.allele[0] = form == Form::Micro ? 1 : 0;
    return std::pair{gift_value(Species::Companion, g, gift, kGiftBaseMin),
                     gift_value(Species::Companion, g, gift, kGiftBaseMax)};
  };
  CHECK(range(Form::Normal, 0) == std::pair<uint8_t, uint8_t>{5, 15});
  CHECK(range(Form::Micro, 3) == std::pair<uint8_t, uint8_t>{10, 20});  // charisma
  CHECK(range(Form::Micro, 0) == std::pair<uint8_t, uint8_t>{0, 10});   // strength
  CHECK(range(Form::Omega, 2) == std::pair<uint8_t, uint8_t>{10, 20});  // constitution
  CHECK(range(Form::Omega, 1) == std::pair<uint8_t, uint8_t>{0, 10});   // dexterity
  CHECK(range(Form::Alpha, 4) == std::pair<uint8_t, uint8_t>{7, 17});   // intelligence
  const Traits a = roll_traits(5, 9, Species::Slime, {}), b = roll_traits(5, 9, Species::Slime, {});
  CHECK(a.size_mm == b.size_mm);
  CHECK(a.gifts == b.gifts);
}

TEST_CASE("variants move gifts and fight stats: rare forms, very rare colors, the Gloubi's eyes and top") {
  const auto mod = [](Species species, std::initializer_list<const char*> ids, Stat stat) {
    Genes g;
    for (const char* id : ids) {
      for (size_t i = 0; i < variant_count(species); ++i) {
        if (variant_id(species, i) != id) continue;
        const Genes one = genes_with(species, i);
        for (size_t k = 1; k < kMaxGenes; ++k) g.allele[k] = std::max(g.allele[k], one.allele[k]);
        g.chromatic = g.chromatic || one.chromatic;
        g.elemental = g.elemental || one.elemental;
      }
    }
    return stat_mod(species, g, stat);
  };
  CHECK(mod(Species::Slime, {}, Stat::Charisma) == 0);
  CHECK(mod(Species::Crow, {"chromatic"}, Stat::Charisma) == 2);
  CHECK(mod(Species::Crow, {"elemental"}, Stat::Charisma) == 1);
  CHECK(mod(Species::Crow, {"elemental"}, Stat::Intelligence) == 1);
  CHECK(mod(Species::Bird, {"plumage.emerald"}, Stat::Charisma) == 1);  // a very rare color
  CHECK(mod(Species::Bird, {"plumage.blue"}, Stat::Charisma) == 0);
  CHECK(mod(Species::Slime, {"top.antennae"}, Stat::Intelligence) == 1);
  CHECK(mod(Species::Slime, {"eyes.three"}, Stat::Intelligence) == 1);
  CHECK(mod(Species::Slime, {"eyes.three"}, Stat::Constitution) == -1);
  CHECK(mod(Species::Slime, {"eyes.one"}, Stat::Intelligence) == -1);
  CHECK(mod(Species::Slime, {"eyes.one"}, Stat::Constitution) == 1);
  CHECK(mod(Species::Slime, {"top.horn"}, Stat::Charisma) == -1);
  CHECK(mod(Species::Slime, {"top.horn"}, Stat::Defense) == 2);
  CHECK(mod(Species::Slime, {"eyes.three", "top.antennae", "chromatic"}, Stat::Intelligence) == 2);
  // The horn: 2 more defense points, at any level.
  Genes horn;
  horn.allele[3] = 1;
  CHECK(monster_fight_stats(Species::Slime, 6, horn).defense == monster_fight_stats(Species::Slime, 6).defense + 2);
  // A gift stays within 0 to 20.
  Genes micro;
  micro.allele[0] = 1;
  micro.form = Form::Micro;
  micro.chromatic = true;
  CHECK(gift_value(Species::Slime, micro, 3, kGiftBaseMax) == kGiftMax);  // 15 + 5 + 2, at most 20
  // Shown on the team's sheet: the value, its base, its range, where its modifiers come from.
  World w = new_game(3);
  w.team.push_back(starting_companion({}));
  Monster& m = w.team.back();
  m.species = Species::Slime;
  m.genes = horn;
  m.gifts = {10, 10, 10, 10, 10};
  const std::string team = team_json(w);
  CHECK(team.find("{\"stat\":\"charisma\",\"value\":9,\"base\":10,\"min\":4,\"max\":14,"
                  "\"mods\":[{\"source\":\"top.horn\",\"value\":-1}]}") != std::string::npos);
  CHECK(team.find("\"statMods\":{\"defense\":[{\"source\":\"top.horn\",\"value\":2}]}") != std::string::npos);
}

TEST_CASE("a captured monster gets its traits, counted in the bestiary; saved and loaded") {
  World w = new_game(11);
  CHECK(w.team[0].size_mm > 0);
  CHECK(w.team[0].gifts[0] >= 5);
  // A weakened slime next to the companion, captured.
  Creature wild;
  wild.species = Species::Slime;
  wild.genes.form = Form::Alpha;
  wild.body.pos = w.player.pos;
  wild.body.pos.x += 2;
  wild.home = {wild.body.pos.x, wild.body.pos.z};
  wild.timer = 1000;
  const size_t i = add_creature(w, wild);
  const uint32_t id = w.creatures[i].id;
  step(w, command(Command::Engage, static_cast<uint16_t>(i)));
  REQUIRE(w.battle.phase == BattlePhase::Fight);
  w.battle.foes[0].hp = 1;
  REQUIRE(seal(w));
  REQUIRE(w.team.size() == 2);
  const Monster& m = w.team[1];
  const Traits t = roll_traits(w.seed, id, Species::Slime, m.genes);
  CHECK(m.gifts == t.gifts);
  CHECK(m.size_mm == t.size_mm);
  CHECK(world_day(w) - m.born_day == t.age_days);
  CHECK(monster_gifts(m)[0] >= 7);  // an alpha: +2
  CHECK(w.captured_count[static_cast<size_t>(Species::Slime)] == 1);
  CHECK(bestiary_json(w).find("\"captured\":1,") != std::string::npos);
  CHECK(team_json(w).find("\"form\":\"alpha\"") != std::string::npos);

  w.wizard.mana = 7;
  w.detect_ticks = 99;
  const auto loaded = deserialize(serialize(w));
  REQUIRE(loaded.has_value());
  CHECK(loaded->team == w.team);
  CHECK(loaded->wizard.mana == 7);
  CHECK(loaded->detect_ticks == 99);
  CHECK(loaded->captured_count == w.captured_count);
}

TEST_CASE("each species fights its own way: a mushroom keeps away, a slime stays close") {
  const auto distance_kept = [](Species species) {
    World w = arena(species, 3);
    step(w, command(Command::Engage, 1));
    REQUIRE(w.battle.phase == BattlePhase::Fight);
    float total = 0;
    int samples = 0;
    for (int t = 0; t < 8 * rules::kTicksPerSecond; ++t) {
      w.team[0].hp = 999;  // it lasts
      w.battle.foes[0].hp = w.battle.foes[0].max_hp;
      step(w, Input{});
      if (t > 2 * rules::kTicksPerSecond && t % 10 == 0) {
        const Vec3 a = w.creatures[0].body.pos, b = w.creatures[1].body.pos;
        total += std::sqrt((a.x - b.x) * (a.x - b.x) + (a.z - b.z) * (a.z - b.z));
        ++samples;
      }
    }
    return total / static_cast<float>(samples);
  };
  const float mushroom = distance_kept(Species::Mushroom);
  const float slime = distance_kept(Species::Slime);
  CHECK(mushroom > slime + 1.0f);
  CHECK(species_temper(Species::Bird).dodge_odds < species_temper(Species::Slime).dodge_odds);
  CHECK(species_temper(Species::Crow).lift > species_temper(Species::Slime).lift);
}

TEST_CASE("a crow companion flies over the lake; a walker stops at its shore") {
  for (const Species species : {Species::Crow, Species::Companion}) {
    World w = arena();
    w.creatures.resize(2);
    w.creatures[1].body.pos = {0, 0, -14};  // the foe across the lake
    w.creatures[1].home = {0, -14};
    w.village.lake = {{0, -7}, 4, 3};
    w.team[0].species = species;
    w.creatures[0].species = species;
    step(w, command(Command::Engage, 1));
    REQUIRE(w.battle.phase == BattlePhase::Fight);
    Input ahead;
    ahead.move_y = 127;
    for (int t = 0; t < 3 * rules::kTicksPerSecond; ++t) {
      w.battle.foes[0].fighter.state = FighterState::Stunned;  // it waits
      w.battle.foes[0].fighter.state_ticks = 100;
      step(w, ahead);
    }
    const Vec3 at = w.creatures[0].body.pos;
    if (species == Species::Crow) {
      CHECK(at.z < -8.0f);  // past the middle of the lake
      CHECK(at.y >= rules::kWaterLevel);
    } else {
      // Stopped by the water (its shore on that side: the outline's).
      CHECK(at.z > -7.0f + 3.0f * outline_scale({0, -7}, kLakeOutline, static_cast<float>(kPi / 2)) - 0.3f);
    }
  }
}

namespace {

void go_to(World& w, Vec2 at) {
  w.player.pos = {at.x, w.ground.height(at.x, at.z), at.z};
  if (w.active >= 0) w.creatures[0].body.pos = w.player.pos;
  update_population(w);
}

int giant_index(const World& w) {
  for (size_t i = 1; i < w.creatures.size(); ++i) {
    if (w.creatures[i].tag == Tag::Giant) return static_cast<int>(i);
  }
  return -1;
}

void near_lison(World& w) {
  const int i = npc_index(w, npc::kLison);
  REQUIRE(i > 0);
  const Vec3 p = w.creatures[static_cast<size_t>(i)].body.pos;
  go_to(w, {p.x + 1.5f, p.z});
}

// Engages the giant, weakens it, captures it, and beats the others.
// Returns the experience the giant was worth.
uint32_t catch_giant(World& w) {
  const int g = giant_index(w);
  REQUIRE(g > 0);
  const Vec3 p = w.creatures[static_cast<size_t>(g)].body.pos;
  w.player.pos = {p.x + 3, w.ground.height(p.x + 3, p.z), p.z};
  w.creatures[0].body.pos = w.player.pos;
  w.team[0].hp = 999;
  step(w, command(Command::Engage, static_cast<uint16_t>(g)));
  REQUIRE(w.battle.phase == BattlePhase::Fight);
  REQUIRE(w.creatures[w.battle.foes[0].index].tag == Tag::Giant);
  w.battle.target = 0;
  w.battle.foes[0].hp = 1;
  const uint32_t xp = w.battle.foes[0].xp;
  REQUIRE(seal(w));
  for (Foe& f : w.battle.foes) f.hp = 0;
  step(w, Input{});
  REQUIRE(w.battle.phase == BattlePhase::None);
  w.team[0].hp = monster_fight_stats(w.team[0]).hp;
  return xp;
}

}  // namespace

TEST_CASE("the giant Champi: an omega with its group in the forest, caught, shown to Lison, let go, caught again") {
  World w = new_game(5);
  const Vec2 home = giant_home(w);
  go_to(w, home);
  const int g = giant_index(w);
  REQUIRE(g > 0);
  const Creature& giant = w.creatures[static_cast<size_t>(g)];
  CHECK(giant.species == Species::Mushroom);
  CHECK(giant.genes.form == Form::Omega);
  CHECK(valid_genes(giant.species, giant.genes));
  int alpha = 0, normal = 0;
  for (const Creature& c : w.creatures) {
    if (c.species != Species::Mushroom || c.tag != Tag::None) continue;
    const float dx = c.home.x - home.x, dz = c.home.z - home.z;
    if (dx * dx + dz * dz > 12 * 12) continue;
    alpha += c.genes.form == Form::Alpha && c.level == rules::kGiantAlphaLevel;
    normal += c.genes.form == Form::Normal && c.level == rules::kGiantEscortLevel;
  }
  CHECK(alpha == 2);
  CHECK(normal >= 2);
  update_population(w);
  CHECK(giant_index(w) == g);  // only one

  // Lison gives the quest; caught, it is shown to her: experience, once.
  near_lison(w);
  const QuestId quest = QuestId::GiantChampi;
  step(w, command(Command::QuestAccept, static_cast<uint16_t>(quest)));
  REQUIRE(w.quests[2].stage == QuestStage::Active);
  go_to(w, home);
  const uint32_t xp_before = w.wizard.xp + 100000u * w.wizard.level;
  CHECK(catch_giant(w) > 0);
  CHECK((w.quests[2].progress & kGiantCaught) != 0);
  CHECK(quest_ready(w, quest));
  CHECK(w.team.back().tag == Tag::Giant);
  CHECK(w.team.back().genes.form == Form::Omega);
  near_lison(w);
  step(w, command(Command::QuestComplete, static_cast<uint16_t>(quest)));
  CHECK(w.quests[2].stage == QuestStage::Done);
  CHECK(w.wizard.xp + 100000u * w.wizard.level > xp_before);
  // Caught: it does not come back.
  go_to(w, {home.x + 200, home.z});  // far: the forest is emptied
  go_to(w, home);
  CHECK(giant_index(w) == -1);

  // Let go, then gone: the quest can be taken again; neither it nor the capture give experience.
  const size_t slot = w.team.size() - 1;
  step(w, command(Command::Release, static_cast<uint16_t>(slot)));
  REQUIRE(giant_index(w) > 0);
  CHECK(w.quests[2].stage == QuestStage::Done);  // still out there
  go_to(w, {-home.x, -home.z});
  go_to(w, {home.x + 200, home.z});
  step(w, Input{});
  CHECK(giant_index(w) == -1);
  CHECK(w.quests[2].stage == QuestStage::Unknown);
  go_to(w, home);
  CHECK(giant_index(w) == -1);  // only once the quest is taken again
  near_lison(w);
  step(w, command(Command::QuestAccept, static_cast<uint16_t>(quest)));
  REQUIRE(w.quests[2].stage == QuestStage::Active);
  go_to(w, {home.x + 200, home.z});
  go_to(w, home);
  REQUIRE(giant_index(w) > 0);
  CHECK(w.creatures[static_cast<size_t>(giant_index(w))].released);
  CHECK(catch_giant(w) == 0);
  near_lison(w);
  const Wizard before = w.wizard;
  step(w, command(Command::QuestComplete, static_cast<uint16_t>(quest)));
  CHECK(w.quests[2].stage == QuestStage::Done);
  CHECK(w.wizard.xp == before.xp);
  CHECK(w.wizard.level == before.level);

  // Saved and loaded with it.
  const auto loaded = deserialize(serialize(w));
  REQUIRE(loaded.has_value());
  CHECK(loaded->quests == w.quests);
  CHECK(loaded->team.back().tag == Tag::Giant);
}
