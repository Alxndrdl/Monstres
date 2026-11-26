// The continent's rivers (rivers.hpp): their ways down from the mountains,
// their water going down by steps (the waterfalls), their beds dug into the
// ground, the first going on into the playable area's stream; the map's
// pictures drawn from that same ground (outer.hpp: continent_map_rows, map_detail).

#include "monstres/core/continent.hpp"
#include "monstres/core/heightmap.hpp"
#include "monstres/core/outer.hpp"
#include "monstres/core/rivers.hpp"
#include "test_helpers.hpp"

#include <algorithm>
#include <cmath>

using namespace monstres;

namespace {

float dist(Vec2 a, Vec2 b) { return std::hypot(a.x - b.x, a.z - b.z); }

}  // namespace

TEST_CASE("two rivers: down from the mountains, one into the stream, one through the capital and the lake to the sea") {
  for (const uint64_t seed : {1ull, 3ull, 4ull}) {
    CAPTURE(seed);
    const World& w = cached_game(seed);
    const std::vector<River>& rivers = w.ground.rivers;
    REQUIRE(rivers.size() == 2);
    for (const River& r : rivers) {
      REQUIRE(r.points.size() > 200);
      REQUIRE(r.level.size() == r.points.size());
      // From up in the mountains, never into the playable area's relief (its own lands).
      CHECK(mountains_share(r.points.front().x, r.points.front().z) > 0.3f);
      CHECK(r.level.front() > 60.0f);
      // (The first's last meters over its edge, along the stream it goes on into.)
      for (size_t i = 0; i < r.points.size(); ++i) CHECK(rules::relief_inset(r.points[i].x, r.points[i].z) < (&r == &rivers[0] ? 12.0f : 0.0f));
      // Its water goes down, never up; by steps where it is steep (many waterfalls).
      for (size_t i = 1; i < r.level.size(); ++i) CHECK(r.level[i] <= r.level[i - 1] + 1e-4f);
      CHECK(r.falls.size() >= 4);
      for (const uint32_t f : r.falls) CHECK(r.level[f] - r.level[f + 1] >= 2.4f);
    }
    // The first ends in the stream, at its level: one river going on, its water drawn all along.
    const River& west = rivers[0];
    float nearest = 1e9f;
    size_t at = 0;
    for (size_t i = 0; i < w.west.stream.points.size() / 4; ++i) {
      if (dist(west.points.back(), w.west.stream.points[i]) < nearest) {
        nearest = dist(west.points.back(), w.west.stream.points[i]);
        at = i;
      }
    }
    CHECK(nearest < 1.0f);
    CHECK(west.level.back() == doctest::Approx(w.west.stream.level[at]).epsilon(0.02));
    CHECK(rules::relief_inset(west.points.back().x, west.points.back().z) > 0.0f);
    // The second through the capital, into the Lac Miroir, then to the sea.
    const River& big = rivers[1];
    const Vec2 capital = continent_landmarks()[9].at;
    const Pond& lake = continent_ponds()[0];
    float to_capital = 1e9f;
    bool in_lake = false;
    for (size_t i = 0; i < big.points.size(); ++i) {
      to_capital = std::min(to_capital, dist(big.points[i], capital));
      if (pond_shape(lake, big.points[i].x, big.points[i].z) > 0.2f) {
        in_lake = true;
        CHECK(big.level[i] == doctest::Approx(lake.level));
      }
    }
    CHECK(to_capital < 60.0f);
    CHECK(in_lake);
    CHECK(w.ground.sea(big.points.back().x, big.points.back().z));
    CHECK(island_inland(seed, big.points.back().x, big.points.back().z) < 0.05f);  // the open sea
    CHECK(big.level.back() == doctest::Approx(rules::kSeaLevel));
  }
}

TEST_CASE("a river's water in its bed: deeper than the ground in its middle, held in by its banks") {
  const World& w = cached_game(3);
  const Heightmap& g = w.ground;
  int wet = 0, held = 0, checked = 0;
  for (const River& r : g.rivers) {
    for (size_t i = 10; i + 10 < r.points.size(); i += 37) {
      // (Not by a waterfall: its lip and its pool.)
      bool falls = false;
      for (const uint32_t f : r.falls) falls = falls || (i + 3 >= f && i <= f + 4);
      if (falls || rules::relief_inset(r.points[i].x, r.points[i].z) > -rules::kOuterBlend) continue;
      if (pond_shape(continent_ponds()[0], r.points[i].x, r.points[i].z) > -0.1f) continue;  // (the lake: its own shore)
      const Vec2 p = r.points[i];
      const Vec2 a = r.points[i - 1], b = r.points[i + 1];
      const float len = dist(a, b);
      const Vec2 across{-(b.z - a.z) / len, (b.x - a.x) / len};
      ++checked;
      wet += g.height(p.x, p.z) < r.level[i] - 0.2f;
      const float bank = r.width[i] + 2.5f;
      held += g.height(p.x + across.x * bank, p.z + across.z * bank) > r.level[i] &&
              g.height(p.x - across.x * bank, p.z - across.z * bank) > r.level[i];
      float level = 0;
      if (r.width[i] > 2.0f) CHECK(in_river(g.rivers, p.x, p.z, g.height(p.x, p.z), level));
    }
  }
  CHECK(checked > 40);
  CHECK(wet >= checked * 95 / 100);  // (the narrowest, a stream a meter or two wide: on the ground's 2 m lattice)
  CHECK(held >= checked * 85 / 100);
  // Nothing grows in it.
  for (const River& r : g.rivers) {
    for (size_t i = 0; i < r.points.size(); i += 50) {
      int ti, tj;
      outer_tile_of(r.points[i].x, r.points[i].z, ti, tj);
      for (const Object& o : outer_objects(w, ti, tj)) {
        if (on_the_bottom(w, o)) continue;  // seaweed, rocks (water.hpp)
        RiverSpot s;
        if (nearest_river(g.rivers, o.pos.x, o.pos.z, 20.0f, s)) CHECK(s.distance > s.width + 1.0f);
      }
    }
  }
}

TEST_CASE("the map's pictures: the ground as walked on, its rivers in them") {
  const World& w = cached_game(3);
  const Continent& k = *continent_of(3);
  const River& big = w.ground.rivers[1];
  // A wide pixel on a wide stretch of the river is water (kind 2).
  int water = 0, looked = 0;
  for (size_t i = 0; i < big.points.size(); i += 60) {
    const Vec2 p = big.points[i];
    if (big.width[i] < 3.0f || rules::relief_inset(p.x, p.z) > -20.0f) continue;
    const int j = static_cast<int>((p.z - Continent::kMinZ) / continent_map::kCell);
    const int c = static_cast<int>((p.x - Continent::kMinX) / continent_map::kCell);
    std::vector<uint8_t> rows;
    continent_map_rows(w, k, j, j + 1, rows);
    ++looked;
    bool near = false;
    for (int d = -1; d <= 1; ++d) near = near || rows[static_cast<size_t>((c + d) * 4 + 3)] == 2;
    water += near;
  }
  CHECK(looked > 5);
  CHECK(water >= looked * 3 / 4);
  // Zoomed in, a meter a pixel: the river's middle is water, its things beside it.
  const Vec2 p = big.points[big.points.size() / 3];
  int ti, tj;
  outer_tile_of(p.x, p.z, ti, tj);
  std::vector<uint8_t> detail;
  map_detail(w, ti, tj, detail);
  REQUIRE(detail.size() == static_cast<size_t>(continent_map::kDetail * continent_map::kDetail * 4));
  const float x0 = Heightmap::kMinX + continent_map::kDetailSize * static_cast<float>(ti);
  const float z0 = Heightmap::kMinZ + continent_map::kDetailSize * static_cast<float>(tj);
  const int a = static_cast<int>(p.x - x0), b = static_cast<int>(p.z - z0);
  CHECK(detail[static_cast<size_t>((b * continent_map::kDetail + a) * 4 + 3)] == 2);
}
