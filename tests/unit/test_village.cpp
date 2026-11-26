// The village, its lake, and the people and animals living there.

#include "monstres/core/world.hpp"
#include "monstres/render/scene.hpp"
#include "monstres/render/terrain_mesh.hpp"
#include "monstres/render/water_mesh.hpp"
#include "monstres/core/west.hpp"
#include "monstres/core/rivers.hpp"
#include "monstres/core/debug.hpp"
#include "test_helpers.hpp"

#include <cmath>

using namespace monstres;
using doctest::Approx;

namespace {

size_t count(const World& w, ObjectKind kind) {
  size_t n = 0;
  for (const Object& o : w.objects) n += o.kind == kind;
  return n;
}

size_t count(const World& w, Species species) {
  size_t n = 0;
  for (const Creature& c : w.creatures) n += c.species == species;
  return n;
}

Creature& first_of(World& w, Species species) {
  for (Creature& c : w.creatures) {
    if (c.species == species) return c;
  }
  FAIL("no creature of that species");
  return w.creatures[0];
}

bool is_house(ObjectKind k) {
  return k == ObjectKind::HouseCottage || k == ObjectKind::HouseChimney || k == ObjectKind::HouseHut;
}

}  // namespace

TEST_CASE("the village sits on the path, the lake beside it, both in the world") {
  for (uint64_t seed = 0; seed < kManySeeds; ++seed) {
    CAPTURE(seed);
    const World& w = cached_game(seed);
    const VillageLayout& v = w.village;
    CHECK(distance_to_path(w.path, v.center.x, v.center.z) < 1.5f);
    CHECK(std::fabs(v.lake.center.x) + v.lake.radius_x < rules::kWorldHalfSize - 2);
    CHECK(distance_to_path(w.path, v.lake.center.x, v.lake.center.z) > v.lake.radius_x + 2);
    CHECK_FALSE(in_lake(v.lake, 0, 0, 2));  // not on the start
  }
}

TEST_CASE("the village has houses of every type, a well, signs, fences and lamps") {
  size_t types[3] = {};
  for (uint64_t seed = 0; seed < 10; ++seed) {
    CAPTURE(seed);
    const World& w = cached_game(seed);
    size_t houses = 0;
    for (const Object& o : w.objects) {
      // (The valley's: Rochebrune, the inn, the farmstead have their own, north.hpp, roads.hpp.)
      if (!is_house(o.kind) || std::max(std::fabs(o.pos.x), std::fabs(o.pos.z)) > rules::kValleyHalfSize) continue;
      ++houses;
      ++types[static_cast<size_t>(o.kind) - static_cast<size_t>(ObjectKind::HouseCottage)];
      CHECK(in_village(w.village, o.pos.x, o.pos.z));
      // The door faces the square more or less.
      const SinCos sc = sincos16(o.yaw);
      const float fx = -sc.sin, fz = -sc.cos;
      CHECK(fx * (w.village.center.x - o.pos.x) + fz * (w.village.center.z - o.pos.z) > 0);
    }
    CHECK(houses >= 5);
    size_t wells = 0;  // the farm has its own (west.hpp)
    for (const Object& o : w.objects) wells += o.kind == ObjectKind::Well && in_village(w.village, o.pos.x, o.pos.z);
    CHECK(wells == 1);
    CHECK(count(w, ObjectKind::Sign) >= 1);
    CHECK(count(w, ObjectKind::Lamp) >= 2);
    CHECK(count(w, ObjectKind::Fence) >= 1);
    check_invariants(w);
  }
  for (size_t n : types) CHECK(n > 0);
}

TEST_CASE("no tree or rock grows in the village or in the lake") {
  for (uint64_t seed = 0; seed < 10; ++seed) {
    const World& w = cached_game(seed);
    for (const Object& o : w.objects) {
      if (o.kind == ObjectKind::Seaweed) continue;  // on its bottom (water.hpp)
      CHECK_FALSE(in_lake(w.village.lake, o.pos.x, o.pos.z, 0.5f));
      if (o.kind <= ObjectKind::Crate) CHECK_FALSE(in_village(w.village, o.pos.x, o.pos.z));
    }
  }
}

TEST_CASE("village objects do not overlap") {
  const World& w = cached_game(4);
  std::vector<Box> boxes;
  for (const Object& o : w.objects) boxes.push_back(object_box(o));
  for (const auto& [a, b] : overlapping_pairs(boxes)) {
    FAIL_CHECK(std::string(object_kind_name(w.objects[a].kind)) << " " << a << " at " << w.objects[a].pos << " over " << std::string(object_kind_name(w.objects[b].kind)) << " " << b << " at " << w.objects[b].pos);
  }
}

TEST_CASE("the player walks into the lake, and goes under (water.hpp)") {
  World w = cached_game(8);
  const Lake& lake = w.village.lake;
  w.player.pos = {lake.center.x, 0, lake.center.z + lake.radius_z + 3};  // south of the lake
  w.creatures.resize(1);
  // Walk north, into the water, slower as it gets deeper.
  for (int t = 0; t < 6 * rules::kTicksPerSecond && !swimming(w); ++t) step(w, forward());
  CHECK(in_lake(lake, w.player.pos.x, w.player.pos.z, 0));
  CHECK(swimming(w));
}

TEST_CASE("fish stay in the lake and jump out of it now and then") {
  World w = cached_game(8);
  REQUIRE(count(w, Species::Fish) > 0);
  bool jumped = false;
  for (int t = 0; t < 60 * rules::kTicksPerSecond; ++t) {
    step(w, move(0, 0));
    for (const Creature& c : w.creatures) {
      if (c.species != Species::Fish) continue;
      REQUIRE(in_lake(w.village.lake, c.body.pos.x, c.body.pos.z, -0.5f));
      CHECK(c.body.pos.y >= rules::kSwimLevel);
      jumped |= c.body.pos.y > rules::kSwimLevel + 0.3f;
    }
  }
  CHECK(jumped);
}

TEST_CASE("crows start on roofs and fly to another perch when the player comes") {
  World w = cached_game(8);
  const auto perches = crow_perches(w);
  REQUIRE(perches.size() >= 5);
  REQUIRE(count(w, Species::Crow) > 0);
  Creature& crow = first_of(w, Species::Crow);
  const size_t index = static_cast<size_t>(&crow - w.creatures.data());
  CHECK(crow.body.pos.y > 2.0f);  // on a roof or the well
  const Vec3 start = crow.body.pos;

  w.player.pos = {start.x + 1, 0, start.z};
  step(w, move(0, 0));
  CHECK(w.creatures[index].mind == Mind::Fly);
  w.player.pos = {0, w.ground.height(0, 0), 0};  // walk away; the crow lands somewhere else
  for (int i = 0; i < 20 * rules::kTicksPerSecond && w.creatures[index].mind == Mind::Fly; ++i) step(w, move(0, 0));
  const Creature& after = w.creatures[index];
  const float moved = std::hypot(after.body.pos.x - start.x, after.body.pos.z - start.z);
  CHECK(moved > 1.0f);
  bool on_a_perch = false;
  for (const Vec3& p : perches) on_a_perch |= std::hypot(p.x - after.body.pos.x, p.z - after.body.pos.z) < 0.1f;
  CHECK((on_a_perch || after.mind == Mind::Fly));
}

TEST_CASE("villagers stay around the village and greet the player") {
  World w = cached_game(8);
  REQUIRE(count(w, Species::Villager) == rules::kVillagers + 1 + 9 + 2 + 3 + 2 + 9);  // Isidore, the west, the north, Rochebrune, the roads, the sites
  run(w, move(0, 0), 30 * rules::kTicksPerSecond);
  for (const Creature& c : w.creatures) {
    if (c.species != Species::Villager) continue;
    if (c.npc > npc::kMentor) {  // the west's stay at their place
      CHECK(std::hypot(c.body.pos.x - c.home.x, c.body.pos.z - c.home.z) < 3.0f);
      continue;
    }
    CHECK(in_village(w.village, c.body.pos.x, c.body.pos.z, 2.0f));
  }
  Creature& v = first_of(w, Species::Villager);
  const size_t index = static_cast<size_t>(&v - w.creatures.data());
  // On the square, with the player just to its right.
  v.body.pos = {w.village.center.x, 0, w.village.center.z};
  w.player.pos = {v.body.pos.x + 1.5f, 0, v.body.pos.z};
  step(w, move(0, 0));
  const Creature& greeting = w.creatures[index];
  CHECK(greeting.mind == Mind::Greet);
  CHECK(greeting.body.facing_x == Approx(1).epsilon(0.05));  // turned towards the player
}

TEST_CASE("the lake has water above a sunken bed, chimneys smoke") {
  const World& w = cached_game(8);
  const Lake& lake = w.village.lake;
  CHECK(ground_height(w, lake.center.x, lake.center.z) < rules::kWaterLevel - 0.5f);
  CHECK(ground_height(w, w.village.center.x, w.village.center.z) == 0.0f);  // the village is flat
  const MeshData water = build_water_mesh(w);  // (water_mesh.hpp: all the waters alike)
  CHECK(water.transparent);
  REQUIRE(water.vertex_count() > 0);
  size_t in_lake_vertices = 0;
  for (size_t i = 0; i < water.vertices.size(); i += MeshData::kFloatsPerVertex) {
    const float x = water.vertices[i], y = water.vertices[i + 1], z = water.vertices[i + 2];
    if (in_lake(lake, x, z, 9.0f) && y == Approx(rules::kWaterLevel)) {  // (a cell past its shore too)
      in_lake_vertices += in_lake(lake, x, z) ? 1 : 0;
      continue;
    }
    // The river at its own level, its cove, its waterfall; else the sea.
    if (distance_to_stream(w.west.stream, x, z, 30.0f) < 30.0f && y > rules::kSeaLevel + 0.01f) continue;
    if (RiverSpot r; nearest_river(w.ground.rivers, x, z, 30.0f, r) && y > rules::kSeaLevel + 0.01f) continue;  // the continent's, by the edge
    CHECK(y == Approx(rules::kSeaLevel));
  }
  CHECK(in_lake_vertices > 0);

  DrawList list;
  build_dynamic(w, 3.0f, list);
  const size_t chimneys = count(w, ObjectKind::HouseChimney);
  // (And the north's refuge: its chimney, the steam over the hot springs.)
  CHECK(list.batches[static_cast<size_t>(MeshId::Smoke)].size() == chimneys * 7 + (w.north.valid() ? 7 + 12 : 0));
  CHECK(list.batches[static_cast<size_t>(MeshId::Ripple)].size() == count(w, Species::Fish));
}
