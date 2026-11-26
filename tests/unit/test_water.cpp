// All the waters alike (water.hpp): the sea, the lakes, the stream and the
// rivers found the same way; the magician going under water (sinking,
// rising while the jump is held, its pocket of air), only aquatic monsters
// coming along; monsters, seaweed and rocks down there; battles fought
// swimming; the lakes frozen in winter; one surface drawn for all of them.

#include "monstres/core/battle.hpp"
#include "monstres/core/continent.hpp"
#include "monstres/core/outer.hpp"
#include "monstres/core/rivers.hpp"
#include "monstres/core/water.hpp"
#include "monstres/core/west.hpp"
#include "monstres/render/scene.hpp"
#include "monstres/render/water_mesh.hpp"
#include "test_helpers.hpp"

#include <algorithm>
#include <cmath>

using namespace monstres;

namespace {

// Somewhere in the world's water of that kind, so deep, with dry land within `shore` meters.
bool find_water(const World& w, WaterKind kind, float min_depth, float max_depth, Vec2& at, Vec2* land = nullptr,
                float shore = 40.0f) {
  for (float x = -560; x < 560; x += 6) {
    for (float z = -560; z < 560; z += 6) {
      WaterSpot ws;
      if (!water_at(w, x, z, ws) || ws.kind != kind || ws.depth() < min_depth || ws.depth() > max_depth) continue;
      if (!land) {
        at = {x, z};
        return true;
      }
      for (float a = 0; a < 6.28f; a += 0.2f) {
        for (float r = 4; r < shore; r += 2) {
          const float lx = x + std::cos(a) * r, lz = z + std::sin(a) * r;
          if (!water_at(w, lx, lz, ws, 0.3f) && w.ground.height(lx, lz) < rules::kSeaLevel + 3.0f && !steep_ground(w, lx, lz)) {
            at = {x, z};
            *land = {lx, lz};
            return true;
          }
        }
      }
    }
  }
  return false;
}

void put_in_water(World& w, Vec2 at, float under) {
  WaterSpot ws;
  REQUIRE(water_at(w, at.x, at.z, ws));
  w.player.pos = {at.x, std::max(ws.ground, ws.level - under), at.z};
  w.player.velocity_y = 0;
  w.player.on_ground = false;
}

Input command(Command c, uint16_t target = 0) {
  Input in;
  in.command = c;
  in.target = target;
  return in;
}

Input held(bool jump_held, int8_t x = 0, int8_t y = 0, Angle16 yaw = 0) {
  Input in = move(x, y, yaw);
  in.jump_held = jump_held;
  return in;
}

}  // namespace

TEST_CASE("one water everywhere: the sea, the lakes, the stream and the rivers, found alike") {
  const World& w = cached_game(3);
  WaterSpot ws;
  // The village's lake (deep enough in its middle to go under).
  const Lake& lake = w.village.lake;
  REQUIRE(water_at(w, lake.center.x, lake.center.z, ws));
  CHECK(ws.kind == WaterKind::Lake);
  CHECK(ws.level == doctest::Approx(rules::kWaterLevel));
  CHECK(ws.depth() > 2.5f);
  // The continent's lake.
  const Pond& miroir = continent_ponds()[0];
  REQUIRE(water_at(w, miroir.center.x, miroir.center.z, ws));
  CHECK(ws.kind == WaterKind::Lake);
  CHECK(ws.level == doctest::Approx(miroir.level));
  // The west's stream, at its own level.
  const Stream& s = w.west.stream;
  const size_t mid = s.points.size() / 3;
  REQUIRE(water_at(w, s.points[mid].x, s.points[mid].z, ws));
  CHECK(ws.kind == WaterKind::River);
  CHECK(ws.level == doctest::Approx(s.level[mid]).epsilon(0.02));
  // The sea.
  Vec2 sea;
  REQUIRE(find_water(w, WaterKind::Sea, 4.0f, 30.0f, sea));
  REQUIRE(water_at(w, sea.x, sea.z, ws));
  CHECK(ws.level == doctest::Approx(rules::kSeaLevel));
  CHECK(under_water(w, sea.x, sea.z));
  // A river going down to the lake, then to the sea: its water, all along,
  // at the level of the river, then the lake's in the lake, then the sea's.
  const River& big = w.ground.rivers[1];
  float previous = 1e9f;
  for (size_t i = 0; i < big.points.size(); i += 3) {
    if (!water_at(w, big.points[i].x, big.points[i].z, ws)) continue;
    CHECK(ws.level <= previous + 0.05f);  // never up
    CHECK(ws.level >= big.level[i] - 0.05f);
    previous = ws.level;
  }
  REQUIRE(water_at(w, big.points.back().x, big.points.back().z, ws));
  CHECK(ws.level == doctest::Approx(rules::kSeaLevel));
}

TEST_CASE("the magician goes under water: it sinks, rises while the jump is held, its head out at the surface") {
  World w = cached_game(3);
  Vec2 at, land;
  REQUIRE(find_water(w, WaterKind::Sea, 6.0f, 14.0f, at, &land));
  WaterSpot ws;
  REQUIRE(water_at(w, at.x, at.z, ws));
  put_in_water(w, at, rules::kFloatDepth);
  step(w, Input{});
  CHECK(swimming(w));
  // Let go: down to the bottom, slowly.
  const float y0 = w.player.pos.y;
  run(w, Input{}, rules::kTicksPerSecond);
  CHECK(w.player.pos.y < y0 - 0.6f);
  CHECK(w.player.pos.y > y0 - rules::kSwimSink * 1.2f);
  CHECK(diving(w));
  run(w, Input{}, 15 * rules::kTicksPerSecond);
  CHECK(w.player.pos.y == doctest::Approx(w.ground.height(w.player.pos.x, w.player.pos.z)).epsilon(0.02));
  CHECK(w.player.on_ground);  // walking on the bottom
  // The jump held: up to the surface, its head out, staying there.
  run(w, held(true), 10 * rules::kTicksPerSecond);
  CHECK(w.player.pos.y == doctest::Approx(ws.level - rules::kFloatDepth).epsilon(0.02));
  CHECK_FALSE(diving(w));
  CHECK(swimming(w));
  // Slower across than walking.
  const Vec3 before = w.player.pos;
  run(w, held(true, 0, 127), rules::kTicksPerSecond);
  CHECK(std::hypot(w.player.pos.x - before.x, w.player.pos.z - before.z) <= rules::kSwimSpeed + 0.05f);
}

TEST_CASE("swimming back to the shore: out of the water, walking again") {
  World w = cached_game(3);
  Vec2 at, land;
  REQUIRE(find_water(w, WaterKind::Sea, 1.5f, 4.0f, at, &land, 30.0f));
  put_in_water(w, at, rules::kFloatDepth);
  // Towards the land (the camera's yaw: forward is (-sin, -cos)).
  const float dx = land.x - at.x, dz = land.z - at.z;
  const auto yaw = static_cast<Angle16>(std::lround(std::atan2(-dx, -dz) / 6.2831853f * 65536.0f) & 0xFFFF);
  for (int t = 0; t < 30 * rules::kTicksPerSecond && swimming(w); ++t) step(w, held(true, 0, 127, yaw));
  run(w, move(0, 127, yaw), 2 * rules::kTicksPerSecond);
  CHECK_FALSE(swimming(w));
}

TEST_CASE("only aquatic monsters come under water; the others wait in the crystal and come out again ashore") {
  World w = cached_game(3);
  REQUIRE(w.active == 0);  // Pompon, out
  Vec2 at, land;
  REQUIRE(find_water(w, WaterKind::Sea, 6.0f, 14.0f, at, &land));
  const Vec3 shore = w.player.pos;
  put_in_water(w, at, 2.0f);
  step(w, Input{});
  CHECK(w.active == -1);  // back into the crystal
  CHECK(w.ashore.slot == 0);
  // It cannot be called out down there; an aquatic one can.
  step(w, command(Command::Summon, 0));
  CHECK(w.active == -1);
  w.team.push_back({Species::Fish, {}, 5, 0, 30});
  const auto fish = static_cast<uint16_t>(w.team.size() - 1);
  step(w, command(Command::Summon, fish));
  REQUIRE(w.active == static_cast<int8_t>(fish));
  CHECK(w.ashore.slot == -1);  // another one chosen: Pompon stays in
  // It swims after the magician, at its depth.
  run(w, Input{}, 6 * rules::kTicksPerSecond);
  const Body& b = w.creatures[0].body;
  CHECK(submerged(w, b, species_info(Species::Fish).shape));
  CHECK(std::hypot(b.pos.x - w.player.pos.x, b.pos.z - w.player.pos.z) < 4.0f);
  CHECK(std::fabs(b.pos.y - w.player.pos.y) < 1.5f);
  // Pompon: back on dry land, it comes out again by itself.
  step(w, command(Command::Summon, fish));  // the fish goes back in
  put_in_water(w, at, 2.0f);
  step(w, command(Command::Summon, 0));
  CHECK(w.active == -1);
  w.ashore.slot = 0;
  w.player.pos = shore;
  w.player.on_ground = true;
  run(w, Input{}, rules::kAshoreTicks + 2);
  CHECK(w.active == 0);
  CHECK(w.ashore.slot == -1);
}

TEST_CASE("aquatic monsters live under water, among seaweed and rocks") {
  World w = cached_game(3);
  // Seaweed and rocks on the bottom, swum through (the seaweed).
  size_t weed = 0;
  for (size_t i = w.objects.size() - w.wild_objects; i < w.objects.size(); ++i) {
    const Object& o = w.objects[i];
    if (o.kind != ObjectKind::Seaweed) continue;
    ++weed;
    WaterSpot ws;
    CHECK(water_at(w, o.pos.x, o.pos.z, ws));
    CHECK(o.pos.y + o.size.y < ws.level);
    const Box b = object_box(o);
    CHECK(b.max.x - b.min.x == 0.0f);
  }
  CHECK(weed > 50);
  // Beyond the world: the continent's sea has them too.
  int ti = 0, tj = 0, outer_weed = 0;
  for (float x = 600; x < 1400 && outer_weed == 0; x += 64) {
    for (float z = -400; z < 400 && outer_weed == 0; z += 64) {
      outer_tile_of(-x, z, ti, tj);
      for (const Object& o : outer_objects(w, ti, tj)) outer_weed += o.kind == ObjectKind::Seaweed;
    }
  }
  CHECK(outer_weed > 0);
  // Monsters in the water near the player, staying in it.
  Vec2 at;
  REQUIRE(find_water(w, WaterKind::Sea, 6.0f, 30.0f, at));
  put_in_water(w, at, 1.0f);
  w.team[0].hp = 0;  // (nobody to fight: they keep away)
  for (int k = 0; k < 4; ++k) run(w, held(true), rules::kPopulationEveryTicks);
  std::vector<uint32_t> swimmers;
  for (const Creature& c : w.creatures) {
    if (&c != &w.creatures[0] && aquatic(c.species) && submerged(w, c.body, species_info(c.species).shape)) swimmers.push_back(c.id);
  }
  REQUIRE(swimmers.size() >= 2);
  run(w, held(true), 10 * rules::kTicksPerSecond);
  int still = 0;
  for (const Creature& c : w.creatures) {
    if (std::find(swimmers.begin(), swimmers.end(), c.id) == swimmers.end()) continue;
    still += submerged(w, c.body, species_info(c.species).shape) ? 1 : 0;
  }
  CHECK(still >= static_cast<int>(swimmers.size()) - 1);
}

TEST_CASE("a battle under water: only an aquatic monster fights there, swimming up and down") {
  World w = cached_game(3);
  Vec2 at;
  REQUIRE(find_water(w, WaterKind::Sea, 8.0f, 30.0f, at));
  WaterSpot ws;
  REQUIRE(water_at(w, at.x, at.z, ws));
  // A Bloup down there.
  Creature foe;
  foe.species = Species::Fish;
  foe.body.pos = {at.x + 3, ws.ground + 2.0f, at.z};
  foe.body.on_ground = false;
  foe.home = {at.x + 3, at.z};
  foe.level = 3;
  add_creature(w, foe);
  const uint32_t id = w.creatures.back().id;
  const auto find = [&]() {
    for (size_t k = 1; k < w.creatures.size(); ++k) {
      if (w.creatures[k].id == id) return k;
    }
    return size_t{0};
  };
  // From the shore with Pompon: not there.
  CHECK(can_engage(w, find()) == EngageCheck::Underwater);
  // Under water with a Ploufi.
  put_in_water(w, at, 3.0f);
  w.team.push_back({Species::Axolotl, {}, 6, 0, 40});
  step(w, Input{});
  step(w, command(Command::Summon, static_cast<uint16_t>(w.team.size() - 1)));
  run(w, Input{}, rules::kTicksPerSecond);
  REQUIRE(submerged(w, w.creatures[0].body, species_info(Species::Axolotl).shape));
  REQUIRE(can_engage(w, find()) == EngageCheck::Ok);
  step(w, command(Command::Engage, static_cast<uint16_t>(find())));
  REQUIRE(w.battle.phase == BattlePhase::Fight);
  // The jump held: it rises; let go: it sinks.
  const float y0 = w.creatures[0].body.pos.y;
  run(w, held(true), rules::kTicksPerSecond);
  const float y1 = w.creatures[0].body.pos.y;
  CHECK(y1 > y0 + 1.0f);
  run(w, held(false), rules::kTicksPerSecond);
  CHECK(w.creatures[0].body.pos.y < y1 - 0.5f);
  // The foe keeps in the water, near its depth.
  REQUIRE(find() > 0);
  const Creature& f = w.creatures[find()];
  CHECK(submerged(w, f.body, species_info(f.species).shape));
}

TEST_CASE("in winter the lakes and rivers freeze (walked on), never the sea") {
  World w = cached_game(3);
  w.sky.season = Season::Winter;
  const Lake& lake = w.village.lake;
  WaterSpot ws;
  REQUIRE(water_at(w, lake.center.x, lake.center.z, ws));
  CHECK(frozen(w, ws));
  CHECK(floor_height(w, lake.center.x, lake.center.z) == doctest::Approx(rules::kWaterLevel));
  w.player.pos = {lake.center.x, rules::kWaterLevel + 0.5f, lake.center.z};
  w.player.on_ground = false;
  run(w, Input{}, rules::kTicksPerSecond);
  CHECK(w.player.pos.y == doctest::Approx(rules::kWaterLevel));
  CHECK_FALSE(swimming(w));
  Vec2 sea;
  REQUIRE(find_water(w, WaterKind::Sea, 4.0f, 30.0f, sea));
  REQUIRE(water_at(w, sea.x, sea.z, ws));
  CHECK_FALSE(frozen(w, ws));
}

TEST_CASE("one surface drawn for all the waters; seen from under it, everything blue-green and close") {
  const World& w = cached_game(3);
  // Where the big river comes into the sea: one level at each point of the
  // surface (never a river's ribbon over the sea), the water's own.
  const River& big = w.ground.rivers[1];
  const Vec2 mouth = big.points[big.points.size() - 20];
  int ti = static_cast<int>(std::floor((mouth.x - Heightmap::kMinX) / water_tiles::kTile));
  int tj = static_cast<int>(std::floor((mouth.z - Heightmap::kMinZ) / water_tiles::kTile));
  const MeshData m = build_water_tile(w, ti, tj, water_tiles::kFine);
  REQUIRE(!m.indices.empty());
  int checked = 0;
  for (size_t v = 0; v < m.vertex_count(); ++v) {
    const float* p = &m.vertices[v * MeshData::kFloatsPerVertex];
    WaterSpot ws;
    if (!water_at(w, p[0], p[2], ws)) continue;  // (past the shore)
    CHECK(p[1] == doctest::Approx(ws.level));
    ++checked;
  }
  CHECK(checked > 100);
  // Under it, no sky: the water's color, close.
  World under = w;
  Vec2 at;
  REQUIRE(find_water(under, WaterKind::Sea, 8.0f, 30.0f, at));
  View v;
  WaterSpot ws;
  REQUIRE(water_at(under, at.x, at.z, ws));
  v.camera_x = at.x;
  v.camera_y = ws.level - 3.0f;
  v.camera_z = at.z;
  CHECK(under_water_view(under, v));
  CHECK_FALSE(v.sky);
  CHECK(v.fog_end < 100.0f);
  v = View{};
  v.camera_x = at.x;
  v.camera_y = ws.level + 3.0f;
  v.camera_z = at.z;
  CHECK_FALSE(under_water_view(under, v));
}
