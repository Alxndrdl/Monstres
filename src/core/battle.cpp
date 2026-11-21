#include "monstres/core/battle.hpp"
#include "monstres/core/interior.hpp"
#include "monstres/core/north.hpp"

#include "monstres/core/quests.hpp"
#include "monstres/core/variants.hpp"
#include "monstres/core/west.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>

namespace monstres {

namespace {

using enum MoveShape;

constexpr uint16_t kQuick = 20, kSpecial = 3 * rules::kTicksPerSecond, kBig = 7 * rules::kTicksPerSecond;

// Per species, in the order of Species. Villagers do not fight.
constexpr Moves kMoves[] = {
    {{{"bite", Melee, false, 6, kQuick, 0.9f}, {"ember", Projectile, true, 14, kSpecial, 12.0f},
      {"blaze", Burst, true, 20, kBig, 3.0f}}},  // Companion
    {{{"bounce", Melee, false, 6, kQuick, 0.8f}, {"splash", Projectile, true, 13, kSpecial, 11.0f},
      {"flood", Burst, true, 18, kBig, 2.8f}}},  // Slime
    {{{"headbutt", Melee, false, 6, kQuick, 0.8f}, {"spores", Projectile, true, 12, kSpecial, 10.0f},
      {"spore_cloud", Burst, true, 19, kBig, 3.0f}}},  // Mushroom
    {{{"peck", Melee, false, 7, kQuick, 0.9f}, {"feather", Projectile, true, 13, kSpecial, 12.0f},
      {"gust", Burst, true, 17, kBig, 3.2f}}},  // Bird
    {{{"none", Melee, false, 0, kQuick, 0}, {"none", Melee, false, 0, kSpecial, 0},
      {"none", Melee, false, 0, kBig, 0}}},  // Villager
    {{{"peck", Melee, false, 7, kQuick, 0.9f}, {"shade", Projectile, true, 14, kSpecial, 12.0f},
      {"flock", Burst, true, 18, kBig, 3.0f}}},  // Crow
    {{{"tail_slap", Melee, false, 6, kQuick, 0.8f}, {"bubble", Projectile, true, 12, kSpecial, 10.0f},
      {"wave", Burst, true, 18, kBig, 2.8f}}},  // Fish
    {{{"zap", Melee, false, 6, kQuick, 0.85f}, {"thunderbolt", Projectile, true, 13, kSpecial, 12.0f},
      {"discharge", Burst, true, 19, kBig, 3.0f}}},  // Spark
    {{{"nip", Melee, false, 6, kQuick, 0.8f}, {"water_jet", Projectile, true, 13, kSpecial, 11.0f},
      {"geyser", Burst, true, 18, kBig, 2.8f}}},  // Axolotl
    {{{"tumble", Melee, false, 7, kQuick, 0.8f}, {"shard", Projectile, true, 13, kSpecial, 12.0f},
      {"prism", Burst, true, 19, kBig, 3.0f}}},  // Cristi
    {{{"swipe", Melee, false, 7, kQuick, 0.85f}, {"pilfer", Projectile, true, 13, kSpecial, 11.0f},
      {"smoke_bomb", Burst, true, 18, kBig, 3.0f}}},  // Raccoon
    {{{"talon", Melee, false, 7, kQuick, 0.9f}, {"squall", Projectile, true, 14, kSpecial, 13.0f},
      {"hoot", Burst, true, 17, kBig, 3.4f}}},  // Owl
    {{{"ram", Melee, false, 7, kQuick, 0.85f}, {"fluff", Projectile, true, 11, kSpecial, 10.0f},
      {"wool_wave", Burst, true, 19, kBig, 3.0f}}},  // Sheep
    {{{"scratch", Melee, false, 7, kQuick, 0.8f}, {"clod", Projectile, true, 13, kSpecial, 11.0f},
      {"tremor", Burst, true, 20, kBig, 3.2f}}},  // Mole
    {{{"slam", Melee, false, 7, kQuick, 0.85f}, {"rock_throw", Projectile, true, 14, kSpecial, 11.0f},
      {"landslide", Burst, true, 21, kBig, 3.2f}}},  // Golem
    {{{"butt", Melee, false, 7, kQuick, 0.9f}, {"pebble_kick", Projectile, true, 13, kSpecial, 12.0f},
      {"avalanche", Burst, true, 20, kBig, 3.2f}}},  // Goat
    {{{"frost_bite", Melee, false, 7, kQuick, 0.85f}, {"icicle", Projectile, true, 13, kSpecial, 12.0f},
      {"blizzard", Burst, true, 19, kBig, 3.2f}}},  // Ermine
};
static_assert(std::size(kMoves) == kSpeciesCount);

// Foes show each attack before it comes (Windup); their cooldowns are the
// companion's, counted from the start of the windup, so both attack as often.
constexpr uint16_t kFoeWindup[] = {12, 24, rules::kBurstCharge};
constexpr uint16_t kRecoverTicks = 8;
constexpr uint64_t kAimHold = 20;  // moving freely: it faces where it attacked this long
constexpr uint16_t kFoeStartCooldown[] = {40, 120, 240};  // it takes a moment to start
constexpr float kSwoopLift = 0.15f;  // a flying foe swoops down to bite

// How each species fights, in the order of Species (see Temper).
//                         keep  close throw dodge pace  lift
constexpr Temper kTempers[] = {
    {3.5f, 0.7f, 3.0f, 24, 1.0f, 2.0f},   // Companion: a bit of everything
    {1.8f, 0.6f, 6.0f, 40, 1.15f, 2.0f},  // Slime: stays close, and leaps at its foe
    {6.0f, 0.7f, 2.0f, 30, 1.0f, 2.0f},   // Mushroom: keeps away, and throws its spores
    {5.0f, 0.7f, 3.5f, 8, 1.25f, 2.0f},   // Bird: bites and runs, dodges well
    {3.5f, 0.7f, 3.0f, 24, 1.0f, 2.0f},   // Villager: does not fight
    {4.5f, 0.7f, 3.0f, 16, 1.0f, 3.2f},   // Crow: harasses from above, swoops down to bite
    {3.5f, 0.7f, 0.0f, 24, 1.0f, 2.0f},   // Fish: holds its place in the lake
    {4.5f, 0.7f, 3.0f, 12, 1.2f, 2.0f},   // Spark: quick, darts in and out
    {3.0f, 0.7f, 3.0f, 28, 0.95f, 2.0f},  // Axolotl: patient, sprays from close by
    {4.0f, 0.7f, 3.0f, 18, 1.1f, 2.0f},   // Cristi: scuttles around, throws its shards
    {4.5f, 0.7f, 3.5f, 10, 1.2f, 2.0f},   // Raccoon: snatches and darts away, dodges well
    {5.0f, 0.7f, 4.0f, 14, 1.0f, 3.4f},   // Owl: hovers high, swoops, throws gusts
    {2.5f, 0.7f, 2.5f, 36, 0.9f, 2.0f},   // Sheep: charges head first, rarely dodges
    {3.0f, 0.7f, 3.0f, 26, 0.95f, 2.0f},  // Mole: stubborn, throws clods
    {2.5f, 0.7f, 2.5f, 40, 0.85f, 2.0f},  // Golem: slow and heavy, rarely dodges, hurls rocks
    {3.0f, 0.7f, 3.5f, 20, 1.15f, 2.0f},  // Goat: charges horns first, leaps aside
    {4.5f, 0.7f, 3.0f, 10, 1.25f, 2.0f},  // Ermine: quick, bites and darts away
};
static_assert(std::size(kTempers) == kSpeciesCount);
constexpr float kLeapSpeed = 1.9f;  // a slime leaping at its foe: this much faster

float flat_distance(const Body& a, const Body& b, float& dx, float& dz) {
  const float x = b.pos.x - a.pos.x, z = b.pos.z - a.pos.z;
  const float d = std::sqrt(x * x + z * z);
  dx = d > 1e-4f ? x / d : a.facing_x;
  dz = d > 1e-4f ? z / d : a.facing_z;
  return d;
}

Creature& companion(World& w) { return w.creatures[0]; }

bool training(const World& w) { return w.battle.phase == BattlePhase::Training; }

// One side of a battle: a body and its shape. The other side of the
// companion is its foes, or the magician when training.
struct Side {
  Body* body;
  BodyShape shape;
};
Side ally_side(World& w) { return {&w.creatures[0].body, species_info(w.creatures[0].species).shape}; }
Creature& foe_creature(World& w, size_t k) { return w.creatures[w.battle.foes[k].index]; }
Side foe_side(World& w, size_t k) {
  if (training(w)) return {&w.player, kPlayerShape};
  Creature& e = foe_creature(w, k);
  return {&e.body, species_info(e.species).shape};
}
// How many foes there are (training: the magician).
size_t foe_count(const World& w) { return training(w) ? 1 : w.battle.foes.size(); }
Fighter& foe_fighter(World& w, size_t k) { return training(w) ? w.battle.shield : w.battle.foes[k].fighter; }

// Height between two bodies (0 if they overlap vertically): a bite needs both
// at the same height; a crow on a roof is out of reach.
float vertical_gap(const Side& a, const Side& b) {
  const float a0 = a.body->pos.y, a1 = a0 + a.shape.height;
  const float b0 = b.body->pos.y, b1 = b0 + b.shape.height;
  return std::max({0.0f, b0 - a1, a0 - b1});
}

// From the middle of one body to the middle of the other (unit vector), and the distance.
float aim(const Side& from, const Side& to, float& x, float& y, float& z) {
  x = to.body->pos.x - from.body->pos.x;
  y = to.body->pos.y + to.shape.height * 0.5f - (from.body->pos.y + from.shape.height * 0.5f);
  z = to.body->pos.z - from.body->pos.z;
  const float d = std::sqrt(x * x + y * y + z * z);
  if (d > 1e-4f) {
    x /= d;
    y /= d;
    z /= d;
  }
  return d;
}

// The object a body stands on (a roof, a rock...), or SIZE_MAX.
size_t standing_on(const World& w, const Body& body) {
  constexpr float kNear = 0.3f;
  size_t found = SIZE_MAX;
  const Box area{{body.pos.x - kNear, body.pos.y - kNear, body.pos.z - kNear}, {body.pos.x + kNear, body.pos.y + kNear, body.pos.z + kNear}};
  for_objects_near(w, area, [&](uint32_t k, const Box& b) {
    if (std::fabs(body.pos.y - b.max.y) < kNear && body.pos.x > b.min.x - kNear && body.pos.x < b.max.x + kNear &&
        body.pos.z > b.min.z - kNear && body.pos.z < b.max.z + kNear) {
      found = std::min<size_t>(found, k);  // the first one, as when they were all gone through
    }
  });
  return found;
}

constexpr float kMeleeHeight = 0.6f;  // highest gap a bite still reaches
Monster& ally(World& w) { return *active_monster(w); }  // in a battle, there is one (but between a trainer's rounds)

FightStats ally_stats(const World& w) { return monster_fight_stats(*active_monster(w)); }
FightStats foe_stats(const World& w, size_t k) {
  if (training(w)) return ally_stats(w);  // damage shown as against a monster like itself
  const Creature& e = w.creatures[w.battle.foes[k].index];
  return monster_fight_stats(e.species, e.level, e.genes);
}

Element move_element(const Move& m, Species species, const Genes& genes) {
  return m.elemental ? creature_elements(species, genes).first : Element::Normal;
}

// The monster of trainer `n` (0: the one fighting now, or Bastien's).
int rival_index(const World& w, uint8_t n = 0) {
  if (n == 0) n = w.battle.rival.npc ? w.battle.rival.npc : npc::kBastien;
  for (size_t i = 1; i < w.creatures.size(); ++i) {
    const Creature& c = w.creatures[i];
    if (c.tag == Tag::Rival && (c.npc == n || (c.npc == 0 && n == npc::kBastien))) return static_cast<int>(i);
  }
  return -1;
}

Vec3 on_ground(const World& w, Vec2 p) { return {p.x, w.ground.height(p.x, p.z), p.z}; }

}  // namespace

void grant_monster_xp(Monster& m, uint32_t xp) {
  m.xp += xp;
  while (m.level < 100 && m.xp >= monster_xp_to_next(m.level)) {
    m.xp -= monster_xp_to_next(m.level);
    const uint16_t before = monster_fight_stats(m).hp;
    ++m.level;
    m.hp = static_cast<uint16_t>(m.hp + monster_fight_stats(m).hp - before);
  }
}

void grant_wizard_xp(Wizard& wiz, uint32_t xp) {
  wiz.xp += xp;
  while (wiz.level < 100 && wiz.xp >= wizard_stats(wiz).xp_to_next) {
    wiz.xp -= wizard_stats(wiz).xp_to_next;
    const uint16_t before = wizard_stats(wiz).mana;
    ++wiz.level;
    wiz.mana = static_cast<uint16_t>(wiz.mana + wizard_stats(wiz).mana - before);  // its new mana is full
  }
}

namespace {

// A trainer's monster goes back to the first of its team, standing again
// by its trainer (after a battle, or a game loaded in the middle of one).
void restore_rival(World& w) {
  const int i = rival_index(w);
  if (i < 0) return;
  Creature& c = w.creatures[static_cast<size_t>(i)];
  uint8_t count = 0;
  const RivalMonster& first = rival_team(w, c.npc ? c.npc : npc::kBastien, count)[0];
  c.species = first.species;
  c.genes = first.genes;
  c.level = first.level;
  c.mind = Mind::Idle;
  c.timer = 0;
}

// Ends the battle. `xp`: the experience of the foes beaten, shared by the
// team members by the hit points each dealt (xp * dealt / (1 + all dealt)).
void end_battle(World& w, BattleOutcome outcome, uint32_t xp) {
  Battle& b = w.battle;
  b.outcome_species = b.last_species;
  if (b.beaten == 0 && !b.foes.empty()) {
    const Foe* t = battle_target(w);
    b.outcome_species = w.creatures[(t ? *t : b.foes[0]).index].species;
  }
  b.outcome_gains.clear();
  b.outcome_wizard_xp = 0;
  if (xp > 0) {
    uint64_t total = 0;
    for (uint32_t d : b.dealt) total += d;
    for (size_t slot = 0; slot < w.team.size() && slot < b.dealt.size(); ++slot) {
      if (b.dealt[slot] == 0) continue;
      // Rounded to the nearest point.
      const uint64_t share = (2u * uint64_t{xp} * b.dealt[slot] + (1 + total)) / (2 * (1 + total));
      Monster& m = w.team[slot];
      b.outcome_gains.push_back({static_cast<uint8_t>(slot), static_cast<uint32_t>(share), m.level, m.xp});
      grant_monster_xp(m, static_cast<uint32_t>(share));
    }
    b.outcome_wizard_xp = xp / 2;
    grant_wizard_xp(w.wizard, xp / 2);
  }
  // Those still standing go back to their lives.
  for (const Foe& f : b.foes) {
    Creature& c = w.creatures[f.index];
    if (c.mind == Mind::Battle) {
      c.mind = Mind::Idle;
      c.timer = 2 * rules::kTicksPerSecond;
    }
  }
  b.outcome_rival = b.rival.npc;
  if (b.rival.npc) {
    restore_rival(w);
    const int trainer = npc_index(w, b.rival.npc);
    if (trainer > 0) w.creatures[static_cast<size_t>(trainer)].mind = Mind::Idle;
  }
  b.outcome_count = b.beaten;
  b.outcome_no_xp = xp == 0 ? b.no_xp : NoXp::None;
  b.phase = BattlePhase::None;
  b.foes.clear();
  b.target = 0;
  b.projectiles.clear();
  b.ally = {};
  b.shield = {};
  b.timer = 0;
  b.away_ticks = 0;
  b.dealt = {};
  b.won_xp = 0;
  b.beaten = 0;
  b.no_xp = NoXp::None;
  b.mercy = false;
  b.rival = {};
  b.outcome = outcome;
  b.outcome_tick = w.tick;
  b.outcome_xp = xp;
  // Against an aggressive monster: lost or fled, the magician is sent back to the farm.
  const bool forced = b.forced;
  b.forced = false;
  if (forced && (outcome == BattleOutcome::Defeat || outcome == BattleOutcome::Fled || outcome == BattleOutcome::Forfeit)) {
    rescue(w);
  }
}

// A monster beaten once, or let go by the player, gives nothing when beaten or caught again.
uint32_t reward(const Creature& c) { return c.released || c.beaten ? 0u : 8u * c.level; }

void end_training(World& w) {
  Battle& b = w.battle;
  b.phase = BattlePhase::None;
  b.away_ticks = 0;
  b.target = 0;
  b.projectiles.clear();
  b.ally = {};
  b.shield = {};
  b.timer = 0;
}

// The foe closest to the companion, or kNoTarget.
uint16_t nearest_foe(World& w) {
  uint16_t best = kNoTarget;
  float best_d = 0;
  for (size_t k = 0; k < w.battle.foes.size(); ++k) {
    float dx, dz;
    const float d = flat_distance(companion(w).body, foe_creature(w, k).body, dx, dz);
    if (best == kNoTarget || d < best_d) {
      best = static_cast<uint16_t>(k);
      best_d = d;
    }
  }
  return best;
}

// foes[k] is out of the fight (beaten or caught): if it was aimed at, the
// companion aims at the closest other one.
void drop_foe(World& w, size_t k) {
  Battle& b = w.battle;
  b.foes.erase(b.foes.begin() + static_cast<std::ptrdiff_t>(k));
  if (b.target == kNoTarget) return;
  if (b.target > k) {
    --b.target;
  } else if (b.target == k) {
    b.target = nearest_foe(w);
    if (b.target == kNoTarget) b.target = 0;  // a trainer's next one will be aimed at
  }
}

// What beating or catching a foe gives (counted for the end of the battle).
void count_beaten(World& w, const Foe& f) {
  Battle& b = w.battle;
  const Creature& c = w.creatures[f.index];
  b.last_species = c.species;
  ++b.beaten;
  b.won_xp += f.xp;
  if (f.xp == 0 && b.no_xp == NoXp::None) b.no_xp = c.released ? NoXp::Released : NoXp::Beaten;
}

// foes[k] has no hit points left: it lies there (a trainer's one too, until
// the next one comes out).
void beat(World& w, size_t k) {
  Battle& b = w.battle;
  const Foe f = b.foes[k];
  Creature& c = w.creatures[f.index];
  c.mind = Mind::KnockedOut;
  c.timer = rules::kKnockedOutTicks;
  c.body.speed = 0;
  if (b.rival.npc) {
    b.rival.team[b.rival.out].hp = 0;
    b.rival.next_in = rules::kRivalNextTicks;
    b.last_species = c.species;
    ++b.beaten;
  } else {
    count_beaten(w, f);
    raider_down(w, c);
    guardian_down(w, c);
    if (c.tag == Tag::Raider) c.tag = Tag::None;  // beaten: it gives up the raid
    c.beaten = true;  // after its reward: the next time, none
    uint16_t& n = w.beaten_count[static_cast<size_t>(c.species)];
    n = static_cast<uint16_t>(std::min(n + 1, 65535));
  }
  drop_foe(w, k);
}

Foe make_foe(const World& w, size_t index, size_t slot) {
  const Creature& c = w.creatures[index];
  Foe f;
  f.index = static_cast<uint16_t>(index);
  f.max_hp = f.hp = monster_fight_stats(c.species, c.level, c.genes).hp;
  f.xp = c.tag == Tag::Rival ? 0 : reward(c);
  f.fighter.cooldown = {static_cast<uint16_t>(kFoeStartCooldown[0] + 25 * slot), kFoeStartCooldown[1],
                        static_cast<uint16_t>(kFoeStartCooldown[2] + 60 * slot)};
  f.fighter.strafe = slot % 2 == 0 ? 1 : -1;  // they circle both ways
  return f;
}

// A trainer sends the next monster of its team, where the first one stood.
void send_next(World& w) {
  Battle& b = w.battle;
  Rival& r = b.rival;
  const int i = rival_index(w);
  if (i < 0) return;
  for (uint8_t n = 0; n < r.count; ++n) {
    if (r.team[n].hp == 0) continue;
    RivalMonster& m = r.team[n];
    Creature& c = w.creatures[static_cast<size_t>(i)];
    c.species = m.species;
    c.genes = m.genes;
    c.level = m.level;
    c.mind = Mind::Battle;
    c.timer = 0;
    c.body.pos = on_ground(w, r.foe_spot);
    c.body.velocity_y = 0;
    c.body.speed = 0;
    r.out = n;
    m.seen = true;
    Foe f = make_foe(w, static_cast<size_t>(i), 0);
    f.hp = m.hp;
    b.foes.push_back(f);
    w.released_id = c.id;  // out of its trainer's crystal, in a beam of light
    w.released_tick = w.tick;
    return;
  }
}

// Everyone at its place for a trainer's battle, facing the other side.
void take_places(World& w) {
  Rival& r = w.battle.rival;
  float ax, az;
  const float len = std::sqrt((r.foe_spot.x - r.ally_spot.x) * (r.foe_spot.x - r.ally_spot.x) +
                              (r.foe_spot.z - r.ally_spot.z) * (r.foe_spot.z - r.ally_spot.z));
  ax = len > 0 ? (r.foe_spot.x - r.ally_spot.x) / len : 0;
  az = len > 0 ? (r.foe_spot.z - r.ally_spot.z) / len : 1;
  const auto put = [&](Body& b, Vec2 spot, float fx, float fz) {
    b.pos = on_ground(w, spot);
    b.velocity_y = 0;
    b.on_ground = true;
    b.speed = 0;
    b.facing_x = fx;
    b.facing_z = fz;
  };
  put(w.player, r.player_spot, ax, az);
  if (w.active >= 0) {
    put(companion(w).body, r.ally_spot, ax, az);
    if (const float hover = companion_hover(companion(w).species); hover > 0) companion(w).body.pos.y += hover;
  }
  if (const int i = rival_index(w); i > 0) put(w.creatures[static_cast<size_t>(i)].body, r.foe_spot, -ax, -az);
  if (const int t = npc_index(w, r.npc); t > 0) put(w.creatures[static_cast<size_t>(t)].body, r.trainer_spot, -ax, -az);
}

// A hit on one side (on the companion: by foes[k]; else on foes[k]), by an
// attack of type `element` (against the types of the one hit). Returns whether it landed.
bool hit(World& w, bool on_ally, size_t k, uint16_t damage, Element element, bool melee) {
  Battle& b = w.battle;
  Fighter& target = on_ally ? b.ally : foe_fighter(w, k);
  if (target.state == FighterState::Dodge) return false;
  if (on_ally && target.shield_ticks > 0) {  // the magician's shield takes it
    target.parried_tick = w.tick;
    return false;
  }
  if (!on_ally && training(w)) {  // the magician's shield takes it (shows what it would have done; no types)
    target.parried_tick = w.tick;
    target.last_damage = damage;
    target.last_effect = 0;
    return false;
  }
  if (target.state == FighterState::Parry) {
    target.parried_tick = w.tick;
    if (melee) {
      Fighter& attacker = on_ally ? foe_fighter(w, k) : b.ally;
      attacker.state = FighterState::Stunned;
      attacker.state_ticks = rules::kStunTicks;
    }
    return false;
  }
  const Elements types = on_ally ? creature_elements(ally(w).species, ally(w).genes)
                                 : creature_elements(foe_creature(w, k).species, foe_creature(w, k).genes);
  const int effect = type_effect(element, types.first, types.second);
  damage = type_damage(damage, effect);
  uint16_t& hp = on_ally ? ally(w).hp : b.foes[k].hp;
  uint16_t lost = std::min(hp, damage);
  if (!on_ally && b.mercy && lost >= hp) {  // the Mercy spell holds the blow back: 1 hit point left
    lost = static_cast<uint16_t>(hp - 1);
    b.mercy = false;
    b.mercy_tick = w.tick;
  }
  hp = static_cast<uint16_t>(hp - lost);
  if (!on_ally && w.active >= 0 && static_cast<size_t>(w.active) < b.dealt.size()) {
    b.dealt[static_cast<size_t>(w.active)] += lost;
  }
  target.hit_tick = w.tick;
  target.last_damage = damage;
  target.last_effect = static_cast<int8_t>(effect);
  return true;
}

void throw_projectile(World& w, const Side& self, float dx, float dz, float ax, float ay, float az, bool by_ally,
                      uint8_t slot, Element element, uint16_t damage, float range) {
  monstres::Projectile p;
  p.pos = {self.body->pos.x + dx * self.shape.half_width, self.body->pos.y + self.shape.height * 0.5f,
           self.body->pos.z + dz * self.shape.half_width};
  p.dir_x = ax;
  p.dir_y = ay;
  p.dir_z = az;
  p.ticks = static_cast<uint16_t>(range / rules::kProjectileSpeed * rules::kTicksPerSecond);
  p.from_ally = by_ally;
  p.move = slot;
  p.element = element;
  p.damage = damage;
  w.battle.projectiles.push_back(p);
}

// The companion's attack `slot` (a burst: once it has gathered): at its
// target, or at what is in front of it (moving freely: where the camera
// looks, at a foe if one is right there; it turns that way).
void ally_attack(World& w, uint8_t slot, float look_x, float look_z) {
  Battle& b = w.battle;
  Fighter& f = b.ally;
  const Side self = ally_side(w);
  const Monster& a = ally(w);
  const Move& m = species_moves(a.species)[slot];
  if (m.shape != Burst) f.cooldown[slot] = m.cooldown;  // a burst's started when it began to gather
  f.attack_tick = w.tick;
  f.attack_move = slot;
  f.state = FighterState::Recover;
  f.state_ticks = kRecoverTicks;
  const FightStats atk = ally_stats(w);
  const Element type = move_element(m, a.species, a.genes);
  const size_t n = foe_count(w);
  const size_t aimed = b.target < n ? b.target : SIZE_MAX;
  if (aimed == SIZE_MAX && m.shape != Burst) {
    self.body->facing_x = f.aim_x = look_x;
    self.body->facing_z = f.aim_z = look_z;
  }
  // What is right in front, the closest first.
  size_t ahead = SIZE_MAX;
  float ahead_d = 0;
  for (size_t k = 0; k < n; ++k) {
    float ax, ay, az;
    const float d = aim(self, foe_side(w, k), ax, ay, az);
    const float flat = std::sqrt(ax * ax + az * az);
    const float front = flat > 1e-3f ? (ax * self.body->facing_x + az * self.body->facing_z) / flat : 0;
    if (front > 0.9f && d <= m.range + 2.0f && (ahead == SIZE_MAX || d < ahead_d)) {
      ahead = k;
      ahead_d = d;
    }
  }
  switch (m.shape) {
    case Melee: {
      // The one aimed at if it is in reach, else the closest one in reach, in front.
      size_t best = SIZE_MAX;
      float best_d = 0;
      for (size_t k = 0; k < n; ++k) {
        const Side o = foe_side(w, k);
        float dx, dz;
        const float d = flat_distance(*self.body, *o.body, dx, dz);
        if (d > self.shape.half_width + o.shape.half_width + m.range || vertical_gap(self, o) > kMeleeHeight) continue;
        if (k == aimed) {
          best = k;
          break;
        }
        if (dx * self.body->facing_x + dz * self.body->facing_z < 0.2f) continue;
        if (best == SIZE_MAX || d < best_d) {
          best = k;
          best_d = d;
        }
      }
      if (best != SIZE_MAX) hit(w, false, best, move_damage(m, atk, foe_stats(w, best)), type, true);
      break;
    }
    case Burst:
      for (size_t k = n; k-- > 0;) {
        const Side o = foe_side(w, k);
        float ax, ay, az;
        if (aim(self, o, ax, ay, az) <= m.range + o.shape.half_width) {
          hit(w, false, k, move_damage(m, atk, foe_stats(w, k)), type, false);
        }
      }
      break;
    case Projectile: {
      // Thrown at the middle of the one aimed at, even up on a roof or down in the water.
      const size_t at = aimed != SIZE_MAX ? aimed : ahead;
      float dx = self.body->facing_x, dz = self.body->facing_z, ax = dx, ay = 0, az = dz;
      uint16_t damage = 0;
      if (at != SIZE_MAX) {
        flat_distance(*self.body, *foe_side(w, at).body, dx, dz);
        aim(self, foe_side(w, at), ax, ay, az);
        damage = move_damage(m, atk, foe_stats(w, at));
      } else if (n > 0) {
        damage = move_damage(m, atk, foe_stats(w, 0));
      }
      throw_projectile(w, self, dx, dz, ax, ay, az, true, slot, type, damage, m.range);
      break;
    }
  }
}

// foes[k]'s attack (after its windup), at the companion.
void foe_attack(World& w, size_t k, uint8_t slot) {
  Fighter& f = foe_fighter(w, k);
  f.attack_tick = w.tick;
  f.attack_move = slot;
  f.state = FighterState::Recover;
  f.state_ticks = kRecoverTicks;
  if (!active_monster(w)) return;  // its foe went back into the crystal
  const Creature& e = foe_creature(w, k);
  const Move& m = species_moves(e.species)[slot];
  const Side self = foe_side(w, k), other = ally_side(w);
  const uint16_t damage = move_damage(m, foe_stats(w, k), ally_stats(w));
  const Element type = move_element(m, e.species, e.genes);
  float dx, dz, ax, ay, az;
  const float d = flat_distance(*self.body, *other.body, dx, dz);
  const float d3 = aim(self, other, ax, ay, az);
  switch (m.shape) {
    case Melee:
      if (d <= self.shape.half_width + other.shape.half_width + m.range && vertical_gap(self, other) <= kMeleeHeight) {
        hit(w, true, k, damage, type, true);
      }
      break;
    case Burst:
      if (d3 <= m.range + other.shape.half_width) hit(w, true, k, damage, type, false);
      break;
    case Projectile:
      throw_projectile(w, self, dx, dz, ax, ay, az, false, slot, type, damage, m.range);
      break;
  }
}

void count_down(Fighter& f) {
  for (uint16_t& c : f.cooldown) c = c > 0 ? static_cast<uint16_t>(c - 1) : 0;
  if (f.dodge_cooldown > 0) --f.dodge_cooldown;
  if (f.parry_cooldown > 0) --f.parry_cooldown;
  if (f.shield_ticks > 0) --f.shield_ticks;
}

bool free_to_act(const Fighter& f) { return f.state == FighterState::Ready || f.state == FighterState::Recover; }

// The camera's way on the ground (forward = (-sin, 0, -cos) of its yaw).
void camera_forward(const Input& in, float& x, float& z) {
  const SinCos sc = sincos16(in.yaw);
  x = -sc.sin;
  z = -sc.cos;
}

}  // namespace

namespace {

// Where a monster comes out of the crystal: next to the player, a little
// behind on its right (on the player's spot if something is there).
Body beside_player(const World& w, Species species) {
  const Body& p = w.player;
  Body b;
  b.pos = {p.pos.x - p.facing_x * 1.1f - p.facing_z * 0.7f, p.pos.y, p.pos.z - p.facing_z * 1.1f + p.facing_x * 0.7f};
  b.pos.y = std::max(b.pos.y, w.ground.height(b.pos.x, b.pos.z));
  b.facing_x = p.facing_x;
  b.facing_z = p.facing_z;
  const Box box = body_box(b, species_info(species).shape);
  bool blocked = in_lake(w.village.lake, b.pos.x, b.pos.z) ||
                 (w.ground.sea(b.pos.x, b.pos.z) && w.ground.height(b.pos.x, b.pos.z) < rules::kSeaLevel);
  for_objects_near(w, box, [&](uint32_t, const Box& o) { blocked = blocked || overlaps(box, o); });
  if (blocked) b.pos = p.pos;
  return b;
}

// Lets team[slot] go back to the wild: it comes out of the crystal (or
// leaves the player's side) and runs off.
void release(World& w, size_t slot) {
  if (slot >= w.team.size() || w.team.size() < 2 || w.battle.phase != BattlePhase::None) return;
  const Monster m = w.team[slot];
  Creature c;
  c.species = m.species;
  c.genes = m.genes;
  c.level = static_cast<uint8_t>(std::min<uint16_t>(m.level, 255));
  c.released = true;
  c.beaten = m.beaten;
  c.tag = m.tag;
  const bool out = static_cast<int>(slot) == w.active;
  c.body = out ? w.creatures[0].body : beside_player(w, m.species);
  // Away from the player, towards its new home.
  float ax = c.body.pos.x - w.player.pos.x, az = c.body.pos.z - w.player.pos.z;
  const float len = std::sqrt(ax * ax + az * az);
  ax = len > 0.1f ? ax / len : -w.player.facing_x;
  az = len > 0.1f ? az / len : -w.player.facing_z;
  c.home = {rules::clamp_world_x(c.body.pos.x + ax * 10, 2.0f), rules::clamp_world_z(c.body.pos.z + az * 10, 2.0f)};
  c.target = c.home;
  c.mind = flies(c.species) ? Mind::Fly : Mind::Flee;
  c.target_y = w.ground.height(c.home.x, c.home.z);
  c.timer = 5 * rules::kTicksPerSecond;
  bool beam = !out;
  if (c.species == Species::Fish) {
    // Back to the lake, where fish live (the beam reaches it if it is near).
    const Lake& lake = w.village.lake;
    c.body.pos = {lake.center.x + lake.radius_x * 0.3f, rules::kSwimLevel, lake.center.z};
    c.body.on_ground = true;
    c.home = c.target = {c.body.pos.x, c.body.pos.z};
    c.mind = Mind::Idle;
    c.timer = rules::kTicksPerSecond;
    const float dx = c.body.pos.x - w.player.pos.x, dz = c.body.pos.z - w.player.pos.z;
    beam = dx * dx + dz * dz < 30.0f * 30.0f;
  }
  if (out) {
    w.active = -1;
    w.creatures[0].mind = Mind::Idle;
  }
  const size_t index = add_creature(w, c);
  w.released_id = w.creatures[index].id;
  uint16_t& n = w.released_count[static_cast<size_t>(m.species)];
  n = static_cast<uint16_t>(std::min(n + 1, 65535));
  w.released_tick = beam ? w.tick : 0;  // out of the crystal: a beam of light
  w.team.erase(w.team.begin() + static_cast<std::ptrdiff_t>(slot));
  if (w.active > static_cast<int>(slot)) --w.active;
}

}  // namespace

void summon(World& w, size_t slot) {
  if (slot >= w.team.size() || w.creatures.empty()) return;
  Creature& c = w.creatures[0];
  const bool fighting = w.battle.phase == BattlePhase::Fight || w.battle.phase == BattlePhase::Prepare;
  const bool trains = training(w);
  if ((fighting || trains) && w.team[slot].hp == 0) return;  // knocked out: not in a battle
  if (w.active >= 0) {  // the one out goes back into the crystal
    const Monster& out = w.team[static_cast<size_t>(w.active)];
    if (static_cast<int>(slot) == w.active) {  // just recalled (not in the middle of a fight)
      if (fighting) return;
      if (trains) end_training(w);
      w.went_in = {w.tick, false, out.species, out.genes, c.body};
      w.active = -1;
      c.mind = Mind::Idle;
      return;
    }
    w.went_in = {w.tick, false, out.species, out.genes, c.body};
  } else {
    c.body = beside_player(w, w.team[slot].species);
  }
  // The new one appears where the other was.
  const Monster& m = w.team[slot];
  w.active = static_cast<int8_t>(slot);
  c.species = m.species;
  c.genes = m.genes;
  c.level = static_cast<uint8_t>(std::min<uint16_t>(m.level, 255));
  c.mind = m.hp == 0 ? Mind::KnockedOut : Mind::Idle;  // knocked out: it lies there, waiting for a heal
  c.timer = 0;
  c.body.velocity_y = 0;
  w.came_out_tick = w.tick;
  if (fighting || trains) w.battle.ally = {};  // a fresh fighter
}

bool holds_its_place(Species species) { return species == Species::Fish; }

const Moves& species_moves(Species species) {
  const auto i = static_cast<size_t>(species);
  return kMoves[i < kSpeciesCount ? i : 0];
}

FightStats monster_fight_stats(Species species, uint16_t level, const Genes& genes) {
  const MonsterStats& s = monster_stats(species);
  const uint32_t l = std::max<uint16_t>(level, 1) - 1u;
  // The variants (builds too) change only the gifts' bonuses (stat_mod).
  const auto grow = [&](uint16_t base, uint32_t per, Stat stat) {
    const int v = static_cast<int>(base + base * l / per) + stat_mod(species, genes, stat);
    return static_cast<uint16_t>(std::max(1, v));
  };
  return {grow(s.hp, 8, Stat::Hp), grow(s.attack, 10, Stat::Attack), grow(s.defense, 10, Stat::Defense),
          grow(s.speed, 10, Stat::Speed), grow(s.magic, 10, Stat::Magic)};
}

uint16_t windup_ticks(uint8_t slot) { return slot < std::size(kFoeWindup) ? kFoeWindup[slot] : 0; }

const Temper& species_temper(Species species) {
  const auto i = static_cast<size_t>(species);
  return kTempers[i < kSpeciesCount ? i : 0];
}

uint32_t monster_xp_to_next(uint16_t level) { return 20u * std::max<uint16_t>(level, 1); }

uint16_t move_damage(const Move& m, const FightStats& attacker, const FightStats& defender) {
  const uint32_t atk = m.shape == Melee ? attacker.attack : attacker.magic;
  const uint32_t d = uint32_t{m.power} * atk * 2u / (defender.defense + 10u);
  return static_cast<uint16_t>(std::clamp<uint32_t>(d, 1, 9999));
}

namespace {

// The types' chart (rows: the attack, columns: the monster hit), in the
// order of Element. Each type resists itself, but Earth; Normal has no
// strengths nor weaknesses.
constexpr size_t kTypeCount = static_cast<size_t>(Element::Count);
constexpr auto kTypeChart = [] {
  std::array<std::array<int8_t, kTypeCount>, kTypeCount> c{};
  const auto set = [&](Element a, Element d, int8_t v) { c[static_cast<size_t>(a)][static_cast<size_t>(d)] = v; };
  using E = Element;
  for (size_t t = 0; t < kTypeCount; ++t) {
    if (t != static_cast<size_t>(E::Normal) && t != static_cast<size_t>(E::Earth)) c[t][t] = -1;
  }
  // The triangle: water > fire > nature > water, and the other way.
  set(E::Water, E::Fire, 1), set(E::Fire, E::Nature, 1), set(E::Nature, E::Water, 1);
  set(E::Fire, E::Water, -1), set(E::Nature, E::Fire, -1), set(E::Water, E::Nature, -1);
  // Light and shadow, each against the other.
  set(E::Light, E::Shadow, 1), set(E::Shadow, E::Light, 1);
  set(E::Electric, E::Earth, -1), set(E::Electric, E::Air, 1), set(E::Earth, E::Air, -1);
  set(E::Air, E::Fire, 1), set(E::Fire, E::Ice, 1);
  set(E::Fire, E::Earth, -1), set(E::Water, E::Earth, 1);
  set(E::Ice, E::Water, -1), set(E::Water, E::Ice, -1);
  return c;
}();

}  // namespace

int type_effect(Element attack, Element defense) {
  const auto a = static_cast<size_t>(attack), d = static_cast<size_t>(defense);
  return a < kTypeCount && d < kTypeCount ? kTypeChart[a][d] : 0;
}

int type_effect(Element attack, Element first, Element second) {
  return type_effect(attack, first) + (second != first ? type_effect(attack, second) : 0);
}

uint16_t type_damage(uint16_t damage, int effect) {
  uint32_t num = 1, den = 1;
  for (int i = 0; i < effect; ++i) num *= 3, den *= 2;   // x1.5 each
  for (int i = 0; i > effect; --i) num *= 2, den *= 3;   // /1.5 each
  const uint32_t d = (uint32_t{damage} * num + den / 2) / den;
  return static_cast<uint16_t>(std::clamp<uint32_t>(d, 1, 9999));
}

Monster starting_companion(const Genes& genes) {
  Monster m;
  m.species = Species::Companion;
  m.genes = genes;
  m.level = rules::kCompanionStartLevel;
  m.hp = monster_fight_stats(m).hp;
  return m;
}

bool owned(const World& w, const Creature& c) {
  return c.tag == Tag::Rival || c.tag == Tag::Flock || c.tag == Tag::Lamb || c.tag == Tag::Boarder ||
         (c.tag == Tag::LostPiou && w.quests[static_cast<size_t>(QuestId::LostPiou)].stage == QuestStage::Done);
}

EngageCheck can_engage(const World& w, size_t index) {
  if (index == 0 || index >= w.creatures.size()) return EngageCheck::NotMonster;
  const Creature& c = w.creatures[index];
  if (!monster_stats(c.species).is_monster) return EngageCheck::NotMonster;
  if (w.battle.phase != BattlePhase::None) return EngageCheck::InBattle;
  if (!active_monster(w)) return EngageCheck::NoCompanion;
  if (active_monster(w)->hp == 0) return EngageCheck::CompanionDown;
  if (c.mind == Mind::KnockedOut) return EngageCheck::KnockedOut;
  if (owned(w, c)) return EngageCheck::Owned;
  const float dx = c.body.pos.x - w.player.pos.x, dz = c.body.pos.z - w.player.pos.z;
  if (dx * dx + dz * dz > rules::kEngageDistance * rules::kEngageDistance) return EngageCheck::TooFar;
  return EngageCheck::Ok;
}

const char* engage_check_name(EngageCheck check) {
  static const char* kNames[] = {"ok",         "notMonster", "tooFar", "inBattle", "noCompanion",
                                 "knockedOut", "owned",      "companionDown"};
  const auto i = static_cast<size_t>(check);
  return i < std::size(kNames) ? kNames[i] : "?";
}

const Foe* battle_target(const World& w) {
  const Battle& b = w.battle;
  return b.phase == BattlePhase::Fight && b.target < b.foes.size() ? &b.foes[b.target] : nullptr;
}

int foe_slot(const World& w, size_t index) {
  const Battle& b = w.battle;
  if (b.phase != BattlePhase::Fight && b.phase != BattlePhase::Prepare) return -1;
  for (size_t k = 0; k < b.foes.size(); ++k) {
    if (b.foes[k].index == index) return static_cast<int>(k);
  }
  return -1;
}

bool can_capture(const World& w) {
  const Foe* t = battle_target(w);
  return t && !sealing(w) && w.battle.rival.npc == 0 && t->hp > 0 && (w.team.size() < rules::kTeamSize || farm_open(w)) &&
         uint32_t{t->hp} * 100u <= uint32_t{t->max_hp} * rules::kCaptureHpPercent;
}

const std::array<RivalMonster, kRivalTeamSize>& rival_team(const World& w, uint8_t n, uint8_t& count) {
  if (npc::wanderer(n)) {
    const Wanderer& x = w.wanderers[n - npc::kWanderer];
    count = x.count;
    return x.team;
  }
  // Bastien: a Champi beside him, a Gloubi in reserve.
  static const std::array<RivalMonster, kRivalTeamSize> kBastien = {{
      {Species::Mushroom, Genes{}, 4, 0, false},
      {Species::Slime, Genes{}, 5, 0, false},
      {},
  }};
  // Romane, camping in the woods: three of the west's.
  static const std::array<RivalMonster, kRivalTeamSize> kRomane = {{
      {Species::Owl, Genes{}, 8, 0, false},
      {Species::Raccoon, Genes{}, 9, 0, false},
      {Species::Spark, Genes{}, 10, 0, false},
  }};
  if (n == npc::kRomane) {
    count = 3;
    return kRomane;
  }
  count = n == npc::kBastien ? 2 : 0;
  return kBastien;
}

bool rival_arena(const World& w, uint8_t trainer, Vec2& center, Vec2& axis) {
  if (npc::wanderer(trainer)) {  // where they met: between the player and the trainer
    const int i = npc_index(w, trainer);
    if (i < 0) return false;
    const Vec3 t = w.creatures[static_cast<size_t>(i)].body.pos;
    float dx = t.x - w.player.pos.x, dz = t.z - w.player.pos.z;
    const float l = std::sqrt(dx * dx + dz * dz);
    dx = l > 1e-3f ? dx / l : 1.0f;
    dz = l > 1e-3f ? dz / l : 0.0f;
    center = {(t.x + w.player.pos.x) / 2, (t.z + w.player.pos.z) / 2};
    axis = {dx, dz};
    return true;
  }
  if (trainer == npc::kRomane) {  // in her camp's clearing, south of the fire
    if (!w.west.valid()) return false;
    center = {w.west.camp.x, w.west.camp.z + 2.5f};
    axis = {1, 0};
    return true;
  }
  if (w.village.lake.radius_x <= 0) return false;
  const Vec2 vc = w.village.center;
  const float away = w.village.lake.center.x > vc.x ? -1.0f : 1.0f;  // from the lake
  constexpr float kOut = rules::kVillageRadius + 9.0f;
  // Around the village, starting on the side away from the lake: a flat
  // stretch without anything in the way, along the village's edge.
  for (int i = 0; i < 24; ++i) {
    const int turn = (i + 1) / 2 * (i % 2 == 0 ? 1 : -1);  // 0, -1, +1, -2, +2...
    const SinCos sc = sincos16(static_cast<Angle16>(turn * 2731));
    const Vec2 radial{away * sc.cos, sc.sin};
    const Vec2 c{vc.x + radial.x * kOut, vc.z + radial.z * kOut};
    const Vec2 along{-radial.z, radial.x};
    const float h0 = w.ground.height(c.x, c.z);
    // Only the objects around this stretch can be in the way.
    std::vector<Box> near;
    for_objects_near(w, Box{{c.x - 18, -1e9f, c.z - 18}, {c.x + 18, 1e9f, c.z + 18}}, [&](uint32_t k, const Box& b) {
      const Object& o = w.objects[k];
      const float dx = o.pos.x - c.x, dz = o.pos.z - c.z;
      if (dx * dx + dz * dz < 18.0f * 18.0f) near.push_back(b);
    });
    bool clear = true;
    for (int t = -10; t <= 10 && clear; t += 2) {
      for (int side = -2; side <= 2 && clear; ++side) {  // 3 m on each side: the magician and the trainer
        const float x = c.x + along.x * static_cast<float>(t) + radial.x * static_cast<float>(side) * 1.5f;
        const float z = c.z + along.z * static_cast<float>(t) + radial.z * static_cast<float>(side) * 1.5f;
        const float h = w.ground.height(x, z);
        const Box probe{{x - 0.8f, h, z - 0.8f}, {x + 0.8f, h + 2.0f, z + 0.8f}};
        clear = rules::in_world(x, z, 3.0f) && std::fabs(h - h0) < 1.2f &&
                !in_lake(w.village.lake, x, z, 2.0f) &&
                std::none_of(near.begin(), near.end(), [&](const Box& b) { return overlaps(probe, b); });
      }
    }
    if (clear) {
      center = c;
      axis = along;
      return true;
    }
  }
  center = {vc.x + away * kOut, vc.z};
  axis = {0, 1};
  return true;
}

bool can_challenge(const World& w, uint8_t n) {
  uint8_t count = 0;
  rival_team(w, n, count);
  const Monster* a = active_monster(w);
  if (npc::wanderer(n)) {  // beaten, it offers no more; an aggressive one does not ask
    const int i = npc_index(w, n);
    if (i < 0 || w.creatures[static_cast<size_t>(i)].beaten || w.creatures[static_cast<size_t>(i)].tag == Tag::Raider) {
      return false;
    }
  }
  return count > 0 && w.battle.phase == BattlePhase::None && a && a->hp > 0 && near_npc(w, n) && rival_index(w, n) > 0;
}

bool battle_forward(const World& w, float& fx, float& fz) {
  const Battle& b = w.battle;
  if (w.creatures.empty()) return false;
  switch (b.phase) {
    case BattlePhase::Training:
      if (b.target == kNoTarget) return false;
      flat_distance(w.creatures[0].body, w.player, fx, fz);
      return true;
    case BattlePhase::Prepare: {  // along the line between both sides
      Body from, to;
      from.pos = {b.rival.ally_spot.x, 0, b.rival.ally_spot.z};
      to.pos = {b.rival.foe_spot.x, 0, b.rival.foe_spot.z};
      flat_distance(from, to, fx, fz);
      return true;
    }
    case BattlePhase::Fight:
      if (const Foe* t = battle_target(w)) {
        flat_distance(w.creatures[0].body, w.creatures[t->index].body, fx, fz);
        return true;
      }
      return false;
    default:
      return false;
  }
}

bool can_train(const World& w) {
  return w.battle.phase == BattlePhase::None && active_monster(w) != nullptr && active_monster(w)->hp > 0;
}

const SpellInfo& spell_info(SpellId spell) {
  static const SpellInfo kSpells[] = {
      //  id         combat adventure capture a rune  cooldown                mana
      {"none", false, false, false, false, 0, 0},
      {"heal", true, true, false, false, rules::kHealCooldown, 4},
      {"shield", true, false, false, false, rules::kShieldCooldown, 3},
      {"mercy", true, false, false, false, rules::kMercyCooldown, 5},
      {"detect", false, true, false, false, rules::kDetectTicks, 6},
      {"wayOut", false, true, false, false, rules::kWayOutTicks, 0},
      {"replay", false, false, true, true, 0, 1},
      {"guard", false, false, true, true, 0, 2},
      {"boost", false, false, true, true, 0, 2},
      {"newOrder", false, false, true, false, 0, 3},
      {"steady", false, false, true, false, 0, 8},
      {"hint", false, false, true, false, 0, 2},
      {"home", false, true, false, false, rules::kHomeCooldown, 0},
  };
  static_assert(std::size(kSpells) == kSpellCount);
  const auto i = static_cast<size_t>(spell);
  return kSpells[i < kSpellCount ? i : 0];
}

namespace {
bool in_combat(const World& w) {
  const BattlePhase p = w.battle.phase;
  return p == BattlePhase::Fight || p == BattlePhase::Training || p == BattlePhase::Prepare;
}
}  // namespace

std::span<const SpellId> current_spells(const World& w) {
  if (sealing(w)) return w.capture_spells;
  if (in_combat(w)) return w.combat_spells;
  return w.adventure_spells;
}

uint16_t spell_cost(const World& w, SpellId spell) {
  const SpellInfo& info = spell_info(spell);
  if (!info.per_rune) return info.mana;
  const Seal& s = w.battle.seal;
  return static_cast<uint16_t>(info.mana * (s.length > s.done ? s.length - s.done : 0));
}

namespace {
// Why a capture spell cannot be cast now (nullptr: it can).
const char* cannot_seal_cast(const World& w, SpellId spell) {
  const Seal& s = w.battle.seal;
  const bool show = s.stage == SealStage::Show, input = s.stage == SealStage::Input;
  switch (spell) {
    case SpellId::Guard:
      if (s.guard) return "active";
      if (!show && !input) return "notNow";
      break;
    case SpellId::Boost:
      if (s.boost) return "active";
      if (!show && !input) return "notNow";
      break;
    case SpellId::Hint:
      if (!input) return "notNow";
      if (s.hint != kNoRune) return "active";
      break;
    case SpellId::Steady:
      if (s.power >= rules::kSealSteadyPower) return "steady";
      if (!show && !input) return "notNow";
      break;
    default:  // Replay, NewOrder
      if (!input) return "notNow";
      break;
  }
  if (w.wizard.mana < spell_cost(w, spell)) return "mana";
  return nullptr;
}
}  // namespace

const char* cannot_cast(const World& w, size_t slot) {
  const auto now = current_spells(w);
  if (slot >= now.size()) return "none";
  const SpellId s = now[slot];
  if (s == SpellId::None) return "none";
  if (sealing(w)) return cannot_seal_cast(w, s);
  if (w.battle.phase == BattlePhase::Flee) return "fleeing";
  if ((s == SpellId::Mercy && w.battle.mercy) || (s == SpellId::Detect && w.detect_ticks > 0) ||
      (s == SpellId::WayOut && w.way_out_ticks > 0)) {
    return "active";
  }
  if (s == SpellId::WayOut && !inside(w)) return "outside";
  if (s == SpellId::Home && (w.battle.phase != BattlePhase::None || !w.west.valid())) return "notNow";
  if (w.spell_cooldowns[static_cast<size_t>(s)] > 0) return "cooldown";
  if (s == SpellId::Mercy && training(w)) return "training";  // no one is hurt there
  const Monster* a = active_monster(w);
  if (s != SpellId::Detect && s != SpellId::WayOut && s != SpellId::Home) {
    if (!a) return "noCompanion";
    if (s == SpellId::Heal && a->hp >= monster_fight_stats(*a).hp) return "full";
  }
  if (w.wizard.mana < spell_info(s).mana) return "mana";
  return nullptr;
}

bool can_heal_team(const World& w, uint8_t n) {
  return (n == npc::kOdette || n == npc::kViolette || n == npc::kSolene) && w.battle.phase == BattlePhase::None && near_npc(w, n);
}

namespace {

// Keeps how the last battle ended (for the interface) in a new one.
Battle fresh_battle(const Battle& old, BattlePhase phase) {
  Battle b;
  b.phase = phase;
  b.outcome = old.outcome;
  b.outcome_tick = old.outcome_tick;
  b.outcome_xp = old.outcome_xp;
  b.outcome_species = old.outcome_species;
  b.outcome_count = old.outcome_count;
  b.outcome_no_xp = old.outcome_no_xp;
  b.outcome_rival = old.outcome_rival;
  b.outcome_gains = old.outcome_gains;
  b.outcome_wizard_xp = old.outcome_wizard_xp;
  return b;
}

void engage(World& w, size_t index) {
  Battle b = fresh_battle(w.battle, BattlePhase::Fight);
  b.foes.push_back(make_foe(w, index, 0));
  // Others of its kind around join in, the closest first.
  const Creature& first = w.creatures[index];
  while (b.foes.size() < rules::kMaxFoes) {
    size_t best = 0;
    float best_d = rules::kGroupRadius * rules::kGroupRadius;
    for (size_t i = 1; i < w.creatures.size(); ++i) {
      const Creature& c = w.creatures[i];
      if (c.species != first.species || c.mind == Mind::KnockedOut || owned(w, c)) continue;
      if (std::any_of(b.foes.begin(), b.foes.end(), [&](const Foe& f) { return f.index == i; })) continue;
      const float dx = c.body.pos.x - first.body.pos.x, dz = c.body.pos.z - first.body.pos.z;
      if (dx * dx + dz * dz < best_d) {
        best = i;
        best_d = dx * dx + dz * dz;
      }
    }
    if (best == 0) break;
    b.foes.push_back(make_foe(w, best, b.foes.size()));
  }
  for (const Foe& f : b.foes) {
    w.creatures[f.index].mind = Mind::Battle;
    w.creatures[f.index].timer = 0;
  }
  w.battle = std::move(b);
  ally(w).hp = std::max<uint16_t>(ally(w).hp, 1);
}

// A trainer's battle: both sides walk to their places (see battle_prepare).
void challenge(World& w, uint8_t n) {
  Vec2 center, axis;
  const int i = rival_index(w, n);
  if (i <= 0 || !rival_arena(w, n, center, axis)) return;
  // The player's side: the one closer to it.
  if ((w.player.pos.x - center.x) * axis.x + (w.player.pos.z - center.z) * axis.z > 0) axis = {-axis.x, -axis.z};
  Battle b = fresh_battle(w.battle, BattlePhase::Prepare);
  Rival& r = b.rival;
  r.npc = n;
  r.team = rival_team(w, n, r.count);
  for (uint8_t k = 0; k < r.count; ++k) {
    r.team[k].hp = monster_fight_stats(r.team[k].species, r.team[k].level, r.team[k].genes).hp;
  }
  r.team[0].seen = true;  // it stands beside him
  const float half = rules::kRivalSpacing / 2;
  r.ally_spot = {center.x - axis.x * half, center.z - axis.z * half};
  r.foe_spot = {center.x + axis.x * half, center.z + axis.z * half};
  // The magician and the trainer stand back, to the side (not in the way of the camera).
  const Vec2 side{-axis.z, axis.x};
  r.player_spot = {center.x - axis.x * (half + 2.0f) + side.x * 3.0f, center.z - axis.z * (half + 2.0f) + side.z * 3.0f};
  r.trainer_spot = {center.x + axis.x * (half + 2.0f) + side.x * 3.0f, center.z + axis.z * (half + 2.0f) + side.z * 3.0f};
  w.battle.rival.npc = n;  // whose monster restore_rival dresses
  restore_rival(w);
  Creature& c = w.creatures[static_cast<size_t>(i)];
  c.mind = Mind::Battle;
  b.foes.push_back(make_foe(w, static_cast<size_t>(i), 0));
  w.battle = std::move(b);
}

}  // namespace

namespace {
// Before a forced battle: the first team member standing comes out if needed.
// False if no one can fight (the magician is sent back to the farm).
bool ready_to_fight(World& w) {
  const Monster* a = active_monster(w);
  if (!a || a->hp == 0) {  // the first one standing comes out
    size_t slot = w.team.size();
    for (size_t k = 0; k < w.team.size() && slot == w.team.size(); ++k) {
      if (w.team[k].hp > 0) slot = k;
    }
    if (slot == w.team.size()) {  // no one to fight: away to the farm
      rescue(w);
      return false;
    }
    if (w.active >= 0) {  // the knocked out one goes back in first
      const Monster& out = w.team[static_cast<size_t>(w.active)];
      w.went_in = {w.tick, false, out.species, out.genes, companion(w).body};
      w.active = -1;
      companion(w).mind = Mind::Idle;
    }
    summon(w, slot);
  }
  return true;
}
}  // namespace

void force_battle(World& w, size_t index) {
  if (index == 0 || index >= w.creatures.size() || w.battle.phase != BattlePhase::None) return;
  if (!ready_to_fight(w)) return;
  engage(w, index);
  w.battle.forced = true;
}

void force_challenge(World& w, uint8_t n) {
  uint8_t count = 0;
  rival_team(w, n, count);
  if (count == 0 || w.battle.phase != BattlePhase::None || rival_index(w, n) <= 0) return;
  if (!ready_to_fight(w)) return;
  challenge(w, n);
  w.battle.forced = w.battle.phase == BattlePhase::Prepare;
}

namespace {

// foes[k] joins the team (or the farm, the team being full).
void capture_foe(World& w, size_t k) {
  Battle& b = w.battle;
  const Foe f = b.foes[k];
  const Creature& e = w.creatures[f.index];
  Monster m;
  m.species = e.species;
  m.genes = e.genes;
  m.level = e.level;
  m.hp = f.hp;
  m.tag = e.tag;
  m.beaten = e.beaten || e.released;
  give_traits(m, roll_traits(w.seed, e.id, e.species, e.genes), world_day(w));
  uint16_t& n = w.captured_count[static_cast<size_t>(e.species)];
  n = static_cast<uint16_t>(std::min(n + 1, 65535));
  if (e.tag == Tag::Giant) w.quests[static_cast<size_t>(QuestId::GiantChampi)].progress |= kGiantCaught;
  raider_down(w, e);
  guardian_down(w, e);
  if (m.tag == Tag::Raider) m.tag = Tag::None;
  capture(w, f.index);
  w.went_in = {w.tick, true, e.species, e.genes, e.body};  // runes, then into the crystal
  count_beaten(w, f);
  drop_foe(w, k);
  remove_creature(w, f.index);
  if (b.foes.empty()) end_battle(w, BattleOutcome::Captured, b.won_xp);
  if (w.team.size() < rules::kTeamSize) w.team.push_back(m);
  else send_to_farm(w, m);  // the transfer stone: the team is full
}

// A new series of runes (never the same twice in a row), shown; the spells
// on the last one are over.
void seal_order(World& w) {
  Seal& s = w.battle.seal;
  Rng r(w.seed ^ (w.tick * 0x9E3779B97F4A7C15ULL), 0x5EA1u + s.foe + 977u * s.round);
  s.order = {};
  s.order[0] = static_cast<uint8_t>(r.below(s.runes));
  for (size_t i = 1; i < s.length; ++i) {
    const uint32_t step = 1 + r.below(s.runes - 1u);
    s.order[i] = static_cast<uint8_t>((s.order[i - 1] + step) % s.runes);
  }
  s.done = s.credited = 0;
  s.hint = kNoRune;
  s.guard = s.boost = false;
  s.timer = 0;
  s.stage = SealStage::Show;
}

void start_seal(World& w) {
  Battle& b = w.battle;
  Seal& s = b.seal;
  s = {};
  s.foe = b.foes[b.target].index;
  const Creature& c = w.creatures[s.foe];
  s.runes = c.level <= rules::kSealLowLevel   ? rules::kSealRunesLow
            : c.level <= rules::kSealMidLevel ? rules::kSealRunesMid
                                              : rules::kSealRunesHigh;
  const Rarity rarity = rarest(c.species, c.genes);
  const uint8_t extra = rarity == Rarity::VeryRare ? rules::kSealVeryRareExtra
                        : rarity == Rarity::Rare   ? rules::kSealRareExtra
                                                   : 0;
  s.runes = static_cast<uint8_t>(s.runes + extra);
  s.length = static_cast<uint8_t>(s.runes - 1);
  s.gain = rarity == Rarity::VeryRare ? rules::kSealGainVeryRare : rules::kSealGain;
  s.power = s.before = rules::kSealStart;
  b.ally.state = FighterState::Ready;  // its burst, if gathering, is lost
  b.ally.state_ticks = 0;
  seal_order(w);
}

uint16_t seal_lead(const Seal& s) { return s.glided ? rules::kSealPauseTicks : rules::kSealGlideTicks; }

// The bar moves to `power` (from where it is drawn now).
void seal_move(World& w, int power) {
  Seal& s = w.battle.seal;
  s.before = static_cast<uint16_t>(std::lround(seal_bar(w) * 10));
  s.power = static_cast<uint16_t>(std::clamp(power, 0, static_cast<int>(rules::kSealFull)));
  s.bar_tick = w.tick;
}

// A capture spell (Command::Cast while sealing).
void seal_cast(World& w, size_t slot) {
  Seal& s = w.battle.seal;
  if (slot >= kCaptureSlots || cannot_cast(w, slot)) return;
  const SpellId spell = w.capture_spells[slot];
  w.wizard.mana = static_cast<uint16_t>(w.wizard.mana - spell_cost(w, spell));
  switch (spell) {
    case SpellId::Replay:  // the same series again, from its first rune
      s.done = 0;
      s.hint = kNoRune;
      s.timer = 0;
      s.stage = SealStage::Show;
      break;
    case SpellId::Guard: s.guard = true; break;
    case SpellId::Boost: s.boost = true; break;
    case SpellId::NewOrder:
      ++s.round;
      seal_order(w);
      break;
    case SpellId::Steady: seal_move(w, rules::kSealSteadyPower); break;
    case SpellId::Hint: s.hint = s.order[s.done]; break;
    default: break;
  }
  w.spell_tick = w.tick;
  w.spell_cast = spell;
}

// The sealed monster broke free: it runs away from the companion.
void seal_escape(World& w, size_t k) {
  Battle& b = w.battle;
  Creature& c = w.creatures[b.foes[k].index];
  float dx = c.body.pos.x - companion(w).body.pos.x, dz = c.body.pos.z - companion(w).body.pos.z;
  const float l = std::sqrt(dx * dx + dz * dz);
  dx = l > 1e-3f ? dx / l : 0.0f;
  dz = l > 1e-3f ? dz / l : 1.0f;
  c.mind = Mind::Flee;
  c.timer = 5 * rules::kTicksPerSecond;
  c.target = {rules::clamp_world_x(c.body.pos.x + dx * 18, 1.0f), rules::clamp_world_z(c.body.pos.z + dz * 18, 1.0f)};
  drop_foe(w, k);
  if (b.foes.empty()) end_battle(w, BattleOutcome::Escaped, b.won_xp);
}

}  // namespace

void battle_command(World& w, const Input& in) {
  Battle& b = w.battle;
  switch (in.command) {
    case Command::None: break;
    case Command::Engage:
      if (can_engage(w, in.target) == EngageCheck::Ok) engage(w, in.target);
      break;
    case Command::Train: {
      if (!can_train(w)) break;
      b = fresh_battle(b, BattlePhase::Training);
      break;
    }
    case Command::Release:
      release(w, in.target);
      break;
    case Command::Teleport:
    case Command::QuestAccept:
    case Command::QuestGive:
    case Command::QuestComplete:
    case Command::UseItem:
    case Command::Discard:
    case Command::Buy:
    case Command::FarmSwap:
    case Command::Rune:  // seal_step
      break;  // quests.cpp, west.cpp
    case Command::Cast: {
      if (in.target >= current_spells(w).size() || cannot_cast(w, in.target)) break;
      const SpellId s = current_spells(w)[in.target];
      if (s == SpellId::Heal) {
        Monster& a = ally(w);
        if (a.hp < monster_fight_stats(a).hp) healed_monster(w);  // Odette's lesson
        a.hp = std::min<uint16_t>(static_cast<uint16_t>(a.hp + rules::kHealPoints), monster_fight_stats(a).hp);
        if (companion(w).mind == Mind::KnockedOut) companion(w).mind = Mind::Idle;  // it wakes up
      } else if (s == SpellId::Shield) {
        b.ally.shield_ticks = rules::kShieldTicks;
      } else if (s == SpellId::Mercy) {
        b.mercy = true;
      } else if (s == SpellId::Detect) {
        w.detect_ticks = rules::kDetectTicks;
      } else if (s == SpellId::WayOut) {
        w.way_out_ticks = rules::kWayOutTicks;
      } else if (s == SpellId::Home) {
        go_home(w);
      }
      w.wizard.mana = static_cast<uint16_t>(w.wizard.mana - spell_info(s).mana);
      w.spell_cooldowns[static_cast<size_t>(s)] = spell_info(s).cooldown;
      w.spell_tick = w.tick;
      w.spell_cast = s;
      break;
    }
    case Command::EquipSpell: {
      size_t slot = in.target / 16;
      const auto spell = static_cast<SpellId>(in.target % 16);
      if (b.phase != BattlePhase::None || slot >= kSpellSlotCount || spell >= SpellId::Count) break;
      const SpellInfo& info = spell_info(spell);
      std::span<SpellId> slots = w.combat_spells;
      bool fits = info.combat;
      if (slot >= kCombatSlots + kAdventureSlots) {
        slots = w.capture_spells;
        fits = info.capture;
        slot -= kCombatSlots + kAdventureSlots;
      } else if (slot >= kCombatSlots) {
        slots = w.adventure_spells;
        fits = info.adventure;
        slot -= kCombatSlots;
      }
      if (spell != SpellId::None && !fits) break;
      // Already in another slot: they swap.
      for (SpellId& other : slots) {
        if (spell != SpellId::None && other == spell) other = slots[slot];
      }
      slots[slot] = spell;
      break;
    }
    case Command::HealTeam:
    case Command::Door:
      if (in.target >= 256 || !can_heal_team(w, static_cast<uint8_t>(in.target))) break;
      for (Monster& m : w.team) m.hp = monster_fight_stats(m).hp;
      if (w.active >= 0 && companion(w).mind == Mind::KnockedOut) companion(w).mind = Mind::Idle;
      w.wizard.mana = wizard_stats(w.wizard).mana;  // and the magician's mana
      w.healed_tick = w.tick;
      break;
    case Command::Target:
      if ((b.phase == BattlePhase::Fight || training(w)) && (in.target == kNoTarget || in.target < foe_count(w))) {
        b.target = in.target;
      }
      break;
    case Command::Challenge:
      if (in.target < 256 && can_challenge(w, static_cast<uint8_t>(in.target))) {
        challenge(w, static_cast<uint8_t>(in.target));
      }
      break;
    case Command::Ready:
      if (b.phase != BattlePhase::Prepare) break;
      take_places(w);
      b.phase = BattlePhase::Fight;
      b.timer = 0;
      break;
    case Command::Flee:
      if (b.phase == BattlePhase::Training) {
        end_training(w);
        break;
      }
      if (b.rival.npc && (b.phase == BattlePhase::Fight || b.phase == BattlePhase::Prepare)) {
        end_battle(w, BattleOutcome::Forfeit, 0);  // the trainer wins
        break;
      }
      if (b.phase != BattlePhase::Fight) break;
      if (!active_monster(w)) {  // no one out to run back
        end_battle(w, BattleOutcome::Fled, b.won_xp);
        break;
      }
      b.phase = BattlePhase::Flee;
      b.timer = 0;
      b.projectiles.clear();
      b.ally = {};
      for (const Foe& f : b.foes) {
        w.creatures[f.index].mind = Mind::Idle;
        w.creatures[f.index].timer = 2 * rules::kTicksPerSecond;
      }
      b.foes.clear();
      break;
    case Command::Summon:
      summon(w, in.target);
      break;
    case Command::Capture:
      if (can_capture(w)) start_seal(w);
      break;
  }
}

bool sealing(const World& w) { return w.battle.seal.stage != SealStage::None; }

void seal_step(World& w, const Input& in) {
  Battle& b = w.battle;
  Seal& s = b.seal;
  const int k = foe_slot(w, s.foe);
  if (k < 0 || b.phase != BattlePhase::Fight) {  // cannot happen: let go
    s = {};
    return;
  }
  ++s.timer;
  if (in.command == Command::Cast) seal_cast(w, in.target);
  switch (s.stage) {
    case SealStage::Show:
      if (s.timer >= seal_lead(s) + s.length * rules::kSealRuneTicks) {
        s.stage = SealStage::Input;
        s.timer = 0;
        s.glided = true;
      }
      break;
    case SealStage::Input:
      if (in.command != Command::Rune || in.target >= s.runes) break;
      s.touched = static_cast<uint8_t>(in.target);
      s.touch_tick = w.tick;
      s.hint = kNoRune;
      if (in.target != s.order[s.done]) {
        if (!s.guard) seal_move(w, s.power - rules::kSealLoss);
        s.stage = SealStage::Wrong;
        s.timer = 0;
        break;
      }
      if (++s.done > s.credited) {  // not twice for the same rune (Replay)
        s.credited = s.done;
        seal_move(w, s.power + (s.boost ? s.gain * 3 / 2 : s.gain));
      }
      if (s.done == s.length || s.power >= rules::kSealFull) {
        s.stage = SealStage::Right;
        s.timer = 0;
      }
      break;
    case SealStage::Right:
    case SealStage::Wrong:
      if (s.timer < rules::kSealResultTicks) break;
      if (s.power >= rules::kSealFull) {
        s.stage = SealStage::Caught;
        s.timer = 0;
      } else if (s.power == 0) {
        s.stage = SealStage::Escaped;
        s.timer = 0;
      } else {
        ++s.round;
        seal_order(w);
      }
      break;
    case SealStage::Caught:
      if (s.timer >= rules::kSealEndTicks) {
        s = {};
        capture_foe(w, static_cast<size_t>(k));
      }
      break;
    case SealStage::Escaped:
      if (s.timer >= rules::kSealEndTicks) {
        s = {};
        seal_escape(w, static_cast<size_t>(k));
      }
      break;
    case SealStage::None: break;
  }
}

int seal_lit(const World& w, float* glow) {
  const Seal& s = w.battle.seal;
  if (s.stage != SealStage::Show) return -1;
  const uint16_t lead = seal_lead(s);
  if (s.timer < lead) return -1;
  const uint16_t t = static_cast<uint16_t>(s.timer - lead);
  const size_t i = t / rules::kSealRuneTicks;
  const uint16_t in = static_cast<uint16_t>(t % rules::kSealRuneTicks);
  if (i >= s.length || in >= rules::kSealLitTicks) return -1;
  if (glow) *glow = static_cast<float>(in) / rules::kSealLitTicks;
  return s.order[i];
}

float seal_bar(const World& w) {
  const Seal& s = w.battle.seal;
  const float t = std::min(1.0f, static_cast<float>(w.tick - s.bar_tick) / rules::kSealBarTicks);
  const float e = t * t * (3 - 2 * t);
  return (static_cast<float>(s.before) + (static_cast<float>(s.power) - s.before) * e) / 10;
}

Steer battle_ally(World& w, const Input& in, float dir_x, float dir_z) {
  Fighter& f = w.battle.ally;
  count_down(f);
  float look_x, look_z;
  camera_forward(in, look_x, look_z);
  if (f.state != FighterState::Ready && f.state_ticks > 0 && --f.state_ticks == 0) {
    if (f.state == FighterState::Windup) ally_attack(w, f.move, look_x, look_z);  // the burst has gathered
    else f.state = FighterState::Ready;
  }

  // A flying companion climbs while the jump is held, and slowly comes down.
  if (flies(companion(w).species)) {
    f.lift = in.jump_held ? std::min(f.lift + 3.0f * rules::kDt, rules::kFlyLiftMax)
                          : std::max(f.lift - 2.0f * rules::kDt, 0.0f);
  }
  const bool flies = companion_hover(companion(w).species) > 0;
  const bool moving = dir_x * dir_x + dir_z * dir_z > 0.01f;
  bool jump = false;
  if (in.jump && f.dodge_cooldown == 0 && f.state != FighterState::Dodge && f.state != FighterState::Stunned &&
      (moving || !flies)) {
    f.state = FighterState::Dodge;
    f.state_ticks = rules::kDodgeTicks;
    f.dodge_cooldown = rules::kDodgeTicks + rules::kDodgeCooldown;
    const float len = std::sqrt(dir_x * dir_x + dir_z * dir_z);
    f.dodge_x = moving ? dir_x / len : 0;  // standing still: a jump on the spot
    f.dodge_z = moving ? dir_z / len : 0;
    jump = !moving;
  } else if (in.parry && f.parry_cooldown == 0 && free_to_act(f)) {
    f.state = FighterState::Parry;
    f.state_ticks = rules::kParryTicks;
    f.parry_cooldown = rules::kParryTicks + rules::kParryCooldown;
  } else if (in.attack >= 1 && in.attack <= 3 && free_to_act(f) && f.cooldown[in.attack - 1u] == 0) {
    const uint8_t slot = static_cast<uint8_t>(in.attack - 1);
    const Move& m = species_moves(ally(w).species)[slot];
    if (m.shape == MoveShape::Burst) {  // it gathers first
      f.state = FighterState::Windup;
      f.move = slot;
      f.state_ticks = rules::kBurstCharge;
      f.cooldown[slot] = m.cooldown;
    } else {
      ally_attack(w, slot, look_x, look_z);
    }
  }

  Steer s;
  s.jump = jump;
  const bool dashing = f.dodge_x != 0 || f.dodge_z != 0;
  if (f.state == FighterState::Dodge && dashing) {
    s.dir_x = f.dodge_x;
    s.dir_z = f.dodge_z;
    s.speed = rules::kDodgeSpeed;
  } else if (f.state == FighterState::Dodge) {
    // Jumping on the spot.
  } else if (f.state != FighterState::Stunned) {
    s.dir_x = dir_x;
    s.dir_z = dir_z;
    // Monsters do not walk through each other: against a foe, only sideways or back.
    const Side me = ally_side(w);
    for (size_t k = 0; k < foe_count(w); ++k) {
      const Side other = foe_side(w, k);
      const float touching = me.shape.half_width + other.shape.half_width + 0.1f;
      float to_x, to_z;
      if (flat_distance(*me.body, *other.body, to_x, to_z) > touching || vertical_gap(me, other) > 0) continue;
      const float towards = s.dir_x * to_x + s.dir_z * to_z;
      if (towards > 0) {
        s.dir_x -= to_x * towards;
        s.dir_z -= to_z * towards;
      }
    }
    const bool slowed = f.state == FighterState::Parry || f.state == FighterState::Windup;
    s.speed = std::max(species_info(companion(w).species).run_speed, rules::kCompanionMinRunSpeed) *
              rules::kBattlePace * (slowed ? 0.3f : 1.0f);
  }
  return s;
}

// A foe's mind: it attacks when it can; else it circles the companion,
// closing in when its bite is ready, keeping away while it is not; it dodges
// what is thrown at it, now and then. Each species in its own way (Temper):
// a slime stays close and leaps at its foe, a mushroom keeps away and throws
// its spores, a bird bites and runs, a crow harasses from above and swoops
// down to bite, a fish holds its place.
Steer battle_foe(World& w, size_t k) {
  Battle& b = w.battle;
  Fighter& f = b.foes[k].fighter;
  count_down(f);
  Creature& e = foe_creature(w, k);
  const SpeciesInfo& info = species_info(e.species);
  const Moves& moves = species_moves(e.species);
  const Temper& temper = species_temper(e.species);
  const bool stays = holds_its_place(e.species);  // a fish in the lake
  Steer s;
  s.fly = flies(e.species);
  s.lift = temper.lift;
  if (!active_monster(w) || b.phase != BattlePhase::Fight) return s;  // no one to fight: it waits

  const Side me = foe_side(w, k), other = ally_side(w);
  float dx, dz;
  const float d = flat_distance(e.body, companion(w).body, dx, dz);
  float ax, ay, az;
  const float d3 = aim(me, other, ax, ay, az);
  const float reach = info.shape.half_width + other.shape.half_width + moves[0].range;
  const bool bite = d <= reach && vertical_gap(me, other) <= kMeleeHeight;
  if (s.fly && f.cooldown[0] < 30 && d < reach + 2.5f) s.lift = kSwoopLift;

  switch (f.state) {
    case FighterState::Windup:
      if (--f.state_ticks == 0) foe_attack(w, k, f.move);
      return s;
    case FighterState::Dodge:
      if (f.state_ticks == 0 || --f.state_ticks == 0) f.state = FighterState::Ready;
      s.dir_x = f.dodge_x;
      s.dir_z = f.dodge_z;
      s.speed = rules::kDodgeSpeed;
      return s;
    case FighterState::Ready:
      break;
    default:  // recovering or stunned
      if (f.state_ticks == 0 || --f.state_ticks == 0) f.state = FighterState::Ready;
      return s;
  }

  // Something thrown at it: now and then, it jumps aside.
  if (!stays && f.dodge_cooldown == 0) {
    for (const monstres::Projectile& p : b.projectiles) {
      if (!p.from_ally) continue;
      const float px = e.body.pos.x - p.pos.x, pz = e.body.pos.z - p.pos.z;
      const float pd = std::sqrt(px * px + pz * pz);
      const float flat = std::sqrt(p.dir_x * p.dir_x + p.dir_z * p.dir_z);
      if (pd > 5.0f || pd < 0.4f || flat < 0.2f || (px * p.dir_x + pz * p.dir_z) / (pd * flat) < 0.85f) continue;
      if (w.rng.below(temper.dodge_odds) != 0) break;
      const float side = f.strafe < 0 ? -1.0f : 1.0f;
      f.state = FighterState::Dodge;
      f.state_ticks = rules::kDodgeTicks;
      f.dodge_cooldown = rules::kDodgeTicks + rules::kDodgeCooldown;
      f.dodge_x = -p.dir_z / flat * side;
      f.dodge_z = p.dir_x / flat * side;
      s.dir_x = f.dodge_x;
      s.dir_z = f.dodge_z;
      s.speed = rules::kDodgeSpeed;
      return s;
    }
  }

  const auto windup = [&](uint8_t slot) {
    f.state = FighterState::Windup;
    f.move = slot;
    f.state_ticks = kFoeWindup[slot];
    f.cooldown[slot] = moves[slot].cooldown;  // the same delays as the companion's
  };
  if (bite && f.cooldown[0] == 0) {
    windup(0);
    return s;
  }
  if (d3 <= moves[2].range && f.cooldown[2] == 0) {
    windup(2);
    return s;
  }
  if ((d > temper.throw_from || stays) && d3 < moves[1].range && f.cooldown[1] == 0) {
    windup(1);
    return s;
  }
  if (stays) return s;

  // It changes its mind now and then: which way it circles, or it holds still.
  if (f.think > 0) {
    --f.think;
  } else {
    const uint32_t roll = w.rng.below(5);
    f.strafe = static_cast<int8_t>(roll < 2 ? 1 : roll < 4 ? -1 : 0);
    f.think = static_cast<uint16_t>(rules::kTicksPerSecond / 2 + w.rng.below(2 * rules::kTicksPerSecond));
  }
  // A mushroom never comes to bite: it keeps away (and bites what comes close).
  const bool bites = f.cooldown[0] < 20 && e.species != Species::Mushroom;
  const float wanted = bites ? reach * temper.close_in : temper.keep_away;
  const float closer = std::clamp((d - wanted) / 1.5f, -1.0f, 1.0f);
  const float around = 0.8f * static_cast<float>(f.strafe);
  float mx = dx * closer - dz * around;
  float mz = dz * closer + dx * around;
  // Not on top of the other foes.
  for (size_t j = 0; j < b.foes.size(); ++j) {
    if (j == k) continue;
    float ox, oz;
    const float od = flat_distance(foe_creature(w, j).body, e.body, ox, oz);
    if (od < 1.6f) {
      mx += ox * (1.6f - od);
      mz += oz * (1.6f - od);
    }
  }
  const float len = std::sqrt(mx * mx + mz * mz);
  if (len < 0.15f) return s;
  const float pace = std::min(len, 1.0f);
  s.dir_x = mx / len;
  s.dir_z = mz / len;
  s.speed = std::max(info.walk_speed, info.run_speed * 0.6f) * pace * temper.pace * rules::kBattlePace;
  if (e.species == Species::Slime) {  // slimes hop: they only move in the air
    s.jump = e.body.on_ground;
    if (e.body.on_ground) s.speed = 0;
    // Its bite ready and its foe near: it leaps at it.
    else if (bites && d < 3.5f && closer > 0) s.speed *= kLeapSpeed;
  }
  return s;
}

Steer battle_prepare(World& w, Stand who) {
  Steer s;
  const Rival& r = w.battle.rival;
  if (!r.npc || (w.battle.phase != BattlePhase::Prepare && who != Stand::Trainer)) return s;
  const Body* body = nullptr;
  Vec2 spot;
  float speed = 0;
  switch (who) {
    case Stand::Magician:
      body = &w.player;
      spot = r.player_spot;
      speed = rules::kWalkSpeed * 1.2f;
      break;
    case Stand::Ally:
      if (w.active < 0) return s;
      body = &companion(w).body;
      spot = r.ally_spot;
      speed = std::max(species_info(companion(w).species).run_speed, rules::kCompanionMinRunSpeed);
      break;
    case Stand::Foe: {
      const int i = rival_index(w);
      if (i <= 0) return s;
      body = &w.creatures[static_cast<size_t>(i)].body;
      spot = r.foe_spot;
      speed = std::max(species_info(w.creatures[static_cast<size_t>(i)].species).run_speed, 3.0f);
      break;
    }
    case Stand::Trainer: {
      const int t = npc_index(w, r.npc);
      if (t <= 0) return s;
      body = &w.creatures[static_cast<size_t>(t)].body;
      spot = r.trainer_spot;
      speed = 3.5f;
      break;
    }
  }
  const float x = spot.x - body->pos.x, z = spot.z - body->pos.z;
  const float d = std::sqrt(x * x + z * z);
  if (d < 0.2f) return s;
  s.dir_x = x / d;
  s.dir_z = z / d;
  s.speed = std::min(speed, d * static_cast<float>(rules::kTicksPerSecond));
  return s;
}

void battle_resolve(World& w) {
  Battle& b = w.battle;
  if (b.phase == BattlePhase::None) return;
  Creature& ally_c = companion(w);
  if (b.phase == BattlePhase::Flee) {
    float dx, dz;
    const bool back = flat_distance(ally_c.body, w.player, dx, dz) <= rules::kFleeArrival;
    if (back || ++b.timer >= rules::kFleeMaxTicks) end_battle(w, BattleOutcome::Fled, b.won_xp);
    return;
  }
  if (b.phase == BattlePhase::Prepare) {
    // Taking their places; after a while, everyone is there at once.
    if (++b.timer >= rules::kPrepareMaxTicks) {
      take_places(w);
      b.timer = 0;
    }
    return;
  }

  // Projectiles fly straight, and stop on what they hit.
  const bool has_ally = active_monster(w) != nullptr;
  std::vector<size_t> perches{standing_on(w, ally_c.body)};
  for (size_t k = 0; k < foe_count(w); ++k) perches.push_back(standing_on(w, *foe_side(w, k).body));
  for (size_t i = 0; i < b.projectiles.size();) {
    monstres::Projectile& p = b.projectiles[i];
    p.pos.x += p.dir_x * rules::kProjectileSpeed * rules::kDt;
    p.pos.y += p.dir_y * rules::kProjectileSpeed * rules::kDt;
    p.pos.z += p.dir_z * rules::kProjectileSpeed * rules::kDt;
    bool gone = p.ticks == 0 || --p.ticks == 0 || p.pos.y < w.ground.height(p.pos.x, p.pos.z);
    // Training, a wind monster's throw into the mill's sails: they turn again.
    if (!gone && training(w) && p.from_ally && has_ally) {
      const Elements e = creature_elements(ally_c.species, ally_c.genes);
      if ((e.first == Element::Air || e.second == Element::Air) && push_sails(w, p.pos)) {
        b.impacts.push_back({p.pos, Element::Air, w.tick});
        b.projectiles.erase(b.projectiles.begin() + static_cast<std::ptrdiff_t>(i));
        continue;
      }
    }
    const auto touches = [&](const Side& t) {
      const float tx = t.body->pos.x - p.pos.x, tz = t.body->pos.z - p.pos.z;
      const float r = t.shape.half_width + 0.3f;
      return tx * tx + tz * tz <= r * r && p.pos.y >= t.body->pos.y - 0.4f &&
             p.pos.y <= t.body->pos.y + t.shape.height + 0.4f;
    };
    if (!gone && p.from_ally) {
      for (size_t k = 0; k < foe_count(w) && !gone; ++k) {
        if (!touches(foe_side(w, k)) || foe_fighter(w, k).state == FighterState::Dodge) continue;  // a dodged one flies on
        hit(w, false, k, p.damage, p.element, false);
        gone = true;
      }
    } else if (!gone && has_ally && touches(ally_side(w)) && b.ally.state != FighterState::Dodge) {
      hit(w, true, 0, p.damage, p.element, false);
      gone = true;
    }
    // Objects stop it, but not what a fighter stands on (the roof of a crow).
    if (!gone) {
      const Box box{{p.pos.x - 0.1f, p.pos.y - 0.1f, p.pos.z - 0.1f}, {p.pos.x + 0.1f, p.pos.y + 0.1f, p.pos.z + 0.1f}};
      for_objects_near(w, box, [&](uint32_t k, const Box& o) {
        gone = gone || (std::find(perches.begin(), perches.end(), k) == perches.end() && overlaps(box, o));
      });
    }
    if (gone) {
      if (p.ticks > 0) b.impacts.push_back({p.pos, p.element, w.tick});  // stopped by something (not faded)
      b.projectiles.erase(b.projectiles.begin() + static_cast<std::ptrdiff_t>(i));
    } else {
      ++i;
    }
  }
  std::erase_if(b.impacts, [&](const Impact& m) { return w.tick - m.tick > rules::kImpactTicks; });

  // The companion faces its target (moving freely: where it goes); the foes face the companion.
  if (has_ally) {
    float dx, dz;
    if (b.target < foe_count(w) && b.ally.state != FighterState::Dodge &&
        flat_distance(ally_c.body, *foe_side(w, b.target).body, dx, dz) > 1e-3f) {
      ally_c.body.facing_x = dx;
      ally_c.body.facing_z = dz;
    } else if (b.target >= foe_count(w) && b.ally.attack_tick > 0 && w.tick - b.ally.attack_tick < kAimHold &&
               b.ally.attack_move != 2 && (b.ally.aim_x != 0 || b.ally.aim_z != 0)) {
      ally_c.body.facing_x = b.ally.aim_x;  // moving freely: it keeps facing where it attacked for a moment
      ally_c.body.facing_z = b.ally.aim_z;
    }
    for (size_t k = 0; k < foe_count(w); ++k) {
      Body& fb = *foe_side(w, k).body;
      if (flat_distance(fb, ally_c.body, dx, dz) > 1e-3f && (training(w) || b.foes[k].fighter.state != FighterState::Dodge)) {
        fb.facing_x = dx;
        fb.facing_z = dz;
      }
    }
  }

  if (training(w)) {  // the magician keeps its shield up: no one wins
    float dx, dz;
    const bool away = flat_distance(ally_c.body, w.player, dx, dz) > rules::kBattleLeash;
    b.away_ticks = away ? static_cast<uint16_t>(b.away_ticks + 1) : 0;
    if (b.away_ticks >= rules::kAwayTicks) end_training(w);
    return;
  }

  // The beaten ones lie there.
  for (size_t k = b.foes.size(); k-- > 0;) {
    if (b.foes[k].hp == 0) beat(w, k);
  }
  // A trainer sends its next monster.
  Rival& r = b.rival;
  const bool rival_left = std::any_of(r.team.begin(), r.team.begin() + r.count,
                                      [](const RivalMonster& m) { return m.hp > 0; });
  if (r.npc && b.foes.empty() && rival_left && r.next_in > 0 && --r.next_in == 0) send_next(w);

  if (b.foes.empty() && !(r.npc && rival_left)) {
    uint32_t xp = b.won_xp;
    if (r.npc) {  // only the first victory against a trainer gives experience (and coins)
      uint32_t levels = 0;
      for (uint8_t k = 0; k < r.count; ++k) levels += r.team[k].level;
      uint8_t passing = 0;  // a trainer passing by: met once
      uint8_t& wins = npc::wanderer(r.npc) ? passing : r.npc == npc::kRomane ? w.romane_wins : w.rival_wins;
      xp = wins == 0 ? rules::kRivalXpPerLevel * levels : 0;
      if (xp == 0) b.no_xp = NoXp::Trainer;
      if (wins == 0) w.bag[static_cast<size_t>(Item::Coin)] += 4 * levels;
      if (wins < 255) ++wins;
      if (const int t = npc_index(w, r.npc); npc::wanderer(r.npc) && t > 0) {  // beaten: it offers no more
        w.creatures[static_cast<size_t>(t)].beaten = true;
        w.creatures[static_cast<size_t>(t)].tag = Tag::None;
      }
    } else {
      w.bag[static_cast<size_t>(Item::Coin)] += (b.won_xp + 3) / 4;  // what the beaten ones dropped
    }
    ++w.battles_won;
    end_battle(w, BattleOutcome::Victory, xp);
    return;
  }
  if (Monster* a = active_monster(w); a && a->hp == 0) {
    if (std::any_of(w.team.begin(), w.team.end(), [](const Monster& m) { return m.hp > 0; })) {
      // Knocked out: back into the crystal; the player sends another one.
      w.went_in = {w.tick, false, a->species, a->genes, ally_c.body};
      w.active = -1;
      ally_c.mind = Mind::Idle;
      b.ally = {};
      b.away_ticks = 0;
      return;
    }
    // None left: the battle is lost; the last one lies there, waiting for a heal.
    ally_c.mind = Mind::KnockedOut;
    ally_c.timer = 0;
    ally_c.body.speed = 0;
    end_battle(w, BattleOutcome::Defeat, r.npc ? 0 : b.won_xp);
    return;
  }
  if (!r.npc && has_ally && !b.foes.empty()) {
    // Too far from all of them: the battle ends, after a while to come back.
    float nearest = 1e9f;
    for (size_t k = 0; k < b.foes.size(); ++k) {
      float dx, dz;
      nearest = std::min(nearest, flat_distance(ally_c.body, foe_creature(w, k).body, dx, dz));
    }
    b.away_ticks = nearest > rules::kBattleLeash ? static_cast<uint16_t>(b.away_ticks + 1) : 0;
    if (b.away_ticks >= rules::kAwayTicks) end_battle(w, BattleOutcome::Fled, b.won_xp);
  }
}

void spells_step(World& w) {
  for (uint16_t& c : w.spell_cooldowns) c = c > 0 ? static_cast<uint16_t>(c - 1) : 0;
  if (w.detect_ticks > 0) --w.detect_ticks;
  if (w.way_out_ticks > 0) --w.way_out_ticks;
}

}  // namespace monstres
