// The grown world: the wilds around the valley (objects drawn from the seed,
// not saved), the sea on the west, monsters out there, the objects' index,
// and what it all costs (time, sizes).

#include "monstres/core/battle.hpp"
#include "monstres/core/continent.hpp"
#include "monstres/core/north.hpp"
#include "monstres/core/roads.hpp"
#include "monstres/core/sites.hpp"
#include "monstres/core/save.hpp"
#include "monstres/core/west.hpp"
#include "test_helpers.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>

using namespace monstres;

namespace {

double ms_since(std::chrono::steady_clock::time_point start) {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

float chebyshev(float x, float z) { return std::max(std::fabs(x), std::fabs(z)); }

}  // namespace

TEST_CASE("the wilds: many objects around the valley, the valley's own unchanged, nothing at sea") {
  const World& w = cached_game(3);
  const std::span<const Object> valley = valley_objects(w);
  CHECK(w.wild_objects > 10000);
  CHECK(valley.size() + w.wild_objects == w.objects.size());
  for (const Object& o : valley) CHECK(chebyshev(o.pos.x, o.pos.z) < rules::kValleyHalfSize);
  size_t trees = 0, rocks = 0, north_pines = 0, north = 0;
  for (size_t k = valley.size(); k < w.objects.size(); ++k) {
    const Object& o = w.objects[k];
    // Nature (and the pieces of logs), and the west's buildings (west.hpp, drawn again at each load too).
    // (And the north's, and those along the roads: roads.hpp.)
    if (in_west_place(w.west, o.pos.x, o.pos.z, 4.0f) || o.kind > ObjectKind::Crate) continue;
    if (in_north_place(w.north, o.pos.x, o.pos.z, 4.0f) || in_roadside_place(w.roads, o.pos.x, o.pos.z, 4.0f) ||
        in_site(w.sites, o.pos.x, o.pos.z, 4.0f)) {
      continue;
    }
    CHECK((o.kind <= ObjectKind::Bush || o.kind == ObjectKind::Collider));
    if (o.kind == ObjectKind::Collider) continue;  // a piece of a log, along it
    CHECK(chebyshev(o.pos.x, o.pos.z) >= rules::kValleyHalfSize - 2.0f);
    CHECK_FALSE((w.ground.sea(o.pos.x, o.pos.z) && w.ground.height(o.pos.x, o.pos.z) < rules::kSeaLevel + 1.0f));
    trees += o.kind == ObjectKind::Tree || o.kind == ObjectKind::Pine;
    rocks += o.kind == ObjectKind::Rock;
    if (o.pos.z < -300 && zone_at(w, o.pos.x, o.pos.z) == Zone::Hills) {
      ++north;
      north_pines += o.kind == ObjectKind::Pine;
    }
  }
  CHECK(trees > 3000);
  CHECK(rocks > 1000);
  CHECK(north_pines > north / 3);  // pines and rocks on the hills, no leafy trees there
  // The same for the same seed; drawn again identically.
  World again = w;
  again.objects.resize(valley.size());
  again.wild_objects = 0;
  generate_wilds(again);
  CHECK(again.objects == w.objects);
}

TEST_CASE("the sea comes into the playable area on the west: a beach, and walkers stop at the water") {
  const World& w = cached_game(3);
  // Somewhere west of the valley, the ground goes below the sea.
  float coast = 0;
  for (float x = -150; x > -rules::kWorldHalfSize; x -= 2) {
    if (w.ground.sea(x, 0) && w.ground.height(x, 0) < rules::kSeaLevel) {
      coast = x;
      break;
    }
  }
  REQUIRE(coast < 0);
  CHECK(coast > -rules::kWorldHalfSize + 20);
  CHECK(zone_at(w, coast + 4, 0) == Zone::Coast);
  // Walking west from the shore: stopped at the water's edge.
  World walk = w;
  walk.player.pos = {coast + 25, w.ground.height(coast + 25, 0), 0};
  walk.creatures.resize(1);
  walk.active = -1;
  Input west;
  west.move_x = -127;
  run(walk, west, 15 * rules::kTicksPerSecond);
  CHECK(walk.ground.height(walk.player.pos.x, walk.player.pos.z) >= rules::kSeaLevel - 0.3f);
  CHECK(walk.player.pos.x > coast - 6);
}

TEST_CASE("the valley has no sea: its hollows may go below the sea's level, they are dry land") {
  for (uint64_t seed : {3, 42, 7}) {
    const World& w = cached_game(seed);
    for (float x = -rules::kValleyHalfSize; x <= rules::kValleyHalfSize; x += 7) {
      for (float z = -rules::kValleyHalfSize; z <= rules::kValleyHalfSize; z += 7) CHECK_FALSE(w.ground.sea(x, z));
    }
  }
  // Seed 42 starts in a hollow below the sea's level: the player walks out of it.
  World w = cached_game(42);
  REQUIRE(w.ground.height(0, 0) < rules::kSeaLevel);
  run(w, forward(), rules::kTicksPerSecond);
  CHECK(w.player.pos.z < -3);
}

TEST_CASE("the wilds have monsters, stronger far from the valley; none at sea") {
  World w = cached_game(4);
  // Far in the east, then far in the north.
  for (const Vec2 at : {Vec2{460, 30}, Vec2{40, -470}}) {
    w.player.pos = {at.x, w.ground.height(at.x, at.z), at.z};
    w.creatures[0].body.pos = w.player.pos;
    update_population(w);
    int wild = 0, strong = 0;
    for (size_t i = 1; i < w.creatures.size(); ++i) {
      const Creature& c = w.creatures[i];
      if (!monster_stats(c.species).is_monster || chebyshev(c.home.x - at.x, c.home.z - at.z) > 60) continue;
      ++wild;
      strong += c.level >= 4;
      CHECK(w.ground.height(c.body.pos.x, c.body.pos.z) > rules::kSeaLevel);
      CHECK_FALSE(inside_any_object(w, c.body, species_info(c.species).shape));
    }
    CHECK(wild > 5);
    CHECK(strong > wild / 2);
  }
  CHECK(zone_at(w, 460, 30) == Zone::Plains);
  CHECK(zone_at(w, 40, -470) == Zone::Hills);
}

TEST_CASE("the objects' index finds what going through all of them finds") {
  const World& w = cached_game(5);
  for (const Box area : {Box{{-3, -10, -3}, {3, 50, 3}}, Box{{200, -10, -40}, {230, 50, -10}}, Box{{-560, -10, 300}, {-540, 50, 320}}}) {
    std::vector<uint32_t> found;
    for_objects_near(w, area, [&](uint32_t k, const Box& b) {
      if (overlaps(area, b)) found.push_back(k);
    });
    std::sort(found.begin(), found.end());
    found.erase(std::unique(found.begin(), found.end()), found.end());
    std::vector<uint32_t> all;
    for (size_t k = 0; k < w.objects.size(); ++k) {
      if (overlaps(area, object_box(w.objects[k]))) all.push_back(static_cast<uint32_t>(k));
    }
    CHECK(found == all);
  }
  // Rebuilt when the objects change.
  World changed = w;
  add_box(changed, {0, 0, 0}, {0.5f, 0.5f, 0.5f});
  bool hit = false;
  for_objects_near(changed, Box{{-0.1f, 0, -0.1f}, {0.1f, 0.2f, 0.1f}}, [&](uint32_t k, const Box&) {
    hit = hit || k + 1 == changed.objects.size();
  });
  CHECK(hit);
}

TEST_CASE("saves keep only the valley's objects; the wilds come back at loading") {
  const World& w = cached_game(6);
  const std::vector<uint8_t> bytes = serialize(w);
  const auto loaded = deserialize(bytes);
  REQUIRE(loaded.has_value());
  CHECK(loaded->objects == w.objects);
  CHECK(loaded->wild_objects == w.wild_objects);
  // Each object takes 28 bytes: the wilds' would make the save much bigger.
  CHECK(bytes.size() < valley_objects(w).size() * 28 + 40000);
  CHECK(bytes.size() * 3 < w.objects.size() * 28);
}

TEST_CASE("the grown world stays within its budget: building it, a tick, a save") {
  const auto start = std::chrono::steady_clock::now();
  World w = new_game(21);
  const double build_ms = ms_since(start);
  // Walking for 10 s in the wilds, monsters around: the time of a tick.
  w.player.pos = {300, w.ground.height(300, 200), 200};
  w.creatures[0].body.pos = w.player.pos;
  update_population(w);
  run(w, Input{}, 10);  // the index is built once here
  const auto ticks = std::chrono::steady_clock::now();
  constexpr int kTicks = 600;
  Input east;
  east.move_x = 127;
  run(w, east, kTicks);
  const double tick_ms = ms_since(ticks) / kTicks;
  const auto saving = std::chrono::steady_clock::now();
  const std::vector<uint8_t> bytes = serialize(w);
  const auto loaded = deserialize(bytes);
  const double load_ms = ms_since(saving);
  REQUIRE(loaded.has_value());
  std::printf("grown world: %zu objects (%u wild), built in %.0f ms, %.3f ms per tick, save %zu bytes, "
              "saved and loaded in %.0f ms\n",
              w.objects.size(), w.wild_objects, build_ms, tick_ms, bytes.size(), load_ms);
  CHECK(bytes.size() < 200000);
  if (!kSanitized) {
    CHECK(build_ms < 1800);  // (the continent made first: its grid, its rivers)
    CHECK(tick_ms < 1.0);  // 60 per second, with room to spare (WebAssembly is slower)
    CHECK(load_ms < 1800);  // (the world made again from its seed, as built)
  }
}
