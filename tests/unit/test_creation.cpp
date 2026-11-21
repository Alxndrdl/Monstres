// A new game as the player makes it: the magician's look, the first monster
// (Zapou and Ploufi), Isidore and the main quest's first part, the arrival.

#include "monstres/core/battle.hpp"
#include "monstres/core/quests.hpp"
#include "monstres/core/save.hpp"
#include "monstres/core/variants.hpp"
#include "monstres/render/characters.hpp"
#include "monstres/render/intro.hpp"
#include "monstres/render/scene.hpp"
#include "test_helpers.hpp"

#include <cmath>

using namespace monstres;

namespace {

Input command(Command c, uint16_t target = 0) {
  Input in;
  in.command = c;
  in.target = target;
  return in;
}

void go_to_npc(World& w, uint8_t n) {
  const int i = npc_index(w, n);
  REQUIRE(i > 0);
  const Vec3 p = w.creatures[static_cast<size_t>(i)].body.pos;
  w.player.pos = {p.x + 1.5f, w.ground.height(p.x + 1.5f, p.z), p.z};
  w.creatures[0].body.pos = w.player.pos;
}

WizardLook some_look() {
  WizardLook l;
  l.woman = true;
  l.skin = 4;
  l.hair = look::kBun;
  l.hair_color = 6;
  l.eyes = 3;
  l.staff = look::kWand;
  l.robe = 7;
  l.headwear = look::kHood;
  return l;
}

size_t count(const DrawList& list, MeshId id) { return list.batches[static_cast<size_t>(id)].size(); }

}  // namespace

TEST_CASE("a look is kept as one number, and checked") {
  const WizardLook l = some_look();
  CHECK(valid_look(l));
  CHECK(look_from_code(look_code(l)) == l);
  CHECK(look_from_code(look_code(WizardLook{})) == WizardLook{});
  WizardLook bad = l;
  bad.skin = look::kSkins;
  CHECK_FALSE(valid_look(bad));
  bad = l;
  bad.headwear = 3;
  CHECK_FALSE(valid_look(bad));
}

TEST_CASE("a new game is the player's: name, look, and the first monster of its sex") {
  for (const Species starter : kStarters) {
    CAPTURE(static_cast<int>(starter));
    NewGame choices{"Zoé", some_look(), starter};
    const World w = new_game(42, choices);
    CHECK(w.wizard.name == "Zoé");
    CHECK(w.wizard.look == some_look());
    REQUIRE(w.team.size() == 1);
    const Monster& m = w.team[0];
    CHECK(m.species == starter);
    CHECK(m.genes.allele[0] == 1);  // a female, as the magician
    for (size_t g = 1; g < kMaxGenes; ++g) CHECK(m.genes.allele[g] == 0);  // the most common variants
    CHECK_FALSE(m.genes.chromatic);
    CHECK_FALSE(m.genes.elemental);
    CHECK(m.genes.form == Form::Normal);
    CHECK(m.hp == monster_fight_stats(m).hp);
    CHECK(w.creatures[0].species == starter);  // out of the crystal, by the player
    CHECK(w.active == 0);
    // Known in the bestiary: this one only.
    CHECK(w.dex[static_cast<size_t>(starter)][1] == Discovery::Captured);  // the female's variant
    if (starter != Species::Companion) CHECK(w.dex[static_cast<size_t>(Species::Companion)][0] == Discovery::Unknown);
  }
  // Not a starter: Pompon instead.
  CHECK(new_game(42, {"", {}, Species::Crow}).team[0].species == Species::Companion);
  // The same seed, the same world, whatever is chosen.
  CHECK(new_game(42, {"A", some_look(), Species::Spark}).objects == new_game(42).objects);
}

TEST_CASE("the look and the new monsters survive a save") {
  World w = new_game(7, {"Léo", some_look(), Species::Axolotl});
  run(w, move(0, 127), 120);
  const auto loaded = deserialize(serialize(w));
  REQUIRE(loaded.has_value());
  CHECK(loaded->wizard.look == some_look());
  CHECK(loaded->team[0].species == Species::Axolotl);
  CHECK(*loaded == w);
}

TEST_CASE("Zapou and Ploufi live in the wild, with their own variants") {
  for (const Species s : {Species::Spark, Species::Axolotl}) {
    CAPTURE(static_cast<int>(s));
    CHECK(monster_stats(s).is_monster);
    CHECK(variant_count(s) > 6);
    CHECK(creature_rig(s).size() >= creature_rig(Species::Companion).size());  // walks as Pompon (Zapou: and its quills)
    Rng rng(3);
    for (int k = 0; k < 50; ++k) CHECK(valid_genes(s, roll_genes(s, Zone::Plains, rng)));
  }
  CHECK(monster_stats(Species::Spark).element == Element::Electric);
  CHECK(monster_stats(Species::Axolotl).element == Element::Water);
  // Somewhere in the wilds around the valley.
  size_t sparks = 0, axolotls = 0;
  for (uint64_t seed = 0; seed < 6; ++seed) {
    World w = new_game(seed);
    for (float x = -500; x <= 500; x += 250) {
      w.player.pos = {x, w.ground.height(x, 200), 200};
      update_population(w);
      for (const Creature& c : w.creatures) {
        sparks += c.species == Species::Spark;
        axolotls += c.species == Species::Axolotl;
      }
    }
  }
  CHECK(sparks > 0);
  CHECK(axolotls > 0);
}

TEST_CASE("Isidore, the old magician, waits on the village square, and comes to older games") {
  const World& w = cached_game(42);
  const int i = npc_index(w, npc::kMentor);
  REQUIRE(i > 0);
  const Vec3 p = w.creatures[static_cast<size_t>(i)].body.pos;
  CHECK(std::hypot(p.x - w.village.center.x, p.z - w.village.center.z) < 15);
  World old = cached_game(42);
  old.creatures.erase(old.creatures.begin() + i);
  add_mentor(old);
  CHECK(npc_index(old, npc::kMentor) > 0);
  add_mentor(old);  // only once
  size_t mentors = 0;
  for (const Creature& c : old.creatures) mentors += c.species == Species::Villager && c.npc == npc::kMentor;
  CHECK(mentors == 1);
}

TEST_CASE("the main quest: Isidore sends the player to the village's lessons, then rewards it") {
  World w = new_game(42);
  const auto stage = [&](QuestId q) { return w.quests[static_cast<size_t>(q)].stage; };
  go_to_npc(w, npc::kMentor);
  step(w, command(Command::QuestAccept, static_cast<uint16_t>(QuestId::Main)));
  CHECK(stage(QuestId::Main) == QuestStage::Active);
  CHECK_FALSE(quest_ready(w, QuestId::Main));

  // Bastien's lesson: win one battle against him (the victory is counted elsewhere).
  go_to_npc(w, npc::kBastien);
  step(w, command(Command::QuestAccept, static_cast<uint16_t>(QuestId::Combat)));
  step(w, command(Command::QuestComplete, static_cast<uint16_t>(QuestId::Combat)));
  CHECK(stage(QuestId::Combat) == QuestStage::Active);  // not yet
  w.rival_wins = 1;
  step(w, command(Command::QuestComplete, static_cast<uint16_t>(QuestId::Combat)));
  CHECK(stage(QuestId::Combat) == QuestStage::Done);

  // Odette's: the Heal spell on a hurt monster.
  go_to_npc(w, npc::kOdette);
  step(w, command(Command::QuestAccept, static_cast<uint16_t>(QuestId::Healing)));
  step(w, command(Command::Cast, 0));  // healthy: it does not count
  CHECK_FALSE(quest_ready(w, QuestId::Healing));
  w.team[0].hp = 3;
  w.spell_cooldowns = {};
  step(w, command(Command::Cast, 0));
  CHECK(quest_ready(w, QuestId::Healing));
  const uint32_t xp = w.wizard.xp;
  step(w, command(Command::QuestComplete, static_cast<uint16_t>(QuestId::Healing)));
  CHECK(stage(QuestId::Healing) == QuestStage::Done);
  CHECK(w.wizard.xp + (w.wizard.level > 1 ? 1000 : 0) > xp);

  // The two others (their own tests: test_quests.cpp), done.
  w.quests[static_cast<size_t>(QuestId::LostPiou)].stage = QuestStage::Done;
  CHECK_FALSE(quest_ready(w, QuestId::Main));
  w.quests[static_cast<size_t>(QuestId::Teleporters)].stage = QuestStage::Done;
  CHECK(quest_ready(w, QuestId::Main));
  step(w, command(Command::QuestComplete, static_cast<uint16_t>(QuestId::Main)));
  CHECK(stage(QuestId::Main) == QuestStage::Active);  // to be told to Isidore, not far away
  go_to_npc(w, npc::kMentor);
  step(w, command(Command::QuestComplete, static_cast<uint16_t>(QuestId::Main)));
  CHECK(stage(QuestId::Main) == QuestStage::Done);
  CHECK((w.quests[static_cast<size_t>(QuestId::Main)].progress & kMainPart1Done) != 0);
}

TEST_CASE("the magician is drawn as chosen: a woman's robe, her hair, a hood, a wand") {
  const WizardRig r = wizard_rig(some_look());
  const auto has = [&](MeshId id) {
    for (const RigPart& p : r.parts) {
      if (p.mesh == id) return true;
    }
    return false;
  };
  CHECK(has(MeshId::PlayerTorsoWoman));
  CHECK(has(MeshId::PlayerHeadWoman));
  CHECK(has(MeshId::PlayerHood));
  CHECK(has(MeshId::PlayerWand));
  CHECK_FALSE(has(MeshId::PlayerHat));
  CHECK_FALSE(has(MeshId::PlayerStaff));
  CHECK(has(MeshId::PlayerHairShortHood));  // pressed under the hood (a bun does not fit)
  // Without it (choosing the hair): no hood, the bun.
  const WizardRig bare = wizard_rig(some_look(), {.bare_head = true});
  bool bun = false, hood = false;
  for (const RigPart& p : bare.parts) {
    bun = bun || p.mesh == MeshId::PlayerHairBun;
    hood = hood || p.mesh == MeshId::PlayerHood;
  }
  CHECK(bun);
  CHECK_FALSE(hood);
  // Under a hat: the hair that fits under it.
  WizardLook hat = some_look();
  hat.headwear = look::kHat;
  bool fitted = false;
  for (const RigPart& p : wizard_rig(hat).parts) fitted = fitted || p.mesh == MeshId::PlayerHairBunHat;
  CHECK(fitted);
  // Colors: the robe, the skin, the hair, the eyes differ from the default look.
  const WizardRig plain = wizard_rig(WizardLook{});
  for (size_t k = 1; k < 5; ++k) {
    CAPTURE(k);
    CHECK((r.tints[k].r != plain.tints[k].r || r.tints[k].b != plain.tints[k].b));
  }
  // The game draws the player that way.
  World w = new_game(3, {"", some_look(), Species::Companion});
  DrawList list;
  list.batches.resize(kMeshCount);
  add_characters(w, 0, list);
  CHECK(count(list, MeshId::PlayerTorsoWoman) == 1);
  CHECK(count(list, MeshId::PlayerBeard) == 1);  // Isidore's
}

TEST_CASE("the character creation's picture turns and zooms around the magician") {
  PortraitSubject s;
  s.wizard = true;
  s.look = some_look();
  const Portrait front = build_preview(s, 1, {0, 0.1f, 1, 0, 0}, 0);
  const Portrait back = build_preview(s, 1, {3.14159f, 0.1f, 1, 0, 0}, 0);
  const Portrait close = build_preview(s, 1, {0, 0.1f, 3, 0, 0.4f}, 0);
  CHECK(front.view.camera_z > 0);
  CHECK(back.view.camera_z < 0);
  CHECK(std::hypot(close.view.camera_x, close.view.camera_z) < std::hypot(front.view.camera_x, front.view.camera_z));
  CHECK(count(front.list, MeshId::PlayerHood) == 1);
}

TEST_CASE("the arrival comes down from high above to the game's camera, never under the ground") {
  const World& w = cached_game(42);
  const Shot last{{w.player.pos.x, w.player.pos.y + 3, w.player.pos.z + 6}, {w.player.pos.x, w.player.pos.y + 1, w.player.pos.z}};
  const Shot start = intro_shot(w, 0, last);
  CHECK(start.eye.y > w.player.pos.y + 150);
  float previous = start.eye.y;
  for (float t = 0.05f; t <= kIntroSeconds; t += 0.05f) {
    const Shot s = intro_shot(w, t, last);
    CHECK(s.eye.y >= w.ground.height(s.eye.x, s.eye.z) + 1.9f);
    CHECK(s.target.x == doctest::Approx(last.target.x));  // always on the magician
    CHECK(s.eye.y <= previous + 0.01f);                   // only coming down
    previous = s.eye.y;
  }
  const Shot end = intro_shot(w, kIntroSeconds, last);
  CHECK(end.eye.x == doctest::Approx(last.eye.x));
  CHECK(end.eye.y == doctest::Approx(last.eye.y));
  CHECK(intro_shot(w, kIntroSeconds - 0.05f, last).eye.y == doctest::Approx(last.eye.y).epsilon(0.05));
}

TEST_CASE("villagers show what they have for the player: quests to take or going on, healing, battles") {
  World w = new_game(42);
  CHECK(npc_marks(w, npc::kMentor) == kMarkNewQuest);
  CHECK(npc_marks(w, npc::kOdette) == (kMarkNewQuest | kMarkHeal));
  CHECK(npc_marks(w, npc::kBastien) == (kMarkNewQuest | kMarkFight));
  CHECK(npc_marks(w, 6) == 0);  // Félix: nothing
  w.quests[static_cast<size_t>(QuestId::Main)].stage = QuestStage::Active;
  CHECK(npc_marks(w, npc::kMentor) == kMarkQuest);
  w.quests[static_cast<size_t>(QuestId::Main)].stage = QuestStage::Done;
  CHECK(npc_marks(w, npc::kMentor) == kMarkNewQuest);  // the strange stone to bring to the farm (west.hpp)
  w.quests[static_cast<size_t>(QuestId::Delivery)].stage = QuestStage::Done;
  CHECK(npc_marks(w, npc::kMentor) == 0);
  // Drawn over their heads.
  DrawList list;
  list.batches.resize(kMeshCount);
  add_characters(w, 0, list);
  // Odette, Violette at the farm, Solène at the refuge, Agathe at Rochebrune, Aubin at the inn
  CHECK(count(list, MeshId::IconPlus) == 5);
  CHECK(count(list, MeshId::IconCross) == 3);  // Bastien, Romane in the woods, Barnabé at the arena
  // A quest to take: a white "!" (Mirabelle, Gaspard, Lison, Bastien, Odette...); "?" no more.
  CHECK(count(list, MeshId::IconExclaim) >= 4);
  CHECK(count(list, MeshId::IconQuestion) == 0);
}

TEST_CASE("each species' habitat, and how common it is there") {
  CHECK(species_habitat(Species::Fish) == (1u << static_cast<unsigned>(Zone::Lake)));
  CHECK(species_habitat(Species::Villager) == 0);
  CHECK(species_chance(Species::Spark, Zone::Hills) > species_chance(Species::Spark, Zone::Plains));
  CHECK(species_chance(Species::Spark, Zone::Village) == 0);  // nothing wild in the village
  CHECK(species_chance(Species::Axolotl, Zone::Coast) > 0);
  for (const Species s : {Species::Slime, Species::Mushroom, Species::Bird, Species::Spark, Species::Axolotl}) {
    CHECK(species_habitat(s) != 0);
  }
  const TypicalMeasures m = typical_measures(Species::Spark);
  CHECK(m.size_mm > 200);
  CHECK(m.size_mm < 260);
}

TEST_CASE("victories are counted, and kept") {
  World w = new_game(42);
  CHECK(w.battles_won == 0);
  w.battles_won = 7;
  CHECK(deserialize(serialize(w))->battles_won == 7);
}
