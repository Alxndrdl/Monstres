// Only the surroundings of the player live: squares of the world fill with
// wild monsters when the player comes near, and empty when it goes away.

#include "monstres/core/battle.hpp"
#include "monstres/core/debug.hpp"
#include "monstres/core/save.hpp"
#include "test_helpers.hpp"

#include <cmath>
#include <set>

using namespace monstres;

namespace {

size_t wild(const World& w) {
  size_t n = 0;
  for (size_t i = 1; i < w.creatures.size(); ++i) n += monster_stats(w.creatures[i].species).is_monster;
  return n;
}

// Teleports the player and lets the squares follow.
void go_to(World& w, float x, float z) {
  w.player.pos = {x, w.ground.height(x, z), z};
  update_population(w);
}

}  // namespace

TEST_CASE("wild monsters live only around the player, and others come back") {
  World w = cached_game(21);
  const size_t at_start = wild(w);
  CHECK(at_start > 10);
  std::set<uint32_t> first_ids;
  for (const Creature& c : w.creatures) first_ids.insert(c.id);

  // Far away (the other end of the world): the monsters of the start are gone.
  go_to(w, 0, 120);
  for (const Creature& c : w.creatures) {
    if (!monster_stats(c.species).is_monster || &c == &w.creatures[0]) continue;
    CHECK(std::hypot(c.home.x - w.player.pos.x, c.home.z - w.player.pos.z) < rules::kDespawnRadius + 60);
  }
  // Back: new ones (new ids), about as many.
  go_to(w, 0, 0);
  size_t again = 0, old = 0;
  for (size_t i = 1; i < w.creatures.size(); ++i) {
    const Creature& c = w.creatures[i];
    if (!monster_stats(c.species).is_monster) continue;
    ++again;
    old += first_ids.count(c.id) && c.home.z < 20;  // from the squares emptied while far away
  }
  CHECK(again > at_start / 2);
  CHECK(old == 0);
  // Villagers never leave.
  size_t villagers = 0;
  for (const Creature& c : w.creatures) villagers += c.species == Species::Villager;
  CHECK(villagers == rules::kVillagers);
  check_invariants(w);
}

TEST_CASE("the squares are saved, and a loaded game goes on the same way") {
  World w = cached_game(5);
  go_to(w, 60, 60);
  const auto loaded = deserialize(serialize(w));
  REQUIRE(loaded.has_value());
  CHECK(*loaded == w);
  World a = w, b = *loaded;
  for (int i = 0; i < 200; ++i) {
    step(a, forward());
    step(b, forward());
  }
  CHECK(a == b);
}

TEST_CASE("the enemy of a battle stays, even far from its square") {
  World w = cached_game(9);
  size_t foe = 0;
  for (size_t i = 1; i < w.creatures.size() && !foe; ++i) {
    if (w.creatures[i].species == Species::Slime) foe = i;
  }
  REQUIRE(foe > 0);
  w.player.pos = w.creatures[foe].body.pos + Vec3{3, 0, 0};
  w.creatures[0].body.pos = w.player.pos;  // the companion comes along
  Input in;
  in.command = Command::Engage;
  in.target = static_cast<uint16_t>(foe);
  step(w, in);
  REQUIRE(w.battle.phase == BattlePhase::Fight);
  const uint32_t id = w.creatures[w.battle.enemy].id;
  // Its square empties around it (as if the player were far): it stays, the index follows.
  for (size_t k = 0; k < kPopulationCellCount; ++k) w.population.active[k] = 1;
  const Vec3 p = w.player.pos;
  w.player.pos = {-p.x, p.y, -p.z};
  update_population(w);
  w.player.pos = p;
  CHECK(w.creatures[w.battle.enemy].id == id);
}

TEST_CASE("the world has its places: forest, cave, castle") {
  const World& w = cached_game(12);
  REQUIRE(w.forest.radius_x > 0);
  REQUIRE(w.cave.radius > 0);
  REQUIRE(w.castle.radius > 0);
  CHECK(zone_at(w, w.forest.center.x, w.forest.center.z) == Zone::Forest);
  CHECK(zone_at(w, w.cave.mouth.x + w.cave.facing.x * 3, w.cave.mouth.z) == Zone::Cave);
  CHECK(zone_at(w, w.castle.center.x, w.castle.center.z) == Zone::Castle);
  int trees_in_forest = 0, trees_outside = 0;
  for (const Object& o : w.objects) {
    if (o.kind != ObjectKind::Tree && o.kind != ObjectKind::Pine) continue;
    (in_forest(w.forest, o.pos.x, o.pos.z) ? trees_in_forest : trees_outside) += 1;
  }
  CHECK(trees_in_forest > trees_outside / 2);  // denser there (a much smaller area)
  int walls = 0, towers = 0, keep = 0, gate = 0, mouth = 0, crystals = 0;
  for (const Object& o : w.objects) {
    walls += o.kind == ObjectKind::CastleWall;
    towers += o.kind == ObjectKind::CastleTower;
    keep += o.kind == ObjectKind::CastleKeep;
    gate += o.kind == ObjectKind::CastleGate;
    mouth += o.kind == ObjectKind::CaveMouth;
    crystals += o.kind == ObjectKind::Crystal;
  }
  CHECK(walls == 22);
  CHECK(towers == 4);
  CHECK(keep == 1);
  CHECK(gate == 1);
  CHECK(mouth == 1);
  CHECK(crystals >= 6);
}

TEST_CASE("the castle gate and the cave can be walked into") {
  World w = cached_game(12);
  w.creatures.resize(1);
  w.active = -1;
  // Through the gate, towards the keep.
  const CastleLayout& k = w.castle;
  const float gx = k.center.x + k.facing.x * (rules::kCastleHalf + 5);
  w.player.pos = {gx, w.ground.height(gx, k.center.z), k.center.z};
  const Angle16 inwards = k.facing.x > 0 ? Angle16{16384} : Angle16{49152};  // forward = -facing
  run(w, forward(inwards), 3 * rules::kTicksPerSecond);
  CHECK(std::fabs(w.player.pos.x - k.center.x) < rules::kCastleHalf - 1);  // inside the walls
  // Into the cave's hollow.
  const CaveLayout& c = w.cave;
  const float cx = c.mouth.x + c.facing.x * 7;
  w.player.pos = {cx, w.ground.height(cx, c.mouth.z), c.mouth.z};
  const Angle16 in_cave = c.facing.x > 0 ? Angle16{16384} : Angle16{49152};
  run(w, forward(in_cave), 4 * rules::kTicksPerSecond);
  CHECK((w.player.pos.x - c.mouth.x) * c.facing.x < -1.0f);  // past the arch
  check_invariants(w);
}

TEST_CASE("a signpost tells where the places are") {
  const World& w = cached_game(4);
  int sign = -1;
  for (size_t i = 0; i < w.objects.size(); ++i) {
    if (w.objects[i].kind == ObjectKind::Sign) sign = static_cast<int>(i);
  }
  REQUIRE(sign >= 0);
  const std::string json = sign_json(w, sign);
  for (const char* zone : {"village", "lake", "ruins", "forest", "cave", "castle"}) {
    CHECK(json.find(std::string("\"zone\":\"") + zone + "\"") != std::string::npos);
  }
  CHECK(sign_json(w, 0).find("null") == 0);  // not a signpost
}

TEST_CASE("a fish let go goes back to the lake") {
  World w = cached_game(8);
  w.team.push_back({Species::Fish, {}, 2, 0, 20});
  Input in;
  in.command = Command::Release;
  in.target = 1;
  step(w, in);
  REQUIRE(w.team.size() == 1);
  const Creature& fish = w.creatures[static_cast<size_t>(find_creature(w, w.released_id))];
  CHECK(fish.species == Species::Fish);
  CHECK(in_lake(w.village.lake, fish.body.pos.x, fish.body.pos.z, -1.0f));
  for (int i = 0; i < 120; ++i) step(w, Input{});
  check_invariants(w);
}
