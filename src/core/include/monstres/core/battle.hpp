#pragma once

#include "monstres/core/world.hpp"

#include <array>
#include <span>

namespace monstres {

// Battles: the companion fights wild monsters, controlled by the player
// (the player's magician waits). Wild monsters of the same kind close to the
// one engaged join in (at most kMaxFoes). The companion aims at one of them
// (Command::Target), or at no one: it then moves freely, relative to the
// camera, and its attacks go straight ahead. The victory's experience is
// shared by the team members who fought, by the hit points each dealt; a
// beaten monster lies knocked out for a while. Training (Command::Train)
// works the same way against the magician, who shields itself: no one is
// hurt. Ended by a victory, a defeat, a capture, or by fleeing (the
// companion runs back to the player first).
//
// Trainers (Bastien, in the village) offer battles (Command::Challenge):
// both sides first take their places, facing each other outside the
// village, while the player reads the rules (BattlePhase::Prepare), then the
// fight starts (Command::Ready). One monster of each side fights at a time;
// a beaten one is replaced by the next of its team. The side with no
// monster left standing loses; fleeing is forfeiting. Only the first
// victory against a trainer gives experience.
//
// During a fight the joystick moves the companion relative to the line
// between it and its target (forward: towards it); the jump becomes a
// dodge (a flying companion climbs while it is held), and the companion has
// three attacks: a quick one, and two special ones with a cooldown. Moving
// freely, it turns where the camera looks to attack (the middle of the
// screen). A burst gathers for a second (Windup) before it hits, the
// companion's as the foes': time to parry it, or to get away; dodging
// cancels the companion's. Parrying blocks the next hit and stuns a foe
// whose bite was parried. Foes have the same attacks and delays, but show
// theirs before (Windup); they circle, dodge, and fly if they can. An
// attack does more or less to a monster by their types (type_effect).

namespace rules {
inline constexpr uint16_t kCompanionStartLevel = 3;
inline constexpr float kEngageDistance = 15.0f;     // from the player to the monster
inline constexpr float kBattleLeash = 25.0f;        // companion this far from the enemy: the battle ends
inline constexpr uint16_t kCaptureHpPercent = 25;   // weakened: at most this much of its hit points left
// Capturing (Battle::seal), per mille: the bar starts halfway; each rune
// touched right moves it towards the capture, a mistake towards the escape.
inline constexpr uint16_t kSealFull = 1000;
inline constexpr uint16_t kSealStart = 500;
inline constexpr uint8_t kSealGain = 50;          // +5 % a rune
inline constexpr uint8_t kSealGainVeryRare = 40;  // +4 % for a very rare monster
inline constexpr uint16_t kSealLoss = 400;        // -40 % a mistake
// Runes around the circle, and in a series: by level, then more for rare ones.
inline constexpr uint8_t kSealLowLevel = 3, kSealMidLevel = 6;
inline constexpr uint8_t kSealRunesLow = 4, kSealRunesMid = 5, kSealRunesHigh = 6;  // a series: one less
inline constexpr uint8_t kSealRareExtra = 1, kSealVeryRareExtra = 2;  // more runes, and more in a series
inline constexpr uint16_t kSealGlideTicks = 60;   // the camera glides to the monster first
inline constexpr uint16_t kSealPauseTicks = 30;   // before the next series
inline constexpr uint16_t kSealRuneTicks = 39;    // one rune after the other...
inline constexpr uint16_t kSealLitTicks = 30;     // ...lit this long
inline constexpr uint16_t kSealBarTicks = 20;     // the bar moves
inline constexpr uint16_t kSealResultTicks = 45;  // after a series, or a mistake
inline constexpr uint16_t kSealEndTicks = 70;     // caught or escaped, then the battle goes on
inline constexpr uint16_t kSealSteadyPower = 500;  // the Steady spell: back to halfway
inline constexpr float kFleeArrival = 2.0f;         // the companion is back next to the player
inline constexpr uint16_t kFleeMaxTicks = 8 * rules::kTicksPerSecond;
inline constexpr uint16_t kDodgeTicks = 12;         // 0.2 s
inline constexpr float kDodgeSpeed = 15.6f;         // m/s
inline constexpr float kBattlePace = 0.8f;          // monsters run this much slower in a fight (easier to follow)
inline constexpr uint16_t kBurstCharge = rules::kTicksPerSecond;  // a burst gathers this long before it hits
inline constexpr uint16_t kDodgeCooldown = 36;
inline constexpr uint16_t kParryTicks = 30;         // 0.5 s guard
inline constexpr uint16_t kParryCooldown = 90;      // 2 s from the guard to the next one
inline constexpr uint16_t kStunTicks = 70;          // an enemy whose attack was parried
inline constexpr float kProjectileSpeed = 13.0f;
inline constexpr uint16_t kImpactTicks = 30;        // where a projectile stopped is remembered this long (drawing)
inline constexpr float kCompanionMinRunSpeed = 6.0f;  // slow species keep up with the player
inline constexpr size_t kMaxFoes = 6;               // a wild monster and those of its kind around
inline constexpr float kGroupRadius = 7.0f;         // they join from this far (from the one engaged)
inline constexpr float kFlyLiftMax = 5.0f;          // a flying companion climbs this high above its usual height
inline constexpr float kRivalSpacing = 10.0f;       // a trainer's battle: the monsters start this far apart
inline constexpr uint16_t kPrepareMaxTicks = 6 * rules::kTicksPerSecond;  // then everyone is at its place
inline constexpr uint16_t kRivalNextTicks = 90;     // a trainer sends its next monster after this
inline constexpr uint32_t kRivalXpPerLevel = 8;     // the first victory against a trainer
// Rochebrune's arena (north.hpp): its tournaments, three cups, each three
// contenders one after the other (two monsters, then three, then three);
// the team is healed before each round. Each cup needs the one before won.
inline constexpr uint8_t kCups = 3;
inline constexpr uint8_t kTournamentRounds = 3;
inline constexpr std::array<uint8_t, kCups> kCupLevels = {10, 17, 25};    // the first contender's monsters (then 2 more a round)
inline constexpr std::array<uint32_t, kCups> kCupCoins = {80, 160, 320};  // the cup's prize
inline constexpr uint16_t kRoundPause = 4 * kTicksPerSecond;             // between two rounds
inline constexpr uint16_t kAwayTicks = 5 * rules::kTicksPerSecond;  // out of reach this long: the battle ends
inline constexpr float kKnockedOutRecall = 15.0f;   // a knocked out companion goes back into the crystal from this far
inline constexpr uint16_t kHealPoints = 10;
inline constexpr uint16_t kHealCooldown = 5 * rules::kTicksPerSecond;
inline constexpr uint16_t kShieldTicks = rules::kTicksPerSecond;
inline constexpr uint16_t kShieldCooldown = 5 * rules::kTicksPerSecond;
inline constexpr uint16_t kMercyCooldown = rules::kTicksPerSecond;
inline constexpr uint16_t kDetectTicks = 60 * rules::kTicksPerSecond;
inline constexpr uint16_t kWayOutTicks = 60 * rules::kTicksPerSecond;
inline constexpr uint16_t kHomeCooldown = 5 * 60 * rules::kTicksPerSecond;  // the Home spell
inline constexpr uint16_t kSwiftTicks = 20 * rules::kTicksPerSecond;      // the Swift spell
inline constexpr float kSwiftSpeed = 10.0f;                               // times the walk
inline constexpr uint16_t kWizardManaPerLevel = 5;  // see wizard_stats
}  // namespace rules

// Spells: the magician casts them, paying their mana (it does not come back
// by itself: the healer fills it up).
// Heal (combat and adventure): +10 hit points; a knocked out monster lying
// there wakes up with them (in a battle, a knocked out one is in the crystal).
// Shield (combat): for a second, no hit lands on the monster.
// Mercy (combat): until the battle ends, the next blow of the monster that
// would beat a foe leaves it 1 hit point instead (to capture it).
// Detection (adventure): for a minute, the staff's crystal looks for very
// rare monsters within 50 m (see variants.hpp, detect_rare), and points at
// the closest one.
// Way out (adventure, free): inside a cave or a building, for a minute, the
// staff's crystal points the way back to the door one came in by, along the
// way (round the walls, through the tunnels), not straight at it.
// Home (adventure, free, once every 5 minutes): back to the farm, out of a
// cave or a building too, the companion along (west.hpp, go_home).
// Swift (adventure, free): for 20 seconds, the magician walks ten times
// faster (to roam the continent); not in a battle.
// The capture spells (while sealing, see seal_step; those marked "a rune"
// cost their mana for each rune left to touch in the series):
// Replay: the series lights up again, to be touched from its first rune (the
// ones already right do not move the bar twice); a rune.
// Guard: a mistake in this series takes nothing from the bar; a rune.
// Boost: each rune of this series moves the bar half again as much; a rune.
// New order: another series at once, nothing lost.
// Steady: the bar back to halfway (when below it).
// Hint: the next rune to touch is shown.
// Two combat spells, four adventure spells and four capture spells are
// chosen out of battles (Command::EquipSpell); training uses the combat ones.
struct SpellInfo {
  const char* id;  // names and texts in web/lore.js (spells)
  bool combat, adventure, capture;
  bool per_rune;   // capture: its mana for each rune left in the series
  uint16_t cooldown;
  uint16_t mana;   // what casting it costs
};
const SpellInfo& spell_info(SpellId spell);
// The spells that can be cast now (capturing: the capture ones; a battle or
// training: the combat ones; else the adventure ones).
std::span<const SpellId> current_spells(const World& world);
// What casting `spell` costs now (a capture spell: by the runes left).
uint16_t spell_cost(const World& world, SpellId spell);
// Why spell slot `slot` cannot be cast now (nullptr: it can).
const char* cannot_cast(const World& world, size_t slot);

// Can the healer `npc` heal the team now (close to her, no battle)?
bool can_heal_team(const World& world, uint8_t npc);

enum class MoveShape : uint8_t {
  Melee,       // hits the enemy in reach, in front
  Projectile,  // thrown straight ahead (towards the enemy)
  Burst,       // around itself
};

// One attack. Names and texts in web/lore.js (moves), by identifier.
struct Move {
  const char* id;
  MoveShape shape;
  bool elemental;     // of the monster's element (else Normal)
  uint8_t power;
  uint16_t cooldown;  // ticks, from the attack (a foe's: from the start of its windup)
  float range;        // Melee: reach beyond both bodies; Projectile: distance; Burst: radius
};
using Moves = std::array<Move, 3>;  // quick attack, then two special ones
const Moves& species_moves(Species species);

// Characteristics of a monster at a level: its build makes it weaker or
// stronger (percent), some variants add a few points (variants.hpp, stat_mod).
struct FightStats {
  uint16_t hp, attack, defense, speed, magic;
};
FightStats monster_fight_stats(Species species, uint16_t level, const Genes& genes = {});
inline FightStats monster_fight_stats(const Monster& m) { return monster_fight_stats(m.species, m.level, m.genes); }
// A build's fight stats, in percent of a normal one's.

uint32_t monster_xp_to_next(uint16_t level);

// How long attack `slot` shows before it comes: a foe's windup; the
// companion's burst gathers as long (its other attacks come at once).
uint16_t windup_ticks(uint8_t slot);

// Hit points dealt by `move` of an attacker with these characteristics.
uint16_t move_damage(const Move& move, const FightStats& attacker, const FightStats& defender);

// The types against each other: how an attack of type `attack` does on a
// monster of type `defense`: +1 efficient, -1 inefficient, 0 nothing
// particular. Also told in the guide (web/guide.js reads it from the game).
int type_effect(Element attack, Element defense);
// The types' effect on a monster with one or two types (`second`:
// Element::Count when it has one): the sum of both (-2..2).
int type_effect(Element attack, Element first, Element second);
// Damage of an attack of that type on such a monster: x1.5 for each of its
// types weak to it, /1.5 for each one resisting it (both count: x2.25, /2.25;
// one of each: as is). Rounded, at least 1.
uint16_t type_damage(uint16_t damage, int effect);

// Experience for a team member (levels up, hit points grow) or the magician.
void grant_monster_xp(Monster& monster, uint32_t xp);
void grant_wizard_xp(Wizard& wizard, uint32_t xp);

// The companion as a new team member (new game, old saves).
Monster starting_companion(const Genes& genes);

// How a species fights (wild and trainers' monsters): how far it keeps while
// its bite is not ready, whether it throws from afar, how often it dodges.
struct Temper {
  float keep_away;      // meters from the companion while its bite recharges
  float close_in;       // with its bite ready: this part of its reach
  float throw_from;     // throws its projectile from this far
  uint32_t dodge_odds;  // dodges one projectile in ...
  float pace;           // speed, compared to the others
  float lift;           // flying: its usual height
};
const Temper& species_temper(Species species);

// Can a battle against creatures[index] start now?
enum class EngageCheck : uint8_t {
  Ok,
  NotMonster,   // villagers, the companion itself
  TooFar,
  InBattle,
  NoCompanion,  // no monster out of the crystal
  KnockedOut,   // already beaten, it lies there
  Owned,        // someone's (Mirabelle's Piou, back with her; a trainer's)
  CompanionDown,  // the monster out is knocked out
};
EngageCheck can_engage(const World& world, size_t index);
const char* engage_check_name(EngageCheck check);

bool can_capture(const World& world);

// Capturing (Command::Capture starts it, Battle::seal): the camera glides to
// the foe, a circle of runes around it (4 up to level 3, 5 up to level 6, else
// 6; one more for a rare monster, two for a very rare one); a series of them
// (one less) light up one after the other (never the same twice in a row),
// then the player touches them in that order (Command::Rune). The bar starts
// at kSealStart: each rune right moves it kSealGain towards the capture
// (kSealFull; kSealGainVeryRare for a very rare monster), a mistake kSealLoss
// towards the escape (0) and a new series starts; a whole series right, too.
// The capture spells help (Command::Cast). Caught, it goes into the crystal;
// escaped, it runs away (the battle goes on with the others, or ends).
// Meanwhile the world holds still: step() only calls seal_step.
bool sealing(const World& world);
void seal_step(World& world, const Input& in);
// The rune lit now (Show), or -1; and how far into its light (0..1).
int seal_lit(const World& world, float* glow = nullptr);
// The bar, 0..100 %, as drawn now (it moves after each rune).
float seal_bar(const World& world);

// Can the player enter the arena's cup now (close to Barnabé, no battle, no
// tournament going on, a monster standing, the cup before won)? Entered
// (Command::Tournament), the team is healed and the first contender comes
// into the arena; beaten, the next one comes after a while; the third beaten,
// the cup is won (World::cups): its coins, three potions, and only the
// first time each contender's experience. Lost or fled: the tournament ends.
bool can_enter_tournament(const World& world, uint8_t cup);
// Each tick (step): the next round starts, or the tournament ends.
void tournament_step(World& world);

// Can the player challenge the trainer `npc` now (close to it, a monster out, no battle)?
bool can_challenge(const World& world, uint8_t npc);

// A trainer's team (Bastien's, Romane's, or one passing by): the first one
// stands beside it. `count`: 0 if `npc` has none.
const std::array<RivalMonster, kRivalTeamSize>& rival_team(const World& world, uint8_t npc, uint8_t& count);

// A trainer passing by (npc::kWanderer...) offers a battle; an aggressive one
// (its villager tagged Raider: the aggressive monsters' bolt over its head)
// starts it as soon as the player comes close (rules::kAggroDistance), and
// losing it, or forfeiting, sends the magician back to the farm (west.hpp).
// Beaten, it offers no more.
void force_challenge(World& world, uint8_t npc);

// Where a trainer's battle takes place: its middle, and the line between the
// two sides (unit vector), outside the village. False if this world has no village.
bool rival_arena(const World& world, uint8_t trainer, Vec2& center, Vec2& axis);

// An aggressive monster (creatures[index], west.hpp) attacks: a battle starts,
// the first team member standing coming out if needed (none: the magician is
// sent back to the farm). Lost or fled, it sends the magician to the farm.
void force_battle(World& world, size_t index);

// The foe aimed at, or nullptr (free move, or no battle).
const Foe* battle_target(const World& world);
// foes[k] for creatures[index], or -1.
int foe_slot(const World& world, size_t index);

// Whether a creature belongs to someone (Mirabelle's Piou once back, a trainer's monster).
bool owned(const World& world, const Creature& creature);

// Can the companion start training against the magician now?
bool can_train(const World& world);

// Direction the companion faces its target (unit vector on the ground):
// the battle camera looks along it, forward on the joystick goes along it.
// False out of a fight, or moving freely (no target): the camera decides.
bool battle_forward(const World& world, float& fx, float& fz);

// How a body should move during this tick (world.cpp moves it).
struct Steer {
  float dir_x = 0, dir_z = 0;
  float speed = 0;
  bool jump = false;
  bool fly = false;  // a crow: flies freely, `lift` meters above the ground
  float lift = 0;
};

// Fish do not move during a battle (in the lake): they bite only what comes
// close, and throw.
bool holds_its_place(Species species);

// Summon team[slot] out of the staff's crystal, where the companion was (it
// goes back in), or next to the player. If it is the one out: recalled
// (not during a fight). During a fight, swapping is allowed.
void summon(World& world, size_t slot);

// Before a trainer's battle: who walks to its place.
enum class Stand : uint8_t { Magician, Ally, Foe, Trainer };

// Steps of a tick, called by step() in this order:
void battle_command(World& world, const Input& input);  // Engage, Flee, Capture, Summon, Release, Train, Target, Challenge, Ready
Steer battle_ally(World& world, const Input& input, float dir_x, float dir_z);  // Fight phase
Steer battle_foe(World& world, size_t slot);             // Fight phase, foes[slot]
Steer battle_prepare(World& world, Stand who);           // Prepare phase (and the trainer, waiting)
void battle_resolve(World& world);  // projectiles, timers, victory or defeat (after moving)
void spells_step(World& world);    // spells' cooldowns (no hit points come back by themselves)

}  // namespace monstres
