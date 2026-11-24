// The world makes sense, in many worlds: nothing stands in the water or on a
// cliff that should not, the waterfall hides the cleft behind it, the ways
// stay dry but on their bridges, the bridges meet the ground, the teleporters
// stand on level ground at the foot of the cliffs (not on their edge).

#include "monstres/core/debug.hpp"
#include "monstres/core/interior.hpp"
#include "monstres/core/landfix.hpp"
#include "monstres/core/west.hpp"
#include "test_helpers.hpp"

#include <algorithm>
#include <cmath>
#include <string>

using namespace monstres;

namespace {

// In the water (deeper than a few centimeters).
bool wet(const World& w, float x, float z) { return under_water(w, x, z, -0.1f); }

// Out of the playable area, or at its very edge (where the path runs into the sea).
bool out_of_reach(Vec2 p) { return std::max(std::fabs(p.x), std::fabs(p.z)) > rules::kWorldHalfSize - 15.0f; }

bool nature(ObjectKind k) {
  return k == ObjectKind::Tree || k == ObjectKind::Pine || k == ObjectKind::Rock || k == ObjectKind::Bush ||
         k == ObjectKind::Stump || k == ObjectKind::FruitTree;
}

// Where a way is on one of the west's bridges.
bool on_bridge(const WestLayout& v, Vec2 p) {
  for (const auto& [c, a] : {std::pair{v.bridge, v.bridge_along}, std::pair{v.path_bridge, v.path_bridge_along}}) {
    const float u = (p.x - c.x) * a.x + (p.z - c.z) * a.z, q = -(p.x - c.x) * a.z + (p.z - c.z) * a.x;
    if (std::fabs(u) < rules::kBridgeHalf + 1.0f && std::fabs(q) < 3.0f) return true;
  }
  return false;
}

}  // namespace

TEST_CASE("the waterfall is high, its water far above the hidden cleft") {
  for (uint64_t seed = 0; seed < kManySeeds; ++seed) {
    CAPTURE(seed);
    const World& w = cached_game(seed);
    const Stream& s = w.west.stream;
    REQUIRE(s.falls > 0);
    CHECK(s.level[s.falls] - w.west.cove_level >= rules::kFallsLeast - 0.01f);
    // The cleft right behind the falling water, its floor just above the pool.
    for (const Object& o : w.objects) {
      if (o.kind != ObjectKind::HiddenCave) continue;
      CHECK(std::hypot(o.pos.x - w.west.falls.x, o.pos.z - w.west.falls.z) < rules::kHiddenOut + 0.5f);
      CHECK(o.pos.y > w.west.cove_level);
      CHECK(o.pos.y < w.west.cove_level + 1.0f);
    }
  }
}

TEST_CASE("no tree, rock or building in the water or on a cliff") {
  for (uint64_t seed = 0; seed < kManySeeds; ++seed) {
    CAPTURE(seed);
    const World& w = cached_game(seed);
    for (const Object& o : w.objects) {
      const Vec2 at{o.pos.x, o.pos.z};
      if (out_of_reach(at)) continue;
      const std::string kind = object_kind_name(o.kind);
      CAPTURE(kind);
      CAPTURE(o.pos);
      const bool in_water_by_design = o.kind == ObjectKind::Bridge || o.kind == ObjectKind::WaterMill ||
                                      o.kind == ObjectKind::HiddenCave || o.kind == ObjectKind::Collider ||
                                      o.kind == ObjectKind::Log || o.kind == ObjectKind::Pier || o.kind == ObjectKind::Boat;
      if (!in_water_by_design) CHECK_FALSE(wet(w, at.x, at.z));
      if (nature(o.kind)) CHECK_FALSE(steep_ground(w, at.x, at.z));
    }
  }
}

TEST_CASE("the path and the road stay dry but on their bridges, whose ends meet the ground") {
  for (uint64_t seed = 0; seed < kManySeeds; ++seed) {
    CAPTURE(seed);
    const World& w = cached_game(seed);
    const WestLayout& v = w.west;
    for (const auto* way : {&w.path.points, &v.road.points}) {
      for (const Vec2& p : *way) {
        if (on_bridge(v, p) || out_of_reach(p)) continue;
        CAPTURE(p.x);
        CAPTURE(p.z);
        CHECK_FALSE(wet(w, p.x, p.z));
      }
    }
    int k = 0;
    for (const Object& o : w.objects) {
      if (o.kind != ObjectKind::Bridge) continue;
      const Vec2 way = k++ == 0 ? v.bridge_along : v.path_bridge_along;
      // (Its deck along X or Z, the axis nearest the way's: where it meets the banks.)
      const Vec2 a = std::fabs(way.x) >= std::fabs(way.z) ? Vec2{way.x >= 0 ? 1.0f : -1.0f, 0} : Vec2{0, way.z >= 0 ? 1.0f : -1.0f};
      const float deck = o.pos.y + 2 * o.size.y;
      for (const float t : {-1.0f, 1.0f}) {
        const float g = w.ground.height(o.pos.x + a.x * t * (rules::kBridgeHalf - 0.3f), o.pos.z + a.z * t * (rules::kBridgeHalf - 0.3f));
        CHECK(std::fabs(deck - g) < 0.5f);
      }
    }
    CHECK(k == 2);
  }
}

TEST_CASE("the teleporters stand on level ground, not on the edge of a cliff") {
  for (uint64_t seed = 0; seed < kManySeeds; ++seed) {
    CAPTURE(seed);
    const World& w = cached_game(seed);
    for (const Object& o : w.objects) {
      if (o.kind != ObjectKind::Teleporter) continue;
      CAPTURE(int(o.variant));
      CAPTURE(o.pos);
      CHECK_FALSE(steep_ground(w, o.pos.x, o.pos.z));
      CHECK_FALSE(wet(w, o.pos.x, o.pos.z));
      CHECK(std::fabs(o.pos.y - w.ground.height(o.pos.x, o.pos.z)) < 0.3f);
      if (w.west.valid()) CHECK(std::fabs(scarp_side(w.west, o.pos.x, o.pos.z)) > 4.0f);
    }
  }
}

TEST_CASE("no water shut in inland: every sea drawn reaches the open sea") {
  for (uint64_t seed = 0; seed < kManySeeds; ++seed) {
    CAPTURE(seed);
    const World& w = cached_game(seed);
    // The sea as drawn: squares of 16 m wet where some ground is under it (build_water_mesh).
    constexpr float kSquare = 16.0f;
    const int nx = static_cast<int>(2 * rules::kReliefHalfSize / kSquare);
    const int nz = static_cast<int>((rules::kReliefHalfSize + rules::kReliefNorth) / kSquare);
    std::vector<int> state(static_cast<size_t>(nx * nz), 0);  // 0 dry, 1 wet, 2 reached from the open sea
    std::vector<std::pair<int, int>> stack;
    for (int i = 0; i < nx; ++i) {
      for (int j = 0; j < nz; ++j) {
        const float x0 = Heightmap::kMinX + kSquare * static_cast<float>(i), z0 = Heightmap::kMinZ + kSquare * static_cast<float>(j);
        bool wet_square = false;
        for (float a = x0; a <= x0 + kSquare && !wet_square; a += 2) {
          for (float b = z0; b <= z0 + kSquare && !wet_square; b += 2) {
            wet_square = w.ground.height(a, b) < rules::kSeaLevel + 0.2f && w.ground.sea(a, b);
          }
        }
        if (!wet_square) continue;
        const bool open = i == 0 || j == 0 || i == nx - 1 || j == nz - 1 || !rules::in_world(x0 + 8, z0 + 8, -8.0f);
        state[static_cast<size_t>(i * nz + j)] = open ? 2 : 1;
        if (open) stack.push_back({i, j});
      }
    }
    while (!stack.empty()) {
      const auto [i, j] = stack.back();
      stack.pop_back();
      for (const auto& [u, v] : {std::pair{i + 1, j}, std::pair{i - 1, j}, std::pair{i, j + 1}, std::pair{i, j - 1}}) {
        if (u < 0 || v < 0 || u >= nx || v >= nz || state[static_cast<size_t>(u * nz + v)] != 1) continue;
        state[static_cast<size_t>(u * nz + v)] = 2;
        stack.push_back({u, v});
      }
    }
    int shut = 0;
    for (const int k : state) shut += k == 1;
    CHECK(shut == 0);
  }
}

TEST_CASE("the buildings of the wilds stand on flat ground, their doors at its level") {
  for (uint64_t seed = 0; seed < kManySeeds; ++seed) {
    CAPTURE(seed);
    const World& w = cached_game(seed);
    for (const Object& o : w.objects) {
      if (!seated_kind(o.kind) || std::max(std::fabs(o.pos.x), std::fabs(o.pos.z)) < rules::kValleyHalfSize + 12.0f) continue;
      const std::string kind = object_kind_name(o.kind);
      CAPTURE(kind);
      CAPTURE(o.pos);
      CHECK(std::fabs(w.ground.height(o.pos.x, o.pos.z) - o.pos.y) < 0.15f);
      if (place_behind(o.kind) == PlaceKind::Outside) continue;
      const Vec2 d = door_use_spot(o);
      CHECK(std::fabs(w.ground.height(d.x, d.z) - o.pos.y) < 0.3f);  // no step up nor down at its door
    }
  }
}

TEST_CASE("the abbey's way up follows the slope: never much steeper than 1 in 6") {
  for (uint64_t seed = 0; seed < kManySeeds; ++seed) {
    CAPTURE(seed);
    const World& w = cached_game(seed);
    const auto& way = w.sites.abbey_way;
    REQUIRE(way.size() > 60);
    int steep = 0;
    float worst = 0;
    for (size_t k = 4; k + 4 < way.size(); ++k) {
      const float grade = std::fabs(w.ground.height(way[k + 4].x, way[k + 4].z) - w.ground.height(way[k - 4].x, way[k - 4].z)) / 8.0f;
      worst = std::max(worst, grade);
      steep += grade > 0.18f;
    }
    CHECK(worst < 0.3f);
    CHECK(steep < 20);  // (a few meters in its hairpins at most)
  }
}
