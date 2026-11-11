#include "monstres/core/battle.hpp"

#include "monstres/core/variants.hpp"

#include <algorithm>
#include <cmath>
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
Monster& ally(World& w) { return *active_monster(w); }  // in a battle, there is one
Creature& enemy(World& w) { return w.creatures[w.battle.enemy]; }

FightStats ally_stats(const World& w) {
  const Monster& m = *active_monster(w);
  return monster_fight_stats(m.species, m.level);
}
FightStats foe_stats(const World& w) {
  const Creature& e = w.creatures[w.battle.enemy];
  return monster_fight_stats(e.species, e.level);
}

Element move_element(const Move& m, Species species, const Genes& genes) {
  return m.elemental ? creature_elements(species, genes).first : Element::Normal;
}

void grant_xp(Monster& m, uint32_t xp) {
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

void end_battle(World& w, BattleOutcome outcome, uint32_t xp) {
  Battle& b = w.battle;
  b.outcome_species = enemy(w).species;
  if (xp > 0) {
    grant_xp(ally(w), xp);
    grant_wizard_xp(w.wizard, xp / 2);
  }
  b.phase = BattlePhase::None;
  b.projectiles.clear();
  b.ally = {};
  b.foe = {};
  b.timer = 0;
  b.outcome = outcome;
  b.outcome_tick = w.tick;
  b.outcome_xp = xp;
}

uint32_t reward(World& w) { return 8u * enemy(w).level; }

// A hit on one side. Returns whether it landed.
bool hit(World& w, bool on_ally, uint16_t damage, bool melee) {
  Battle& b = w.battle;
  Fighter& target = on_ally ? b.ally : b.foe;
  if (target.state == FighterState::Dodge) return false;
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
  hp = static_cast<uint16_t>(hp - std::min(hp, damage));
  target.hit_tick = w.tick;
  target.last_damage = damage;
  return true;
}

void attack(World& w, bool by_ally, uint8_t slot) {
  Battle& b = w.battle;
  Fighter& f = by_ally ? b.ally : b.foe;
  const Creature& self = by_ally ? companion(w) : enemy(w);
  const Creature& other = by_ally ? enemy(w) : companion(w);
  const Species species = by_ally ? ally(w).species : self.species;
  const Genes& genes = by_ally ? ally(w).genes : self.genes;
  const Move& m = species_moves(species)[slot];
  f.cooldown[slot] = by_ally ? m.cooldown : foe_cooldown(m, slot);
  f.attack_tick = w.tick;
  f.attack_move = slot;
  f.state = FighterState::Recover;
  f.state_ticks = by_ally ? 8 : 30;

  const FightStats atk = by_ally ? ally_stats(w) : foe_stats(w);
  const FightStats def = by_ally ? foe_stats(w) : ally_stats(w);
  const uint16_t damage = move_damage(m, atk, def);
  const float self_w = species_info(self.species).shape.half_width;
  const float other_w = species_info(other.species).shape.half_width;
  float dx, dz;
  const float d = flat_distance(self.body, other.body, dx, dz);
  switch (m.shape) {
    case Melee:
      if (d <= self_w + other_w + m.range) hit(w, !by_ally, damage, true);
      break;
    case Burst:
      if (d <= m.range + other_w) hit(w, !by_ally, damage, false);
      break;
    case Projectile: {
      monstres::Projectile p;
      p.pos = {self.body.pos.x + dx * self_w, self.body.pos.y + species_info(self.species).shape.height * 0.5f,
               self.body.pos.z + dz * self_w};
      p.dir_x = dx;
      p.dir_z = dz;
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

void summon(World& w, size_t slot) {
  if (slot >= w.team.size() || w.creatures.empty()) return;
  Creature& c = w.creatures[0];
  const bool fighting = w.battle.phase == BattlePhase::Fight;
  if (w.active >= 0) {  // the one out goes back into the crystal
    const Monster& out = w.team[static_cast<size_t>(w.active)];
    w.went_in = {w.tick, false, out.species, out.genes, c.body};
    if (static_cast<int>(slot) == w.active) {  // just recalled (not in the middle of a fight)
      if (fighting) return;
      w.active = -1;
      c.mind = Mind::Idle;
      return;
    }
  } else {
    // Out next to the player, a little behind on its right.
    const Body& p = w.player;
    c.body = {};
    c.body.pos = {p.pos.x - p.facing_x * 1.1f - p.facing_z * 0.7f, p.pos.y,
                  p.pos.z - p.facing_z * 1.1f + p.facing_x * 0.7f};
    c.body.facing_x = p.facing_x;
    c.body.facing_z = p.facing_z;
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
  if (fighting) w.battle.ally = {};  // a fresh fighter
}

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

void assign_levels(World& w) {
  for (size_t i = 0; i < w.creatures.size(); ++i) {
    Creature& c = w.creatures[i];
    if (i == 0 || !monster_stats(c.species).is_monster) {
      c.level = 1;
      continue;
    }
    Rng rng(w.seed ^ 0x4C4556454CULL, i);  // "LEVEL", one stream per creature
    const uint8_t base = zone_at(w, c.home.x, c.home.z) == Zone::Ruins ? 3 : 1;
    c.level = static_cast<uint8_t>(base + rng.below(3));
  }
}

EngageCheck can_engage(const World& w, size_t index) {
  if (index == 0 || index >= w.creatures.size()) return EngageCheck::NotMonster;
  const Creature& c = w.creatures[index];
  if (!monster_stats(c.species).is_monster) return EngageCheck::NotMonster;
  if (w.battle.phase != BattlePhase::None) return EngageCheck::InBattle;
  if (!active_monster(w)) return EngageCheck::NoCompanion;
  if (c.species == Species::Crow || c.species == Species::Fish) return EngageCheck::Unreachable;
  const float dx = c.body.pos.x - w.player.pos.x, dz = c.body.pos.z - w.player.pos.z;
  if (dx * dx + dz * dz > rules::kEngageDistance * rules::kEngageDistance) return EngageCheck::TooFar;
  return EngageCheck::Ok;
}

const char* engage_check_name(EngageCheck check) {
  static const char* kNames[] = {"ok", "notMonster", "unreachable", "tooFar", "inBattle", "noCompanion"};
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
  flat_distance(w.creatures[0].body, w.creatures[w.battle.enemy].body, fx, fz);
  return true;
}

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
    case Command::Flee:
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
      capture(w, index);
      w.went_in = {w.tick, true, e.species, e.genes, e.body};  // runes, then into the crystal
      end_battle(w, BattleOutcome::Captured, reward(w));
      w.team.push_back(m);
      w.creatures.erase(w.creatures.begin() + static_cast<std::ptrdiff_t>(index));
      break;
    }
  }
}

Steer battle_ally(World& w, const Input& in, float dir_x, float dir_z) {
  Fighter& f = w.battle.ally;
  count_down(f);
  if (f.state != FighterState::Ready && f.state_ticks > 0 && --f.state_ticks == 0) f.state = FighterState::Ready;

  float fx, fz;
  battle_forward(w, fx, fz);
  const bool moving = dir_x * dir_x + dir_z * dir_z > 0.01f;
  if (in.jump && f.dodge_cooldown == 0 && f.state != FighterState::Dodge && f.state != FighterState::Stunned) {
    f.state = FighterState::Dodge;
    f.state_ticks = rules::kDodgeTicks;
    f.dodge_cooldown = rules::kDodgeTicks + rules::kDodgeCooldown;
    const float len = std::sqrt(dir_x * dir_x + dir_z * dir_z);
    f.dodge_x = moving ? dir_x / len : -fx;  // standing still: jump back
    f.dodge_z = moving ? dir_z / len : -fz;
  } else if (in.parry && f.parry_cooldown == 0 && free_to_act(f)) {
    f.state = FighterState::Parry;
    f.state_ticks = rules::kParryTicks;
    f.parry_cooldown = rules::kParryTicks + rules::kParryCooldown;
  } else if (in.attack >= 1 && in.attack <= 3 && free_to_act(f) && f.cooldown[in.attack - 1u] == 0) {
    attack(w, true, static_cast<uint8_t>(in.attack - 1));
  }

  Steer s;
  if (f.state == FighterState::Dodge) {
    s.dir_x = f.dodge_x;
    s.dir_z = f.dodge_z;
    s.speed = rules::kDodgeSpeed;
  } else if (f.state != FighterState::Stunned) {
    s.dir_x = dir_x;
    s.dir_z = dir_z;
    // Monsters do not walk through each other: against the enemy, only sideways or back.
    const float touching = species_info(companion(w).species).shape.half_width +
                           species_info(enemy(w).species).shape.half_width + 0.1f;
    float to_x, to_z;
    const float towards = dir_x * fx + dir_z * fz;
    if (flat_distance(companion(w).body, enemy(w).body, to_x, to_z) <= touching && towards > 0) {
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
  float dx, dz;
  const float d = flat_distance(e.body, companion(w).body, dx, dz);
  const float reach = info.shape.half_width + species_info(companion(w).species).shape.half_width + moves[0].range;

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
  if (d <= reach && f.cooldown[0] == 0) {
    windup(0);
  } else if (d <= moves[2].range && f.cooldown[2] == 0) {
    windup(2);
  } else if (d > 3.0f && d < moves[1].range && f.cooldown[1] == 0) {
    windup(1);
  } else if (d > reach * 0.8f) {
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
  for (size_t i = 0; i < b.projectiles.size();) {
    monstres::Projectile& p = b.projectiles[i];
    p.pos.x += p.dir_x * rules::kProjectileSpeed * rules::kDt;
    p.pos.z += p.dir_z * rules::kProjectileSpeed * rules::kDt;
    bool gone = p.ticks == 0 || --p.ticks == 0 || p.pos.y < w.ground.height(p.pos.x, p.pos.z);
    const Creature& target = p.from_ally ? enemy(w) : ally;
    const BodyShape& shape = species_info(target.species).shape;
    const float tx = target.body.pos.x - p.pos.x, tz = target.body.pos.z - p.pos.z;
    const float r = shape.half_width + 0.3f;
    if (!gone && tx * tx + tz * tz <= r * r && p.pos.y >= target.body.pos.y - 0.4f &&
        p.pos.y <= target.body.pos.y + shape.height + 0.4f) {
      const Fighter& t = p.from_ally ? b.foe : b.ally;
      if (t.state != FighterState::Dodge) {  // a dodged one flies on
        hit(w, !p.from_ally, p.damage, false);
        gone = true;
      }
    }
    if (!gone) {
      const Box box{{p.pos.x - 0.1f, p.pos.y - 0.1f, p.pos.z - 0.1f}, {p.pos.x + 0.1f, p.pos.y + 0.1f, p.pos.z + 0.1f}};
      gone = std::any_of(w.objects.begin(), w.objects.end(), [&](const Object& o) { return overlaps(box, object_box(o)); });
    }
    if (gone) b.projectiles.erase(b.projectiles.begin() + static_cast<std::ptrdiff_t>(i));
    else ++i;
  }

  // Both face each other (the companion strafes around its enemy).
  Creature& foe = enemy(w);
  float dx, dz;
  const float d = flat_distance(ally.body, foe.body, dx, dz);
  if (d > 1e-3f) {
    if (b.ally.state != FighterState::Dodge) {
      ally.body.facing_x = dx;
      ally.body.facing_z = dz;
    }
    foe.body.facing_x = -dx;
    foe.body.facing_z = -dz;
  }

  if (b.enemy_hp == 0) {
    foe.mind = Mind::Flee;  // beaten, it runs home
    foe.target = foe.home;
    foe.timer = 4 * rules::kTicksPerSecond;
    end_battle(w, BattleOutcome::Victory, reward(w));
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
