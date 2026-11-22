#include "monstres/core/quests.hpp"

#include "monstres/core/battle.hpp"
#include "monstres/core/north.hpp"
#include "monstres/core/variants.hpp"
#include "monstres/core/west.hpp"

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
  return i > 0 && w.creatures[static_cast<size_t>(i)].act != Activity::Home &&  // not in its house
         flat_distance2(w.creatures[static_cast<size_t>(i)].body.pos, w.player.pos) <
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

Vec2 giant_home(const World& w) {
  // The heart of the forest, beside the teleporter that may stand there.
  const Vec2 c = w.forest.center;
  Vec3 t;
  if (!teleporter_position(w, TeleporterId::Forest, t)) return c;
  float dx = c.x - t.x, dz = c.z - t.z;
  const float d = std::sqrt(dx * dx + dz * dz);
  constexpr float kAway = 12.0f;
  if (d >= kAway) return c;
  dx = d > 0.1f ? dx / d : 0.0f;
  dz = d > 0.1f ? dz / d : 1.0f;
  return {t.x + dx * kAway, t.z + dz * kAway};
}

bool giant_exists(const World& w) {
  for (const Monster& m : w.team) {
    if (m.tag == Tag::Giant) return true;
  }
  for (const Creature& c : w.creatures) {
    if (c.tag == Tag::Giant) return true;
  }
  return false;
}

bool giant_wanted(const World& w) {
  if (w.forest.radius_x <= 0 || giant_exists(w)) return false;
  const QuestState& q = w.quests[static_cast<size_t>(QuestId::GiantChampi)];
  // Never caught: it lives there. Caught once: only when the quest is taken again.
  return !(q.progress & kGiantCaught) || q.stage == QuestStage::Active;
}

std::array<Creature, 5> make_giant_group(const World& w, uint32_t visit) {
  Rng rng(w.seed ^ 0x4749414E54ULL, visit);  // "GIANT"
  const bool again = (w.quests[static_cast<size_t>(QuestId::GiantChampi)].progress & kGiantCaught) != 0;
  std::array<Creature, 5> group;
  const Vec2 home = giant_home(w);
  for (size_t k = 0; k < group.size(); ++k) {
    Creature& c = group[k];
    c.species = Species::Mushroom;
    c.genes = roll_genes(Species::Mushroom, Zone::Forest, rng);
    c.genes.chromatic = c.genes.elemental = false;
    c.genes.form = Form::Normal;
    if (k == 0) {
      c.genes.allele[0] = 0;  // a male: only males become omega
      c.genes.form = Form::Omega;
      c.level = rules::kGiantLevel;
      c.tag = Tag::Giant;
      c.released = again;  // caught before: no experience any more
    } else if (k <= 2) {
      c.genes.form = Form::Alpha;
      c.level = rules::kGiantAlphaLevel;
    } else {
      c.level = rules::kGiantEscortLevel;
    }
    c.home = home;
    c.body.pos = {home.x, w.ground.height(home.x, home.z), home.z};
    c.timer = static_cast<uint16_t>(rng.below(3 * rules::kTicksPerSecond));
  }
  return group;
}

uint8_t quest_giver(QuestId q) {
  switch (q) {
    case QuestId::LostPiou: return npc::kMirabelle;
    case QuestId::Teleporters: return npc::kGaspard;
    case QuestId::Combat: return npc::kBastien;
    case QuestId::Healing: return npc::kOdette;
    case QuestId::Main:
    case QuestId::Delivery: return npc::kMentor;
    case QuestId::Merchant: return npc::kAnselme;
    case QuestId::Transfer:
    case QuestId::Orchard: return npc::kCapucine;
    case QuestId::Shepherd: return npc::kBasile;
    case QuestId::Mill: return npc::kLucien;
    case QuestId::WaterWheel: return npc::kMathurin;
    case QuestId::MineGuardian: return npc::kGaston;
    default: return npc::kLison;
  }
}

uint8_t quest_receiver(QuestId q) { return q == QuestId::Delivery ? npc::kCapucine : quest_giver(q); }

bool quest_available(const World& w, QuestId q) {
  const auto done = [&](QuestId p) { return w.quests[static_cast<size_t>(p)].stage == QuestStage::Done; };
  switch (q) {
    case QuestId::Delivery: return done(QuestId::Main);
    case QuestId::Transfer: return done(QuestId::Delivery);
    case QuestId::Orchard: return done(QuestId::Transfer);
    default: return true;
  }
}

Monster shepherd_gift(const World& w) {
  Rng rng(w.seed ^ 0x4C41494E49ULL, 77);  // "LAINI"
  Monster m;
  m.species = Species::Sheep;
  m.genes = roll_genes(Species::Sheep, Zone::Hills, rng);
  m.genes.chromatic = m.genes.elemental = false;
  m.genes.form = Form::Normal;
  m.level = rules::kShepherdGiftLevel;
  m.hp = monster_fight_stats(m).hp;
  give_traits(m, roll_traits(w.seed, 0xFFFE0000u, m.species, m.genes), world_day(w));
  return m;
}

uint8_t npc_marks(const World& w, uint8_t n) {
  uint8_t marks = 0;
  for (size_t k = 0; k < kQuestCount; ++k) {
    const auto id = static_cast<QuestId>(k);
    const QuestStage stage = w.quests[k].stage;
    if (quest_giver(id) == n && stage == QuestStage::Unknown && quest_available(w, id)) marks |= kMarkNewQuest;
    if (quest_receiver(id) == n && stage == QuestStage::Active) marks |= kMarkQuest;
  }
  if (npc::healer(n)) marks |= kMarkHeal;
  if (n == npc::kBastien || n == npc::kRomane || n == npc::kTristan) marks |= kMarkFight;
  // The abbey's monks: until beaten; the abbot once they are.
  if (npc::monk(n) && !((w.trials >> (n - npc::kHugues)) & 1u) && (n != npc::kAmbroise || (w.trials & 0x07) == 0x07)) marks |= kMarkFight;
  if (n == npc::kBarnabe && !w.tournament.on && w.cups != (1u << rules::kCups) - 1) marks |= kMarkFight;  // a cup to win
  if (npc::wanderer(n)) {  // a trainer passing by: until beaten (an aggressive one: the bolt, not this)
    const int i = npc_index(w, n);
    if (i > 0 && !w.creatures[static_cast<size_t>(i)].beaten && w.creatures[static_cast<size_t>(i)].tag != Tag::Raider) {
      marks |= kMarkFight;
    }
  }
  if (n == npc::kAnselme && w.quests[static_cast<size_t>(QuestId::Merchant)].stage == QuestStage::Done) marks |= kMarkShop;
  if ((n == npc::kCapucine || n == npc::kMargaux) && farm_open(w)) marks |= kMarkFarm;
  return marks;
}

void healed_monster(World& w) {
  QuestState& q = quest(w, QuestId::Healing);
  if (q.stage == QuestStage::Active) q.progress |= kHealedMonster;
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
    case QuestId::GiantChampi:
      for (const Monster& m : w.team) {
        if (m.tag == Tag::Giant) return true;
      }
      return false;
    case QuestId::Combat: return w.rival_wins > 0;
    case QuestId::Healing: return (s.progress & kHealedMonster) != 0;
    case QuestId::Main:
      for (const QuestId part : kMainPart1) {
        if (w.quests[static_cast<size_t>(part)].stage != QuestStage::Done) return false;
      }
      return true;
    case QuestId::Merchant: return (s.progress & kGoodsFound) == kGoodsFound;
    case QuestId::Delivery: return w.bag[static_cast<size_t>(Item::StrangeStone)] > 0;
    case QuestId::Transfer: return (s.progress & (kSentToFarm | kTookFromFarm)) == (kSentToFarm | kTookFromFarm);
    case QuestId::Orchard: return s.progress >= rules::kRaiders;
    case QuestId::Shepherd: return (s.progress & kLambsHome) == kLambsHome;
    case QuestId::Mill: return (s.progress & kMillTurning) != 0;
    case QuestId::WaterWheel: return s.progress >= rules::kWheelRaiders;
    case QuestId::MineGuardian: return (s.progress & kGuardianBeaten) != 0;
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
      QuestState& q = quest(w, id);
      if (q.stage == QuestStage::Unknown && quest_available(w, id) && near_npc(w, quest_giver(id))) {
        q.stage = QuestStage::Active;
        if (id == QuestId::Delivery) w.bag[static_cast<size_t>(Item::StrangeStone)] = 1;  // Isidore's parcel
      }
      break;
    }
    case Command::QuestGive:
      give(w, in.target);
      break;
    case Command::QuestComplete: {
      if (in.target == static_cast<uint16_t>(QuestId::GiantChampi)) {
        QuestState& q = quest(w, QuestId::GiantChampi);
        if (q.stage == QuestStage::Active && quest_ready(w, QuestId::GiantChampi) && near_npc(w, npc::kLison) &&
            w.battle.phase == BattlePhase::None) {
          q.stage = QuestStage::Done;
          if (!(q.progress & kGiantRewarded)) {  // only the first time
            q.progress |= kGiantRewarded;
            reward(w, rules::kGiantWizardXp, rules::kGiantMonsterXp);
          }
        }
        break;
      }
      // The others: told to their giver, when ready.
      if (in.target >= kQuestCount || in.target == static_cast<uint16_t>(QuestId::LostPiou)) break;
      const auto id = static_cast<QuestId>(in.target);
      QuestState& q = quest(w, id);
      if (q.stage != QuestStage::Active || !quest_ready(w, id) || !near_npc(w, quest_receiver(id)) ||
          w.battle.phase != BattlePhase::None) {
        break;
      }
      // The shepherd's gift needs room: in the team, or at the farm.
      if (id == QuestId::Shepherd && w.team.size() >= rules::kTeamSize && !farm_open(w)) break;
      q.stage = QuestStage::Done;
      const auto coins = [&](uint32_t n) { w.bag[static_cast<size_t>(Item::Coin)] += n; };
      const auto potions = [&](uint32_t n) { w.bag[static_cast<size_t>(Item::ManaPotion)] += n; };
      switch (id) {
        case QuestId::Teleporters: reward(w, rules::kTeleportersWizardXp, rules::kTeleportersMonsterXp); break;
        case QuestId::Combat: reward(w, rules::kCombatWizardXp, rules::kCombatMonsterXp); break;
        case QuestId::Healing: reward(w, rules::kHealingWizardXp, rules::kHealingMonsterXp); break;
        case QuestId::Main:
          q.progress |= kMainPart1Done;
          reward(w, rules::kMainPart1WizardXp, rules::kMainPart1MonsterXp);
          break;
        case QuestId::Merchant:  // his goods back, the cart up again: he sells
          q.progress |= kCartUp;
          w.bag[static_cast<size_t>(Item::Goods)] = 0;
          coins(rules::kMerchantCoins);
          potions(rules::kMerchantPotions);
          reward(w, rules::kMerchantWizardXp, rules::kMerchantMonsterXp);
          break;
        case QuestId::Delivery:  // the transfer stone stays at the farm
          w.bag[static_cast<size_t>(Item::StrangeStone)] = 0;
          reward(w, rules::kDeliveryWizardXp, rules::kDeliveryMonsterXp);
          break;
        case QuestId::Transfer:
          coins(rules::kTransferCoins);
          reward(w, rules::kTransferWizardXp, rules::kTransferMonsterXp);
          break;
        case QuestId::Orchard:  // the main quest's second part
          w.quests[static_cast<size_t>(QuestId::Main)].progress |= kMainPart2Done;
          coins(rules::kOrchardCoins);
          potions(rules::kOrchardPotions);
          reward(w, rules::kOrchardWizardXp, rules::kOrchardMonsterXp);
          break;
        case QuestId::Shepherd: {  // a Laini of his flock
          const Monster gift = shepherd_gift(w);
          Creature probe;  // seen and caught in the bestiary
          probe.species = gift.species;
          probe.genes = gift.genes;
          probe.body.pos = w.player.pos;
          const size_t i = add_creature(w, probe);
          capture(w, i);
          remove_creature(w, i);
          ++w.captured_count[static_cast<size_t>(gift.species)];
          if (w.team.size() < rules::kTeamSize) w.team.push_back(gift);
          else send_to_farm(w, gift);
          reward(w, rules::kShepherdWizardXp, rules::kShepherdMonsterXp);
          break;
        }
        case QuestId::Mill:
          coins(rules::kMillCoins);
          potions(rules::kMillPotions);
          reward(w, rules::kMillWizardXp, rules::kMillMonsterXp);
          break;
        case QuestId::WaterWheel:
          coins(rules::kWheelCoins);
          potions(rules::kWheelPotions);
          reward(w, rules::kWheelWizardXp, rules::kWheelMonsterXp);
          break;
        case QuestId::MineGuardian:
          coins(rules::kGuardianCoins);
          potions(rules::kGuardianPotions);
          reward(w, rules::kGuardianWizardXp, rules::kGuardianMonsterXp);
          break;
        default: break;
      }
      break;
    }
    default: break;
  }
}

void quests_step(World& w) {
  // The giant Champi caught once, let go, and gone: the quest can be taken again.
  QuestState& giant = quest(w, QuestId::GiantChampi);
  if (giant.stage == QuestStage::Done && (giant.progress & kGiantCaught) && !giant_exists(w)) {
    giant.stage = QuestStage::Unknown;
  }
  // Teleporters become known when the player walks near them.
  for (size_t k = 0; k < kTeleporterCount; ++k) {
    Vec3 at;
    if (teleporter_known(w, static_cast<TeleporterId>(k)) || !teleporter_position(w, static_cast<TeleporterId>(k), at)) {
      continue;
    }
    if (flat_distance2(at, w.player.pos) < rules::kTeleporterDiscoverDistance * rules::kTeleporterDiscoverDistance) {
      w.teleporters = static_cast<uint16_t>(w.teleporters | (1u << k));
    }
  }
}

}  // namespace monstres
