#include "monstres/core/routines.hpp"

#include "monstres/core/battle.hpp"
#include "monstres/core/interior.hpp"
#include "monstres/core/quests.hpp"
#include "monstres/core/west.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace monstres {

namespace {

uint32_t mix32(uint64_t x) {
  x += 0x9E3779B97F4A7C15ULL;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
  return static_cast<uint32_t>((x ^ (x >> 31)) >> 16);
}

float unit_of(uint32_t h) { return static_cast<float>(h & 0xFFFF) / 65536.0f; }

float flat(const Vec3& a, Vec2 b) { return std::hypot(a.x - b.x, a.z - b.z); }

bool player_near(const World& w, Vec3 p, float d) {
  const float dx = p.x - w.player.pos.x, dz = p.z - w.player.pos.z;
  return dx * dx + dz * dz < d * d;
}

float pick(Rng& r, float lo, float hi) { return lo + (hi - lo) * static_cast<float>(r.below(10001)) / 10000.0f; }

uint16_t seconds(float s) { return static_cast<uint16_t>(std::max(1.0f, s * static_cast<float>(rules::kTicksPerSecond))); }

// ---- The villagers ----

// A house one lives in: the spot just before its door (where the player uses it).
bool door_spot(const Object& o, Vec2& at) {
  const PlaceKind place = place_behind(o.kind);
  if (place != PlaceKind::House && place != PlaceKind::Lighthouse) return false;
  at = door_use_spot(o);
  return true;
}

Vec3 on_ground(const World& w, Vec2 p) { return {p.x, w.ground.empty() ? 0.0f : w.ground.height(p.x, p.z), p.z}; }

// ---- The wild monsters ----

struct Habits {
  bool day = false;         // sleeps at night
  bool night = false;       // sleeps by day
  bool hates_rain = false;  // goes under a tree
  bool loves_rain = false;  // roams farther in it
  bool snow = false;        // plays in the snow
  bool storm = false;       // dashes about in a storm
  bool shade = false;       // the shade of a tree at noon in summer
  bool drinks = false;
  bool grazes = false;
  bool herds = false;
  bool playful = false;
  bool cold = false;        // sleeps by a fire
};

Habits habits(Species s) {
  Habits h;
  switch (s) {
    case Species::Slime: h.day = h.loves_rain = h.drinks = h.playful = true; break;
    case Species::Mushroom: h.day = h.loves_rain = h.shade = true; break;
    case Species::Bird: h.day = h.hates_rain = h.drinks = h.grazes = h.playful = h.cold = true; break;
    case Species::Spark: h.day = h.hates_rain = h.storm = h.drinks = h.playful = h.cold = true; break;
    case Species::Axolotl: h.day = h.loves_rain = h.drinks = h.playful = true; break;
    case Species::Raccoon: h.night = h.hates_rain = h.drinks = h.playful = h.cold = true; break;
    case Species::Sheep: h.day = h.hates_rain = h.drinks = h.grazes = h.herds = true; break;
    case Species::Mole: h.day = h.hates_rain = true; break;
    case Species::Golem: h.day = true; break;
    case Species::Goat: h.day = h.hates_rain = h.drinks = h.grazes = h.herds = h.playful = true; break;
    case Species::Ermine: h.day = h.hates_rain = h.snow = h.drinks = h.playful = h.cold = true; break;
    default: break;
  }
  return h;
}

// Who hunts whom (no harm done: a chase, then the prey gets away).
bool hunts(Species hunter, Species prey) {
  if (hunter == Species::Raccoon) return prey == Species::Bird;
  if (hunter == Species::Ermine) return prey == Species::Bird || prey == Species::Mole || prey == Species::Spark;
  return false;
}

// A monster with no part to play (not the shepherd's flock, a trainer's,
// an aggressive one...), out in the wild, not fighting.
bool free_monster(const World& w, const Creature& c) {
  return c.tag == Tag::None && c.species != Species::Villager && c.species != Species::Fish && !flies(c.species) &&
         c.species != Species::Cristi && c.mind != Mind::KnockedOut && c.mind != Mind::Battle && !owned(w, c);
}

bool water_at(const World& w, float x, float z) {
  if (in_lake(w.village.lake, x, z)) return true;
  if (w.ground.empty()) return false;
  const float g = w.ground.height(x, z);
  if (w.ground.sea(x, z) && g < rules::kSeaLevel - 0.1f) return true;
  if (w.west.valid()) {
    float level = 0, width = 0;
    if (river_surface(w.west, x, z, g, 6.0f, level, width) < 6.0f && g < level - 0.05f) return true;
  }
  return false;
}

// The nearest tree (its middle and how far its trunk reaches), within reach.
bool tree_near(const World& w, Vec2 from, float reach, Vec2& at, float& trunk) {
  float best = reach;
  bool found = false;
  const Box area{{from.x - reach, -1e9f, from.z - reach}, {from.x + reach, 1e9f, from.z + reach}};
  for_objects_near(w, area, [&](uint32_t k, const Box& b) {
    const Object& o = w.objects[k];
    if (o.kind != ObjectKind::Tree && o.kind != ObjectKind::Pine && o.kind != ObjectKind::Larch) return;
    const float d = flat(o.pos, from);
    if (d >= best) return;
    best = d;
    at = {o.pos.x, o.pos.z};
    trunk = std::max(b.max.x - b.min.x, b.max.z - b.min.z) * 0.5f;
    found = true;
  });
  return found;
}

bool fire_near(const World& w, Vec2 from, float reach, Vec2& at) {
  float best = reach;
  bool found = false;
  const Box area{{from.x - reach, -1e9f, from.z - reach}, {from.x + reach, 1e9f, from.z + reach}};
  for_objects_near(w, area, [&](uint32_t k, const Box&) {
    const Object& o = w.objects[k];
    if (o.kind != ObjectKind::Campfire) return;
    const float d = flat(o.pos, from);
    if (d >= best) return;
    best = d;
    at = {o.pos.x, o.pos.z};
    found = true;
  });
  return found;
}

// Its hours of sleep: the day sleepers from about 21:30 to 5:30, Chipou from
// about 10:00 to 17:00; each its own (a quarter of an hour to an hour and a half later).
bool sleeping_hours(const World& w, const Creature& c, const Habits& h) {
  const float hour = hour_of_day(w);
  const float late = 0.25f + 1.25f * unit_of(mix32(c.id * 0x51ED27ULL + 7));
  if (h.day) return hour >= 21.0f + late || hour < 5.0f + late;
  if (h.night) return hour >= 9.5f + late && hour < 16.5f + late;
  return false;
}

void face(Body& b, Vec2 to) {
  const float dx = to.x - b.pos.x, dz = to.z - b.pos.z, d = std::hypot(dx, dz);
  if (d < 1e-3f) return;
  b.facing_x = dx / d;
  b.facing_z = dz / d;
}

void set_off(Creature& c, Activity act, Vec2 target, float timeout) {
  c.act = act;
  c.mind = Mind::Wander;
  c.target = target;
  c.timer = seconds(timeout);
}

void stay(Creature& c, Activity act, float time) {
  c.act = act;
  c.mind = Mind::Idle;
  c.timer = seconds(time);
}

}  // namespace

// ---- The villagers ----

bool keeps_routine(uint8_t n) {
  if (n == 0 || npc::wanderer(n) || npc::monk(n) || n == npc::kContender) return false;
  // Waiting for a battle, keeping a gate or an arena: in all weathers.
  return n != npc::kBastien && n != npc::kRomane && n != npc::kTristan && n != npc::kBarnabe && n != npc::kGauthier;
}

bool wants_home(const World& w, const Creature& c, bool home) {
  if (!keeps_routine(c.npc)) return false;
  const uint32_t own = mix32(0x524F5554494E45ULL ^ c.npc);  // "ROUTINE"
  const float bed = 20.5f + 2.0f * unit_of(own);         // in bed between 20:30 and 22:30...
  const float rise = 5.5f + 2.0f * unit_of(own >> 16);   // ...up between 5:30 and 7:30
  const float hour = hour_of_day(w);
  if (hour >= bed || hour < rise) return true;
  const Weather sky = weather_field(w).at(c.home.x, c.home.z);
  if (sky.rain > (home ? rules::kRainOut : rules::kRainHome) || sky.snow > (home ? rules::kSnowOut : rules::kSnowHome) ||
      sky.storm > (home ? rules::kStormOut : rules::kStormHome)) {
    return true;
  }
  if (w.sky.season == Season::Winter) {
    if (hour < rise + 2.5f || hour >= bed - 3.0f) return true;  // the short days
    const auto slot = static_cast<uint64_t>(world_day(w)) * 12 + static_cast<uint64_t>(hour / 2.0f);
    if (mix32(own ^ (slot * 0x9E3779B1ULL)) % 100 < rules::kWinterHearth) return true;  // by the hearth a while
  }
  return false;
}

bool home_door(const World& w, const Creature& c, Vec2& door) {
  if (inside(w) || w.ground.empty()) return false;
  const float reach = rules::kHomeReach;
  const Box area{{c.home.x - reach, -1e9f, c.home.z - reach}, {c.home.x + reach, 1e9f, c.home.z + reach}};
  // The houses around its place (each once, in the objects' order).
  std::vector<std::pair<uint32_t, float>> houses;
  for_objects_near(w, area, [&](uint32_t k, const Box&) {
    Vec2 at;
    const float d = flat(w.objects[k].pos, c.home);
    if (d > reach || !door_spot(w.objects[k], at)) return;
    const Vec3 o = w.objects[k].pos;
    if (c.npc <= npc::kMentor && w.village.lake.radius_x > 0 && !in_village(w.village, o.x, o.z, 8.0f)) return;  // the village's
    if (std::none_of(houses.begin(), houses.end(), [k](const auto& h) { return h.first == k; })) houses.emplace_back(k, d);
  });
  if (houses.empty()) return false;
  std::sort(houses.begin(), houses.end());
  size_t pick_k = 0;
  if (c.npc <= npc::kMentor) {
    // The village's people (and Isidore): each its own house among the village's (two to a house when too few).
    pick_k = (static_cast<size_t>(c.npc) + mix32(w.seed)) % houses.size();
  } else {
    // The others: the nearest to their place (their hut, their farm, their lighthouse).
    for (size_t k = 1; k < houses.size(); ++k) pick_k = houses[k].second < houses[pick_k].second ? k : pick_k;
  }
  door = door_use_spot(w.objects[houses[pick_k].first]);
  return true;
}

void villager_routine(World& w, Creature& c) {
  if (c.species != Species::Villager || inside(w)) return;
  if (!keeps_routine(c.npc)) {
    if (c.act == Activity::Home || c.act == Activity::GoHome) c.act = Activity::None;
    return;
  }
  if ((w.tick + c.id) % rules::kRoutineEvery != 0) return;
  const bool home = c.act == Activity::Home || c.act == Activity::GoHome;
  const bool want = wants_home(w, c, home);
  const bool seen = player_near(w, c.body.pos, rules::kSeenDistance);
  if (want && !home) {
    Vec2 door;
    if (!home_door(w, c, door)) return;  // no house: it stays out
    if (seen) {
      set_off(c, Activity::GoHome, door, 45.0f);
      c.other = 0;
    } else {
      c.act = Activity::Home;
      c.mind = Mind::Idle;
      c.body.pos = on_ground(w, door);
    }
  } else if (!want && home) {
    if (c.act == Activity::Home) {
      Vec2 door;
      const bool has_door = home_door(w, c, door);
      // Seen: out by its door. Unseen: back at its place at once (the villagers of the village: by their door).
      const bool own_place = c.npc >= npc::kMentor || c.npc == npc::kGaspard;
      if (!seen && own_place) c.body.pos = on_ground(w, c.home);
      else if (has_door) c.body.pos = on_ground(w, door);
      if (has_door) face(c.body, {2 * c.body.pos.x - c.home.x, 2 * c.body.pos.z - c.home.z});  // its back to the door
    }
    c.act = Activity::None;
    c.mind = Mind::Idle;
    c.timer = seconds(1.0f);
  }
}

void spawn_residents(World& w) {
  if (!inside(w) || w.place.door >= w.outdoors.objects.size()) return;
  Vec2 door;
  if (!door_spot(w.outdoors.objects[w.place.door], door)) return;
  const BodyShape& shape = species_info(Species::Villager).shape;
  Rng rng(w.seed ^ 0x5245534944454E54ULL, w.place.door);  // "RESIDENT"
  const Vec3 entry = w.interior.entry;
  for (const Creature& out : w.outdoors.creatures) {
    if (out.species != Species::Villager || out.act != Activity::Home || flat(out.body.pos, door) > 0.6f) continue;
    if (npc_index(w, out.npc) >= 0) continue;
    Creature c = out;
    c.act = Activity::None;
    c.mind = Mind::Idle;
    c.timer = seconds(1.0f + pick(rng, 0, 3));
    bool placed = false;
    for (int attempt = 0; attempt < 80 && !placed; ++attempt) {
      const float x = pick(rng, -7, 7), z = pick(rng, -7, 7);
      if (wall_distance(w.interior, x, z) < 0.75f || std::hypot(x - entry.x, z - entry.z) < 1.6f) continue;
      c.body = Body{};
      c.body.pos = {x, floor_height(w.interior, x, z), z};
      if (std::fabs(c.body.pos.y - entry.y) > 0.3f || inside_any_object(w, c.body, shape)) continue;
      placed = true;
    }
    if (!placed) continue;
    face(c.body, {entry.x, entry.z});  // turned to who comes in
    c.home = {c.body.pos.x, c.body.pos.z};
    add_creature(w, c);
  }
}

// ---- The wild monsters ----

int creature_by_id(const World& w, uint32_t id) {
  if (id == 0) return -1;
  for (size_t i = 0; i < w.creatures.size(); ++i) {
    if (w.creatures[i].id == id) return static_cast<int>(i);
  }
  return -1;
}

bool water_edge(const World& w, Vec2 from, float reach, Vec2& edge, Vec2& facing) {
  float best = 1e9f;
  for (float r = 2.0f; r <= reach; r += 2.0f) {
    for (int k = 0; k < 16; ++k) {
      const SinCos sc = sincos16(static_cast<Angle16>(k * 4096 + static_cast<int>(r * 997.0f)));
      const Vec2 p{from.x + sc.cos * r, from.z + sc.sin * r};
      if (!rules::in_world(p.x, p.z, 2.0f) || water_at(w, p.x, p.z)) continue;
      // Water just ahead, on one of four sides.
      for (int s = 0; s < 4; ++s) {
        const SinCos d = sincos16(static_cast<Angle16>(s * 16384 + k * 4096));
        if (!water_at(w, p.x + d.cos * 1.6f, p.z + d.sin * 1.6f)) continue;
        const float score = r;
        if (score < best) {
          best = score;
          edge = p;
          facing = {d.cos, d.sin};
        }
      }
    }
    if (best < 1e9f) return true;
  }
  return false;
}

float wander_reach(const World& w, const Creature& c) {
  if (c.tag == Tag::Flock) return rules::kPenHalf - 1.2f;
  if (c.tag == Tag::Boarder) return 6.0f;
  if (!free_monster(w, c) || w.ground.empty()) return rules::kWanderRadius;
  const Habits h = habits(c.species);
  if (h.loves_rain && weather_field(w).at(c.body.pos.x, c.body.pos.z).rain > 0.3f) return rules::kWanderRadius * 1.8f;
  return rules::kWanderRadius;
}

bool wild_runs(const Creature& c) {
  return c.mind == Mind::Flee || c.act == Activity::Chase || c.act == Activity::Play;
}

bool wild_decide(World& w, Creature& c) {
  c.act = Activity::None;
  c.other = 0;
  if (!free_monster(w, c) || w.ground.empty() || inside(w)) return false;
  const Habits h = habits(c.species);
  Rng& rng = w.rng;
  const Vec2 at{c.body.pos.x, c.body.pos.z};

  // The night (Chipou: the day): asleep, by a fire for the cold ones.
  if (sleeping_hours(w, c, h)) {
    Vec2 fire;
    if (h.cold && fire_near(w, at, rules::kFireReach, fire)) {
      const SinCos sc = sincos16(static_cast<Angle16>(mix32(c.id) & 0xFFFF));
      const float r = 2.0f + 0.6f * unit_of(mix32(c.id + 3));
      set_off(c, Activity::Warm, {fire.x + sc.cos * r, fire.z + sc.sin * r}, 20.0f);
      return true;
    }
    stay(c, Activity::Sleep, 20.0f);
    return true;
  }

  const Weather sky = weather_field(w).at(at.x, at.z);
  const Zone zone = zone_at(w, at.x, at.z);
  const float hour = hour_of_day(w);
  // Zapou in a storm, Givri in the snow: they dash about.
  if ((h.storm && sky.storm > 0.3f) ||
      (h.snow && (sky.snow > 0.3f || zone == Zone::Frost || zone == Zone::FrozenLake) && rng.below(100) < 35)) {
    const SinCos sc = sincos16(static_cast<Angle16>(rng.below(65536)));
    const float r = pick(rng, 6.0f, 12.0f);
    Vec2 to{c.home.x + sc.cos * r, c.home.z + sc.sin * r};
    to = {std::clamp(to.x, -1e4f, 1e4f), std::clamp(to.z, -1e4f, 1e4f)};
    set_off(c, Activity::Play, to, 3.0f);
    return true;
  }
  // The rain (the snow, but for Givri): under a tree. Champi: in its shade at noon in summer.
  const bool wet = sky.rain > 0.35f || (sky.snow > 0.4f && !h.snow) || sky.storm > 0.25f;
  const bool sunny = w.sky.season == Season::Summer && sky.cover < 0.35f && hour > 11.0f && hour < 16.5f;
  if ((h.hates_rain && wet) || (h.shade && sunny)) {
    Vec2 tree;
    float trunk = 0.5f;
    if (tree_near(w, at, rules::kShelterReach, tree, trunk)) {
      const float dx = at.x - tree.x, dz = at.z - tree.z, d = std::max(0.01f, std::hypot(dx, dz));
      const float r = trunk + 0.9f;
      set_off(c, Activity::Shelter, {tree.x + dx / d * r, tree.z + dz / d * r}, 12.0f);
      return true;
    }
    if (h.hates_rain && wet) {  // nowhere to go: it waits, huddled
      stay(c, Activity::None, pick(rng, 4.0f, 8.0f));
      return true;
    }
  }

  // The others around.
  const Creature* kin = nullptr;
  const Creature* prey = nullptr;
  float kin_d = rules::kKinReach, prey_d = rules::kPreyReach;
  float herd_x = 0, herd_z = 0;
  int herd = 0;
  for (const Creature& o : w.creatures) {
    if (&o == &c || o.species == Species::Villager) continue;
    const float d = flat(o.body.pos, at);
    if (d > rules::kHerdReach) continue;
    if (o.species == c.species && free_monster(w, o)) {
      herd_x += o.body.pos.x;
      herd_z += o.body.pos.z;
      ++herd;
      if (d < kin_d && o.act == Activity::None && o.mind != Mind::Flee) {
        kin = &o;
        kin_d = d;
      }
    }
    if (hunts(c.species, o.species) && free_monster(w, o) && !asleep(o) && d < prey_d) {
      prey = &o;
      prey_d = d;
    }
  }
  if (prey && rng.below(100) < 35) {
    set_off(c, Activity::Chase, {prey->body.pos.x, prey->body.pos.z}, 3.5f);
    c.other = prey->id;
    return true;
  }
  // The companion, of its kind: it comes to sniff it.
  const Creature& companion = w.creatures[0];
  if (w.active >= 0 && companion.species == c.species && w.battle.phase == BattlePhase::None &&
      flat(companion.body.pos, at) < 10.0f && rng.below(100) < 12) {
    set_off(c, Activity::Sniff, {companion.body.pos.x, companion.body.pos.z}, 6.0f);
    c.other = companion.id;
    return true;
  }
  if (kin && h.playful && rng.below(100) < 15) {
    // A game of chase: it runs after the other, which runs away.
    Creature& other = w.creatures[static_cast<size_t>(creature_by_id(w, kin->id))];
    set_off(c, Activity::Play, {other.body.pos.x, other.body.pos.z}, 3.5f);
    c.other = other.id;
    other.act = Activity::Play;
    other.other = c.id;
    other.mind = Mind::Flee;
    other.timer = seconds(3.5f);
    other.target = {2 * other.body.pos.x - at.x, 2 * other.body.pos.z - at.z};
    return true;
  }
  if (kin && rng.below(100) < 8) {  // nose to nose
    set_off(c, Activity::Sniff, {kin->body.pos.x, kin->body.pos.z}, 5.0f);
    c.other = kin->id;
    return true;
  }
  if (h.herds && herd > 0 && rng.below(100) < 60) {  // back to the herd
    const float cx = herd_x / static_cast<float>(herd), cz = herd_z / static_cast<float>(herd);
    set_off(c, Activity::None, {cx + pick(rng, -2.5f, 2.5f), cz + pick(rng, -2.5f, 2.5f)}, 8.0f);
    return true;
  }

  // The land: a drink at the water's edge (more on hot days), a nibble of grass.
  const bool hot = w.sky.season == Season::Summer && sky.cover < 0.5f && hour > 11.0f && hour < 18.0f;
  if (h.drinks && rng.below(100) < (hot ? 22u : 8u)) {
    Vec2 edge, facing;
    if (water_edge(w, at, rules::kWaterReach, edge, facing)) {
      set_off(c, Activity::Drink, edge, 14.0f);
      return true;
    }
  }
  if (h.grazes && rng.below(100) < 45) {
    stay(c, Activity::Graze, pick(rng, 3.0f, 7.0f));
    return true;
  }
  return false;
}

bool wild_watch(World& w, Creature& c) {
  // A prey looks about now and then (but asleep, or at play): a hunter close by, it runs.
  if ((w.tick + c.id) % 10 == 0 && c.mind != Mind::Flee && !asleep(c) && c.act != Activity::Chase &&
      c.act != Activity::Play && free_monster(w, c)) {
    for (const Creature& o : w.creatures) {
      if (!hunts(o.species, c.species) || !free_monster(w, o) || asleep(o)) continue;
      const float dx = c.body.pos.x - o.body.pos.x, dz = c.body.pos.z - o.body.pos.z, d = std::hypot(dx, dz);
      if (d > rules::kPreyFlee || d < 1e-3f) continue;
      c.act = Activity::None;
      c.other = 0;
      c.mind = Mind::Flee;
      c.timer = seconds(2.0f);
      c.target = {c.body.pos.x + dx / d * 7.0f, c.body.pos.z + dz / d * 7.0f};
      return true;
    }
  }
  if (c.act == Activity::None) return true;
  if (!free_monster(w, c)) {
    c.act = Activity::None;
    return true;
  }
  const float pd = std::hypot(c.body.pos.x - w.player.pos.x, c.body.pos.z - w.player.pos.z);
  switch (c.act) {
    case Activity::Sleep:
    case Activity::Warm:
      if (pd < rules::kWakeDistance) {  // woken up
        c.act = Activity::None;
        c.mind = Mind::Idle;
        c.timer = seconds(0.6f);
        return true;
      }
      return c.act == Activity::Warm && c.mind == Mind::Wander;  // asleep: still
    case Activity::Chase:
    case Activity::Play:
    case Activity::Sniff: {
      const int i = creature_by_id(w, c.other);
      if (i < 0) {
        c.act = Activity::None;
        c.other = 0;
        return true;
      }
      const Creature& o = w.creatures[static_cast<size_t>(i)];
      const Vec2 op{o.body.pos.x, o.body.pos.z};
      if (c.mind == Mind::Flee) {  // running away (the one chased at play)
        if ((w.tick + c.id) % 20 == 0) c.target = {2 * c.body.pos.x - op.x, 2 * c.body.pos.z - op.z};
        return true;
      }
      if (c.act == Activity::Sniff) {
        const float d = flat(c.body.pos, op);
        if (d < 1.3f) {  // there: nose to nose, still
          face(c.body, op);
          if (c.mind == Mind::Wander) {
            c.mind = Mind::Idle;
            c.timer = seconds(3.0f);
          }
          return false;
        }
      }
      c.target = op;
      if (c.act == Activity::Chase && flat(c.body.pos, op) < 1.0f) {  // caught up: the prey slips away
        c.act = Activity::None;
        c.other = 0;
        c.mind = Mind::Idle;
        c.timer = seconds(1.5f);
      }
      return true;
    }
    default: return true;
  }
}

const char* activity_name(Activity a) {
  static const char* kNames[] = {"none", "goHome", "home", "sleep", "shelter", "drink", "graze", "play", "chase", "sniff", "warm"};
  static_assert(std::size(kNames) == static_cast<size_t>(Activity::Count));
  const auto i = static_cast<size_t>(a);
  return i < std::size(kNames) ? kNames[i] : "none";
}

}  // namespace monstres
