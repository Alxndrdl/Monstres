#include "monstres/core/battle.hpp"

#include "monstres/core/variants.hpp"

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
};
static_assert(std::size(kMoves) == kSpeciesCount);

// Wild monsters attack less often than the player can, and show it first.
constexpr uint16_t foe_cooldown(const Move& m, size_t slot) {
  return slot == 0 ? 75 : static_cast<uint16_t>(m.cooldown + m.cooldown / (slot == 1 ? 3 : 10));
}
constexpr uint16_t kFoeWindup[] = {28, 36, 45};

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
// companion is its enemy, or the magician when training.
struct Side {
  Body* body;
  BodyShape shape;
};
Side ally_side(World& w) { return {&w.creatures[0].body, species_info(w.creatures[0].species).shape}; }
Side foe_side(World& w) {
  if (training(w)) return {&w.player, kPlayerShape};
  Creature& e = w.creatures[w.battle.enemy];
  return {&e.body, species_info(e.species).shape};
}

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
  for (size_t k = 0; k < w.objects.size(); ++k) {
    const Box b = object_box(w.objects[k]);
    if (std::fabs(body.pos.y - b.max.y) < kNear && body.pos.x > b.min.x - kNear && body.pos.x < b.max.x + kNear &&
        body.pos.z > b.min.z - kNear && body.pos.z < b.max.z + kNear) {
      return k;
    }
  }
  return SIZE_MAX;
}

constexpr float kMeleeHeight = 0.6f;  // highest gap a bite still reaches
Monster& ally(World& w) { return *active_monster(w); }  // in a battle, there is one
Creature& enemy(World& w) { return w.creatures[w.battle.enemy]; }

FightStats ally_stats(const World& w) {
  const Monster& m = *active_monster(w);
  return monster_fight_stats(m.species, m.level);
}
FightStats foe_stats(const World& w) {
  if (training(w)) return ally_stats(w);  // damage shown as against a monster like itself
  const Creature& e = w.creatures[w.battle.enemy];
  return monster_fight_stats(e.species, e.level);
}

Element move_element(const Move& m, Species species, const Genes& genes) {
  return m.elemental ? creature_elements(species, genes).first : Element::Normal;
}

}  // namespace

void grant_monster_xp(Monster& m, uint32_t xp) {
  m.xp += xp;
  while (m.level < 100 && m.xp >= monster_xp_to_next(m.level)) {
    m.xp -= monster_xp_to_next(m.level);
    const uint16_t before = monster_fight_stats(m.species, m.level).hp;
    ++m.level;
    m.hp = static_cast<uint16_t>(m.hp + monster_fight_stats(m.species, m.level).hp - before);
  }
}

void grant_wizard_xp(Wizard& wiz, uint32_t xp) {
  wiz.xp += xp;
  while (wiz.level < 100 && wiz.xp >= wizard_stats(wiz).xp_to_next) {
    wiz.xp -= wizard_stats(wiz).xp_to_next;
    ++wiz.level;
  }
}

namespace {

// Ends the battle. `xp`: the experience of a victory, shared by the team
// members by the hit points each dealt (xp * dealt / (1 + all dealt)).
void end_battle(World& w, BattleOutcome outcome, uint32_t xp) {
  Battle& b = w.battle;
  b.outcome_species = enemy(w).species;
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
  b.phase = BattlePhase::None;
  b.projectiles.clear();
  b.ally = {};
  b.foe = {};
  b.timer = 0;
  b.dealt = {};
  b.outcome = outcome;
  b.outcome_tick = w.tick;
  b.outcome_xp = xp;
}

// A monster beaten once, or let go by the player, gives nothing when beaten or caught again.
uint32_t reward(World& w) { return enemy(w).released || enemy(w).beaten ? 0u : 8u * enemy(w).level; }

void end_training(World& w) {
  Battle& b = w.battle;
  b.phase = BattlePhase::None;
  b.projectiles.clear();
  b.ally = {};
  b.foe = {};
  b.timer = 0;
}

// A hit on one side. Returns whether it landed.
bool hit(World& w, bool on_ally, uint16_t damage, bool melee) {
  Battle& b = w.battle;
  Fighter& target = on_ally ? b.ally : b.foe;
  if (target.state == FighterState::Dodge) return false;
  if (!on_ally && training(w)) {  // the magician's shield takes it (shows what it would have done)
    target.parried_tick = w.tick;
    target.last_damage = damage;
    return false;
  }
  if (target.state == FighterState::Parry) {
    target.parried_tick = w.tick;
    if (melee) {
      Fighter& attacker = on_ally ? b.foe : b.ally;
      attacker.state = FighterState::Stunned;
      attacker.state_ticks = rules::kStunTicks;
    }
    return false;
  }
  uint16_t& hp = on_ally ? ally(w).hp : b.enemy_hp;
  const uint16_t lost = std::min(hp, damage);
  hp = static_cast<uint16_t>(hp - lost);
  if (!on_ally && w.active >= 0 && static_cast<size_t>(w.active) < b.dealt.size()) {
    b.dealt[static_cast<size_t>(w.active)] += lost;
  }
  target.hit_tick = w.tick;
  target.last_damage = damage;
  return true;
}

void attack(World& w, bool by_ally, uint8_t slot) {
  Battle& b = w.battle;
  Fighter& f = by_ally ? b.ally : b.foe;
  const Side self = by_ally ? ally_side(w) : foe_side(w);
  const Side other = by_ally ? foe_side(w) : ally_side(w);
  const Species species = by_ally ? ally(w).species : enemy(w).species;
  const Genes& genes = by_ally ? ally(w).genes : enemy(w).genes;
  const Move& m = species_moves(species)[slot];
  f.cooldown[slot] = by_ally ? m.cooldown : foe_cooldown(m, slot);
  f.attack_tick = w.tick;
  f.attack_move = slot;
  f.state = FighterState::Recover;
  f.state_ticks = by_ally ? 8 : 30;

  const FightStats atk = by_ally ? ally_stats(w) : foe_stats(w);
  const FightStats def = by_ally ? foe_stats(w) : ally_stats(w);
  const uint16_t damage = move_damage(m, atk, def);
  const float self_w = self.shape.half_width;
  const float other_w = other.shape.half_width;
  float dx, dz;
  const float d = flat_distance(*self.body, *other.body, dx, dz);
  float ax, ay, az;
  const float d3 = aim(self, other, ax, ay, az);
  switch (m.shape) {
    case Melee:
      if (d <= self_w + other_w + m.range && vertical_gap(self, other) <= kMeleeHeight) hit(w, !by_ally, damage, true);
      break;
    case Burst:
      if (d3 <= m.range + other_w) hit(w, !by_ally, damage, false);
      break;
    case Projectile: {
      // Thrown at the middle of the target, even up on a roof or down in the water.
      monstres::Projectile p;
      p.pos = {self.body->pos.x + dx * self_w, self.body->pos.y + self.shape.height * 0.5f,
               self.body->pos.z + dz * self_w};
      p.dir_x = ax;
      p.dir_y = ay;
      p.dir_z = az;
      p.ticks = static_cast<uint16_t>(m.range / rules::kProjectileSpeed * rules::kTicksPerSecond);
      p.from_ally = by_ally;
      p.move = slot;
      p.element = move_element(m, species, genes);
      p.damage = damage;
      b.projectiles.push_back(p);
      break;
    }
  }
}

void count_down(Fighter& f) {
  for (uint16_t& c : f.cooldown) c = c > 0 ? static_cast<uint16_t>(c - 1) : 0;
  if (f.dodge_cooldown > 0) --f.dodge_cooldown;
  if (f.parry_cooldown > 0) --f.parry_cooldown;
}

bool free_to_act(const Fighter& f) { return f.state == FighterState::Ready || f.state == FighterState::Recover; }

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
  const bool blocked = in_lake(w.village.lake, b.pos.x, b.pos.z) ||
                       std::any_of(w.objects.begin(), w.objects.end(),
                                   [&](const Object& o) { return overlaps(box, object_box(o)); });
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
  const float limit = rules::kWorldHalfSize - 2.0f;
  c.home = {std::clamp(c.body.pos.x + ax * 10, -limit, limit), std::clamp(c.body.pos.z + az * 10, -limit, limit)};
  c.target = c.home;
  c.mind = c.species == Species::Crow ? Mind::Fly : Mind::Flee;
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
  w.released_tick = beam ? w.tick : 0;  // out of the crystal: a beam of light
  w.team.erase(w.team.begin() + static_cast<std::ptrdiff_t>(slot));
  if (w.active > static_cast<int>(slot)) --w.active;
}

}  // namespace

void summon(World& w, size_t slot) {
  if (slot >= w.team.size() || w.creatures.empty()) return;
  Creature& c = w.creatures[0];
  const bool fighting = w.battle.phase == BattlePhase::Fight;
  const bool trains = training(w);
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
  c.mind = Mind::Idle;
  c.timer = 0;
  c.body.velocity_y = 0;
  w.came_out_tick = w.tick;
  if (fighting || trains) w.battle.ally = {};  // a fresh fighter
}

bool holds_its_place(Species species) { return species == Species::Crow || species == Species::Fish; }

const Moves& species_moves(Species species) {
  const auto i = static_cast<size_t>(species);
  return kMoves[i < kSpeciesCount ? i : 0];
}

FightStats monster_fight_stats(Species species, uint16_t level) {
  const MonsterStats& s = monster_stats(species);
  const uint32_t l = std::max<uint16_t>(level, 1) - 1u;
  const auto grow = [&](uint16_t base, uint32_t per) { return static_cast<uint16_t>(base + base * l / per); };
  return {grow(s.hp, 8), grow(s.attack, 10), grow(s.defense, 10), grow(s.speed, 10), grow(s.magic, 10)};
}

uint32_t monster_xp_to_next(uint16_t level) { return 20u * std::max<uint16_t>(level, 1); }

uint16_t move_damage(const Move& m, const FightStats& attacker, const FightStats& defender) {
  const uint32_t atk = m.shape == Melee ? attacker.attack : attacker.magic;
  const uint32_t d = uint32_t{m.power} * atk * 2u / (defender.defense + 10u);
  return static_cast<uint16_t>(std::clamp<uint32_t>(d, 1, 9999));
}

Monster starting_companion(const Genes& genes) {
  Monster m;
  m.species = Species::Companion;
  m.genes = genes;
  m.level = rules::kCompanionStartLevel;
  m.hp = monster_fight_stats(m.species, m.level).hp;
  return m;
}

EngageCheck can_engage(const World& w, size_t index) {
  if (index == 0 || index >= w.creatures.size()) return EngageCheck::NotMonster;
  const Creature& c = w.creatures[index];
  if (!monster_stats(c.species).is_monster) return EngageCheck::NotMonster;
  if (w.battle.phase != BattlePhase::None) return EngageCheck::InBattle;
  if (!active_monster(w)) return EngageCheck::NoCompanion;
  if (c.mind == Mind::KnockedOut) return EngageCheck::KnockedOut;
  if (c.tag == Tag::LostPiou && w.quests[static_cast<size_t>(QuestId::LostPiou)].stage == QuestStage::Done) {
    return EngageCheck::Owned;
  }
  const float dx = c.body.pos.x - w.player.pos.x, dz = c.body.pos.z - w.player.pos.z;
  if (dx * dx + dz * dz > rules::kEngageDistance * rules::kEngageDistance) return EngageCheck::TooFar;
  return EngageCheck::Ok;
}

const char* engage_check_name(EngageCheck check) {
  static const char* kNames[] = {"ok", "notMonster", "tooFar", "inBattle", "noCompanion", "knockedOut", "owned"};
  const auto i = static_cast<size_t>(check);
  return i < std::size(kNames) ? kNames[i] : "?";
}

bool can_capture(const World& w) {
  const Battle& b = w.battle;
  return b.phase == BattlePhase::Fight && b.enemy_hp > 0 && w.team.size() < rules::kTeamSize &&
         uint32_t{b.enemy_hp} * 100u <= uint32_t{b.enemy_max_hp} * rules::kCaptureHpPercent;
}

bool battle_forward(const World& w, float& fx, float& fz) {
  if (w.battle.phase == BattlePhase::None || w.creatures.empty()) return false;
  flat_distance(w.creatures[0].body, training(w) ? w.player : w.creatures[w.battle.enemy].body, fx, fz);
  return true;
}

bool can_train(const World& w) { return w.battle.phase == BattlePhase::None && active_monster(w) != nullptr; }

void battle_command(World& w, const Input& in) {
  Battle& b = w.battle;
  switch (in.command) {
    case Command::None: break;
    case Command::Engage: {
      if (can_engage(w, in.target) != EngageCheck::Ok) break;
      Battle fresh;
      fresh.phase = BattlePhase::Fight;
      fresh.enemy = in.target;
      Creature& e = w.creatures[in.target];
      fresh.enemy_max_hp = fresh.enemy_hp = monster_fight_stats(e.species, e.level).hp;
      fresh.foe.cooldown = {40, 120, 240};  // it takes a moment to start
      ally(w).hp = std::max<uint16_t>(ally(w).hp, 1);
      b = fresh;
      e.mind = Mind::Battle;
      e.timer = 0;
      break;
    }
    case Command::Train: {
      if (!can_train(w)) break;
      Battle fresh;
      fresh.phase = BattlePhase::Training;
      fresh.outcome = b.outcome;  // the last battle's, for the interface
      fresh.outcome_tick = b.outcome_tick;
      fresh.outcome_xp = b.outcome_xp;
      fresh.outcome_species = b.outcome_species;
      fresh.outcome_gains = b.outcome_gains;
      fresh.outcome_wizard_xp = b.outcome_wizard_xp;
      b = fresh;
      break;
    }
    case Command::Release:
      release(w, in.target);
      break;
    case Command::Teleport:
    case Command::QuestAccept:
    case Command::QuestGive:
    case Command::QuestComplete:
      break;  // quests.cpp
    case Command::Flee:
      if (b.phase == BattlePhase::Training) {
        end_training(w);
        break;
      }
      if (b.phase != BattlePhase::Fight) break;
      b.phase = BattlePhase::Flee;
      b.timer = 0;
      b.projectiles.clear();
      b.ally = {};
      b.foe = {};
      enemy(w).mind = Mind::Idle;
      enemy(w).timer = 2 * rules::kTicksPerSecond;
      break;
    case Command::Summon:
      summon(w, in.target);
      break;
    case Command::Capture: {
      if (!can_capture(w)) break;
      const size_t index = b.enemy;
      const Creature& e = w.creatures[index];
      Monster m;
      m.species = e.species;
      m.genes = e.genes;
      m.level = e.level;
      m.hp = b.enemy_hp;
      m.tag = e.tag;
      m.beaten = e.beaten || e.released;
      capture(w, index);
      w.went_in = {w.tick, true, e.species, e.genes, e.body};  // runes, then into the crystal
      end_battle(w, BattleOutcome::Captured, reward(w));
      w.team.push_back(m);
      remove_creature(w, index);
      break;
    }
  }
}

Steer battle_ally(World& w, const Input& in, float dir_x, float dir_z) {
  Fighter& f = w.battle.ally;
  count_down(f);
  if (f.state != FighterState::Ready && f.state_ticks > 0 && --f.state_ticks == 0) f.state = FighterState::Ready;

  float fx = 0, fz = -1;
  battle_forward(w, fx, fz);
  const bool moving = dir_x * dir_x + dir_z * dir_z > 0.01f;
  bool jump = false;
  if (in.jump && f.dodge_cooldown == 0 && f.state != FighterState::Dodge && f.state != FighterState::Stunned) {
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
    attack(w, true, static_cast<uint8_t>(in.attack - 1));
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
    // Monsters do not walk through each other: against the enemy, only sideways or back.
    const Side me = ally_side(w), other = foe_side(w);
    const float touching = me.shape.half_width + other.shape.half_width + 0.1f;
    float to_x, to_z;
    const float towards = dir_x * fx + dir_z * fz;
    if (flat_distance(*me.body, *other.body, to_x, to_z) <= touching && towards > 0) {
      s.dir_x -= fx * towards;
      s.dir_z -= fz * towards;
    }
    s.speed = std::max(species_info(companion(w).species).run_speed, rules::kCompanionMinRunSpeed) *
              (f.state == FighterState::Parry ? 0.3f : 1.0f);
  }
  return s;
}

Steer battle_foe(World& w) {
  Battle& b = w.battle;
  Fighter& f = b.foe;
  count_down(f);
  Creature& e = enemy(w);
  const SpeciesInfo& info = species_info(e.species);
  const Moves& moves = species_moves(e.species);
  const Side me = foe_side(w), other = ally_side(w);
  float dx, dz;
  const float d = flat_distance(e.body, companion(w).body, dx, dz);
  float ax, ay, az;
  const float d3 = aim(me, other, ax, ay, az);
  const float reach = info.shape.half_width + other.shape.half_width + moves[0].range;
  const bool bite = d <= reach && vertical_gap(me, other) <= kMeleeHeight;
  const bool stays = holds_its_place(e.species);  // a crow up there, a fish in the lake

  Steer s;
  switch (f.state) {
    case FighterState::Windup:
      if (--f.state_ticks == 0) attack(w, false, f.move);
      return s;
    case FighterState::Ready:
      break;
    default:  // recovering or stunned
      if (f.state_ticks == 0 || --f.state_ticks == 0) f.state = FighterState::Ready;
      return s;
  }
  const auto windup = [&](uint8_t slot) {
    f.state = FighterState::Windup;
    f.move = slot;
    f.state_ticks = kFoeWindup[slot];
  };
  if (bite && f.cooldown[0] == 0) {
    windup(0);
  } else if (d3 <= moves[2].range && f.cooldown[2] == 0) {
    windup(2);
  } else if ((d > 3.0f || stays) && d3 < moves[1].range && f.cooldown[1] == 0) {
    windup(1);
  } else if (!stays && d > reach * 0.8f) {
    s.dir_x = dx;
    s.dir_z = dz;
    s.speed = std::max(info.walk_speed, info.run_speed * 0.6f);
    if (e.species == Species::Slime) {  // slimes hop: they only move in the air
      s.jump = e.body.on_ground;
      if (e.body.on_ground) s.speed = 0;
    }
  }
  return s;
}

void battle_resolve(World& w) {
  Battle& b = w.battle;
  if (b.phase == BattlePhase::None) return;
  Creature& ally = companion(w);
  if (b.phase == BattlePhase::Flee) {
    float dx, dz;
    const bool back = flat_distance(ally.body, w.player, dx, dz) <= rules::kFleeArrival;
    if (back || ++b.timer >= rules::kFleeMaxTicks) end_battle(w, BattleOutcome::Fled, 0);
    return;
  }

  // Projectiles fly straight, and stop on what they hit.
  const Side foe_s = foe_side(w);
  const size_t ally_perch = standing_on(w, ally.body), foe_perch = standing_on(w, *foe_s.body);
  for (size_t i = 0; i < b.projectiles.size();) {
    monstres::Projectile& p = b.projectiles[i];
    p.pos.x += p.dir_x * rules::kProjectileSpeed * rules::kDt;
    p.pos.y += p.dir_y * rules::kProjectileSpeed * rules::kDt;
    p.pos.z += p.dir_z * rules::kProjectileSpeed * rules::kDt;
    bool gone = p.ticks == 0 || --p.ticks == 0 || p.pos.y < w.ground.height(p.pos.x, p.pos.z);
    const Side target = p.from_ally ? foe_s : ally_side(w);
    const BodyShape& shape = target.shape;
    const Body& tb = *target.body;
    const float tx = tb.pos.x - p.pos.x, tz = tb.pos.z - p.pos.z;
    const float r = shape.half_width + 0.3f;
    if (!gone && tx * tx + tz * tz <= r * r && p.pos.y >= tb.pos.y - 0.4f && p.pos.y <= tb.pos.y + shape.height + 0.4f) {
      const Fighter& t = p.from_ally ? b.foe : b.ally;
      if (t.state != FighterState::Dodge) {  // a dodged one flies on
        hit(w, !p.from_ally, p.damage, false);
        gone = true;
      }
    }
    // Objects stop it, but not what either fighter stands on (the roof of a crow).
    if (!gone) {
      const Box box{{p.pos.x - 0.1f, p.pos.y - 0.1f, p.pos.z - 0.1f}, {p.pos.x + 0.1f, p.pos.y + 0.1f, p.pos.z + 0.1f}};
      for (size_t k = 0; k < w.objects.size() && !gone; ++k) {
        gone = k != ally_perch && k != foe_perch && overlaps(box, object_box(w.objects[k]));
      }
    }
    if (gone) b.projectiles.erase(b.projectiles.begin() + static_cast<std::ptrdiff_t>(i));
    else ++i;
  }

  // Both face each other (the companion strafes around its enemy).
  Body& foe_body = *foe_s.body;
  float dx, dz;
  const float d = flat_distance(ally.body, foe_body, dx, dz);
  if (d > 1e-3f) {
    if (b.ally.state != FighterState::Dodge) {
      ally.body.facing_x = dx;
      ally.body.facing_z = dz;
    }
    foe_body.facing_x = -dx;
    foe_body.facing_z = -dz;
  }

  if (training(w)) {  // the magician keeps its shield up: no one wins
    if (d > rules::kBattleLeash) end_training(w);
    return;
  }
  Creature& foe = enemy(w);
  if (b.enemy_hp == 0) {
    // Beaten: it lies there for a while (see step), then runs home or flies away.
    foe.mind = Mind::KnockedOut;
    foe.timer = rules::kKnockedOutTicks;
    foe.body.speed = 0;
    end_battle(w, BattleOutcome::Victory, reward(w));
    foe.beaten = true;  // after its reward: the next time, none
  } else if (monstres::ally(w).hp == 0) {
    foe.mind = Mind::Idle;
    foe.timer = 2 * rules::kTicksPerSecond;
    monstres::ally(w).hp = 1;  // knocked out: it comes back to the player and rests
    end_battle(w, BattleOutcome::Defeat, 0);
  } else if (d > rules::kBattleLeash) {
    foe.mind = Mind::Idle;
    end_battle(w, BattleOutcome::Fled, 0);
  }
}

void team_regen(World& w) {
  if (w.tick % rules::kRegenEveryTicks != 0) return;
  for (size_t i = 0; i < w.team.size(); ++i) {
    if (static_cast<int>(i) == w.active && w.battle.phase != BattlePhase::None) continue;
    Monster& m = w.team[i];
    if (m.hp < monster_fight_stats(m.species, m.level).hp) ++m.hp;
  }
}

}  // namespace monstres
