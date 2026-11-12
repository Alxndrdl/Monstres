// Quests, teleporters and the villagers who give them.

#include "monstres/core/battle.hpp"
#include "monstres/core/debug.hpp"
#include "monstres/core/quests.hpp"
#include "monstres/core/save.hpp"
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

// Puts the player (and its companion) next to villager `npc`.
void go_to_npc(World& w, uint8_t n) {
  const int i = npc_index(w, n);
  REQUIRE(i > 0);
  const Vec3 p = w.creatures[static_cast<size_t>(i)].body.pos;
  w.player.pos = {p.x + 1.5f, w.ground.height(p.x + 1.5f, p.z), p.z};
  w.creatures[0].body.pos = w.player.pos;
}

void go_to_teleporter(World& w, TeleporterId id) {
  Vec3 at;
  REQUIRE(teleporter_position(w, id, at));
  w.player.pos = at;
  w.creatures[0].body.pos = at;
  update_population(w);
  step(w, Input{});  // seen: known from now on
}

int lost_piou(const World& w) {
  for (size_t i = 1; i < w.creatures.size(); ++i) {
    if (w.creatures[i].tag == Tag::LostPiou) return static_cast<int>(i);
  }
  return -1;
}

}  // namespace

TEST_CASE("the villagers have names; two of them give quests") {
  const World& w = cached_game(3);
  for (uint8_t n = 1; n <= rules::kVillagers; ++n) CHECK(npc_index(w, n) > 0);
  // Gaspard stands by the village's teleporter.
  Vec3 t;
  REQUIRE(teleporter_position(w, TeleporterId::Village, t));
  const Vec3 g = w.creatures[static_cast<size_t>(npc_index(w, npc::kGaspard))].body.pos;
  CHECK(std::hypot(g.x - t.x, g.z - t.z) < 4.0f);
  CHECK(quests_json(w).find("\"stage\":\"unknown\"") != std::string::npos);
}

TEST_CASE("every place has a teleporter, known once the player walks near it") {
  World w = cached_game(3);
  for (size_t k = 0; k < kTeleporterCount; ++k) {
    Vec3 at;
    CHECK(teleporter_position(w, static_cast<TeleporterId>(k), at));
  }
  CHECK(w.teleporters == 0);
  go_to_teleporter(w, TeleporterId::Village);
  CHECK(teleporter_known(w, TeleporterId::Village));
  CHECK(teleporter_here(w) == static_cast<int>(TeleporterId::Village));
  // Only one known: it leads nowhere.
  CHECK_FALSE(can_teleport(w, TeleporterId::Castle));
  go_to_teleporter(w, TeleporterId::Castle);
  // From the castle, back to the village in one step.
  REQUIRE(can_teleport(w, TeleporterId::Village));
  step(w, command(Command::Teleport, static_cast<uint16_t>(TeleporterId::Village)));
  Vec3 village;
  teleporter_position(w, TeleporterId::Village, village);
  CHECK(std::hypot(w.player.pos.x - village.x, w.player.pos.z - village.z) < 0.5f);
  CHECK(w.teleport_tick == w.tick);
  CHECK(std::hypot(w.creatures[0].body.pos.x - village.x, w.creatures[0].body.pos.z - village.z) < 3.0f);
  check_invariants(w);
}

TEST_CASE("the teleporters' quest: to the castle by teleporter, and back, then tell Gaspard") {
  World w = cached_game(3);
  go_to_npc(w, npc::kGaspard);
  step(w, command(Command::QuestAccept, static_cast<uint16_t>(QuestId::Teleporters)));
  REQUIRE(w.quests[1].stage == QuestStage::Active);
  go_to_teleporter(w, TeleporterId::Castle);  // found on foot
  go_to_teleporter(w, TeleporterId::Village);
  CHECK_FALSE(quest_ready(w, QuestId::Teleporters));
  step(w, command(Command::Teleport, static_cast<uint16_t>(TeleporterId::Castle)));
  CHECK((w.quests[1].progress & kWentToCastle) != 0);
  step(w, command(Command::Teleport, static_cast<uint16_t>(TeleporterId::Village)));
  CHECK(quest_ready(w, QuestId::Teleporters));
  // Telling him from far away does nothing; next to him, the quest is done.
  World far = w;
  far.player.pos = {0, far.ground.height(0, 0), 0};
  step(far, command(Command::QuestComplete, static_cast<uint16_t>(QuestId::Teleporters)));
  CHECK(far.quests[1].stage == QuestStage::Active);
  go_to_npc(w, npc::kGaspard);
  const uint32_t xp = w.wizard.xp;
  const uint16_t level = w.wizard.level;
  step(w, command(Command::QuestComplete, static_cast<uint16_t>(QuestId::Teleporters)));
  CHECK(w.quests[1].stage == QuestStage::Done);
  CHECK((w.wizard.xp > xp || w.wizard.level > level));
}

TEST_CASE("Mirabelle's lost Piou: emerald, near the village, back each time; given back, it stays with her") {
  World w = cached_game(3);
  const int found = lost_piou(w);
  REQUIRE(found > 0);  // it roams near the village, close to the start
  const Creature& piou = w.creatures[static_cast<size_t>(found)];
  CHECK(piou.species == Species::Bird);
  CHECK(piou.genes.allele[1] == rules::kLostPiouPlumage);
  CHECK_FALSE(piou.genes.chromatic);
  const Vec2 home = lost_piou_home(w);
  CHECK(std::hypot(home.x - w.village.center.x, home.z - w.village.center.z) < 40.0f);

  // Gone far away, then back: it is there again (only one).
  w.player.pos = {0, w.ground.height(0, 130), 130};
  update_population(w);
  CHECK(lost_piou(w) < 0);
  w.player.pos = {0, w.ground.height(0, 0), 0};
  update_population(w);
  CHECK(lost_piou(w) > 0);

  // Caught (here: put in the team as a capture would), it is not in the world anymore and does not come back.
  const int i = lost_piou(w);
  Monster m;
  m.species = Species::Bird;
  m.genes = w.creatures[static_cast<size_t>(i)].genes;
  m.level = 3;
  m.hp = 10;
  m.tag = Tag::LostPiou;
  remove_creature(w, static_cast<size_t>(i));
  w.team.push_back(m);
  w.player.pos = {0, w.ground.height(0, 130), 130};
  update_population(w);
  w.player.pos = {0, w.ground.height(0, 0), 0};
  update_population(w);
  CHECK(lost_piou(w) < 0);
  CHECK(quest_ready(w, QuestId::LostPiou));

  // Given to someone else: nothing. To Mirabelle: the quest is done, the Piou stays with her.
  step(w, command(Command::QuestGive, 1));
  CHECK(w.team.size() == 2);
  go_to_npc(w, npc::kMirabelle);
  const uint32_t xp = w.wizard.xp;
  step(w, command(Command::QuestGive, 1));
  REQUIRE(w.team.size() == 1);
  CHECK(w.quests[0].stage == QuestStage::Done);
  CHECK(w.wizard.xp > xp);
  const int pet = lost_piou(w);
  REQUIRE(pet > 0);
  CHECK(can_engage(w, static_cast<size_t>(pet)) == EngageCheck::Owned);
  // Mirabelle walks around: her Piou follows her, and stays when the player goes far away.
  run(w, Input{}, 20 * rules::kTicksPerSecond);
  const Vec3 her = w.creatures[static_cast<size_t>(npc_index(w, npc::kMirabelle))].body.pos;
  const Vec3 bird = w.creatures[static_cast<size_t>(lost_piou(w))].body.pos;
  CHECK(std::hypot(her.x - bird.x, her.z - bird.z) < 3.0f);
  w.player.pos = {0, w.ground.height(0, 130), 130};
  update_population(w);
  CHECK(lost_piou(w) > 0);
}

TEST_CASE("quests and teleporters are saved") {
  World w = cached_game(3);
  go_to_teleporter(w, TeleporterId::Village);
  go_to_npc(w, npc::kMirabelle);
  step(w, command(Command::QuestAccept, static_cast<uint16_t>(QuestId::LostPiou)));
  REQUIRE(w.quests[0].stage == QuestStage::Active);
  w.creatures.back().beaten = true;
  const auto loaded = deserialize(serialize(w));
  REQUIRE(loaded.has_value());
  CHECK(*loaded == w);
}
