// Going anywhere on the continent (step 4): the playable ground's edges open
// (the sea and the continent's own edge stop the player), the continent's
// trees and rocks are in the way, its monsters come and go by tile, games
// are saved anywhere; the far ground and the landscape follow the player.

#include "monstres/core/battle.hpp"
#include "monstres/core/outer.hpp"
#include "monstres/core/save.hpp"
#include "monstres/render/far_terrain.hpp"
#include "monstres/render/landscape.hpp"
#include "test_helpers.hpp"

#include <chrono>
#include <cmath>

using namespace monstres;

namespace {

Vec2 landmark(Region r) {
  for (const Landmark& l : continent_landmarks()) {
    if (l.region == r) return l.at;
  }
  FAIL("no such landmark");
  return {};
}

// A free spot on dry land near `at` (the landmark itself may be in a wood).
Vec3 free_spot(const World& w, Vec2 at) {
  for (int k = 0; k < 400; ++k) {
    const float a = 0.7f * static_cast<float>(k), r = 1.5f * static_cast<float>(k);
    const float x = at.x + r * std::cos(a), z = at.z + r * std::sin(a);
    Body b;
    b.pos = {x, w.ground.height(x, z), z};
    if (b.pos.y > rules::kSeaLevel + 1 && w.ground.normal(x, z).y > 0.9f && !inside_any_object(w, b, kPlayerShape)) return b.pos;
  }
  FAIL("no free spot");
  return {};
}

size_t outer_monsters(const World& w) {
  size_t n = 0;
  for (const Creature& c : w.creatures) n += monster_stats(c.species).is_monster && c.npc == 0 && !rules::in_world(c.home.x, c.home.z, -1.0f);
  return n;
}

void go(World& w, Vec3 to) {
  w.player.pos = to;
  w.player.velocity_y = 0;
  w.creatures[0].body.pos = {to.x + 0.8f, w.ground.height(to.x + 0.8f, to.z + 1.2f), to.z + 1.2f};
  update_population(w);
}

}  // namespace

TEST_CASE("over the playable ground's edge: walked on into the continent") {
  World w = new_game(3);
  // East, across the relief's edge, on and on (wherever the ground lets it).
  const float z = 120.0f;
  w.player.pos = {rules::kWorldHalfSize - 30, w.ground.height(rules::kWorldHalfSize - 30, z), z};
  float farthest = w.player.pos.x;
  for (int s = 0; s < 40 && farthest < rules::kWorldHalfSize + 40; ++s) {
    run(w, forward(static_cast<Angle16>(49152 + 2000 * ((s % 5) - 2))), rules::kTicksPerSecond);  // eastwards, weaving
    farthest = std::max(farthest, w.player.pos.x);
    check_invariants(w);
  }
  CHECK(farthest > rules::kWorldHalfSize + 40);
}

TEST_CASE("the continent's trees and rocks are in the way") {
  World w = new_game(4);
  int ti, tj, tested = 0;
  outer_tile_of(landmark(Region::Forest).x, landmark(Region::Forest).z, ti, tj);
  for (const Object& o : outer_objects(w, ti, tj)) {
    if (o.kind != ObjectKind::Tree && o.kind != ObjectKind::Pine) continue;  // (a low rock is stepped onto)
    const Box tree = object_box(o);
    // Walking north towards it, from 3 m south of it.
    const float x = o.pos.x, z = tree.max.z + 3.0f;
    Body start;
    start.pos = {x, w.ground.height(x, z), z};
    if (inside_any_object(w, start, kPlayerShape) || w.ground.normal(x, z).y < 0.9f) continue;
    w.player.pos = start.pos;
    w.player.velocity_y = 0;
    Body at_tree;
    at_tree.pos = {o.pos.x, w.ground.height(o.pos.x, o.pos.z), o.pos.z};
    CHECK(inside_any_object(w, at_tree, kPlayerShape));  // the continent's count
    run(w, forward(), rules::kTicksPerSecond * 3 / 2);
    CAPTURE(o.pos);
    CHECK(w.player.pos.z >= tree.max.z + rules::kPlayerHalfWidth - 1e-3f);  // never through it
    if (++tested == 12) break;
  }
  CHECK(tested >= 6);
}

TEST_CASE("the continent's monsters come with the player, go when it goes") {
  World w = new_game(7);
  CHECK(outer_monsters(w) == 0);
  go(w, free_spot(w, landmark(Region::Plains)));
  size_t total = 0;
  // A walk across a few tiles.
  const Vec3 start = w.player.pos;
  for (int k = 0; k < 6; ++k) {
    go(w, free_spot(w, {start.x + 90.0f * static_cast<float>(k), start.z + 40.0f * static_cast<float>(k % 2)}));
    total = std::max(total, outer_monsters(w));
    for (const Creature& c : w.creatures) {
      if (!monster_stats(c.species).is_monster || c.npc != 0 || rules::in_world(c.home.x, c.home.z, -1.0f)) continue;
      CHECK(c.body.pos.y > rules::kSeaLevel);
      CHECK(c.level >= 3);
      CHECK_FALSE(inside_any_object(w, c.body, species_info(c.species).shape));
    }
  }
  CHECK(total > 0);
  // Back home: all gone, none left behind.
  go(w, {0, w.ground.height(0, 0), 0});
  CHECK(outer_monsters(w) == 0);
  CHECK(std::none_of(w.population.outer.begin(), w.population.outer.end(), [](const OuterVisit& t) { return t.active != 0; }));
}

TEST_CASE("saved anywhere: loaded where the player was, the continent's monsters drawn again") {
  World w = new_game(9);
  go(w, free_spot(w, landmark(Region::Plains)));
  for (int i = 0; i < 4 && outer_monsters(w) == 0; ++i) go(w, free_spot(w, {w.player.pos.x + 140, w.player.pos.z}));
  REQUIRE(outer_monsters(w) > 0);
  const std::vector<uint8_t> bytes = serialize(w);
  LoadError error = LoadError::None;
  const std::optional<World> loaded = deserialize(bytes, &error);
  REQUIRE_MESSAGE(loaded.has_value(), load_error_name(error));
  World l = *loaded;
  CHECK(l.player.pos.x == w.player.pos.x);
  CHECK(l.player.pos.z == w.player.pos.z);
  CHECK(outer_monsters(l) == 0);  // not saved
  update_population(l);
  CHECK(outer_monsters(l) > 0);
  // Beyond the continent: not a place a save may hold.
  World far = w;
  far.player.pos = {rules::kReachMaxX + 50, 0, 0};
  CHECK_FALSE(deserialize(serialize(far)).has_value());
}

TEST_CASE("the far ground follows the player: around it, the same ground, quick enough") {
  const World& w = cached_game(3);
  const Continent k = make_continent(3);
  CHECK(far_center_for({100, -300}, {0, 0}).x == 0);  // near the start: as it is
  const Vec2 at = landmark(Region::Plains);
  const Vec2 c = far_center_for(at, {0, 0});
  CHECK(std::fabs(c.x - at.x) <= far_terrain::kCenterStep / 2);
  CHECK(std::fmod(c.x, far_terrain::kCenterStep) == 0);
  CHECK(far_center_for({c.x + 100, c.z}, c).x == c.x);  // a few steps: kept
  const auto start = std::chrono::steady_clock::now();
  const MeshData m = build_far_ground_mesh(k, &w.ground, c);
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  MESSAGE("far ground around " << c.x << ", " << c.z << ": " << ms << " ms, " << m.triangle_count() << " triangles");
  CHECK(m.triangle_count() < 220000);
  if (!kSanitized) CHECK(ms < 2000.0);
  // Its finest ring around the center: the tiles' ground (but the finest folds).
  int near = 0;
  for (size_t v = 0; v < m.vertices.size(); v += MeshData::kFloatsPerVertex) {
    const float x = m.vertices[v], y = m.vertices[v + 1], z = m.vertices[v + 2];
    if (std::max(std::fabs(x - c.x), std::fabs(z - c.z)) > far_terrain::kOuterAway[0] - 20 || m.vertices[v + 4] < 0.5f) continue;
    if (rules::relief_inset(x, z) > -(rules::kOuterBlend + 4.0f)) continue;  // by the relief: its own ground
    CHECK(y == doctest::Approx(outer_ground(k.seed, x, z, k.at_point(x, z) + w.ground.rise(x, z), 10.0f)));
    near += std::hypot(x - at.x, z - at.z) < 40.0f;
  }
  CHECK(near > 0);  // the player's surroundings are in it
}

TEST_CASE("the landscape follows the player over the continent, forgets what is far behind") {
  const World& w = cached_game(5);
  LandscapeCache cache;
  const Vec2 far = landmark(Region::Forest);
  cache.prepare(w, far, 1e9);
  CHECK(cache.objects_ready(far.x, far.z));
  DrawList list;
  build_landscape(w, list, landscape_center(far.x, far.z), cache);
  size_t trees = 0;
  for (const MeshId id : {MeshId::Tree, MeshId::TreeMid, MeshId::TreeFar, MeshId::Pine}) trees += list.batches[static_cast<size_t>(id)].size();
  CHECK(trees > 100);  // the forest's
  cache.prepare(w, {0, 0}, 1e9);  // back at the start
  CHECK_FALSE(cache.objects_ready(far.x, far.z));
  CHECK(cache.objects_ready(0, 0));
}

TEST_CASE("grass over the continent: thick in the plains, hardly any in the desert") {
  const World& w = cached_game(6);
  const auto grass_at = [&](Vec2 at) {
    DrawList list;
    LandscapeCache cache;
    build_landscape(w, list, landscape_center(at.x, at.z), cache);
    size_t n = 0;
    for (const MeshId id : {MeshId::GrassShort, MeshId::GrassTall, MeshId::GrassShortFar, MeshId::GrassTallFar, MeshId::Flower, MeshId::FlowerFar}) {
      n += list.batches[static_cast<size_t>(id)].size();
    }
    return n;
  };
  const Vec3 spot = free_spot(w, landmark(Region::Plains));
  const size_t plains = grass_at({spot.x, spot.z});
  const size_t desert = grass_at(landmark(Region::Desert));
  MESSAGE("grass: plains " << plains << ", desert " << desert);
  CHECK(plains > 2000);
  CHECK(desert * 5 < plains);
}

TEST_CASE("the Swift spell: ten times faster for 20 seconds, free, never through a fence") {
  World w = empty_world();
  w.creatures.push_back(Creature{});  // the companion
  w.adventure_spells = {SpellId::Swift, SpellId::None, SpellId::None, SpellId::None};
  w.wizard.mana = 0;
  CHECK(cannot_cast(w, 0) == nullptr);  // free
  Input cast;
  cast.command = Command::Cast;
  cast.target = 0;
  step(w, cast);
  CHECK(w.swift_ticks > 0);
  CHECK(std::string(cannot_cast(w, 0)) == "active");
  const float z0 = w.player.pos.z;
  run(w, forward(), rules::kTicksPerSecond);
  CHECK(z0 - w.player.pos.z == doctest::Approx(10 * rules::kWalkSpeed).epsilon(0.03));
  // A thin fence in the way: stopped at it, not through.
  add_box(w, {0, 0, w.player.pos.z - 20}, {3, 1, 0.05f});
  const float fence = w.player.pos.z - 20 + 0.05f;
  run(w, forward(), rules::kTicksPerSecond);
  CHECK(w.player.pos.z == doctest::Approx(fence + rules::kPlayerHalfWidth));
  // Over after 20 seconds: walking again.
  run(w, move(0, 0), 20 * rules::kTicksPerSecond);
  CHECK(w.swift_ticks == 0);
  const float z1 = w.player.pos.z;
  run(w, forward(static_cast<Angle16>(32768)), rules::kTicksPerSecond);  // back south
  CHECK(w.player.pos.z - z1 == doctest::Approx(rules::kWalkSpeed).epsilon(0.03));
}

TEST_CASE("past the old playable ground's edge, the land goes on as the relief left it") {
  for (uint64_t seed = 0; seed < kManySeeds / 3 + 2; ++seed) {
    CAPTURE(seed);
    const World w = new_game(seed);
    const Heightmap& g = w.ground;
    REQUIRE(g.rim.size() > 100);
    // All round: no straight coast drawn along the edge. Where the relief's
    // edge is dry land a little above the sea, 40 m on it still is (the sea
    // and the map say so too), and the ground does not fall away.
    int dry = 0, still = 0;
    float worst = 0;
    const float e = rules::kReliefHalfSize, n = rules::kReliefNorth;
    for (float t = -0.95f; t <= 0.95f; t += 0.01f) {
      const std::pair<Vec2, Vec2> sides[] = {{{t * e, -n}, {0, -1}}, {{e, -n + (t + 1) * 0.5f * (n + e)}, {1, 0}},
                                             {{t * e, e}, {0, 1}}, {{-e, -n + (t + 1) * 0.5f * (n + e)}, {-1, 0}}};
      for (const auto& [p, out] : sides) {
        const float at = g.height(p.x, p.z);
        if (at < rules::kSeaLevel + 3) continue;
        ++dry;
        const Vec2 q{p.x + out.x * 40.0f, p.z + out.z * 40.0f};
        still += !g.sea(q.x, q.z) && g.height(q.x, q.z) > rules::kSeaLevel;
        // (Against the slope just inside, the same way: a mountain's side goes on.)
        const Vec2 r{p.x - out.x * 40.0f, p.z - out.z * 40.0f};
        const float inside = g.height(r.x, r.z) - at;
        // (And against the continent's own slope there: its mountains' sides.)
        const float own = g.land_height(p.x, p.z) - (g.land_height(q.x, q.z) - g.rise(q.x, q.z));
        worst = std::max(worst, at - g.height(q.x, q.z) - std::max({0.0f, inside, own}));
      }
    }
    CHECK(still >= dry * 95 / 100);
    CHECK(worst < 22.0f);  // no cliff down from it (more than the slopes there have)
  }
}

TEST_CASE("the map's picture of the continent: the ground as walked on, no gap by the relief, quick enough") {
  const World& w = cached_game(3);
  const Continent k = make_continent(3);
  using continent_map::kCell;
  using continent_map::kWidth;
  std::vector<uint8_t> rows;
  double ms = 0;
  int sea = 0, sea_right = 0, land = 0, land_right = 0;
  for (int j = 0; j < continent_map::kHeight; j += 25) {
    const auto start = std::chrono::steady_clock::now();
    continent_map_rows(w, k, j, j + 1, rows);
    ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    REQUIRE(rows.size() == static_cast<size_t>(kWidth) * 4);
    const float z = Continent::kMinZ + kCell * (static_cast<float>(j) + 0.5f);
    for (int i = 0; i < kWidth; i += 3) {
      const float x = Continent::kMinX + kCell * (static_cast<float>(i) + 0.5f);
      const uint8_t kind = rows[static_cast<size_t>(i) * 4 + 3];
      const float inset = rules::relief_inset(x, z);
      if (inset > 3 * kCell + 1) {
        CHECK(kind == 0);  // the relief's own picture there
        continue;
      }
      CHECK(kind != 0);  // drawn all round it: no gap
      if (inset > -100) continue;
      const bool is_sea = w.ground.sea(x, z) && w.ground.height(x, z) < rules::kSeaLevel;
      (is_sea ? sea : land) += 1;
      (is_sea ? sea_right : land_right) += is_sea ? kind == 1 : kind >= 2;
    }
  }
  MESSAGE("one row in 25: " << ms << " ms");
  CHECK(sea > 1000);
  CHECK(land > 1000);
  CHECK(sea_right >= sea * 97 / 100);
  CHECK(land_right >= land * 97 / 100);
  if (!kSanitized) CHECK(ms < 500.0);
}
