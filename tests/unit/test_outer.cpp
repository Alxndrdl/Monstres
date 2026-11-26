// The continent's nature and monsters beyond the relief (outer.hpp): by
// tile, the same whatever was asked first, no seams, no rows, by region.

#include "monstres/core/outer.hpp"
#include "monstres/core/west.hpp"
#include "monstres/core/variants.hpp"
#include "test_helpers.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>

using namespace monstres;

namespace {

// A copy of a game: its caches empty.
World fresh(uint64_t seed) {
  World w = cached_game(seed);
  REQUIRE(w.outer.size() == 0);
  return w;
}

Vec2 landmark(Region r) {
  for (const Landmark& l : continent_landmarks()) {
    if (l.region == r) return l.at;
  }
  FAIL("no such landmark");
  return {};
}

// The tiles around a place (a square of n by n).
std::vector<std::pair<int, int>> tiles_around(Vec2 at, int n) {
  int ti, tj;
  outer_tile_of(at.x, at.z, ti, tj);
  std::vector<std::pair<int, int>> out;
  for (int a = 0; a < n; ++a) {
    for (int b = 0; b < n; ++b) out.push_back({ti - n / 2 + a, tj - n / 2 + b});
  }
  return out;
}

std::vector<Object> gather(const World& w, const std::vector<std::pair<int, int>>& tiles) {
  std::vector<Object> all;
  for (const auto& [ti, tj] : tiles) {
    const auto& t = outer_objects(w, ti, tj);
    all.insert(all.end(), t.begin(), t.end());
  }
  return all;
}

}  // namespace

TEST_CASE("the continent's nature: the same whatever tile was made first") {
  const auto tiles = tiles_around(landmark(Region::Forest), 3);
  World a = fresh(4), b = fresh(4);
  std::vector<std::vector<Object>> first;
  for (const auto& [ti, tj] : tiles) first.push_back(outer_objects(a, ti, tj));
  for (size_t k = tiles.size(); k-- > 0;) {
    CAPTURE(k);
    CHECK(outer_objects(b, tiles[k].first, tiles[k].second) == first[k]);
  }
}

TEST_CASE("no seam: nothing in another's way, across the tiles' edges too") {
  for (const Region r : {Region::Forest, Region::Mountains, Region::Plains}) {
    CAPTURE(static_cast<int>(r));
    const World w = fresh(2);
    const std::vector<Object> all = gather(w, tiles_around(landmark(r), 3));
    std::vector<Box> boxes;
    for (const Object& o : all) boxes.push_back(object_box(o));
    CHECK(overlapping_pairs(boxes).empty());
  }
}

TEST_CASE("nothing in the water, on a cliff, nor in the playable world (up to the relief's edge: as anywhere)") {
  const World w = fresh(3);
  int seen = 0;
  for (const Region r : {Region::Forest, Region::Marsh, Region::Lake, Region::Mountains, Region::Desert, Region::Volcano}) {
    for (const Object& o : gather(w, tiles_around(landmark(r), 3))) {
      ++seen;
      CAPTURE(o.pos);
      if (on_the_bottom(w, o)) continue;  // seaweed, rocks (water.hpp)
      CHECK(w.ground.height(o.pos.x, o.pos.z) > rules::kSeaLevel + 0.5f);
      CHECK(rules::world_inset(o.pos.x, o.pos.z) < -2.9f);
      CHECK(w.ground.normal(o.pos.x, o.pos.z).y > 0.67f);
      CHECK(std::fabs(o.pos.y - w.ground.height(o.pos.x, o.pos.z)) < 0.05f + 0.4f * 2.0f);
    }
  }
  CHECK(seen > 500);
  // And none within the world (its own there).
  CHECK(outer_objects(w, 2, 3).empty());
  // Between the world's edge and the relief's, the continent's (no band
  // left bare along the old edge), but none on the world's ways nor in its stream.
  int band = 0;
  for (int tj = 0; tj < Heightmap::kCellsZ / rules::kTileCells; ++tj) {
    for (const int ti : {0, Heightmap::kCellsX / rules::kTileCells - 1}) {
      for (const Object& o : outer_objects(w, ti, tj)) {
        CAPTURE(o.pos);
        CHECK(rules::world_inset(o.pos.x, o.pos.z) < -2.9f);
        CHECK(distance_to_path(w.path, o.pos.x, o.pos.z) > 5.0f);
        if (!on_the_bottom(w, o)) CHECK(distance_to_stream(w.west.stream, o.pos.x, o.pos.z) > 6.0f);
        band += rules::relief_inset(o.pos.x, o.pos.z) > 0;
      }
    }
  }
  CHECK(band > 100);
}

TEST_CASE("each region its own: thick woods, bare desert, pines and rocks up the mountains") {
  const World w = fresh(5);
  std::map<Region, std::map<ObjectKind, int>> kinds;
  std::map<Region, int> count;
  for (const Region r : {Region::Forest, Region::Desert, Region::Mountains, Region::Plains}) {
    for (const Object& o : gather(w, tiles_around(landmark(r), 3))) {
      if (outer_region(w.seed, o.pos.x, o.pos.z) != r) continue;
      ++kinds[r][o.kind];
      ++count[r];
    }
  }
  CHECK(count[Region::Forest] > 4 * count[Region::Plains]);
  CHECK(count[Region::Plains] > count[Region::Desert]);
  CHECK(kinds[Region::Forest][ObjectKind::Tree] > kinds[Region::Forest][ObjectKind::Rock]);
  CHECK(kinds[Region::Mountains][ObjectKind::Tree] == 0);  // no leafy trees up there
  CHECK(kinds[Region::Desert][ObjectKind::Tree] + kinds[Region::Desert][ObjectKind::Pine] == 0);
  // Up in the snow, only rocks.
  for (const Object& o : gather(w, tiles_around(landmark(Region::Mountains), 5))) {
    if (w.ground.snow(o.pos.x, o.pos.z, w.ground.height(o.pos.x, o.pos.z)) > 0.5f) CHECK(o.kind == ObjectKind::Rock);
  }
}

TEST_CASE("never in rows: patches and clumps, anywhere in their cells") {
  const World w = fresh(6);
  const std::vector<Object> all = gather(w, tiles_around(landmark(Region::Forest), 5));
  REQUIRE(all.size() > 1000);
  // Within their 5 m cells: all over them (not lined up).
  int bins[5] = {};
  for (const Object& o : all) {
    const float in = o.pos.x / rules::kOuterCell - std::floor(o.pos.x / rules::kOuterCell);
    ++bins[std::min(4, static_cast<int>(in * 5))];
  }
  for (const int b : bins) CHECK(b > static_cast<int>(all.size()) / 10);
  // Thick here, thin there: the count by squares of 40 m varies much.
  std::map<std::pair<int, int>, int> squares;
  for (const Object& o : all) ++squares[{static_cast<int>(std::floor(o.pos.x / 40.0f)), static_cast<int>(std::floor(o.pos.z / 40.0f))}];
  double sum = 0, sum2 = 0;
  for (const auto& [k, n] : squares) {
    sum += n;
    sum2 += static_cast<double>(n) * n;
  }
  const double mean = sum / static_cast<double>(squares.size());
  const double spread = std::sqrt(std::max(0.0, sum2 / static_cast<double>(squares.size()) - mean * mean));
  CHECK(spread / mean > 0.3);
}

TEST_CASE("the continent's monsters: of their region, stronger far away, the same for the same visit") {
  const World w = fresh(7);
  int far_levels = 0, near_levels = 0, far_n = 0, near_n = 0;
  for (const Region r : {Region::Plains, Region::Forest, Region::Mountains, Region::Desert, Region::Marsh}) {
    for (const auto& [ti, tj] : tiles_around(landmark(r), 5)) {
      const auto a = outer_creatures(w, ti, tj, 1), b = outer_creatures(w, ti, tj, 1);
      CHECK(a == b);
      for (const Creature& c : a) {
        if (aquatic(c.species) && submerged(w, c.body, species_info(c.species).shape)) continue;  // under water (water.hpp)
        const Region here = outer_region(w.seed, c.body.pos.x, c.body.pos.z);
        const auto kinds = outer_species(here);
        CHECK(std::find(kinds.begin(), kinds.end(), c.species) != kinds.end());
        CHECK(c.body.pos.y > rules::kSeaLevel + 0.5f);
        CHECK(valid_genes(c.species, c.genes));
        const float d = std::hypot(c.body.pos.x, c.body.pos.z);
        if (d > 2200) {
          far_levels += c.level;
          ++far_n;
        } else if (d < 1300) {
          near_levels += c.level;
          ++near_n;
        }
      }
    }
  }
  REQUIRE(far_n > 10);
  REQUIRE(near_n > 10);
  CHECK(far_levels / far_n > near_levels / near_n + 5);
  // Another visit, others.
  int ti, tj;
  outer_tile_of(landmark(Region::Plains).x, landmark(Region::Plains).z, ti, tj);
  CHECK_FALSE(outer_creatures(w, ti, tj, 1) == outer_creatures(w, ti, tj, 2));
}

TEST_CASE("the continent's nature is quick to make, and kept a while only") {
  const World w = fresh(8);
  const auto start = std::chrono::steady_clock::now();
  size_t n = 0;
  for (const auto& [ti, tj] : tiles_around(landmark(Region::Forest), 7)) n += outer_objects(w, ti, tj).size();
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  MESSAGE("49 tiles of the forest: " << n << " things in " << ms << " ms");
  if (!kSanitized) CHECK(ms < 3000.0);
  for (const auto& [ti, tj] : tiles_around(landmark(Region::Desert), 15)) outer_objects(w, ti, tj);
  CHECK(w.outer.size() <= rules::kOuterTilesKept);
}
