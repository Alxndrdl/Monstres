#include "monstres/core/debug.hpp"

#include "monstres/core/north.hpp"

#include "monstres/core/battle.hpp"
#include "monstres/core/continent.hpp"
#include "monstres/core/interior.hpp"
#include "monstres/core/quests.hpp"
#include "monstres/core/roads.hpp"
#include "monstres/core/sites.hpp"
#include "monstres/core/variants.hpp"
#include "monstres/core/west.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <iterator>

namespace monstres {

std::string elements_json(Elements e);
std::string carried_variants_json(Species species, const Genes& genes);

const char* species_name(Species s) {
  static const char* kNames[] = {"companion", "slime", "mushroom", "bird", "villager", "crow", "fish", "spark", "axolotl", "cristi",
                                 "raccoon", "owl", "sheep", "mole", "golem", "goat", "ermine"};
  static_assert(std::size(kNames) == kSpeciesCount);
  const auto i = static_cast<size_t>(s);
  return i < std::size(kNames) ? kNames[i] : "?";
}

namespace {

const char* form_name(Form f) {
  static const char* kNames[] = {"normal", "micro", "alpha", "omega"};
  const auto i = static_cast<size_t>(f);
  return i < std::size(kNames) ? kNames[i] : "?";
}

const char* stat_name(Stat s) {
  static const char* kNames[] = {"strength", "dexterity", "constitution", "charisma", "intelligence",
                                 "hp",       "attack",    "defense",      "speed",    "magic"};
  static_assert(std::size(kNames) == static_cast<size_t>(Stat::Count));
  const auto i = static_cast<size_t>(s);
  return i < std::size(kNames) ? kNames[i] : "?";
}

// The modifiers of one variant: [{"stat":..., "value":...}].
std::string mods_json(Species species, size_t index) {
  const StatMods m = variant_mods(species, index);
  std::string s = "[";
  for (uint8_t k = 0; k < m.count; ++k) {
    if (k) s += ',';
    s += std::string("{\"stat\":\"") + stat_name(m.mods[k].stat) + "\",\"value\":" + std::to_string(m.mods[k].value) + "}";
  }
  return s + "]";
}

// Where the modifiers of one stat of a monster come from: [{"source": variant id, "value":...}].
std::string sources_json(Species species, const Genes& genes, Stat stat) {
  std::string s = "[";
  for (size_t i = 0; i < variant_count(species); ++i) {
    if (!has_variant(species, genes, i)) continue;
    const StatMods m = variant_mods(species, i);
    for (uint8_t k = 0; k < m.count; ++k) {
      if (m.mods[k].stat != stat) continue;
      if (s.size() > 1) s += ',';
      s += "{\"source\":\"" + variant_id(species, i) + "\",\"value\":" + std::to_string(m.mods[k].value) + "}";
    }
  }
  return s + "]";
}

const char* mind_name(Mind m) {
  static const char* kNames[] = {"idle", "wander", "flee", "follow", "greet", "fly", "battle", "knockedOut"};
  const auto i = static_cast<size_t>(m);
  return i < std::size(kNames) ? kNames[i] : "?";
}

std::string num(float v) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.4f", static_cast<double>(v));
  return buf;
}
}  // namespace

std::string command_names_json() {
  static const char* kNames[] = {"none",        "engage",    "flee",          "capture", "summon",    "release",
                                 "train",       "teleport",  "questAccept",   "questGive", "questComplete", "target",
                                 "challenge",   "ready",     "cast",          "equipSpell", "healTeam", "door",
                                 "useItem",     "discard",   "buy",           "farmSwap",   "rune",
                                 "tournament"};
  static_assert(std::size(kNames) == kCommandCount);
  static_assert(static_cast<size_t>(Command::Engage) == 1 && static_cast<size_t>(Command::Train) == 6 &&
                static_cast<size_t>(Command::Target) == 11 && static_cast<size_t>(Command::HealTeam) == 16);
  std::string s = "[";
  for (size_t i = 0; i < kCommandCount; ++i) s += std::string(i ? "," : "") + "\"" + kNames[i] + "\"";
  return s + "]";
}

std::string to_json(const World& w) {
  const Player& p = w.player;
  std::string s;
  s += "{\"tick\":" + std::to_string(w.tick);
  s += ",\"clock\":" + std::to_string(w.clock) + ",\"hour\":" + num(hour_of_day(w));
  s += ",\"player\":{\"x\":" + num(p.pos.x) + ",\"y\":" + num(p.pos.y) + ",\"z\":" + num(p.pos.z) +
       ",\"ground\":" + num(w.ground.height(p.pos.x, p.pos.z)) +  // height of the ground under it
       ",\"zone\":\"" + zone_name(zone_at(w, p.pos.x, p.pos.z)) + "\"" +
       ",\"onGround\":" + (p.on_ground ? "true" : "false") +
       ",\"facingX\":" + num(p.facing_x) + ",\"facingZ\":" + num(p.facing_z);
  // Inside (interior.hpp): where it is, and on the map where it went in.
  static const char* kPlaces[] = {"outside", "cave", "house", "tower", "burrow", "hollowTree", "lighthouse", "mine", "temple"};
  static_assert(std::size(kPlaces) == static_cast<size_t>(PlaceKind::Count));
  const Body& m = map_body(w);
  s += std::string(",\"place\":\"") + kPlaces[static_cast<size_t>(w.place.kind)] + "\"" +
       ",\"mapX\":" + num(m.pos.x) + ",\"mapZ\":" + num(m.pos.z) + ",\"mapFacingX\":" + num(m.facing_x) +
       ",\"mapFacingZ\":" + num(m.facing_z) + "}";
  s += ",\"objects\":" + std::to_string(w.objects.size());
  s += ",\"creatures\":[";
  for (size_t i = 0; i < w.creatures.size(); ++i) {
    const Creature& c = w.creatures[i];
    if (i) s += ',';
    s += std::string("{\"species\":\"") + species_name(c.species) + "\"" + ",\"mind\":\"" + mind_name(c.mind) +
         "\"" + ",\"x\":" + num(c.body.pos.x) + ",\"y\":" + num(c.body.pos.y) + ",\"z\":" + num(c.body.pos.z) +
         (c.tag == Tag::Raider ? ",\"aggressive\":true" : "") + (c.npc ? ",\"npc\":" + std::to_string(c.npc) : "") + "}";
  }
  s += "]";
  s += ",\"bag\":" + bag_json(w);
  s += ",\"farm\":" + std::to_string(w.farm.size());
  s += ",\"rescueTick\":" + std::to_string(w.rescue_tick);
  s += ",\"battleForced\":" + std::string(w.battle.forced ? "true" : "false");
  s += "}";
  return s;
}

// The villagers' icons (NpcMark), bit by bit.
constexpr const char* kMarkNames[] = {"newQuest", "quest", "heal", "fight", "shop", "farm"};

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
    s += std::string(",\"form\":\"") + form_name(c.genes.form) + "\"";
    s += std::string(",\"veryRare\":") + (very_rare(c.species, c.genes) ? "true" : "false");
    s += ",\"id\":" + std::to_string(c.id);
    s += ",\"level\":" + std::to_string(index == 0 && active_monster(w) ? active_monster(w)->level : c.level);
    s += std::string(",\"engage\":\"") + engage_check_name(can_engage(w, static_cast<size_t>(index))) + "\"";
    s += std::string(",\"released\":") + (c.released ? "true" : "false");
    s += std::string(",\"beaten\":") + (c.beaten ? "true" : "false");
    if (c.tag == Tag::LostPiou) s += ",\"tag\":\"lostPiou\"";
    if (c.tag == Tag::Rival) s += ",\"tag\":\"rival\"";
    if (c.tag == Tag::Giant) s += ",\"tag\":\"giant\"";
    if (c.tag == Tag::Raider) s += ",\"tag\":\"raider\",\"aggressive\":true";
    if (c.tag == Tag::Flock) s += ",\"tag\":\"flock\"";
    if (c.tag == Tag::Lamb) s += ",\"tag\":\"lamb\"";
    if (c.tag == Tag::Boarder) s += ",\"tag\":\"boarder\"";
    // Whose it is: 0 the player's (the companion, those left at the farm), else a villager's.
    const int owner = index == 0 || c.tag == Tag::Boarder ? 0
                      : c.tag == Tag::Rival                ? (c.npc ? c.npc : npc::kBastien)
                      : c.tag == Tag::Flock || c.tag == Tag::Lamb ? npc::kBasile
                      : c.tag == Tag::LostPiou            ? npc::kMirabelle
                                                          : -1;
    if (owner >= 0) s += ",\"owner\":" + std::to_string(owner);
    if (const int k = foe_slot(w, static_cast<size_t>(index)); k >= 0) s += ",\"foe\":" + std::to_string(k);
    if (index == 0) s += std::string(",\"train\":") + (can_train(w) ? "true" : "false");
  }
  if (c.npc) {
    s += ",\"npc\":" + std::to_string(c.npc);
    if (c.species == Species::Villager && npc::wanderer(c.npc)) {  // a trainer passing by
      s += std::string(",\"wanderer\":true,\"beaten\":") + (c.beaten ? "true" : "false");
      if (c.tag == Tag::Raider) s += ",\"aggressive\":true";
    }
    s += std::string(",\"talk\":") + (near_npc(w, c.npc) ? "true" : "false");
    uint8_t rivals = 0;
    rival_team(w, c.npc, rivals);
    if (c.npc == npc::kOdette || c.npc == npc::kViolette || c.npc == npc::kSolene) {
      s += std::string(",\"healer\":true,\"heal\":") + (can_heal_team(w, c.npc) ? "true" : "false");
    }
    if (rivals) {
      s += std::string(",\"trainer\":true,\"challenge\":") + (can_challenge(w, c.npc) ? "true" : "false");
      const uint32_t wins = c.npc == npc::kRomane    ? w.romane_wins
                            : c.npc == npc::kTristan ? w.tristan_wins
                            : npc::monk(c.npc)       ? (w.trials >> (c.npc - npc::kHugues)) & 1u
                                                     : w.rival_wins;
      s += ",\"wins\":" + std::to_string(wins) + ",\"trials\":" + std::to_string(w.trials);
    }
    if (c.species == Species::Villager) {  // the icons over its head
      const uint8_t m = npc_marks(w, c.npc);
      s += ",\"marks\":[";
      for (int k = 0, n = 0; k < 6; ++k) {
        if (m & (1 << k)) s += std::string(n++ ? "," : "") + "\"" + kMarkNames[k] + "\"";
      }
      s += "]";
    }
    if (c.npc == npc::kAnselme) s += std::string(",\"shop\":") + (npc_marks(w, c.npc) & kMarkShop ? "true" : "false");
    if (c.npc == npc::kCapucine || c.npc == npc::kMargaux) s += std::string(",\"farm\":") + (farm_open(w) ? "true" : "false");
    if (c.npc == npc::kBarnabe) s += ",\"arena\":" + tournament_json(w);
  }
  s += ",\"x\":" + num(p.x) + ",\"y\":" + num(p.y) + ",\"z\":" + num(p.z);
  s += ",\"distance\":" + num(std::sqrt(dx * dx + dz * dz));
  s += std::string(",\"onGround\":") + (c.body.on_ground ? "true" : "false") + "}";
  return s;
}

}  // namespace monstres

namespace monstres {

const char* zone_name(Zone zone) {
  static const char* kNames[] = {"meadow", "village", "lake", "ruins", "forest", "cave", "castle", "coast", "hills", "plains",
                                 "farm", "woods", "crater", "river", "alpine", "larches", "frost", "frozenLake", "mine"};
  static_assert(std::size(kNames) == static_cast<size_t>(Zone::Count));
  const auto i = static_cast<size_t>(zone);
  return i < std::size(kNames) ? kNames[i] : "?";
}

const char* element_name(Element element) {
  static const char* kNames[] = {"fire", "water", "nature", "air", "shadow", "light", "electric", "normal", "arcane", "earth", "ice"};
  static_assert(std::size(kNames) == static_cast<size_t>(Element::Count));
  const auto i = static_cast<size_t>(element);
  return i < std::size(kNames) ? kNames[i] : "?";
}

std::string type_chart_json() {
  constexpr auto n = static_cast<size_t>(Element::Count);
  std::string types, chart;
  for (size_t a = 0; a < n; ++a) {
    types += std::string(a ? "," : "") + "\"" + element_name(static_cast<Element>(a)) + "\"";
    chart += a ? ",[" : "[";
    for (size_t d = 0; d < n; ++d) {
      chart += (d ? "," : "") + std::to_string(type_effect(static_cast<Element>(a), static_cast<Element>(d)));
    }
    chart += "]";
  }
  return "{\"types\":[" + types + "],\"chart\":[" + chart + "]}";
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
                                 "fallenPillar", "rubble", "runeStone", "castleWall", "castleTower", "castleGate",
                                 "castleKeep", "caveMouth", "crystal", "collider", "teleporter",
                                 "table", "chair", "bed", "shelf", "hearth", "rug", "step", "hatch",
                                 "cart", "bridge", "farmhouse", "barn", "fruitTree", "crops", "haystack", "windmill",
                                 "cabin", "tent", "campfire", "woodpile", "hollowTree", "burrow", "lighthouse",
                                 "menhir", "meteorite", "waterMill", "hiddenCave",
                                 "chalet", "mineEntrance", "minecart", "larch", "stoneWall", "cairn", "hotSpring", "mineProp",
                                 "arena", "grotto", "shrine", "bench", "stall",
                                 "pier", "boat", "fishRack", "abbeyWall", "abbeyGate", "temple", "belfry", "dorter",
                                 "gravestone", "chapel", "crane", "block", "beehive", "dolmen", "wreck", "aqueduct"};
  static_assert(std::size(kNames) == static_cast<size_t>(ObjectKind::Count));
  const auto i = static_cast<size_t>(kind);
  return i < std::size(kNames) ? kNames[i] : "?";
}

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


std::string wizard_json(const World& w) {
  const WizardStats st = wizard_stats(w.wizard);
  std::string s = "{\"name\":" + json_string(w.wizard.name);
  s += ",\"level\":" + std::to_string(w.wizard.level) + ",\"xp\":" + std::to_string(w.wizard.xp);
  s += ",\"xpToNext\":" + std::to_string(st.xp_to_next);
  s += ",\"hp\":" + std::to_string(st.hp) + ",\"mana\":" + std::to_string(st.mana);
  s += ",\"manaNow\":" + std::to_string(w.wizard.mana);
  s += ",\"power\":" + std::to_string(st.power) + ",\"wisdom\":" + std::to_string(st.wisdom);
  s += ",\"agility\":" + std::to_string(st.agility) + ",\"endurance\":" + std::to_string(st.endurance);
  const WizardLook& l = w.wizard.look;
  s += std::string(",\"look\":{\"woman\":") + (l.woman ? "true" : "false") + ",\"skin\":" + std::to_string(l.skin) +
       ",\"hair\":" + std::to_string(l.hair) + ",\"hairColor\":" + std::to_string(l.hair_color) +
       ",\"eyes\":" + std::to_string(l.eyes) + ",\"staff\":" + std::to_string(l.staff) +
       ",\"robe\":" + std::to_string(l.robe) + ",\"headwear\":" + std::to_string(l.headwear) + "}";
  uint32_t captured = 0, beaten = 0;
  for (const uint16_t n : w.captured_count) captured += n;
  for (const uint16_t n : w.beaten_count) beaten += n;
  s += ",\"record\":{\"captured\":" + std::to_string(captured) + ",\"beaten\":" + std::to_string(beaten) +
       ",\"battlesWon\":" + std::to_string(w.battles_won) + ",\"trials\":" + std::to_string(w.trials) + "}}";
  return s;
}

std::string spells_json(const World& w) {
  const auto seconds = [](uint32_t ticks) { return num(static_cast<float>(ticks) / rules::kTicksPerSecond); };
  const auto list = [](std::span<const SpellId> slots) {
    std::string s = "[";
    for (size_t k = 0; k < slots.size(); ++k) {
      if (k) s += ',';
      s += std::string("\"") + spell_info(slots[k]).id + "\"";
    }
    return s + "]";
  };
  const BattlePhase p = w.battle.phase;
  const bool combat = p == BattlePhase::Fight || p == BattlePhase::Training || p == BattlePhase::Prepare;
  std::string s = std::string("{\"inCombat\":") + (combat ? "true" : "false");
  s += std::string(",\"editable\":") + (p == BattlePhase::None ? "true" : "false");
  s += std::string(",\"capturing\":") + (sealing(w) ? "true" : "false");
  s += ",\"combat\":" + list(w.combat_spells) + ",\"adventure\":" + list(w.adventure_spells) +
       ",\"capture\":" + list(w.capture_spells);
  s += ",\"known\":[";
  for (size_t i = 1; i < kSpellCount; ++i) {
    const SpellInfo& info = spell_info(static_cast<SpellId>(i));
    if (i > 1) s += ',';
    s += std::string("{\"id\":\"") + info.id + "\",\"value\":" + std::to_string(i);
    s += std::string(",\"combat\":") + (info.combat ? "true" : "false");
    s += std::string(",\"adventure\":") + (info.adventure ? "true" : "false");
    s += std::string(",\"capture\":") + (info.capture ? "true" : "false");
    s += std::string(",\"perRune\":") + (info.per_rune ? "true" : "false");
    s += ",\"mana\":" + std::to_string(info.mana);
    s += ",\"cooldown\":" + seconds(info.cooldown) + "}";
  }
  s += "],\"now\":[";
  const auto now = current_spells(w);
  for (size_t k = 0; k < now.size(); ++k) {
    const SpellInfo& info = spell_info(now[k]);
    const char* why = cannot_cast(w, k);
    if (k) s += ',';
    s += std::string("{\"slot\":") + std::to_string(k) + ",\"id\":\"" + info.id + "\"";
    s += ",\"cooldown\":" + seconds(w.spell_cooldowns[static_cast<size_t>(now[k])]);
    s += ",\"cooldownTotal\":" + seconds(info.cooldown);
    s += ",\"mana\":" + std::to_string(spell_cost(w, now[k]));
    s += std::string(",\"why\":") + (why ? std::string("\"") + why + "\"" : "null") + "}";
  }
  s += "],\"castTick\":" + std::to_string(w.spell_tick) + ",\"cast\":\"" + spell_info(w.spell_cast).id + "\"";
  s += ",\"healedTick\":" + std::to_string(w.healed_tick);
  s += ",\"manaNow\":" + std::to_string(w.wizard.mana) + ",\"manaMax\":" + std::to_string(wizard_stats(w.wizard).mana);
  s += std::string(",\"mercy\":") + (w.battle.mercy ? "true" : "false");
  s += ",\"mercyTick\":" + std::to_string(w.battle.mercy_tick);
  s += ",\"detect\":" + seconds(w.detect_ticks);
  s += ",\"wayOut\":" + seconds(w.way_out_ticks);
  const int found = detect_rare(w);
  if (found > 0) {
    const Creature& c = w.creatures[static_cast<size_t>(found)];
    const float dx = c.body.pos.x - w.player.pos.x, dz = c.body.pos.z - w.player.pos.z;
    s += ",\"found\":{\"index\":" + std::to_string(found) + ",\"species\":\"" + species_name(c.species) +
         "\",\"dx\":" + num(dx) + ",\"dz\":" + num(dz) + ",\"distance\":" + num(std::sqrt(dx * dx + dz * dz)) + "}";
  } else {
    s += ",\"found\":null";
  }
  return s + "}";
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
      static const char* kKinds[] = {"sex", "color", "shape", "chromatic", "elemental", "build"};
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
      s += ",\"mods\":" + mods_json(species, v);
      s += std::string(",\"status\":\"") + kStatus[static_cast<size_t>(known[v])] + "\"}";
    }
    s += "]";
    s += ",\"habitat\":[";
    const uint32_t habitat = species_habitat(species);
    for (size_t z = 0, n = 0; z < static_cast<size_t>(Zone::Count); ++z) {
      if (!((habitat >> z) & 1u)) continue;
      s += std::string(n++ ? "," : "") + "\"" + zone_name(static_cast<Zone>(z)) + "\"";
    }
    s += "],\"chances\":{";  // out of 100 monsters of each zone of its habitat
    for (size_t z = 0, n = 0; z < static_cast<size_t>(Zone::Count); ++z) {
      if (!((habitat >> z) & 1u)) continue;
      s += std::string(n++ ? "," : "") + "\"" + zone_name(static_cast<Zone>(z)) + "\":" +
           std::to_string(species_chance(species, static_cast<Zone>(z)));
    }
    s += "}";
    const TypicalMeasures typical = typical_measures(species);
    s += ",\"sizeMm\":" + num(typical.size_mm) + ",\"weightG\":" + num(typical.weight_g);
    s += ",\"speed\":" + num((info.walk_speed + info.run_speed) / 2);
    s += ",\"height\":" + num(info.shape.height) + ",\"walkSpeed\":" + num(info.walk_speed);
    s += ",\"runSpeed\":" + num(info.run_speed) + ",\"count\":" + std::to_string(count);
    s += ",\"captured\":" + std::to_string(w.captured_count[i]) + ",\"beaten\":" + std::to_string(w.beaten_count[i]);
    s += ",\"released\":" + std::to_string(w.released_count[i]);
    size_t owned = 0;
    for (const Monster& m : w.team) owned += m.species == species;
    s += ",\"inTeam\":" + std::to_string(owned) + "}";
  }
  return s + "]";
}

namespace {

const char* teleporter_name(TeleporterId id) {
  static const char* kNames[] = {"village", "ruins", "forest", "cave", "castle", "farm", "menhirs", "refuge", "col", "hamlet", "port", "abbey", "camp"};
  static_assert(std::size(kNames) == kTeleporterCount);
  return kNames[static_cast<size_t>(id)];
}

const char* quest_name(QuestId id) {
  static const char* kNames[] = {"lostPiou", "teleporters", "giantChampi", "combat", "healing", "main",
                                 "merchant",  "delivery",    "transfer",    "orchard", "shepherd", "mill",
                                 "waterWheel", "mineGuardian"};
  static_assert(std::size(kNames) == kQuestCount);
  return kNames[static_cast<size_t>(id)];
}

}  // namespace

std::string marks_json(const World& w) {
  std::string s = "[";
  bool first = true;
  for (const Creature& c : w.creatures) {
    if (c.species != Species::Villager || c.npc == 0) continue;
    const uint8_t m = npc_marks(w, c.npc);
    if (!m) continue;
    s += std::string(first ? "" : ",") + "{\"npc\":" + std::to_string(c.npc) + ",\"x\":" + num(c.body.pos.x) +
         ",\"z\":" + num(c.body.pos.z) + ",\"marks\":[";
    first = false;
    for (int k = 0, n = 0; k < 6; ++k) {
      if (m & (1 << k)) s += std::string(n++ ? "," : "") + "\"" + kMarkNames[k] + "\"";
    }
    s += "]}";
  }
  return s + "]";
}

std::string teleporters_json(const World& w) {
  std::string s = "{\"here\":" + std::to_string(teleporter_here(w)) + ",\"list\":[";
  bool first = true;
  for (size_t k = 0; k < kTeleporterCount; ++k) {
    const auto id = static_cast<TeleporterId>(k);
    Vec3 at;
    if (!teleporter_position(w, id, at)) continue;
    if (!first) s += ',';
    first = false;
    int object = -1;  // its index in World::objects (to select it)
    for (size_t i = 0; i < w.objects.size() && object < 0; ++i) {
      if (w.objects[i].kind == ObjectKind::Teleporter && w.objects[i].variant == k) object = static_cast<int>(i);
    }
    s += "{\"id\":" + std::to_string(k) + ",\"object\":" + std::to_string(object) + ",\"place\":\"" +
         teleporter_name(id) + "\",\"x\":" + num(at.x) +
         ",\"z\":" + num(at.z) + ",\"known\":" + (teleporter_known(w, id) ? "true" : "false") +
         ",\"reachable\":" + (can_teleport(w, id) ? "true" : "false") + "}";
  }
  return s + "]}";
}

std::string quests_json(const World& w) {
  static const char* kStages[] = {"unknown", "active", "done"};
  std::string s = "[";
  for (size_t k = 0; k < kQuestCount; ++k) {
    const auto id = static_cast<QuestId>(k);
    const QuestState& q = w.quests[k];
    if (k) s += ',';
    s += std::string("{\"id\":\"") + quest_name(id) + "\",\"stage\":\"" + kStages[static_cast<size_t>(q.stage)] + "\"";
    const bool done = q.stage == QuestStage::Done;
    const bool ready = quest_ready(w, id);
    s += std::string(",\"ready\":") + (ready ? "true" : "false");
    s += std::string(",\"available\":") + (quest_available(w, id) ? "true" : "false");
    s += ",\"giver\":" + std::to_string(quest_giver(id)) + ",\"receiver\":" + std::to_string(quest_receiver(id));
    s += ",\"steps\":{";
    if (id == QuestId::LostPiou) {
      s += std::string("\"find\":") + (done || ready ? "true" : "false");
      s += std::string(",\"bring\":") + (done ? "true" : "false");
      s += std::string(",\"near\":") + (near_npc(w, npc::kMirabelle) ? "true" : "false");
    } else if (id == QuestId::GiantChampi) {
      s += std::string("\"catch\":") + (done || ready ? "true" : "false");
      s += std::string(",\"show\":") + (done ? "true" : "false");
      s += std::string(",\"near\":") + (near_npc(w, npc::kLison) ? "true" : "false");
      s += std::string(",\"again\":") + ((q.progress & kGiantCaught) ? "true" : "false");
      s += std::string(",\"rewarded\":") + ((q.progress & kGiantRewarded) ? "true" : "false");
    } else if (id == QuestId::Combat) {
      s += std::string("\"win\":") + (done || ready ? "true" : "false");
      s += std::string(",\"tell\":") + (done ? "true" : "false");
    } else if (id == QuestId::Healing) {
      s += std::string("\"heal\":") + (done || ready ? "true" : "false");
      s += std::string(",\"tell\":") + (done ? "true" : "false");
    } else if (id == QuestId::Main) {
      for (const QuestId part : kMainPart1) {
        s += std::string("\"") + quest_name(part) + "\":" +
             (w.quests[static_cast<size_t>(part)].stage == QuestStage::Done ? "true" : "false") + ",";
      }
      s += std::string("\"tell\":") + ((q.progress & kMainPart1Done) ? "true" : "false");
      s += std::string(",\"part2\":") + ((q.progress & kMainPart2Done) ? "true" : "false");
    } else if (id == QuestId::Merchant) {
      s += "\"goods\":" + std::to_string(std::popcount(static_cast<unsigned>(q.progress & kGoodsFound)));
      s += ",\"of\":" + std::to_string(rules::kGoods);
      s += std::string(",\"tell\":") + (done ? "true" : "false");
      s += ",\"spots\":[";
      const auto spots = goods_spots(w);
      for (uint32_t g = 0; g < rules::kGoods; ++g) {
        if (g) s += ',';
        s += "{\"x\":" + num(spots[g].x) + ",\"z\":" + num(spots[g].z) + ",\"found\":" +
             ((q.progress >> g) & 1u ? "true" : "false") + "}";
      }
      s += "]";
    } else if (id == QuestId::Delivery) {
      s += std::string("\"stone\":") + (w.bag[static_cast<size_t>(Item::StrangeStone)] > 0 ? "true" : "false");
      s += std::string(",\"given\":") + (done ? "true" : "false");
      s += std::string(",\"near\":") + (near_npc(w, npc::kCapucine) ? "true" : "false");
    } else if (id == QuestId::Transfer) {
      s += std::string("\"sent\":") + ((q.progress & kSentToFarm) || done ? "true" : "false");
      s += std::string(",\"took\":") + ((q.progress & kTookFromFarm) || done ? "true" : "false");
      s += std::string(",\"tell\":") + (done ? "true" : "false");
    } else if (id == QuestId::Orchard) {
      s += "\"beaten\":" + std::to_string(done ? rules::kRaiders : std::min<uint32_t>(q.progress, rules::kRaiders));
      s += ",\"of\":" + std::to_string(rules::kRaiders);
      s += std::string(",\"tell\":") + (done ? "true" : "false");
    } else if (id == QuestId::Shepherd) {
      s += "\"lambs\":" + std::to_string(std::popcount(static_cast<unsigned>(q.progress & kLambsHome)));
      s += ",\"of\":" + std::to_string(rules::kLambs);
      s += std::string(",\"tell\":") + (done ? "true" : "false");
      s += std::string(",\"room\":") + (w.team.size() < rules::kTeamSize || farm_open(w) ? "true" : "false");
    } else if (id == QuestId::Mill) {
      s += std::string("\"turning\":") + ((q.progress & kMillTurning) ? "true" : "false");
      s += std::string(",\"tell\":") + (done ? "true" : "false");
    } else if (id == QuestId::MineGuardian) {
      s += std::string("\"beaten\":") + ((q.progress & kGuardianBeaten) || done ? "true" : "false");
      s += std::string(",\"tell\":") + (done ? "true" : "false");
    } else if (id == QuestId::WaterWheel) {
      s += "\"beaten\":" + std::to_string(done ? rules::kWheelRaiders : std::min<uint32_t>(q.progress, rules::kWheelRaiders));
      s += ",\"of\":" + std::to_string(rules::kWheelRaiders);
      s += std::string(",\"tell\":") + (done ? "true" : "false");
    } else {
      s += std::string("\"discover\":") + (teleporter_known(w, TeleporterId::Castle) ? "true" : "false");
      s += std::string(",\"go\":") + ((q.progress & kWentToCastle) || done ? "true" : "false");
      s += std::string(",\"back\":") + ((q.progress & kCameBack) || done ? "true" : "false");
      s += std::string(",\"tell\":") + (done ? "true" : "false");
      s += std::string(",\"near\":") + (near_npc(w, npc::kGaspard) ? "true" : "false");
    }
    s += "}}";
  }
  return s + "]";
}

std::string object_json(const World& w, int index) {
  if (index < 0 || static_cast<size_t>(index) >= w.objects.size()) return "null";
  const Object& o = w.objects[static_cast<size_t>(index)];
  if (o.kind == ObjectKind::Sign) return sign_json(w, index);
  if (o.kind != ObjectKind::Teleporter || o.variant >= kTeleporterCount) return "null";
  const auto id = static_cast<TeleporterId>(o.variant);
  const float dx = o.pos.x - w.player.pos.x, dz = o.pos.z - w.player.pos.z;
  return "{\"kind\":\"teleporter\",\"index\":" + std::to_string(index) + ",\"id\":" + std::to_string(o.variant) +
         ",\"place\":\"" + teleporter_name(id) + "\",\"known\":" + (teleporter_known(w, id) ? "true" : "false") +
         ",\"here\":" + (teleporter_here(w) == static_cast<int>(o.variant) ? "true" : "false") +
         ",\"distance\":" + num(std::sqrt(dx * dx + dz * dz)) + "}";
}

std::string sign_json(const World& w, int index) {
  if (index < 0 || static_cast<size_t>(index) >= w.objects.size()) return "null";
  const Object& o = w.objects[static_cast<size_t>(index)];
  if (o.kind != ObjectKind::Sign) return "null";
  struct Sight {
    Zone zone;
    Vec2 at;
  };
  std::vector<Sight> places;
  if (w.village.lake.radius_x > 0) {
    places.push_back({Zone::Village, w.village.center});
    places.push_back({Zone::Lake, w.village.lake.center});
  }
  if (w.ruins.radius > 0) places.push_back({Zone::Ruins, w.ruins.center});
  if (w.forest.radius_x > 0) places.push_back({Zone::Forest, w.forest.center});
  if (w.cave.radius > 0) places.push_back({Zone::Cave, w.cave.mouth});
  if (w.castle.radius > 0) places.push_back({Zone::Castle, w.castle.center});
  if (w.north.valid()) {  // the refuge's signpost: the way up to the frozen lake and the mine
    places.push_back({Zone::FrozenLake, w.north.lake});
    places.push_back({Zone::Mine, w.north.mine});
  }
  const auto distance = [&](const Sight& p) {
    const float dx = p.at.x - o.pos.x, dz = p.at.z - o.pos.z;
    return std::sqrt(dx * dx + dz * dz);
  };
  std::sort(places.begin(), places.end(), [&](const Sight& a, const Sight& b) { return distance(a) < distance(b); });
  std::string s = "{\"kind\":\"sign\",\"index\":" + std::to_string(index) + ",\"x\":" + num(o.pos.x) + ",\"z\":" + num(o.pos.z);
  s += std::string(",\"zone\":\"") + zone_name(zone_at(w, o.pos.x, o.pos.z)) + "\",\"places\":[";
  for (size_t i = 0; i < places.size(); ++i) {
    const Sight& p = places[i];
    if (i) s += ',';
    s += std::string("{\"zone\":\"") + zone_name(p.zone) + "\",\"dx\":" + num(p.at.x - o.pos.x) +
         ",\"dz\":" + num(p.at.z - o.pos.z) + ",\"distance\":" + num(distance(p)) + "}";
  }
  return s + "]}";
}

const char* region_name(Region r) {
  static const char* kNames[] = {"ocean", "start", "mountains", "volcano", "desert", "plains",
                                 "forest", "marsh", "lake", "loneMountain", "capital"};
  static_assert(std::size(kNames) == static_cast<size_t>(Region::Count));
  const auto i = static_cast<size_t>(r);
  return i < std::size(kNames) ? kNames[i] : "?";
}

std::string continent_json(const Continent& c) {
  std::string s = "{\"minX\":" + num(Continent::kMinX) + ",\"minZ\":" + num(Continent::kMinZ);
  s += ",\"cell\":" + num(Continent::kCell) + ",\"pointsX\":" + std::to_string(Continent::kPointsX);
  s += ",\"pointsZ\":" + std::to_string(Continent::kPointsZ) + ",\"seaLevel\":" + num(rules::kSeaLevel);
  s += ",\"snowLine\":" + num(rules::kSnowLine) + ",\"start\":" + num(rules::kReliefHalfSize) +
       ",\"startNorth\":" + num(rules::kReliefNorth);
  s += ",\"ready\":" + std::string(c.empty() ? "false" : "true") + ",\"landmarks\":[";
  const auto& marks = continent_landmarks();
  for (size_t k = 0; k < marks.size(); ++k) {
    const Landmark& m = marks[k];
    s += std::string(k ? "," : "") + "{\"region\":\"" + region_name(m.region) + "\",\"x\":" + num(m.at.x) +
         ",\"z\":" + num(m.at.z) + ",\"r\":" + num(m.radius) + "}";
  }
  s += "],\"ponds\":[";
  const auto& ponds = continent_ponds();
  for (size_t k = 0; k < ponds.size(); ++k) {
    const Pond& p = ponds[k];
    s += std::string(k ? "," : "") + "{\"x\":" + num(p.center.x) + ",\"z\":" + num(p.center.z) + ",\"rx\":" +
         num(p.radius_x) + ",\"rz\":" + num(p.radius_z) + ",\"level\":" + num(p.level) + "}";
  }
  const Crater cr = volcano_crater();
  s += "],\"crater\":{\"x\":" + num(cr.center.x) + ",\"z\":" + num(cr.center.z) + ",\"r\":" + num(cr.radius) + "}}";
  return s;
}

std::string map_json(const World& w) {
  // The relief: x from minX, z from minZ (the north), pointsX by pointsZ heights.
  std::string s = "{\"relief\":" + num(rules::kReliefHalfSize) + ",\"reliefNorth\":" + num(rules::kReliefNorth) +
                  ",\"world\":" + num(rules::kWorldHalfSize) + ",\"worldNorth\":" + num(rules::kWorldNorth);
  s += ",\"minX\":" + num(Heightmap::kMinX) + ",\"minZ\":" + num(Heightmap::kMinZ);
  s += ",\"pointsX\":" + std::to_string(w.ground.empty() ? 0 : Heightmap::kPointsX) +
       ",\"pointsZ\":" + std::to_string(w.ground.empty() ? 0 : Heightmap::kPointsZ);
  s += ",\"cell\":" + num(Heightmap::kCellSize);
  s += ",\"waterLevel\":" + num(rules::kWaterLevel) + ",\"seaLevel\":" + num(rules::kSeaLevel);
  // The lake's, the forest's and its clearing's outlines (village.hpp, outline_scale).
  const auto numbers = [](const std::array<float, 4>& a) {
    return "[" + num(a[0]) + "," + num(a[1]) + "," + num(a[2]) + "," + num(a[3]) + "]";
  };
  s += ",\"outlineWaves\":" + numbers(kOutlineWaves) + ",\"outlineDepths\":" + numbers(kOutlineDepths);
  const Lake& l = w.village.lake;
  s += ",\"lake\":" + (l.radius_x > 0 ? "{\"x\":" + num(l.center.x) + ",\"z\":" + num(l.center.z) +
                                          ",\"rx\":" + num(l.radius_x) + ",\"rz\":" + num(l.radius_z) +
                                          ",\"outline\":" + numbers(outline_phases(l.center, kLakeOutline)) + "}"
                                    : std::string("null"));
  s += ",\"village\":" + (l.radius_x > 0 ? "{\"x\":" + num(w.village.center.x) + ",\"z\":" +
                                             num(w.village.center.z) + ",\"r\":" + num(rules::kVillageRadius) + "}"
                                       : std::string("null"));
  const RuinsLayout& r = w.ruins;
  s += ",\"ruins\":" + (r.radius > 0 ? "{\"x\":" + num(r.center.x) + ",\"z\":" + num(r.center.z) +
                                         ",\"r\":" + num(r.radius) + "}"
                                   : std::string("null"));
  const ForestLayout& f = w.forest;
  s += ",\"forest\":" + (f.radius_x > 0 ? "{\"x\":" + num(f.center.x) + ",\"z\":" + num(f.center.z) +
                                          ",\"rx\":" + num(f.radius_x) + ",\"rz\":" + num(f.radius_z) +
                                          ",\"outline\":" + numbers(outline_phases(f.center, kForestOutline)) +
                                          ",\"clearing\":" + num(rules::kClearingRadius) +
                                          ",\"clearingOutline\":" + numbers(outline_phases(f.center, kClearingOutline)) + "}"
                                    : std::string("null"));
  const CaveLayout& c = w.cave;
  s += ",\"cave\":" + (c.radius > 0 ? "{\"x\":" + num(c.center.x) + ",\"z\":" + num(c.center.z) +
                                        ",\"r\":" + num(c.radius) + ",\"mouthX\":" + num(c.mouth.x) +
                                        ",\"mouthZ\":" + num(c.mouth.z) + "}"
                                  : std::string("null"));
  const CastleLayout& k = w.castle;
  s += ",\"castle\":" + (k.radius > 0 ? "{\"x\":" + num(k.center.x) + ",\"z\":" + num(k.center.z) +
                                          ",\"r\":" + num(rules::kCastleHalf) + "}"
                                    : std::string("null"));
  // The zones, every kZoneStep meters across the playable area (one letter
  // each: 'a' + Zone), from (-world, -worldNorth): rows along +Z then columns along +X.
  constexpr float kZoneStep = 8.0f;
  const int zones_x = static_cast<int>(2 * rules::kWorldHalfSize / kZoneStep) + 1;
  const int zones_z = static_cast<int>((rules::kWorldHalfSize + rules::kWorldNorth) / kZoneStep) + 1;
  s += ",\"zoneStep\":" + num(kZoneStep) + ",\"zoneCountX\":" + std::to_string(zones_x) +
       ",\"zoneCountZ\":" + std::to_string(zones_z) + ",\"zones\":\"";
  if (!w.ground.empty()) {
    for (int i = 0; i < zones_x; ++i) {
      for (int j = 0; j < zones_z; ++j) {
        const float x = -rules::kWorldHalfSize + kZoneStep * static_cast<float>(i);
        const float z = -rules::kWorldNorth + kZoneStep * static_cast<float>(j);
        s += static_cast<char>('a' + static_cast<int>(zone_at(w, x, z)));
      }
    }
  }
  s += "\"";
  s += ",\"path\":[";
  for (size_t i = 0; i < w.path.points.size(); ++i) {
    if (i) s += ',';
    s += num(w.path.points[i].x) + "," + num(w.path.points[i].z);
  }
  s += "]";
  // The west (west.hpp): its road and stream (every other point), its places.
  const WestLayout& west = w.west;
  if (west.valid()) {
    const auto line = [&](const std::vector<Vec2>& pts) {
      std::string t = "[";
      for (size_t i = 0; i < pts.size(); i += 2) t += std::string(i ? "," : "") + num(pts[i].x) + "," + num(pts[i].z);
      return t + "]";
    };
    const auto at = [&](const char* name, Vec2 p) { return std::string(",\"") + name + "\":[" + num(p.x) + "," + num(p.z) + "]"; };
    s += ",\"west\":{\"road\":" + line(west.road.points) + ",\"stream\":" + line(west.stream.points);
    s += at("cart", west.cart) + at("bridge", west.bridge) + at("farm", west.farm) + at("mill", west.mill) +
         at("pasture", west.pasture) + at("burrow", west.burrow) + at("woods", west.woods) + at("cabin", west.cabin) +
         at("camp", west.camp) + at("hollowTree", west.hollow_tree) + at("lighthouse", west.lighthouse) +
         at("menhirs", west.menhirs) + at("crater", west.crater) + at("pathBridge", west.path_bridge) +
         at("cove", west.cove) + at("waterMill", west.water_mill) + at("falls", west.falls) + at("fallsOut", west.falls_out);
    s += ",\"streamW\":[";  // the river's half width, at the same points as its line
    for (size_t i = 0; i < west.stream.width.size(); i += 2) s += std::string(i ? "," : "") + num(west.stream.width[i]);
    s += "]";
    s += ",\"farmR\":" + num(rules::kFarmRadius) + ",\"woodsRx\":" + num(west.woods_rx) + ",\"woodsRz\":" + num(west.woods_rz) +
         ",\"craterR\":" + num(rules::kCraterRadius) + "}";
  } else {
    s += ",\"west\":null";
  }
  // The north (north.hpp): its places, the frozen lake's outline, the valley's floor (for the snow).
  const NorthLayout& north = w.north;
  if (north.valid()) {
    const auto at = [&](const char* name, Vec2 p) { return std::string(",\"") + name + "\":[" + num(p.x) + "," + num(p.z) + "]"; };
    s += ",\"north\":{\"frost\":" + num(rules::kFrostFrom) + ",\"floor\":" + num(north.floor_start) +
         ",\"rise\":" + num(rules::kValleyRise);
    s += at("refuge", north.refuge) + at("mine", north.mine) + at("springs", north.springs) + at("col", north.col) +
         at("lake", north.lake) + at("hamlet", north.hamlet) + at("arena", north.arena) + at("ranch", north.ranch) +
         at("paddock", north.paddock) + at("grotto", north.grotto);
    s += ",\"arenaR\":" + num(rules::kArenaRadius) + ",\"paddockHx\":" + num(north.paddock_hx) + ",\"paddockHz\":" + num(north.paddock_hz);
    s += ",\"lakeRx\":" + num(north.lake_rx) + ",\"lakeRz\":" + num(north.lake_rz) +
         ",\"lakeOutline\":" + numbers(outline_phases(north.lake, kNorthLakeOutline)) + "}";  // (numbers: as the lake's above)
  } else {
    s += ",\"north\":null";
  }
  // The side roads and the places along them (roads.hpp).
  if (w.roads.valid()) {
    const auto at = [&](const char* name, Vec2 p) { return std::string(",\"") + name + "\":[" + num(p.x) + "," + num(p.z) + "]"; };
    s += ",\"roads\":{\"lanes\":[";
    for (size_t lane = 0; lane < w.roads.lanes.size(); ++lane) {
      if (lane) s += ",";
      s += "[";
      const auto& pts = w.roads.lanes[lane].points;
      for (size_t i = 0; i < pts.size(); i += 2) s += std::string(i ? "," : "") + num(pts[i].x) + "," + num(pts[i].z);
      s += "]";
    }
    s += "]" + at("inn", w.roads.inn) + at("farmstead", w.roads.farmstead) + at("sawmill", w.roads.sawmill) + "}";
  } else {
    s += ",\"roads\":null";
  }
  // The sites (sites.hpp).
  if (w.sites.valid()) {
    const SitesLayout& st = w.sites;
    const auto at = [&](const char* name, Vec2 p) { return std::string(",\"") + name + "\":[" + num(p.x) + "," + num(p.z) + "]"; };
    s += ",\"sites\":{\"abbeyHx\":" + num(st.abbey_hx) + ",\"abbeyHz\":" + num(st.abbey_hz);
    if (st.port.x != 0 || st.port.z != 0) s += at("port", st.port) + at("portSea", st.port_sea);
    s += at("abbey", st.abbey) + at("temple", st.temple) + at("camp", st.camp) + at("lookout", st.lookout) +
         at("quarry", st.quarry) + at("quarryFront", st.quarry_front) + at("chapel", st.chapel) + at("apiary", st.apiary) + at("dolmen", st.dolmen);
    if (st.wreck.x != 0 || st.wreck.z != 0) s += at("wreck", st.wreck);
    if (st.aqueduct_a.x != 0 || st.aqueduct_a.z != 0) s += at("aqueductA", st.aqueduct_a) + at("aqueductB", st.aqueduct_b);
    s += ",\"quarryR\":" + num(rules::kQuarryRadius + rules::kQuarryRim) + "}";
  } else {
    s += ",\"sites\":null";
  }
  s += ",\"objects\":[";
  for (size_t i = 0; i < w.objects.size(); ++i) {
    const Object& o = w.objects[i];
    Box b = object_box(o);
    if (o.kind == ObjectKind::CastleGate) b = {o.pos - Vec3{4, 0, 4}, o.pos + Vec3{4, 0, 4}};  // its colliders
    if (o.kind == ObjectKind::Arena) {
      const float ar = rules::kArenaRadius;
      b = {o.pos - Vec3{ar, 0, ar}, o.pos + Vec3{ar, 0, ar}};
    }
    if (o.kind == ObjectKind::AbbeyGate) b = {o.pos - Vec3{4, 0, 4}, o.pos + Vec3{4, 0, 4}};
    if (o.kind == ObjectKind::Aqueduct) {
      const bool along_x = ((o.yaw >> 14) & 1) == 0;
      const Vec3 h{along_x ? 4.0f : 1.0f, 0, along_x ? 1.0f : 4.0f};
      b = {o.pos - h, o.pos + h};
    }
    if (o.kind == ObjectKind::Collider) continue;
    if (o.kind == ObjectKind::Crops) b = {o.pos - o.size, o.pos + o.size};  // walked through, but drawn
    if (s.back() != '[') s += ',';
    // Trees: the crown, not the trunk.
    const float crown = o.kind == ObjectKind::Tree ? 1.2f * o.size.x
                        : o.kind == ObjectKind::Pine || o.kind == ObjectKind::Larch ? 1.0f * o.size.x : 0;
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
  static const char* kNames[] = {"none", "victory", "defeat", "fled", "captured", "forfeit", "escaped"};
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
  s += ",\"hitTick\":" + std::to_string(f.hit_tick) + ",\"lastDamage\":" + std::to_string(f.last_damage) +
       ",\"lastEffect\":" + std::to_string(f.last_effect);
  s += ",\"parriedTick\":" + std::to_string(f.parried_tick) + ",\"attackTick\":" + std::to_string(f.attack_tick);
  (void)w;
  return s + "}";
}

}  // namespace

std::string team_json(const World& w) {
  std::string s = "[";
  for (size_t i = 0; i < w.team.size(); ++i) {
    const Monster& m = w.team[i];
    const FightStats st = monster_fight_stats(m);
    if (i) s += ',';
    s += "{\"slot\":" + std::to_string(i) + ",\"species\":\"" + species_name(m.species) + "\"";
    s += std::string(",\"active\":") + (static_cast<int>(i) == w.active ? "true" : "false");
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
    s += std::string(",\"form\":\"") + form_name(m.genes.form) + "\"";
    // Each gift: its value, its drawn base, the lowest and highest it could
    // be with these genes, and where its modifiers come from.
    const Gifts gifts = monster_gifts(m);
    s += ",\"gifts\":[";
    for (size_t k = 0; k < kGiftCount; ++k) s += (k ? "," : "") + std::to_string(gifts[k]);
    s += "],\"giftDetails\":[";
    for (size_t k = 0; k < kGiftCount; ++k) {
      const auto stat = static_cast<Stat>(k);
      s += std::string(k ? "," : "") + "{\"stat\":\"" + stat_name(stat) + "\",\"value\":" + std::to_string(gifts[k]);
      s += ",\"base\":" + std::to_string(m.gifts[k]);
      s += ",\"min\":" + std::to_string(gift_value(m.species, m.genes, k, kGiftBaseMin));
      s += ",\"max\":" + std::to_string(gift_value(m.species, m.genes, k, kGiftBaseMax));
      s += ",\"mods\":" + sources_json(m.species, m.genes, stat) + "}";
    }
    // The fight stats some variants change (the build's percent apart).
    s += "],\"statMods\":{";
    bool first_mod = true;
    for (Stat stat : {Stat::Hp, Stat::Attack, Stat::Defense, Stat::Speed, Stat::Magic}) {
      const std::string sources = sources_json(m.species, m.genes, stat);
      if (sources == "[]") continue;
      s += std::string(first_mod ? "" : ",") + "\"" + stat_name(stat) + "\":" + sources;
      first_mod = false;
    }
    s += "},\"sizeMm\":" + std::to_string(m.size_mm) + ",\"weightG\":" + std::to_string(m.weight_g);
    s += ",\"ageDays\":" + std::to_string(std::max(0, world_day(w) - m.born_day));
    if (m.tag == Tag::LostPiou) s += ",\"tag\":\"lostPiou\"";
    if (m.tag == Tag::Giant) s += ",\"tag\":\"giant\"";
    s += ",\"moves\":" + moves_json(m.species, m.genes, st) + "}";
  }
  return s + "]";
}

namespace {

// Who earned what in the last battle: levels and experience before and after.
std::string gains_json(const World& w) {
  std::string s = "[";
  for (size_t i = 0; i < w.battle.outcome_gains.size(); ++i) {
    const XpGain& g = w.battle.outcome_gains[i];
    if (i) s += ',';
    uint16_t level = g.level_before;
    uint32_t xp = g.xp_before + g.xp;
    while (level < 100 && xp >= monster_xp_to_next(level)) xp -= monster_xp_to_next(level++);
    const Monster* m = g.slot < w.team.size() ? &w.team[g.slot] : nullptr;
    s += "{\"slot\":" + std::to_string(g.slot) + ",\"xp\":" + std::to_string(g.xp);
    if (m) {
      s += std::string(",\"species\":\"") + species_name(m->species) + "\",\"look\":" + std::to_string(genes_code(m->genes));
    }
    s += ",\"levelBefore\":" + std::to_string(g.level_before) + ",\"xpBefore\":" + std::to_string(g.xp_before);
    s += ",\"levelAfter\":" + std::to_string(level) + ",\"xpAfter\":" + std::to_string(xp);
    // How its bar fills, level by level: [level, from, to, needed].
    s += ",\"steps\":[";
    uint16_t l = g.level_before;
    uint32_t from = g.xp_before, left = g.xp;
    for (int n = 0; n < 100; ++n) {
      const uint32_t need = monster_xp_to_next(l);
      const uint32_t to = l < 100 ? std::min(need, from + left) : from;
      if (n) s += ',';
      s += "[" + std::to_string(l) + "," + std::to_string(from) + "," + std::to_string(to) + "," + std::to_string(need) + "]";
      if (to < need || l >= 100) break;
      left -= to - from;
      from = 0;
      ++l;
    }
    s += "]}";
  }
  return s + "]";
}

const char* phase_name(BattlePhase p) {
  static const char* kNames[] = {"none", "fight", "flee", "training", "prepare"};
  return kNames[static_cast<size_t>(p)];
}

}  // namespace

namespace {

const char* no_xp_name(NoXp n) {
  static const char* kNames[] = {"none", "beaten", "released", "trainer"};
  return kNames[static_cast<size_t>(n)];
}

std::string ally_json(const World& w, const Fighter& f) {
  const Monster& a = *active_monster(w);
  const FightStats st = monster_fight_stats(a);
  std::string s = "{\"species\":\"" + std::string(species_name(a.species)) + "\"";
  s += ",\"slot\":" + std::to_string(w.active);
  s += ",\"level\":" + std::to_string(a.level) + ",\"hp\":" + std::to_string(a.hp);
  s += ",\"maxHp\":" + std::to_string(st.hp);
  s += ",\"look\":" + std::to_string(genes_code(a.genes));
  s += ",\"lift\":" + num(f.lift) + ",\"shield\":" + seconds(f.shield_ticks);
  s += ",\"moves\":" + moves_json(a.species, a.genes, st);
  s += ",\"fighter\":" + fighter_json(w, f, species_moves(a.species), true) + "}";
  return s;
}

std::string foe_json(const World& w, size_t k) {
  const Foe& f = w.battle.foes[k];
  const Creature& e = w.creatures[f.index];
  std::string s = "{\"slot\":" + std::to_string(k) + ",\"index\":" + std::to_string(f.index);
  s += std::string(",\"species\":\"") + species_name(e.species) + "\"";
  s += ",\"level\":" + std::to_string(e.level) + ",\"hp\":" + std::to_string(f.hp);
  s += ",\"maxHp\":" + std::to_string(f.max_hp) + ",\"xp\":" + std::to_string(f.xp);
  s += ",\"elements\":" + elements_json(creature_elements(e.species, e.genes));
  s += ",\"look\":" + std::to_string(genes_code(e.genes));
  s += std::string(",\"form\":\"") + form_name(e.genes.form) + "\"";
  s += std::string(",\"targeted\":") + (w.battle.target == k ? "true" : "false");
  s += ",\"y\":" + num(e.body.pos.y - w.ground.height(e.body.pos.x, e.body.pos.z));
  s += ",\"fighter\":" + fighter_json(w, f.fighter, species_moves(e.species), false) + "}";
  return s;
}

}  // namespace

namespace {
// Capturing (battle.hpp): the circle of runes around the foe.
std::string seal_json(const World& w) {
  const Seal& e = w.battle.seal;
  static const char* kStages[] = {"none", "show", "input", "right", "wrong", "caught", "escaped"};
  const Creature& c = w.creatures[e.foe];
  float glow = 0;
  const int lit = seal_lit(w, &glow);
  std::string s = std::string("{\"stage\":\"") + kStages[static_cast<size_t>(e.stage)] + "\"";
  s += ",\"foe\":" + std::to_string(e.foe) + ",\"species\":\"" + species_name(c.species) + "\"";
  s += std::string(",\"element\":\"") + element_name(creature_elements(c.species, c.genes).first) + "\"";
  s += ",\"runes\":" + std::to_string(e.runes) + ",\"length\":" + std::to_string(e.length);
  // In percents.
  s += ",\"power\":" + num(e.power / 10.0f) + ",\"bar\":" + num(seal_bar(w));
  s += ",\"gain\":" + num(e.gain / 10.0f) + ",\"loss\":" + num(rules::kSealLoss / 10.0f);
  s += ",\"lit\":" + std::to_string(lit) + ",\"glow\":" + std::to_string(glow);
  s += ",\"done\":" + std::to_string(e.done) + ",\"credited\":" + std::to_string(e.credited);
  s += ",\"round\":" + std::to_string(e.round);
  s += ",\"touched\":" + (e.touched == kNoRune ? std::string("-1") : std::to_string(e.touched));
  s += ",\"hint\":" + (e.hint == kNoRune ? std::string("-1") : std::to_string(e.hint));
  s += std::string(",\"guard\":") + (e.guard ? "true" : "false") + ",\"boost\":" + (e.boost ? "true" : "false");
  s += ",\"touchTick\":" + std::to_string(e.touch_tick) + ",\"timer\":" + std::to_string(e.timer);
  s += ",\"order\":[";
  for (size_t i = 0; i < e.length; ++i) s += (i ? "," : "") + std::to_string(e.order[i]);
  s += "]";
  return s + "}";
}
}  // namespace

std::string battle_json(const World& w) {
  const Battle& b = w.battle;
  std::string s = std::string("{\"phase\":\"") + phase_name(b.phase) + "\"";
  s += ",\"tick\":" + std::to_string(w.tick);
  // An aggressive monster's battle (west.hpp); the last time the magician was sent back to the farm.
  s += std::string(",\"forced\":") + (b.forced ? "true" : "false") + ",\"rescueTick\":" + std::to_string(w.rescue_tick);
  s += ",\"projectiles\":[";
  for (size_t k = 0; k < b.projectiles.size(); ++k) {
    s += std::string(k ? "," : "") + "\"" + element_name(b.projectiles[k].element) + "\"";
  }
  s += "]";
  s += std::string(",\"outcome\":{\"result\":\"") + outcome_name(b.outcome) + "\",\"tick\":" +
       std::to_string(b.outcome_tick) + ",\"xp\":" + std::to_string(b.outcome_xp) + ",\"species\":\"" +
       species_name(b.outcome_species) + "\",\"count\":" + std::to_string(b.outcome_count) + ",\"noXp\":\"" +
       no_xp_name(b.outcome_no_xp) + "\",\"rival\":" + std::to_string(b.outcome_rival) + ",\"gains\":" + gains_json(w) +
       ",\"wizardXp\":" + std::to_string(b.outcome_wizard_xp) + "}";
  if (b.phase == BattlePhase::None) {
    // A door the player stands at (interior.hpp): its panel.
    const DoorNear d = door_near(w);
    if (d.action != DoorAction::None) {
      static const char* kActions[] = {"none", "enter", "leave", "climb", "descend"};
      static const char* kPlaces[] = {"outside", "cave", "house", "tower", "burrow", "hollowTree", "lighthouse", "mine", "temple"};
      static_assert(std::size(kPlaces) == static_cast<size_t>(PlaceKind::Count));
      s += std::string(",\"door\":{\"action\":\"") + kActions[static_cast<size_t>(d.action)] + "\",\"place\":\"" +
           kPlaces[static_cast<size_t>(d.place)] + "\",\"building\":\"" + object_kind_name(d.building) + "\"";
      if (d.far) s += ",\"far\":true";  // the mine's far way, out of the grotto
      // A house with a name: the refuge, the inn, the farmstead, the sawmill (by where it stands).
      const std::vector<Object>& outside = inside(w) ? w.outdoors.objects : w.objects;
      if (d.object < outside.size()) {
        const Vec2 at{outside[d.object].pos.x, outside[d.object].pos.z};
        const auto by = [&](Vec2 p) { return std::hypot(at.x - p.x, at.z - p.z) < 1.0f; };
        const char* spot = w.north.valid() && by(w.north.refuge) ? "refuge"
                           : w.roads.valid() && by(w.roads.inn) ? "inn"
                           : w.roads.valid() && by(w.roads.farmstead) ? "farmstead"
                           : w.roads.valid() && by(w.roads.sawmill) ? "sawmill"
                           : w.sites.valid() && by(w.sites.lookout) ? "lookout"
                           : w.sites.valid() && by(w.sites.apiary) ? "apiary"
                           : w.sites.valid() && by(w.sites.chapel) ? "chapel" : nullptr;
        if (spot) s += std::string(",\"spot\":\"") + spot + "\"";
      }
      s += "}";
    }
    return s + "}";
  }
  s += ",\"target\":" + (b.target == kNoTarget ? std::string("-1") : std::to_string(b.target));
  // Too far from the foes: seconds left to come back.
  if (b.away_ticks > 0) s += ",\"away\":" + seconds(rules::kAwayTicks - b.away_ticks);
  if (active_monster(w)) s += ",\"ally\":" + ally_json(w, b.ally);
  if (b.phase == BattlePhase::Training) {
    s += ",\"shield\":{\"parriedTick\":" + std::to_string(b.shield.parried_tick) +
         ",\"lastDamage\":" + std::to_string(b.shield.last_damage) + "}";
    return s + "}";
  }
  s += ",\"foes\":[";
  for (size_t k = 0; k < b.foes.size(); ++k) {
    if (k) s += ',';
    s += foe_json(w, k);
  }
  s += "]";
  // The foe aimed at (else the first one): its bars at the top of the screen.
  if (!b.foes.empty()) s += ",\"enemy\":" + foe_json(w, b.target < b.foes.size() ? b.target : 0);
  if (const Foe* t = battle_target(w)) {
    const bool weak = uint32_t{t->hp} * 100u <= uint32_t{t->max_hp} * rules::kCaptureHpPercent;
    s += std::string(",\"capture\":\"") +
         (can_capture(w) ? "ok" : b.rival.npc ? "trainer" : w.team.size() >= rules::kTeamSize ? "teamFull" : !weak ? "notWeak" : "no") + "\"";
  } else {
    s += std::string(",\"capture\":\"") + (b.rival.npc ? "trainer" : "noTarget") + "\"";
  }
  s += ",\"captureHpPercent\":" + std::to_string(rules::kCaptureHpPercent);
  if (sealing(w)) s += ",\"seal\":" + seal_json(w);
  if (b.rival.npc) {
    const Rival& r = b.rival;
    s += ",\"rival\":{\"npc\":" + std::to_string(r.npc) + ",\"out\":" + std::to_string(r.out);
    s += ",\"nextIn\":" + seconds(r.next_in) + ",\"wins\":" + std::to_string(w.rival_wins) + ",\"team\":[";
    for (uint8_t k = 0; k < r.count; ++k) {
      const RivalMonster& m = r.team[k];
      if (k) s += ',';
      s += "{\"slot\":" + std::to_string(k) + std::string(",\"seen\":") + (m.seen ? "true" : "false");
      if (m.seen) {
        s += std::string(",\"species\":\"") + species_name(m.species) + "\",\"look\":" + std::to_string(genes_code(m.genes));
        s += ",\"level\":" + std::to_string(m.level);
      }
      s += std::string(",\"ko\":") + (m.hp == 0 ? "true" : "false");
      s += std::string(",\"out\":") + (k == r.out && !b.foes.empty() ? "true" : "false") + "}";
    }
    s += "],\"teamLeft\":" + std::to_string(std::count_if(w.team.begin(), w.team.end(), [](const Monster& m) { return m.hp > 0; }));
    s += ",\"mine\":[";
    for (size_t k = 0; k < w.team.size(); ++k) {
      if (k) s += ',';
      s += std::string("{\"slot\":") + std::to_string(k) + ",\"ko\":" + (w.team[k].hp == 0 ? "true" : "false") + "}";
    }
    s += "]}";
  }
  return s + "}";
}


std::string bag_json(const World& w) {
  std::string s = "{\"items\":[";
  for (size_t k = 0; k < kItemCount; ++k) {
    const ItemInfo& info = item_info(static_cast<Item>(k));
    if (k) s += ',';
    s += std::string("{\"id\":\"") + info.id + "\",\"item\":" + std::to_string(k) + ",\"count\":" +
         std::to_string(w.bag[k]) + ",\"quest\":" + (info.quest ? "true" : "false") + ",\"price\":" +
         std::to_string(info.price) + "}";
  }
  const uint16_t most = wizard_stats(w.wizard).mana;
  s += "],\"coins\":" + std::to_string(w.bag[static_cast<size_t>(Item::Coin)]);
  s += ",\"potionMana\":" + std::to_string(rules::kPotionMana);
  s += std::string(",\"canDrink\":") +
       (w.bag[static_cast<size_t>(Item::ManaPotion)] > 0 && w.wizard.mana < most ? "true" : "false");
  return s + "}";
}

namespace {
std::string kept_monster_json(const Monster& m, size_t index) {
  std::string s = "{\"index\":" + std::to_string(index);
  s += std::string(",\"species\":\"") + species_name(m.species) + "\"";
  s += ",\"level\":" + std::to_string(m.level) + ",\"hp\":" + std::to_string(m.hp) +
       ",\"maxHp\":" + std::to_string(monster_fight_stats(m).hp);
  s += ",\"look\":" + std::to_string(genes_code(m.genes));
  s += ",\"variants\":" + carried_variants_json(m.species, m.genes);
  s += ",\"elements\":" + elements_json(creature_elements(m.species, m.genes));
  s += std::string(",\"form\":\"") + form_name(m.genes.form) + "\"";
  s += std::string(",\"chromatic\":") + (m.genes.chromatic ? "true" : "false");
  s += std::string(",\"elemental\":") + (m.genes.elemental ? "true" : "false");
  return s + "}";
}
}  // namespace

std::string tournament_json(const World& w) {
  const Tournament& t = w.tournament;
  std::string s = "{\"cups\":[";
  for (uint8_t k = 0; k < rules::kCups; ++k) {
    if (k) s += ",";
    s += std::string("{\"won\":") + ((w.cups >> k) & 1u ? "true" : "false") +
         ",\"open\":" + (can_enter_tournament(w, k) ? "true" : "false") + ",\"level\":" + std::to_string(rules::kCupLevels[k]) +
         ",\"coins\":" + std::to_string(rules::kCupCoins[k]) + "}";
  }
  s += std::string("],\"on\":") + (t.on ? "true" : "false") + ",\"cup\":" + std::to_string(t.cup) +
       ",\"round\":" + std::to_string(t.round) + ",\"rounds\":" + std::to_string(rules::kTournamentRounds) +
       ",\"nextIn\":" + std::to_string(t.next_in) + "}";
  return s;
}

std::string farm_json(const World& w) {
  const bool near = near_npc(w, npc::kCapucine) || near_npc(w, npc::kMargaux);
  std::string s = std::string("{\"open\":") + (farm_open(w) ? "true" : "false");
  s += std::string(",\"near\":") + (near ? "true" : "false");
  s += std::string(",\"usable\":") + (farm_open(w) && near && w.battle.phase == BattlePhase::None ? "true" : "false");
  s += ",\"teamSize\":" + std::to_string(rules::kTeamSize) + ",\"active\":" + std::to_string(w.active);
  s += ",\"team\":[";
  for (size_t k = 0; k < w.team.size(); ++k) s += (k ? "," : "") + kept_monster_json(w.team[k], k);
  s += "],\"kept\":[";
  for (size_t k = 0; k < w.farm.size(); ++k) s += (k ? "," : "") + kept_monster_json(w.farm[k], k);
  return s + "]}";
}

std::string shop_json(const World& w) {
  const bool open = w.quests[static_cast<size_t>(QuestId::Merchant)].stage == QuestStage::Done;
  std::string s = std::string("{\"open\":") + (open ? "true" : "false");
  s += std::string(",\"near\":") + (near_npc(w, npc::kAnselme) ? "true" : "false");
  s += ",\"coins\":" + std::to_string(w.bag[static_cast<size_t>(Item::Coin)]) + ",\"items\":[";
  bool first = true;
  for (size_t k = 0; k < kItemCount; ++k) {
    const ItemInfo& info = item_info(static_cast<Item>(k));
    if (info.price == 0) continue;
    s += std::string(first ? "" : ",") + "{\"id\":\"" + info.id + "\",\"item\":" + std::to_string(k) +
         ",\"price\":" + std::to_string(info.price) + ",\"have\":" + std::to_string(w.bag[k]) + "}";
    first = false;
  }
  return s + "]}";
}

}  // namespace monstres
