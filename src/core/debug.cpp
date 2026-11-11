#include "monstres/core/debug.hpp"

#include <cmath>
#include <cstdio>
#include <iterator>

namespace monstres {

namespace {
const char* species_name(Species s) {
  static const char* kNames[] = {"companion", "slime", "mushroom", "bird", "villager", "crow", "fish"};
  const auto i = static_cast<size_t>(s);
  return i < std::size(kNames) ? kNames[i] : "?";
}

const char* mind_name(Mind m) {
  static const char* kNames[] = {"idle", "wander", "flee", "follow", "greet", "fly"};
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
  static const char* kNames[] = {"fire", "water", "nature", "air", "shadow", "light"};
  const auto i = static_cast<size_t>(element);
  return i < std::size(kNames) ? kNames[i] : "?";
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
    s += std::string(",\"element\":\"") + element_name(st.element) + "\"";
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
