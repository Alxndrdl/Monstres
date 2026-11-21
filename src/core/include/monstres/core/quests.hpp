#pragma once

#include "monstres/core/world.hpp"

#include <array>

namespace monstres {

// Quests, teleporters and the villagers who give quests. What is said is
// in web/lore.js and web/dialogue.js; here, only what changes the world.
//
// The lost Piou (QuestId::LostPiou): Mirabelle lost her Piou, whose plumage
// is a very rare emerald. It roams around the village (Tag::LostPiou), and
// comes back each time the player comes near if it was not caught yet.
// Caught and handed back to her (Command::QuestGive), it stays by her side.
//
// The teleporters (QuestId::Teleporters): Gaspard, by the village's
// teleporter, wonders what it is for; he saw another one by the castle.
// Find it, then go from the village to the castle by teleporter, and back,
// and tell him (Command::QuestComplete).
//
// The giant Champi (QuestId::GiantChampi): Lison saw a gigantic Champi in the
// heart of the forest. There, an omega Champi (Tag::Giant) lives with two
// alpha and two normal ones; they come back each time the player comes near,
// until the omega is captured. Captured, it is shown to Lison
// (Command::QuestComplete). Let go, it lives in the wild; once it is gone
// (the player went far away), the quest can be taken again to catch it
// anew: then neither the quest nor its capture give experience.
//
// Fighting (QuestId::Combat): Bastien teaches battles: win one against him
// (World::rival_wins), and tell him.
//
// Healing (QuestId::Healing): Odette teaches the Heal spell: cast it on a
// hurt monster of the team, and tell her.
//
// The main quest (QuestId::Main): Isidore, the old magician of the square,
// welcomes the player. Its first part: help the village (the lost Piou, the
// teleporters, fighting, healing), then come back to him.

namespace rules {
inline constexpr uint8_t kLostPiouPlumage = 3;  // the bird's plumage allele: emerald
inline constexpr uint8_t kLostPiouLevel = 3;
inline constexpr float kLostPiouDistance = 30.0f;  // it roams this far from the village's center
inline constexpr uint32_t kLostPiouWizardXp = 60, kLostPiouMonsterXp = 40;
inline constexpr uint32_t kTeleportersWizardXp = 40, kTeleportersMonsterXp = 25;
inline constexpr uint32_t kGiantWizardXp = 80, kGiantMonsterXp = 50;
inline constexpr uint8_t kGiantLevel = 8, kGiantAlphaLevel = 5, kGiantEscortLevel = 4;
inline constexpr uint32_t kCombatWizardXp = 30, kCombatMonsterXp = 20;
inline constexpr uint32_t kHealingWizardXp = 25, kHealingMonsterXp = 15;
inline constexpr uint32_t kMainPart1WizardXp = 100, kMainPart1MonsterXp = 60;
// The west's quests (west.hpp): experience, and coins (and potions) in the bag.
inline constexpr uint32_t kMerchantWizardXp = 40, kMerchantMonsterXp = 25, kMerchantCoins = 40, kMerchantPotions = 2;
inline constexpr uint32_t kDeliveryWizardXp = 60, kDeliveryMonsterXp = 30;
inline constexpr uint32_t kTransferWizardXp = 50, kTransferMonsterXp = 30, kTransferCoins = 20;
inline constexpr uint32_t kOrchardWizardXp = 150, kOrchardMonsterXp = 90, kOrchardCoins = 80, kOrchardPotions = 3;
inline constexpr uint32_t kShepherdWizardXp = 60, kShepherdMonsterXp = 40;
inline constexpr uint8_t kShepherdGiftLevel = 5;
inline constexpr uint32_t kMillWizardXp = 50, kMillMonsterXp = 30, kMillCoins = 50, kMillPotions = 1;
inline constexpr uint32_t kWheelWizardXp = 80, kWheelMonsterXp = 50, kWheelCoins = 60, kWheelPotions = 2;
// The mine's guardian (Gaston), in the north: stronger.
inline constexpr uint32_t kGuardianWizardXp = 150, kGuardianMonsterXp = 90, kGuardianCoins = 120, kGuardianPotions = 3;
}  // namespace rules

// The quests of the main quest's first part.
inline constexpr QuestId kMainPart1[] = {QuestId::LostPiou, QuestId::Teleporters, QuestId::Combat, QuestId::Healing};

// QuestState::progress of the teleporters' quest.
inline constexpr uint8_t kWentToCastle = 1;  // from the village to the castle, by teleporter
inline constexpr uint8_t kCameBack = 2;      // then back to the village
// And of the giant Champi's.
inline constexpr uint8_t kGiantCaught = 1;    // captured once: it never comes back by itself
inline constexpr uint8_t kGiantRewarded = 2;  // the quest's experience was given
// Odette's: the Heal spell was cast on a hurt monster.
inline constexpr uint8_t kHealedMonster = 1;
// The main quest's: the parts done.
inline constexpr uint8_t kMainPart1Done = 1;
inline constexpr uint8_t kMainPart2Done = 2;  // the orchard saved (the main quest's second part, in the west)

// While Odette's quest is on: a Heal spell was cast on a hurt monster (battle.cpp).
void healed_monster(World& world);

// Index of the villager `npc` in World::creatures, or -1.
int npc_index(const World& world, uint8_t npc);
// Whether the player is close enough to talk to it.
bool near_npc(const World& world, uint8_t npc);

// The known teleporter the player stands at, or -1.
int teleporter_here(const World& world);
bool teleporter_known(const World& world, TeleporterId id);
// Whether the player can go to `to` now (from another known teleporter, not in a battle).
bool can_teleport(const World& world, TeleporterId to);

// Where the lost Piou roams; whether it exists (in the world or the team);
// and a new one (the world gives it its id).
Vec2 lost_piou_home(const World& world);
bool lost_piou_exists(const World& world);
Creature make_lost_piou(const World& world, uint32_t visit);

// The giant Champi: where it lives (the heart of the forest), whether it
// exists (in the world or the team), whether it should be there now, and
// the group (the omega first, then two alpha and two normal Champi).
Vec2 giant_home(const World& world);
bool giant_exists(const World& world);
bool giant_wanted(const World& world);
std::array<Creature, 5> make_giant_group(const World& world, uint32_t visit);

// What a villager has for the player, shown over its head and on the map:
// a quest to take (?), a quest going on (!), healing (+), battles (x).
// The west adds: a shop (the merchant), the farm's monsters (Capucine).
enum NpcMark : uint8_t { kMarkNewQuest = 1, kMarkQuest = 2, kMarkHeal = 4, kMarkFight = 8, kMarkShop = 16, kMarkFarm = 32 };
uint8_t npc_marks(const World& world, uint8_t npc);

// The villager who gives a quest, and the one it is told to (the strange
// stone: Capucine, at the farm).
uint8_t quest_giver(QuestId quest);
uint8_t quest_receiver(QuestId quest);
// Whether a quest can be taken (the main quest's parts, one after the other).
bool quest_available(const World& world, QuestId quest);
// The monster the shepherd gives (a Laini of his flock), the same each time.
Monster shepherd_gift(const World& world);
// Whether a quest's steps are all done (it can be reported).
bool quest_ready(const World& world, QuestId quest);

// Steps of a tick, called by step(): the commands (Teleport, QuestAccept,
// QuestGive, QuestComplete), then what the player discovers by walking.
void quest_command(World& world, const Input& input);
void quests_step(World& world);

}  // namespace monstres
