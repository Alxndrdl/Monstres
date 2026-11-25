// The north (north.hpp): the world grown north, its places, its relief up to
// the col, the old mine and its guardian, the hot springs, its monsters.

#include "monstres/core/battle.hpp"
#include "monstres/core/debug.hpp"
#include "monstres/core/interior.hpp"
#include "monstres/core/north.hpp"
#include "monstres/core/quests.hpp"
#include "monstres/core/save.hpp"
#include "monstres/core/west.hpp"
#include "test_helpers.hpp"

#include <algorithm>
#include <cmath>

using namespace monstres;

namespace {

Input command(Command c, uint16_t target = 0) {
  Input in;
  in.command = c;
  in.target = target;
  return in;
}

float flat(Vec2 a, Vec2 b) { return std::hypot(a.x - b.x, a.z - b.z); }
Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.z + b.z}; }

void put_player(World& w, Vec2 p) {
  w.player.pos = {p.x, w.ground.height(p.x, p.z), p.z};
  w.player.velocity_y = 0;
  w.creatures[0].body.pos = w.player.pos;
  update_population(w);
}

size_t count_kind(const World& w, ObjectKind k) {
  size_t n = 0;
  for (const Object& o : w.objects) n += o.kind == k;
  return n;
}

int kind_index(const World& w, ObjectKind k) {
  for (size_t i = 0; i < w.objects.size(); ++i) {
    if (w.objects[i].kind == k) return static_cast<int>(i);
  }
  return -1;
}

// The steepest slope along a straight walk from a to b (every meter).
float steepest_between(const World& w, Vec2 a, Vec2 b) {
  const float len = flat(a, b);
  const int n = std::max(1, static_cast<int>(len));
  float steepest = 0;
  for (int k = 0; k < n; ++k) {
    const float t0 = static_cast<float>(k) / static_cast<float>(n), t1 = static_cast<float>(k + 1) / static_cast<float>(n);
    const Vec2 p{a.x + (b.x - a.x) * t0, a.z + (b.z - a.z) * t0}, q{a.x + (b.x - a.x) * t1, a.z + (b.z - a.z) * t1};
    steepest = std::max(steepest, std::fabs(w.ground.height(q.x, q.z) - w.ground.height(p.x, p.z)) / std::max(flat(p, q), 1e-3f));
  }
  return steepest;
}

}  // namespace

TEST_CASE("the world grew north: up to the foot of the mountains") {
  CHECK(rules::in_world(0, -1200));
  CHECK_FALSE(rules::in_world(0, -1230));
  CHECK_FALSE(rules::in_world(0, 600));
  CHECK_FALSE(rules::in_world(600, -800));
  CHECK(rules::world_edge(0, 0) == doctest::Approx(0));
  CHECK(rules::world_edge(0, -1220) == doctest::Approx(rules::kWorldHalfSize));
  CHECK(rules::relief_edge(0, -1280) == doctest::Approx(rules::kReliefHalfSize));
  const World& w = cached_game(3);
  // The relief goes on up there, the mountains rising.
  CHECK(w.ground.height(w.north.col.x + 150, -1215) > w.ground.height(w.north.col.x, -1215) + 10);
  // The player no longer stops at the north edge: the continent goes on.
  World e = empty_world();
  e.player.pos = {0, 0, -1200};
  run(e, forward(), 10 * rules::kTicksPerSecond);
  CHECK(e.player.pos.z < -rules::kWorldNorth - 20);
}

TEST_CASE("the north's places, in every world") {
  for (uint64_t seed = 0; seed < kManySeeds; ++seed) {
    CAPTURE(seed);
    const World& w = cached_game(seed);
    const NorthLayout& n = w.north;
    REQUIRE(n.valid());
    for (const Vec2 p : {n.refuge, n.mine, n.springs, n.lake, n.col, n.col_teleporter, n.refuge_teleporter}) {
      CAPTURE(p.x);
      CAPTURE(p.z);
      CHECK(rules::in_world(p.x, p.z, 10.0f));
      CHECK(p.z < rules::kNorthFrom);
      CHECK(w.ground.height(p.x, p.z) > rules::kSeaLevel + 3.0f);  // (the continent's ground may be lower: the terrace)
      CHECK(zone_at(w, p.x, p.z) != Zone::Coast);
    }
    // Up the path, in order: the mine and the refuge, the springs, the lake, the col.
    CHECK(n.refuge.z > n.springs.z);
    CHECK(n.springs.z > n.lake.z);
    CHECK(n.lake.z > n.col.z);
    CHECK(distance_to_path(w.path, n.refuge.x, n.refuge.z) < 40.0f);
    CHECK(distance_to_path(w.path, n.col.x, n.col.z) < 1.0f);
    CHECK(n.walls.size() >= 3);
    CHECK(n.cairns.size() >= 5);
    // Its objects and teleporters.
    Vec3 at;
    CHECK(count_kind(w, ObjectKind::Chalet) >= 1);  // the refuge (and Rochebrune's houses)
    CHECK(count_kind(w, ObjectKind::Arena) == 1);
    CHECK(count_kind(w, ObjectKind::Grotto) == 1);
    CHECK(teleporter_position(w, TeleporterId::Hamlet, at));
    CHECK(npc_index(w, npc::kBarnabe) > 0);
    CHECK(npc_index(w, npc::kMargaux) > 0);
    CHECK(npc_index(w, npc::kAgathe) > 0);
    CHECK(n.houses.size() >= 3);
    CHECK(count_kind(w, ObjectKind::MineEntrance) == 1);
    CHECK(count_kind(w, ObjectKind::HotSpring) == 1);
    CHECK(count_kind(w, ObjectKind::StoneWall) > 30);
    CHECK(count_kind(w, ObjectKind::Cairn) >= 7);
    CHECK(count_kind(w, ObjectKind::Larch) > 200);
    CHECK(teleporter_position(w, TeleporterId::Refuge, at));
    CHECK(teleporter_position(w, TeleporterId::Col, at));
    // Its villagers.
    CHECK(npc_index(w, npc::kSolene) > 0);
    CHECK(npc_index(w, npc::kGaston) > 0);
    // The frozen lake: flat ice, walked on (not water).
    for (float dx = -0.4f; dx <= 0.4f; dx += 0.2f) {
      const float x = n.lake.x + dx * n.lake_rx, z = n.lake.z + dx * 0.4f * n.lake_rz;
      CHECK(w.ground.height(x, z) == doctest::Approx(n.lake_level).epsilon(1e-3));
      CHECK_FALSE(under_water(w, x, z));
      CHECK(zone_at(w, x, z) == Zone::FrozenLake);
    }
    // The hot springs' pool, dug in the ground.
    CHECK(in_springs(n, n.springs.x, n.springs.z));
    CHECK(w.ground.height(n.springs.x, n.springs.z) < w.ground.height(n.springs.x + rules::kSpringsRadius, n.springs.z));
  }
}

TEST_CASE("the path goes up the valley to the col: every step of it walkable, and to the north's places") {
  for (uint64_t seed = 0; seed < kManySeeds; ++seed) {
    CAPTURE(seed);
    const World& w = cached_game(seed);
    const NorthLayout& n = w.north;
    for (size_t k = 1; k < w.path.points.size(); ++k) {
      const Vec2 a = w.path.points[k - 1], b = w.path.points[k];
      if (b.z > rules::kNorthFrom || !rules::in_world(b.x, b.z)) continue;
      CAPTURE(b.z);
      CHECK(std::fabs(w.ground.height(b.x, b.z) - w.ground.height(a.x, a.z)) / flat(a, b) < rules::kMaxWalkSlope * 0.9f);
    }
    // From the path to the refuge's door, the lake's shore, the col's teleporter.
    const auto from_path = [&](Vec2 p) {
      float best = 1e9f;
      Vec2 q = p;
      for (const Vec2& s : w.path.points) {
        if (flat(s, p) < best) best = flat(s, p), q = s;
      }
      return q;
    };
    CHECK(steepest_between(w, from_path(n.refuge), n.refuge + Vec2{n.refuge_front.x * 5.0f, n.refuge_front.z * 5.0f}) < 0.9f);
    CHECK(steepest_between(w, from_path(n.col_teleporter), n.col_teleporter) < 0.9f);
    const Vec2 to_lake{n.lake.x - from_path(n.lake).x, n.lake.z - from_path(n.lake).z};
    const float len = std::hypot(to_lake.x, to_lake.z);
    CHECK(steepest_between(w, from_path(n.lake), {n.lake.x - to_lake.x / len * n.lake_rx * 0.4f, n.lake.z - to_lake.z / len * n.lake_rx * 0.4f}) < 0.9f);
    // Beyond the valley, the mountains rise: snowy up high.
    float snowiest = 0;
    for (float x = -560; x <= 560; x += 8) snowiest = std::max(snowiest, north_snow(n, x, n.col.z, w.ground.height(x, n.col.z)));
    CHECK(snowiest > 0.5f);
    CHECK(north_snow(n, n.refuge.x, n.refuge.z, w.ground.height(n.refuge.x, n.refuge.z)) == 0.0f);
  }
}

TEST_CASE("the north's zones and monsters") {
  const World& w = cached_game(5);
  const NorthLayout& n = w.north;
  CHECK(zone_at(w, n.refuge.x, n.refuge.z) == Zone::Alpine);
  CHECK(zone_at(w, n.col.x, n.col.z) == Zone::Frost);
  CHECK(species_chance(Species::Goat, Zone::Alpine) > 0);
  CHECK(species_chance(Species::Ermine, Zone::FrozenLake) > 0);
  CHECK(species_chance(Species::Golem, Zone::Frost) > 0);
  CHECK(species_chance(Species::Golem, Zone::Mine) > 0);
  CHECK(monster_stats(Species::Golem).element == Element::Earth);
  CHECK(monster_stats(Species::Goat).element == Element::Earth);
  CHECK(monster_stats(Species::Goat).element2 == Element::Ice);
  CHECK(monster_stats(Species::Ermine).element == Element::Ice);
  // Walking up there, they come.
  int counts[static_cast<int>(Species::Count)] = {};
  for (uint64_t seed = 0; seed < 4; ++seed) {
    World g = cached_game(seed);
    for (const Vec2 p : {g.north.refuge, g.north.springs, g.north.lake, Vec2{g.north.col.x, g.north.col.z + 30}}) {
      put_player(g, p);
      for (const Creature& c : g.creatures) {
        ++counts[static_cast<int>(c.species)];
        if (c.species == Species::Golem || c.species == Species::Goat || c.species == Species::Ermine) CHECK(c.level >= 6);
      }
    }
  }
  CHECK(counts[static_cast<int>(Species::Golem)] > 0);
  CHECK(counts[static_cast<int>(Species::Goat)] > 0);
  CHECK(counts[static_cast<int>(Species::Ermine)] > 0);
}

TEST_CASE("the refuge: a house inside, Solène heals the team") {
  World w = cached_game(2);
  const int chalet = kind_index(w, ObjectKind::Chalet);
  REQUIRE(chalet >= 0);
  REQUIRE(enter_place(w, {PlaceKind::House, static_cast<uint32_t>(chalet)}));
  CHECK(w.objects.size() > 4);
  w.player.pos = w.interior.exits[0].at;
  REQUIRE(use_door(w));
  CHECK_FALSE(inside(w));
  CHECK((npc_marks(w, npc::kSolene) & kMarkHeal) != 0);
}

TEST_CASE("the old mine: its galleries, its miners, its guardian, Gaston's quest") {
  World w = cached_game(4);
  const int mouth = kind_index(w, ObjectKind::MineEntrance);
  REQUIRE(mouth >= 0);
  // Gaston gives the quest.
  const int g = npc_index(w, npc::kGaston);
  REQUIRE(g > 0);
  put_player(w, {w.creatures[static_cast<size_t>(g)].body.pos.x + 1.2f, w.creatures[static_cast<size_t>(g)].body.pos.z});
  CHECK((npc_marks(w, npc::kGaston) & kMarkNewQuest) != 0);
  step(w, command(Command::QuestAccept, static_cast<uint16_t>(QuestId::MineGuardian)));
  CHECK(w.quests[static_cast<size_t>(QuestId::MineGuardian)].stage == QuestStage::Active);
  // In front of the entrance, its door.
  const Object& o = w.objects[static_cast<size_t>(mouth)];
  const SinCos sc = sincos16(o.yaw);
  w.player.pos = {o.pos.x + (-2.2f) * sc.sin, 0, o.pos.z + (-2.2f) * sc.cos};
  w.player.pos.y = w.ground.height(w.player.pos.x, w.player.pos.z);
  const DoorNear d = door_near(w);
  CHECK(d.action == DoorAction::Enter);
  CHECK(d.place == PlaceKind::Mine);
  REQUIRE(use_door(w));
  REQUIRE(inside(w));
  CHECK(w.place.kind == PlaceKind::Mine);
  CHECK(zone_at(w, 0, 0) == Zone::Mine);
  CHECK(count_kind(w, ObjectKind::MineProp) > 8);
  // The guardian, an omega Caillou, and the miners.
  int guardian = -1;
  size_t golems = 0;
  for (size_t i = 1; i < w.creatures.size(); ++i) {
    const Creature& c = w.creatures[i];
    golems += c.species == Species::Golem;
    if (c.species == Species::Golem && c.genes.form == Form::Omega) guardian = static_cast<int>(i);
  }
  CHECK(golems >= 3);
  REQUIRE(guardian > 0);
  CHECK(w.creatures[static_cast<size_t>(guardian)].level == rules::kGuardianLevel);
  // The deepest hall: the guardian's, the widest.
  float deepest = 0;
  for (const RoomPiece& p : w.interior.pieces) deepest = std::min({deepest, p.a.z, p.b.z});
  CHECK(w.creatures[static_cast<size_t>(guardian)].body.pos.z < deepest + 8.0f);
  // Beaten: the quest can be told.
  guardian_down(w, w.creatures[static_cast<size_t>(guardian)]);
  CHECK(quest_ready(w, QuestId::MineGuardian));
  // Out, back to Gaston: the reward.
  w.player.pos = w.interior.exits[0].at;
  REQUIRE(use_door(w));
  const int g2 = npc_index(w, npc::kGaston);
  REQUIRE(g2 > 0);
  put_player(w, {w.creatures[static_cast<size_t>(g2)].body.pos.x + 1.2f, w.creatures[static_cast<size_t>(g2)].body.pos.z});
  const uint32_t coins = w.bag[static_cast<size_t>(Item::Coin)];
  step(w, command(Command::QuestComplete, static_cast<uint16_t>(QuestId::MineGuardian)));
  CHECK(w.quests[static_cast<size_t>(QuestId::MineGuardian)].stage == QuestStage::Done);
  CHECK(w.bag[static_cast<size_t>(Item::Coin)] == coins + rules::kGuardianCoins);
  // Done: no guardian any more down there.
  w.player.pos = {o.pos.x + (-2.2f) * sc.sin, 0, o.pos.z + (-2.2f) * sc.cos};
  w.player.pos.y = w.ground.height(w.player.pos.x, w.player.pos.z);
  REQUIRE(use_door(w));
  for (const Creature& c : w.creatures) CHECK_FALSE((c.species == Species::Golem && c.genes.form == Form::Omega));
}

TEST_CASE("the hot springs heal the team, slowly") {
  World w = cached_game(6);
  REQUIRE(!w.team.empty());
  const uint16_t full = monster_fight_stats(w.team[0]).hp;
  w.team[0].hp = 1;
  put_player(w, w.north.springs);
  run(w, Input{}, 10 * rules::kTicksPerSecond);
  CHECK(w.team[0].hp > 3);
  CHECK(w.team[0].hp <= full);
  // Out of the pool: no more.
  const uint16_t hp = w.team[0].hp;
  put_player(w, {w.north.springs.x + 12, w.north.springs.z});
  run(w, Input{}, 6 * rules::kTicksPerSecond);
  CHECK(w.team[0].hp == hp);
}

TEST_CASE("saved: the squares further north, every teleporter, the mine's quest") {
  World w = cached_game(7);
  w.teleporters = static_cast<uint16_t>((1u << kTeleporterCount) - 1);
  w.quests[static_cast<size_t>(QuestId::MineGuardian)] = {QuestStage::Active, kGuardianBeaten};
  put_player(w, {w.north.lake.x, w.north.lake.z + w.north.lake_rz + 8});
  const auto loaded = deserialize(serialize(w));
  REQUIRE(loaded.has_value());
  CHECK(loaded->teleporters == w.teleporters);
  CHECK(loaded->quests == w.quests);
  CHECK(loaded->population == w.population);
  CHECK(loaded->player.pos.z < -1000.0f);
  // The map knows the north.
  const std::string map = map_json(w);
  CHECK(map.find("\"north\":{") != std::string::npos);
  CHECK(map.find("\"worldNorth\":1220") != std::string::npos);
}
