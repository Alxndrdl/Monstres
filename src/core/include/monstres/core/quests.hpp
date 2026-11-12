#pragma once

#include "monstres/core/world.hpp"

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

namespace rules {
inline constexpr uint8_t kLostPiouPlumage = 3;  // the bird's plumage allele: emerald
inline constexpr uint8_t kLostPiouLevel = 3;
inline constexpr float kLostPiouDistance = 30.0f;  // it roams this far from the village's center
inline constexpr uint32_t kLostPiouWizardXp = 60, kLostPiouMonsterXp = 40;
inline constexpr uint32_t kTeleportersWizardXp = 40, kTeleportersMonsterXp = 25;
}  // namespace rules

// QuestState::progress of the teleporters' quest.
inline constexpr uint8_t kWentToCastle = 1;  // from the village to the castle, by teleporter
inline constexpr uint8_t kCameBack = 2;      // then back to the village

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

// Whether a quest's steps are all done (it can be reported).
bool quest_ready(const World& world, QuestId quest);

// Steps of a tick, called by step(): the commands (Teleport, QuestAccept,
// QuestGive, QuestComplete), then what the player discovers by walking.
void quest_command(World& world, const Input& input);
void quests_step(World& world);

}  // namespace monstres
