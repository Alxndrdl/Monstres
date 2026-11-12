#include "monstres/core/quests.hpp"

#include "monstres/core/battle.hpp"
#include "monstres/core/variants.hpp"

#include <cmath>

namespace monstres {

namespace {

float flat_distance2(Vec3 a, Vec3 b) {
  const float dx = a.x - b.x, dz = a.z - b.z;
  return dx * dx + dz * dz;
}

QuestState& quest(World& w, QuestId q) { return w.quests[static_cast<size_t>(q)]; }

// The reward of a quest: experience for the magician and the companion out.
void reward(World& w, uint32_t wizard_xp, uint32_t monster_xp) {
  grant_wizard_xp(w.wizard, wizard_xp);
  if (Monster* m = active_monster(w)) grant_monster_xp(*m, monster_xp);
}

void teleport(World& w, TeleporterId to) {
  if (!can_teleport(w, to)) return;
  const int from = teleporter_here(w);
  Vec3 at;
  teleporter_position(w, to, at);
  w.teleport_tick = w.tick;
  w.teleport_from = w.player.pos;
  w.player.pos = at;
  w.player.velocity_y = 0;
  w.player.on_ground = true;
  if (!w.creatures.empty()) {  // the companion comes along, beside the player
    Body& c = w.creatures[0].body;
    c.pos = {at.x + 0.9f, w.ground.height(at.x + 0.9f, at.z + 1.0f), at.z + 1.0f};
    c.velocity_y = 0;
  }
  // The teleporters' quest: from the village to the castle, and back.
  QuestState& q = quest(w, QuestId::Teleporters);
  if (q.stage == QuestStage::Active) {
    if (from == static_cast<int>(TeleporterId::Village) && to == TeleporterId::Castle) q.progress |= kWentToCastle;
    if (from == static_cast<int>(TeleporterId::Castle) && to == TeleporterId::Village && (q.progress & kWentToCastle)) {
      q.progress |= kCameBack;
    }
  }
  update_population(w);  // monsters around the new place
}

// Mirabelle gets her Piou back: it leaves the team and stays by her side.
void give(World& w, size_t slot) {
  if (slot >= w.team.size() || w.team.size() < 2 || w.team[slot].tag != Tag::LostPiou) return;
  if (!near_npc(w, npc::kMirabelle) || w.battle.phase != BattlePhase::None) return;
  QuestState& q = quest(w, QuestId::LostPiou);
  if (q.stage == QuestStage::Done) return;
  const Monster m = w.team[slot];
  const Creature& owner = w.creatures[static_cast<size_t>(npc_index(w, npc::kMirabelle))];
  Creature pet;
  pet.species = m.species;
  pet.genes = m.genes;
  pet.level = static_cast<uint8_t>(std::min<uint16_t>(m.level, 255));
  pet.tag = Tag::LostPiou;
  pet.body.pos = {owner.body.pos.x + 0.8f, owner.body.pos.y, owner.body.pos.z + 0.6f};
  pet.home = {owner.body.pos.x, owner.body.pos.z};
  if (static_cast<int>(slot) == w.active) {  // it was out: it simply goes to her
    pet.body = w.creatures[0].body;
    w.active = -1;
    w.creatures[0].mind = Mind::Idle;
  } else {
    w.released_tick = w.tick;  // out of the crystal, in a beam of light
  }
  w.released_id = w.creatures[add_creature(w, pet)].id;
  w.team.erase(w.team.begin() + static_cast<std::ptrdiff_t>(slot));
  if (w.active > static_cast<int>(slot)) --w.active;
  q.stage = QuestStage::Done;
  reward(w, rules::kLostPiouWizardXp, rules::kLostPiouMonsterXp);
}

}  // namespace

int npc_index(const World& w, uint8_t n) {
  for (size_t i = 1; i < w.creatures.size(); ++i) {
    if (w.creatures[i].species == Species::Villager && w.creatures[i].npc == n) return static_cast<int>(i);
  }
  return -1;
}

bool near_npc(const World& w, uint8_t n) {
  const int i = npc_index(w, n);
  return i > 0 && flat_distance2(w.creatures[static_cast<size_t>(i)].body.pos, w.player.pos) <
                      rules::kTalkDistance * rules::kTalkDistance;
}

bool teleporter_known(const World& w, TeleporterId id) {
  return (w.teleporters >> static_cast<unsigned>(id)) & 1u;
}

int teleporter_here(const World& w) {
  for (size_t k = 0; k < kTeleporterCount; ++k) {
    const auto id = static_cast<TeleporterId>(k);
    Vec3 at;
    if (!teleporter_known(w, id) || !teleporter_position(w, id, at)) continue;
    if (flat_distance2(at, w.player.pos) < rules::kTeleporterUseDistance * rules::kTeleporterUseDistance) {
      return static_cast<int>(k);
    }
  }
  return -1;
}

bool can_teleport(const World& w, TeleporterId to) {
  const int here = teleporter_here(w);
  return here >= 0 && here != static_cast<int>(to) && static_cast<size_t>(to) < kTeleporterCount &&
         teleporter_known(w, to) && w.battle.phase == BattlePhase::None;
}

Vec2 lost_piou_home(const World& w) {
  // Away from the lake, on the other side of the village, a little south.
  const Vec2 c = w.village.center;
  const float side = w.village.lake.center.x > c.x ? -1.0f : 1.0f;
  return {c.x + side * rules::kLostPiouDistance * 0.8f, c.z + rules::kLostPiouDistance * 0.6f};
}

bool lost_piou_exists(const World& w) {
  for (const Monster& m : w.team) {
    if (m.tag == Tag::LostPiou) return true;
  }
  for (const Creature& c : w.creatures) {
    if (c.tag == Tag::LostPiou) return true;
  }
  return false;
}

Creature make_lost_piou(const World& w, uint32_t visit) {
  Rng rng(w.seed ^ 0x50494F55ULL, visit);  // "PIOU"
  Creature c;
  c.species = Species::Bird;
  c.genes = roll_genes(Species::Bird, Zone::Meadow, rng);
  c.genes.allele[1] = rules::kLostPiouPlumage;  // emerald: hers
  c.genes.chromatic = c.genes.elemental = false;
  c.level = rules::kLostPiouLevel;
  c.tag = Tag::LostPiou;
  const Vec2 home = lost_piou_home(w);
  c.home = home;
  c.body.pos = {home.x, w.ground.height(home.x, home.z), home.z};
  c.timer = static_cast<uint16_t>(rng.below(3 * rules::kTicksPerSecond));
  return c;
}

bool quest_ready(const World& w, QuestId q) {
  const QuestState& s = w.quests[static_cast<size_t>(q)];
  switch (q) {
    case QuestId::LostPiou:
      for (const Monster& m : w.team) {
        if (m.tag == Tag::LostPiou) return true;
      }
      return false;
    case QuestId::Teleporters: return (s.progress & (kWentToCastle | kCameBack)) == (kWentToCastle | kCameBack);
    default: return false;
  }
}

void quest_command(World& w, const Input& in) {
  switch (in.command) {
    case Command::Teleport:
      if (in.target < kTeleporterCount) teleport(w, static_cast<TeleporterId>(in.target));
      break;
    case Command::QuestAccept: {
      if (in.target >= kQuestCount) break;
      const auto id = static_cast<QuestId>(in.target);
      const uint8_t giver = id == QuestId::LostPiou ? npc::kMirabelle : npc::kGaspard;
      QuestState& q = quest(w, id);
      if (q.stage == QuestStage::Unknown && near_npc(w, giver)) q.stage = QuestStage::Active;
      break;
    }
    case Command::QuestGive:
      give(w, in.target);
      break;
    case Command::QuestComplete: {
      if (in.target != static_cast<uint16_t>(QuestId::Teleporters)) break;
      QuestState& q = quest(w, QuestId::Teleporters);
      if (q.stage == QuestStage::Active && quest_ready(w, QuestId::Teleporters) && near_npc(w, npc::kGaspard)) {
        q.stage = QuestStage::Done;
        reward(w, rules::kTeleportersWizardXp, rules::kTeleportersMonsterXp);
      }
      break;
    }
    default: break;
  }
}

void quests_step(World& w) {
  // Teleporters become known when the player walks near them.
  for (size_t k = 0; k < kTeleporterCount; ++k) {
    Vec3 at;
    if (teleporter_known(w, static_cast<TeleporterId>(k)) || !teleporter_position(w, static_cast<TeleporterId>(k), at)) {
      continue;
    }
    if (flat_distance2(at, w.player.pos) < rules::kTeleporterDiscoverDistance * rules::kTeleporterDiscoverDistance) {
      w.teleporters = static_cast<uint8_t>(w.teleporters | (1u << k));
    }
  }
}

}  // namespace monstres
