#include "monstres/core/debug.hpp"

#include "monstres/core/battle.hpp"
#include "monstres/core/variants.hpp"

#include <cmath>
#include <cstdio>
#include <iterator>

namespace monstres {

std::string elements_json(Elements e);
std::string carried_variants_json(Species species, const Genes& genes);

namespace {
const char* species_name(Species s) {
  static const char* kNames[] = {"companion", "slime", "mushroom", "bird", "villager", "crow", "fish"};
  const auto i = static_cast<size_t>(s);
  return i < std::size(kNames) ? kNames[i] : "?";
}

const char* mind_name(Mind m) {
  static const char* kNames[] = {"idle", "wander", "flee", "follow", "greet", "fly", "battle"};
  const auto i = static_cast<size_t>(m);
  return i < std::size(kNames) ? kNames[i] : "?";
}

std::string num(float v) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.4f", static_cast<double>(v));
  return buf;
}
}  // namespace

std::string to_json(const World& w) {
  const Player& p = w.player;
  std::string s;
  s += "{\"tick\":" + std::to_string(w.tick);
  s += ",\"player\":{\"x\":" + num(p.pos.x) + ",\"y\":" + num(p.pos.y) + ",\"z\":" + num(p.pos.z) +
       ",\"ground\":" + num(w.ground.height(p.pos.x, p.pos.z)) +  // height of the ground under it
       ",\"zone\":\"" + zone_name(zone_at(w, p.pos.x, p.pos.z)) + "\"" +
       ",\"onGround\":" + (p.on_ground ? "true" : "false") +
       ",\"facingX\":" + num(p.facing_x) + ",\"facingZ\":" + num(p.facing_z) + "}";
  s += ",\"objects\":" + std::to_string(w.objects.size());
  s += ",\"creatures\":[";
  for (size_t i = 0; i < w.creatures.size(); ++i) {
    const Creature& c = w.creatures[i];
    if (i) s += ',';
    s += std::string("{\"species\":\"") + species_name(c.species) + "\"" + ",\"mind\":\"" + mind_name(c.mind) +
         "\"" + ",\"x\":" + num(c.body.pos.x) + ",\"y\":" + num(c.body.pos.y) + ",\"z\":" + num(c.body.pos.z) + "}";
  }
  s += "]}";
  return s;
}

std::string creature_json(const World& w, int index) {
  if (index < 0 || static_cast<size_t>(index) >= w.creatures.size()) return "null";
  const Creature& c = w.creatures[static_cast<size_t>(index)];
  const Vec3 p = c.body.pos;
  const float dx = p.x - w.player.pos.x, dz = p.z - w.player.pos.z;
  std::string s = "{\"index\":" + std::to_string(index);
  s += std::string(",\"species\":\"") + species_name(c.species) + "\"";
  s += std::string(",\"mind\":\"") + mind_name(c.mind) + "\"";
  s += ",\"variant\":" + std::to_string(c.variant);
  if (monster_stats(c.species).is_monster) {
    s += std::string(",\"family\":\"") + family_name(genetics(c.species).family) + "\"";
    s += ",\"elements\":" + elements_json(creature_elements(c.species, c.genes));
    s += ",\"look\":" + std::to_string(genes_code(c.genes));
    s += ",\"variants\":" + carried_variants_json(c.species, c.genes);
    s += std::string(",\"chromatic\":") + (c.genes.chromatic ? "true" : "false");
    s += std::string(",\"elemental\":") + (c.genes.elemental ? "true" : "false");
    s += ",\"level\":" + std::to_string(index == 0 && !w.team.empty() ? w.team[0].level : c.level);
    s += std::string(",\"engage\":\"") + engage_check_name(can_engage(w, static_cast<size_t>(index))) + "\"";
  }
  s += ",\"x\":" + num(p.x) + ",\"y\":" + num(p.y) + ",\"z\":" + num(p.z);
  s += ",\"distance\":" + num(std::sqrt(dx * dx + dz * dz));
  s += std::string(",\"onGround\":") + (c.body.on_ground ? "true" : "false") + "}";
  return s;
}

}  // namespace monstres

namespace monstres {

const char* zone_name(Zone zone) {
  static const char* kNames[] = {"meadow", "village", "lake", "ruins"};
  const auto i = static_cast<size_t>(zone);
  return i < std::size(kNames) ? kNames[i] : "?";
}

const char* element_name(Element element) {
  static const char* kNames[] = {"fire", "water", "nature", "air", "shadow", "light", "electric", "normal"};
  static_assert(std::size(kNames) == static_cast<size_t>(Element::Count));
  const auto i = static_cast<size_t>(element);
  return i < std::size(kNames) ? kNames[i] : "?";
}

const char* family_name(Family family) {
  static const char* kNames[] = {"beast", "bird", "fish", "reptile", "insect", "plant", "ooze", "construct", "none"};
  static_assert(std::size(kNames) == static_cast<size_t>(Family::Count));
  const auto i = static_cast<size_t>(family);
  return i < std::size(kNames) ? kNames[i] : "?";
}

const char* rarity_name(Rarity rarity) {
  static const char* kNames[] = {"common", "uncommon", "rare", "veryRare"};
  static_assert(std::size(kNames) == static_cast<size_t>(Rarity::Count));
  const auto i = static_cast<size_t>(rarity);
  return i < std::size(kNames) ? kNames[i] : "?";
}

std::string elements_json(Elements e) {
  std::string s = std::string("[\"") + element_name(e.first) + "\"";
  if (e.second != Element::Count) s += std::string(",\"") + element_name(e.second) + "\"";
  return s + "]";
}

// The variants a monster with these genes shows: their identifiers.
std::string carried_variants_json(Species species, const Genes& genes) {
  std::string s = "[";
  bool first = true;
  for (size_t i = 0; i < variant_count(species); ++i) {
    if (!has_variant(species, genes, i)) continue;
    if (!first) s += ',';
    first = false;
    s += "\"" + variant_id(species, i) + "\"";
  }
  return s + "]";
}

const char* object_kind_name(ObjectKind kind) {
  static const char* kNames[] = {"tree",   "pine",   "rock",     "log",       "stump",        "bush",
                                 "crate",  "cottage", "chimney", "hut",       "fence",        "sign",
                                 "well",   "barrel", "lamp",     "tower",     "ruinWall",     "pillar",
                                 "fallenPillar", "rubble", "runeStone"};
  static_assert(std::size(kNames) == static_cast<size_t>(ObjectKind::Count));
  const auto i = static_cast<size_t>(kind);
  return i < std::size(kNames) ? kNames[i] : "?";
}

namespace {

std::string json_string(const std::string& text) {
  std::string out = "\"";
  for (const char ch : text) {
    const auto c = static_cast<unsigned char>(ch);
    if (c == '"' || c == '\\') {
      out += '\\';
      out += ch;
    } else if (c < 0x20) {
      char buf[8];
      std::snprintf(buf, sizeof buf, "\\u%04x", c);
      out += buf;
    } else {
      out += ch;
    }
  }
  return out + "\"";
}

}  // namespace

std::string wizard_json(const World& w) {
  const WizardStats st = wizard_stats(w.wizard);
  std::string s = "{\"name\":" + json_string(w.wizard.name);
  s += ",\"level\":" + std::to_string(w.wizard.level) + ",\"xp\":" + std::to_string(w.wizard.xp);
  s += ",\"xpToNext\":" + std::to_string(st.xp_to_next);
  s += ",\"hp\":" + std::to_string(st.hp) + ",\"mana\":" + std::to_string(st.mana);
  s += ",\"power\":" + std::to_string(st.power) + ",\"wisdom\":" + std::to_string(st.wisdom);
  s += ",\"agility\":" + std::to_string(st.agility) + ",\"endurance\":" + std::to_string(st.endurance) + "}";
  return s;
}

std::string bestiary_json(const World& w) {
  std::string s = "[";
  bool first = true;
  for (size_t i = 0; i < static_cast<size_t>(Species::Count); ++i) {
    const auto species = static_cast<Species>(i);
    const MonsterStats& st = monster_stats(species);
    if (!st.is_monster) continue;
    const SpeciesInfo& info = species_info(species);
    size_t count = 0;
    for (const Creature& c : w.creatures) count += c.species == species;
    if (!first) s += ',';
    first = false;
    s += std::string("{\"species\":\"") + species_name(species) + "\"";
    s += ",\"hp\":" + std::to_string(st.hp) + ",\"attack\":" + std::to_string(st.attack);
    s += ",\"defense\":" + std::to_string(st.defense) + ",\"speed\":" + std::to_string(st.speed);
    s += ",\"magic\":" + std::to_string(st.magic);
    s += ",\"elements\":" + elements_json(creature_elements(species, {}));
    s += std::string(",\"family\":\"") + family_name(genetics(species).family) + "\"";
    s += ",\"variants\":[";
    const auto& known = w.dex[i];
    for (size_t v = 0; v < variant_count(species); ++v) {
      const Variant var = variant_at(species, v);
      static const char* kKinds[] = {"sex", "color", "shape", "chromatic", "elemental"};
      static const char* kStatus[] = {"unknown", "seen", "captured"};
      if (v) s += ',';
      s += "{\"id\":\"" + variant_id(species, v) + "\"";
      s += std::string(",\"kind\":\"") + kKinds[static_cast<size_t>(var.kind)] + "\"";
      s += std::string(",\"rarity\":\"") + rarity_name(var.rarity) + "\"";
      s += ",\"zones\":[";
      bool first_zone = true;
      for (size_t z = 0; z < static_cast<size_t>(Zone::Count); ++z) {
        if (!(var.zones & zone_bit(static_cast<Zone>(z)))) continue;
        if (!first_zone) s += ',';
        first_zone = false;
        s += std::string("\"") + zone_name(static_cast<Zone>(z)) + "\"";
      }
      const Genes genes = genes_with(species, v);
      s += "],\"elements\":" + elements_json(creature_elements(species, genes));
      s += ",\"look\":" + std::to_string(genes_code(genes));
      s += std::string(",\"status\":\"") + kStatus[static_cast<size_t>(known[v])] + "\"}";
    }
    s += "]";
    s += ",\"height\":" + num(info.shape.height) + ",\"walkSpeed\":" + num(info.walk_speed);
    s += ",\"runSpeed\":" + num(info.run_speed) + ",\"count\":" + std::to_string(count) + "}";
  }
  return s + "]";
}

std::string map_json(const World& w) {
  std::string s = "{\"relief\":" + num(rules::kReliefHalfSize) + ",\"world\":" + num(rules::kWorldHalfSize);
  s += ",\"points\":" + std::to_string(w.ground.empty() ? 0 : Heightmap::kPoints);
  s += ",\"waterLevel\":" + num(rules::kWaterLevel);
  const Lake& l = w.village.lake;
  s += ",\"lake\":" + (l.radius_x > 0 ? "{\"x\":" + num(l.center.x) + ",\"z\":" + num(l.center.z) +
                                          ",\"rx\":" + num(l.radius_x) + ",\"rz\":" + num(l.radius_z) + "}"
                                    : std::string("null"));
  s += ",\"village\":" + (l.radius_x > 0 ? "{\"x\":" + num(w.village.center.x) + ",\"z\":" +
                                             num(w.village.center.z) + ",\"r\":" + num(rules::kVillageRadius) + "}"
                                       : std::string("null"));
  const RuinsLayout& r = w.ruins;
  s += ",\"ruins\":" + (r.radius > 0 ? "{\"x\":" + num(r.center.x) + ",\"z\":" + num(r.center.z) +
                                         ",\"r\":" + num(r.radius) + "}"
                                   : std::string("null"));
  s += ",\"path\":[";
  for (size_t i = 0; i < w.path.points.size(); ++i) {
    if (i) s += ',';
    s += num(w.path.points[i].x) + "," + num(w.path.points[i].z);
  }
  s += "],\"objects\":[";
  for (size_t i = 0; i < w.objects.size(); ++i) {
    const Object& o = w.objects[i];
    const Box b = object_box(o);
    if (i) s += ',';
    // Trees: the crown, not the trunk.
    const float crown = o.kind == ObjectKind::Tree ? 1.2f * o.size.x : o.kind == ObjectKind::Pine ? 1.0f * o.size.x : 0;
    s += std::string("[\"") + object_kind_name(o.kind) + "\"," + num(o.pos.x) + "," + num(o.pos.z) + ",";
    s += crown > 0 ? num(crown) + "," + num(crown)
                   : num(0.5f * (b.max.x - b.min.x)) + "," + num(0.5f * (b.max.z - b.min.z));
    s += "]";
  }
  return s + "]}";
}

}  // namespace monstres

namespace monstres {

namespace {

const char* shape_name(MoveShape shape) {
  static const char* kNames[] = {"melee", "projectile", "burst"};
  return kNames[static_cast<size_t>(shape)];
}

std::string seconds(uint32_t ticks) { return num(static_cast<float>(ticks) / rules::kTicksPerSecond); }

std::string moves_json(Species species, const Genes& genes, const FightStats& st) {
  std::string s = "[";
  const Moves& moves = species_moves(species);
  for (size_t i = 0; i < moves.size(); ++i) {
    const Move& m = moves[i];
    if (i) s += ',';
    s += std::string("{\"id\":\"") + m.id + "\",\"shape\":\"" + shape_name(m.shape) + "\"";
    const Element e = m.elemental ? creature_elements(species, genes).first : Element::Normal;
    s += std::string(",\"element\":\"") + element_name(e) + "\"";
    s += ",\"power\":" + std::to_string(m.power) + ",\"cooldown\":" + seconds(m.cooldown);
    s += ",\"range\":" + num(m.range);
    s += ",\"uses\":\"" + std::string(m.shape == MoveShape::Melee ? "attack" : "magic") + "\"";
    s += ",\"strength\":" + std::to_string(m.shape == MoveShape::Melee ? st.attack : st.magic) + "}";
  }
  return s + "]";
}

const char* outcome_name(BattleOutcome o) {
  static const char* kNames[] = {"none", "victory", "defeat", "fled", "captured"};
  return kNames[static_cast<size_t>(o)];
}

const char* state_name(FighterState f) {
  static const char* kNames[] = {"ready", "windup", "recover", "dodge", "parry", "stunned"};
  return kNames[static_cast<size_t>(f)];
}

std::string fighter_json(const World& w, const Fighter& f, const Moves& moves, bool ally) {
  std::string s = std::string("{\"state\":\"") + state_name(f.state) + "\"";
  s += ",\"move\":" + std::to_string(f.move);
  s += ",\"cooldowns\":[";
  for (size_t i = 0; i < 3; ++i) {
    if (i) s += ',';
    s += seconds(f.cooldown[i]);
  }
  s += "],\"cooldownTotals\":[";
  for (size_t i = 0; i < 3; ++i) {
    if (i) s += ',';
    s += seconds(moves[i].cooldown);
  }
  s += "]";
  if (ally) {
    s += ",\"dodgeCooldown\":" + seconds(f.dodge_cooldown) + ",\"parryCooldown\":" + seconds(f.parry_cooldown);
  }
  s += ",\"hitTick\":" + std::to_string(f.hit_tick) + ",\"lastDamage\":" + std::to_string(f.last_damage);
  s += ",\"parriedTick\":" + std::to_string(f.parried_tick) + ",\"attackTick\":" + std::to_string(f.attack_tick);
  (void)w;
  return s + "}";
}

}  // namespace

std::string team_json(const World& w) {
  std::string s = "[";
  for (size_t i = 0; i < w.team.size(); ++i) {
    const Monster& m = w.team[i];
    const FightStats st = monster_fight_stats(m.species, m.level);
    if (i) s += ',';
    s += "{\"slot\":" + std::to_string(i) + ",\"species\":\"" + species_name(m.species) + "\"";
    s += std::string(",\"active\":") + (i == 0 ? "true" : "false");
    s += ",\"level\":" + std::to_string(m.level) + ",\"xp\":" + std::to_string(m.xp);
    s += ",\"xpToNext\":" + std::to_string(monster_xp_to_next(m.level));
    s += ",\"hp\":" + std::to_string(m.hp) + ",\"maxHp\":" + std::to_string(st.hp);
    s += ",\"attack\":" + std::to_string(st.attack) + ",\"defense\":" + std::to_string(st.defense);
    s += ",\"speed\":" + std::to_string(st.speed) + ",\"magic\":" + std::to_string(st.magic);
    s += std::string(",\"family\":\"") + family_name(genetics(m.species).family) + "\"";
    s += ",\"elements\":" + elements_json(creature_elements(m.species, m.genes));
    s += ",\"look\":" + std::to_string(genes_code(m.genes));
    s += ",\"variants\":" + carried_variants_json(m.species, m.genes);
    s += std::string(",\"female\":") + (m.genes.allele[0] == 1 ? "true" : "false");
    s += ",\"moves\":" + moves_json(m.species, m.genes, st) + "}";
  }
  return s + "]";
}

std::string battle_json(const World& w) {
  const Battle& b = w.battle;
  std::string s = std::string("{\"phase\":\"") +
                  (b.phase == BattlePhase::Fight ? "fight" : b.phase == BattlePhase::Flee ? "flee" : "none") + "\"";
  s += ",\"tick\":" + std::to_string(w.tick);
  s += std::string(",\"outcome\":{\"result\":\"") + outcome_name(b.outcome) + "\",\"tick\":" +
       std::to_string(b.outcome_tick) + ",\"xp\":" + std::to_string(b.outcome_xp) + ",\"species\":\"" +
       species_name(b.outcome_species) + "\"}";
  if (b.phase != BattlePhase::None && !w.team.empty()) {
    const Creature& e = w.creatures[b.enemy];
    const Monster& a = w.team[0];
    s += ",\"enemy\":{\"index\":" + std::to_string(b.enemy) + ",\"species\":\"" + species_name(e.species) + "\"";
    s += ",\"level\":" + std::to_string(e.level) + ",\"hp\":" + std::to_string(b.enemy_hp);
    s += ",\"maxHp\":" + std::to_string(b.enemy_max_hp);
    s += ",\"elements\":" + elements_json(creature_elements(e.species, e.genes));
    s += ",\"look\":" + std::to_string(genes_code(e.genes));
    s += ",\"fighter\":" + fighter_json(w, b.foe, species_moves(e.species), false) + "}";
    s += ",\"ally\":{\"species\":\"" + std::string(species_name(a.species)) + "\"";
    s += ",\"level\":" + std::to_string(a.level) + ",\"hp\":" + std::to_string(a.hp);
    s += ",\"maxHp\":" + std::to_string(monster_fight_stats(a.species, a.level).hp);
    s += ",\"moves\":" + moves_json(a.species, a.genes, monster_fight_stats(a.species, a.level));
    s += ",\"fighter\":" + fighter_json(w, b.ally, species_moves(a.species), true) + "}";
    const bool weak = uint32_t{b.enemy_hp} * 100u <= uint32_t{b.enemy_max_hp} * rules::kCaptureHpPercent;
    s += std::string(",\"capture\":\"") +
         (can_capture(w) ? "ok" : w.team.size() >= rules::kTeamSize ? "teamFull" : !weak ? "notWeak" : "no") + "\"";
    s += ",\"captureHpPercent\":" + std::to_string(rules::kCaptureHpPercent);
  }
  return s + "}";
}

}  // namespace monstres
