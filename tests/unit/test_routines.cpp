// The routines (routines.hpp): the villagers going home at night, in the
// rain, in winter, found in their houses; the wild monsters asleep at night,
// under a tree in the rain, at the water's edge, at play, at the chase.

#include "monstres/core/battle.hpp"
#include "monstres/core/debug.hpp"
#include "monstres/core/interior.hpp"
#include "monstres/core/quests.hpp"
#include "monstres/core/routines.hpp"
#include "monstres/core/save.hpp"
#include "test_helpers.hpp"

#include <cmath>

using namespace monstres;

namespace {

float flat(Vec3 a, Vec3 b) { return std::hypot(a.x - b.x, a.z - b.z); }

void put_player(World& w, Vec2 p) {
  w.player.pos = {p.x, w.ground.height(p.x, p.z), p.z};
  w.player.velocity_y = 0;
  w.creatures[0].body.pos = w.player.pos;
}

void set_hour(World& w, float hour) { w.clock = static_cast<uint32_t>(hour * static_cast<float>(rules::kTicksPerHour)); }

// The weather chosen around the player (as the debug menu does).
void set_weather(World& w, WeatherKind kind) {
  w.sky.forced = weather_of(kind);
  w.sky.forced_amount = 1;
}

Creature& villager(World& w, uint8_t n) {
  const int i = npc_index(w, n);
  REQUIRE(i > 0);
  return w.creatures[static_cast<size_t>(i)];
}

// A spot far from every villager (none of them sees the player).
Vec2 far_away(const World& w) {
  for (float x = -500; x <= 500; x += 50) {
    for (float z = -1100; z <= 500; z += 50) {
      bool alone = rules::in_world(x, z, 20.0f);
      for (const Creature& c : w.creatures) {
        if (c.species == Species::Villager && std::hypot(c.body.pos.x - x, c.body.pos.z - z) < 120.0f) alone = false;
      }
      if (alone) return {x, z};
    }
  }
  FAIL("no spot away from the villagers");
  return {};
}

// Only the companion and the villagers left (the wild monsters gone).
void clear_wilds(World& w) {
  for (size_t i = w.creatures.size(); i-- > 1;) {
    if (w.creatures[i].species != Species::Villager) remove_creature(w, i);
  }
}

size_t add_monster(World& w, Species s, Vec2 at) {
  Creature c;
  c.species = s;
  c.body.pos = {at.x, w.ground.height(at.x, at.z), at.z};
  c.home = at;
  c.level = 3;
  return add_creature(w, c);
}

// A tree standing in open land (the valley's), with room around it.
const Object* open_tree(const World& w) {
  for (const Object& o : valley_objects(w)) {
    if (o.kind != ObjectKind::Tree || o.pos.z > -20 || o.pos.z < -300) continue;
    if (w.ground.sea(o.pos.x, o.pos.z) || in_village(w.village, o.pos.x, o.pos.z, 30.0f)) continue;
    return &o;
  }
  return nullptr;
}

}  // namespace

TEST_CASE("every healer heals, and the page knows it") {
  World w = cached_game(6);
  for (const uint8_t n : {npc::kOdette, npc::kViolette, npc::kSolene, npc::kAgathe, npc::kAubin, npc::kGauthier, npc::kYsolde}) {
    CAPTURE(int(n));
    CHECK(npc::healer(n));
    const int i = npc_index(w, n);
    REQUIRE(i > 0);
    const Vec3 p = w.creatures[static_cast<size_t>(i)].body.pos;
    w.player.pos = {p.x + 1.0f, p.y, p.z};
    CHECK(can_heal_team(w, n));
    CHECK(creature_json(w, i).find("\"healer\":true,\"heal\":true") != std::string::npos);
    CHECK((npc_marks(w, n) & kMarkHeal) != 0);
  }
  CHECK_FALSE(npc::healer(npc::kBastien));
}

TEST_CASE("at night the villagers go home, and come out in the morning") {
  World w = cached_game(1);
  set_weather(w, WeatherKind::Clear);
  put_player(w, far_away(w));
  set_hour(w, 23.0f);
  run(w, {}, static_cast<int>(rules::kRoutineEvery) + 2);
  size_t home = 0, out = 0;
  for (const Creature& c : w.creatures) {
    if (c.species != Species::Villager) continue;
    Vec2 door;
    if (keeps_routine(c.npc) && home_door(w, c, door)) {
      CAPTURE(int(c.npc));
      CHECK(c.act == Activity::Home);
      CHECK(std::hypot(c.body.pos.x - door.x, c.body.pos.z - door.z) < 0.5f);  // at its door, unseen
      ++home;
    } else {
      CHECK(c.act == Activity::None);
      ++out;
    }
  }
  CHECK(home >= 8);
  CHECK(out >= 1);  // Bastien, the monks...
  // The village's people each in their own house (two at most to one).
  for (const Creature& a : w.creatures) {
    if (a.species != Species::Villager || a.npc > npc::kMentor || a.act != Activity::Home) continue;
    int sharing = 0;
    for (const Creature& b : w.creatures) {
      sharing += b.species == Species::Villager && b.npc <= npc::kMentor && b.act == Activity::Home && flat(a.body.pos, b.body.pos) < 0.6f;
    }
    CAPTURE(int(a.npc));
    CHECK(sharing <= 2);
  }
  CHECK(villager(w, npc::kBastien).act == Activity::None);
  // Not met outside: no talking at its door.
  Creature& odette = villager(w, npc::kOdette);
  REQUIRE(odette.act == Activity::Home);
  w.player.pos = odette.body.pos;
  CHECK_FALSE(near_npc(w, npc::kOdette));
  CHECK_FALSE(can_heal_team(w, npc::kOdette));
  // In the morning, out again.
  put_player(w, far_away(w));
  set_hour(w, 9.0f);
  run(w, {}, static_cast<int>(rules::kRoutineEvery) + 2);
  for (const Creature& c : w.creatures) {
    if (c.species == Species::Villager) CHECK(c.act == Activity::None);
  }
}

TEST_CASE("a villager seen walks to its door, and is found inside its house") {
  World w = cached_game(1);
  set_weather(w, WeatherKind::Clear);
  set_hour(w, 22.8f);
  Creature& odette = villager(w, npc::kOdette);
  Vec2 door;
  REQUIRE(home_door(w, odette, door));
  // The player some way off: she walks.
  const Vec3 start = odette.body.pos;
  put_player(w, {start.x + 9.0f, start.z + 9.0f});
  run(w, {}, static_cast<int>(rules::kRoutineEvery) + 2);
  CHECK(villager(w, npc::kOdette).act == Activity::GoHome);
  int ticks = 0;
  while (villager(w, npc::kOdette).act == Activity::GoHome && ticks < 50 * rules::kTicksPerSecond) {
    run(w, {}, 10);
    ticks += 10;
  }
  CHECK(villager(w, npc::kOdette).act == Activity::Home);
  // Into her house: there she is, she heals.
  put_player(w, door);
  const DoorNear d = door_near(w);
  REQUIRE(d.action == DoorAction::Enter);
  CHECK(d.place == PlaceKind::House);
  REQUIRE(use_door(w));
  const int inside_index = npc_index(w, npc::kOdette);
  REQUIRE(inside_index > 0);
  Creature& in = w.creatures[static_cast<size_t>(inside_index)];
  CHECK(in.act == Activity::None);
  CHECK(wall_distance(w.interior, in.body.pos.x, in.body.pos.z) > 0.5f);
  w.player.pos = {in.body.pos.x + 1.0f, in.body.pos.y, in.body.pos.z};
  CHECK(near_npc(w, npc::kOdette));
  CHECK(can_heal_team(w, npc::kOdette));
  // A save made there: she is still in.
  const auto loaded = deserialize(serialize(w));
  REQUIRE(loaded.has_value());
  CHECK(loaded->place.kind == PlaceKind::House);
  CHECK(npc_index(*loaded, npc::kOdette) > 0);
  // Out again: she stays home (night).
  leave_place(w);
  CHECK(villager(w, npc::kOdette).act == Activity::Home);
}

TEST_CASE("the rain sends the villagers home, winter keeps them in a good part of the day") {
  World w = cached_game(2);
  put_player(w, far_away(w));
  set_hour(w, 11.0f);
  set_weather(w, WeatherKind::Rain);
  // (The debug choice reaches far around the player: the rain everywhere.)
  run(w, {}, static_cast<int>(rules::kRoutineEvery) + 2);
  size_t home = 0, routine = 0;
  for (const Creature& c : w.creatures) {
    Vec2 door;
    if (c.species != Species::Villager || !keeps_routine(c.npc) || !home_door(w, c, door)) continue;
    ++routine;
    home += c.act == Activity::Home;
  }
  CHECK(routine >= 8);
  CHECK(home == routine);
  // Fine weather again: out.
  set_weather(w, WeatherKind::Clear);
  run(w, {}, static_cast<int>(rules::kRoutineEvery) + 2);
  for (const Creature& c : w.creatures) {
    if (c.species == Species::Villager) CHECK(c.act == Activity::None);
  }
  // Winter: early in the morning all in, at noon some.
  w.sky.season = Season::Winter;
  set_hour(w, 8.0f);
  run(w, {}, static_cast<int>(rules::kRoutineEvery) + 2);
  size_t morning = 0;
  for (const Creature& c : w.creatures) morning += c.act == Activity::Home;
  CHECK(morning == routine);
  size_t noon = 0;
  for (int day = 0; day < 4; ++day) {
    w.tick += rules::kTicksPerDay;
    set_hour(w, 12.5f);
    run(w, {}, static_cast<int>(rules::kRoutineEvery) + 2);
    for (const Creature& c : w.creatures) noon += c.act == Activity::Home;
  }
  CHECK(noon > routine / 2);       // a good part at home...
  CHECK(noon < routine * 4 - routine / 2);  // ...not all
}

TEST_CASE("the villagers at home are saved") {
  World w = cached_game(1);
  set_weather(w, WeatherKind::Clear);
  put_player(w, far_away(w));
  set_hour(w, 23.5f);
  run(w, {}, static_cast<int>(rules::kRoutineEvery) + 2);
  REQUIRE(villager(w, npc::kOdette).act == Activity::Home);
  const auto loaded = deserialize(serialize(w));
  REQUIRE(loaded.has_value());
  // (The continent's wild monsters are not saved: drawn again by tile, outer.hpp.)
  std::vector<Creature> kept;
  for (const Creature& c : w.creatures) {
    if (c.npc != 0 || !monster_stats(c.species).is_monster || rules::in_world(c.home.x, c.home.z, -1.0f)) kept.push_back(c);
  }
  CHECK(loaded->creatures == kept);
}

TEST_CASE("the wild monsters sleep at night, and wake when the player comes") {
  World w = cached_game(3);
  set_weather(w, WeatherKind::Clear);
  set_hour(w, 23.5f);
  run(w, {}, 14 * rules::kTicksPerSecond);
  size_t sleepers = 0, day_kinds = 0;
  for (size_t i = 1; i < w.creatures.size(); ++i) {
    const Creature& c = w.creatures[i];
    if (c.tag != Tag::None || c.species == Species::Villager || c.species == Species::Fish || flies(c.species)) continue;
    if (c.species == Species::Raccoon || c.species == Species::Cristi) continue;
    ++day_kinds;
    sleepers += asleep(c) || c.act == Activity::Warm;
  }
  REQUIRE(day_kinds >= 3);
  CHECK(sleepers * 10 >= day_kinds * 7);
  // The player walks up to one: it wakes.
  for (size_t i = 1; i < w.creatures.size(); ++i) {
    if (w.creatures[i].act != Activity::Sleep) continue;
    const Vec3 p = w.creatures[i].body.pos;
    put_player(w, {p.x + 2.0f, p.z});
    run(w, {}, 2);
    CHECK(w.creatures[i].act == Activity::None);
    break;
  }
}

TEST_CASE("in the rain, those who mind it go under a tree") {
  World w = cached_game(4);
  const Object* tree = open_tree(w);
  REQUIRE(tree != nullptr);
  const Vec2 at{tree->pos.x + 6.0f, tree->pos.z};
  put_player(w, {tree->pos.x + 30.0f, tree->pos.z + 30.0f});
  clear_wilds(w);
  set_hour(w, 12.0f);
  set_weather(w, WeatherKind::Rain);
  const size_t bird = add_monster(w, Species::Sheep, at);
  const uint32_t id = w.creatures[bird].id;
  run(w, {}, 12 * rules::kTicksPerSecond);
  const int i = creature_by_id(w, id);
  REQUIRE(i > 0);
  const Creature& c = w.creatures[static_cast<size_t>(i)];
  CHECK(c.act == Activity::Shelter);
  float nearest = 1e9f;  // under a tree (the nearest to it, maybe not this one)
  for (const Object& o : w.objects) {
    if (o.kind == ObjectKind::Tree || o.kind == ObjectKind::Pine || o.kind == ObjectKind::Larch) nearest = std::min(nearest, flat(c.body.pos, o.pos));
  }
  CHECK(nearest < 3.0f);
}

TEST_CASE("a hunter close by: its prey runs; a monster of the companion's kind comes to sniff it") {
  World w = cached_game(4);
  const Object* tree = open_tree(w);
  REQUIRE(tree != nullptr);
  const Vec2 at{tree->pos.x + 12.0f, tree->pos.z + 12.0f};
  put_player(w, {at.x + 25.0f, at.z});
  clear_wilds(w);
  set_hour(w, 10.0f);
  set_weather(w, WeatherKind::Clear);
  const uint32_t bird = w.creatures[add_monster(w, Species::Bird, at)].id;
  add_monster(w, Species::Ermine, {at.x + 3.0f, at.z});
  run(w, {}, 15);
  const int b = creature_by_id(w, bird);
  REQUIRE(b > 0);
  CHECK(w.creatures[static_cast<size_t>(b)].mind == Mind::Flee);

  // The companion (Pompon): another Pompon nearby comes up to it, sooner or later.
  World v = cached_game(4);
  put_player(v, {at.x + 4.0f, at.z});
  clear_wilds(v);
  set_hour(v, 10.0f);
  set_weather(v, WeatherKind::Clear);
  const uint32_t kin = v.creatures[add_monster(v, Species::Companion, at)].id;
  bool sniffed = false;
  for (int t = 0; t < 120 * rules::kTicksPerSecond && !sniffed; t += 10) {
    run(v, {}, 10);
    const int k = creature_by_id(v, kin);
    REQUIRE(k > 0);
    sniffed = v.creatures[static_cast<size_t>(k)].act == Activity::Sniff;
  }
  CHECK(sniffed);
}

TEST_CASE("water to drink is found at the lake's edge") {
  const World& w = cached_game(1);
  const Lake& lake = w.village.lake;
  REQUIRE(lake.radius_x > 0);
  const Vec2 from{lake.center.x + lake.radius_x + 6.0f, lake.center.z};
  Vec2 edge, facing;
  REQUIRE(water_edge(w, from, rules::kWaterReach, edge, facing));
  CHECK_FALSE(in_lake(lake, edge.x, edge.z));
  CHECK(in_lake(lake, edge.x + facing.x * 1.6f, edge.z + facing.z * 1.6f));
}

TEST_CASE("the activities are saved") {
  World w = cached_game(3);
  set_weather(w, WeatherKind::Clear);
  set_hour(w, 23.5f);
  run(w, {}, 12 * rules::kTicksPerSecond);
  bool any = false;
  for (const Creature& c : w.creatures) any = any || c.act != Activity::None;
  CHECK(any);
  const auto loaded = deserialize(serialize(w));
  REQUIRE(loaded.has_value());
  CHECK(loaded->creatures == w.creatures);
}
