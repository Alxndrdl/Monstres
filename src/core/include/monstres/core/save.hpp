#pragma once

#include "monstres/core/world.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace monstres {

// Binary save format, little-endian:
//   "MNST" magic, u32 version, then the world (see save.cpp).
// Bump kSaveVersion whenever the layout changes, and add a reference save of
// the new version to tests/regression/data/saves/.
//
// Version history:
//   1  grid prototype (2D). Not readable anymore: the game was redesigned
//      before any release, so these saves are refused and a new game starts.
//   2  3D world: player + fixed boxes. Not readable anymore (pre-release).
//   3  objects have a kind: trees, pines, rocks, logs, stumps, bushes, crates.
//      Not readable anymore (pre-release).
//   4  the world seed is saved (path and decoration are rebuilt from it).
//      Still readable: loading one adds the companion and the wild monsters.
//   5  the companion and the wild monsters, and the speed of every body.
//   6  the village (objects), villagers, crows and fish; flying height.
//      Versions 4 and 5 still load: the village is built (removing what was
//      in its way) and the creatures appear anew from the seed.
//   7  relief: same layout, but heights are on hills (objects rest on the
//      ground, bodies walk on it).
//   8  the world grew (150 m wide, ruins of the tower) and the magician has a
//      name, a level and experience (after the creatures: u8 name length,
//      UTF-8 name, u16 level, u32 xp). Versions 4 to 7 still load: the world
//      is built anew from their seed, the player keeps its place if it is
//      free (and lands on the ground), the tick goes on.
//   9  variants: after each creature its genes (u8 allele per gene, u8 bits
//      1 chromatic, 2 elemental); after the magician the bestiary (u8 species
//      count, u8 variants per species, then one u8 Discovery each). Version 8
//      still loads: the monsters get their genes from the seed, nothing is
//      discovered but the companion.
//  10  the sex is the first gene of every monster (one more u8 per creature,
//      first) and its two variants come first in the bestiary. Version 9
//      still loads: monsters get a sex from the seed, the discoveries move
//      after the sex's variants.
//  11  battles: after each creature's genes its level (u8); after the
//      bestiary the team (u8 size, then per monster: u8 species, u8 per
//      allele, u8 forms, u16 level, u32 xp, u16 hit points). Battles are
//      not saved. Versions 8 to 10 still load: the monsters get a level from
//      the seed, the team is the companion alone.
//  12  time of day: after the team, u32 ticks since midnight. Older saves
//      start at 8:00.
//  13  the team member out of the crystal (u8 index in the team, 0xff:
//      none), after the time of day; creatures[0] is that monster. Older
//      saves: Pompon is out.
//  14  the world grew again (280 m wide: forest, cave, castle; the ground
//      on a 2 m grid) and only the squares near the player are lived in:
//      after each creature's level its id (u32) and whether it was let go
//      by the player (u8); after the active member the next id (u32), the
//      number of squares per side (u8) and per square whether it is lived
//      in (u8) and how many times it was (u16). The first team member may be
//      any monster (Pompon can be let go). Versions 4 to 13 still load: the
//      world is built anew from their seed, the player starts at the
//      crossroads with its magician, team, bestiary and time of day.
//  15  teleporters and quests: each creature's flags byte also tells whether
//      it was beaten (2), then its villager number (u8) and quest tag (u8);
//      each team member's tag (u8) and whether it was beaten (u8); at the
//      end the known teleporters (u8 bits), the number of quests (u8) and per
//      quest its stage and progress (u8 each). Version 14 still loads: the
//      teleporters are built where they go, the villagers get their names.
//  16  at the end, the battles won against Bastien (u8). Before: Bastien goes
//      to the edge of the village, where he offers battles.
//  17  at the end, the combat then adventure spells (2 + 2 u8 SpellId).
//  18  monsters' builds: the forms byte of creatures and team members holds
//      chromatic (1), elemental (2) and the Form (bits 2-3); the magician's
//      mana (u16) after its experience; each team member's gift bases (5 u8), size
//      (u16 mm), weight (u32 g) and birth day (i32); at the end the Detection
//      spell's ticks left (u16), the number of species (u8) and per species
//      how many were captured, beaten, let go (u16 each); three quests (the
//      giant Champi's), and the tag Giant. The world grew: 29 squares a side;
//      only the valley's objects are saved (the wilds' are drawn again from
//      the seed, generate_wilds). Before: the mana is full, the team members'
//      traits are drawn, the counts are guessed from the team, the giant
//      Champi's quest is unknown, the valley's 7 x 7 squares go in the middle,
//      the objects settle on the valley's new edge, the wilds are added.
//  19  the magician's look (8 u8 after its mana: woman, skin, hair style,
//      hair color, eyes, staff or wand, robe color, headwear); two more
//      species (Zapou, Ploufi: the bestiary and the counts list 9); six
//      quests (fighting, healing and the main quest); Isidore the magician
//      among the villagers (npc 9). Before: the default look, Isidore comes
//      to the village.
//  20  at the end, the battles won (u32). Before: guessed from the
//      species beaten and Bastien's battles.
//  21  same layout; the forest is bigger, with a clearing (its teleporter at
//      the clearing's edge), and fallen logs lie any way (Colliders along
//      them). Before: the forest's objects and teleporter grow anew
//      (regrow_forest).
//  22  Cristi (the bestiary and the counts list 10 species); at the end
//      where the player is (u8 PlaceKind; inside: u32 index of its door among
//      the objects, its position, its facing x and z). Saved inside, the
//      world written is the one outside, as left (interior.hpp). Before:
//      outside.
//  23  the west (west.hpp): four more species (Chipou, Hulou, Laini, Taupi:
//      the bestiary and the counts list 14), twelve quests, seven
//      teleporters, three more places to be inside (the burrow, the hollow
//      tree, the lighthouse), creatures' tags up to Lamb (a trainer's monster
//      has its trainer's villager number); at the end the battles won
//      against Romane (u8), the bag (u8 item count, u32 each) and the
//      monsters left at the farm (u16 count, each as a team member). The
//      farm's grazing monsters are not saved. Before: the west's villagers
//      come, the bag and the farm are empty.
//  24  four adventure spells and four capture spells: at the very end, the
//      adventure spells of slots 2 and 3, then the capture spells (u8 SpellId
//      each). Before: those adventure slots are empty, the capture spells are
//      the first ones.
//  25  the river (the west's stream, now from the Forêt Profonde to the sea):
//      thirteen quests (the water mill's wheel). Before: twelve, the last
//      one not taken.
//  26  same layout; the river's waterfall moved away from the cave's mouth
//      (and its water drawn up to its banks): before, the valley's trees and
//      rocks now in its water or on its cliffs go, the others settle again;
//      the cave's teleporter is built again, down before its mouth.
//  28  Rochebrune (the village at the foot of the mountains): ten teleporters;
//      at the end of the world, the arena's cups won (u8). The world's lines
//      wind more (the path, the road, the terrace's edge, the river), lanes
//      lead to the places: the valley's trees and rocks on one go. Before: no
//      cups won.
inline constexpr uint32_t kSaveVersion = 28;
inline constexpr uint32_t kOldestReadableSaveVersion = 4;

enum class LoadError { None, TooShort, BadMagic, UnsupportedVersion, Corrupt };

std::vector<uint8_t> serialize(const World& world);
std::optional<World> deserialize(std::span<const uint8_t> bytes, LoadError* error = nullptr);

// What the list of saves shows of one, read without building its world
// (everything is checked as deserialize does).
struct SaveSummary {
  std::string name;  // the magician's
  uint16_t level = 1;
  uint64_t seed = 0;
  uint64_t tick = 0;
};
std::optional<SaveSummary> peek_save(std::span<const uint8_t> bytes, LoadError* error = nullptr);

const char* load_error_name(LoadError error);

// FNV-1a 64 of the serialized world: a fingerprint used by replay tests.
uint64_t state_hash(const World& world);

}  // namespace monstres
