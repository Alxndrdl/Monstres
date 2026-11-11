#include "monstres/core/heightmap.hpp"
#include "monstres/core/world.hpp"
#include "monstres/render/terrain_mesh.hpp"
#include "test_helpers.hpp"

#include <algorithm>
#include <cmath>

using namespace monstres;
using doctest::Approx;

namespace {

// A hand-made world on a heightmap given by f(x, z).
template <typename F>
World world_on(F f) {
  World w;
  w.ground.heights.resize(static_cast<size_t>(Heightmap::kPoints * Heightmap::kPoints));
  for (int i = 0; i < Heightmap::kPoints; ++i) {
    for (int j = 0; j < Heightmap::kPoints; ++j) {
      w.ground.heights[static_cast<size_t>(i * Heightmap::kPoints + j)] =
          f(static_cast<float>(i) - rules::kReliefHalfSize, static_cast<float>(j) - rules::kReliefHalfSize);
    }
  }
  w.player.pos.y = w.ground.height(0, 0);
  return w;
}

// Steepest slope between neighboring grid points inside the playable area.
float steepest_slope(const Heightmap& m, float half_size) {
  float steepest = 0;
  const int lo = static_cast<int>(rules::kReliefHalfSize - half_size);
  const int hi = static_cast<int>(rules::kReliefHalfSize + half_size);
  for (int i = lo; i < hi; ++i) {
    for (int j = lo; j < hi; ++j) {
      steepest = std::max({steepest, std::fabs(m.at(i + 1, j) - m.at(i, j)), std::fabs(m.at(i, j + 1) - m.at(i, j)),
                           std::fabs(m.at(i + 1, j + 1) - m.at(i, j)) / std::sqrt(2.0f)});
    }
  }
  return steepest;
}

}  // namespace

TEST_CASE("the relief depends on the seed only, and has real hills") {
  const World a = new_game(4), b = new_game(4), c = new_game(5);
  CHECK(a.ground == b.ground);
  CHECK_FALSE(a.ground == c.ground);
  for (uint64_t seed = 0; seed < 10; ++seed) {
    CAPTURE(seed);
    const World w = new_game(seed);
    float lo = 1e9f, hi = -1e9f;
    for (float x = -40; x <= 40; x += 2) {
      for (float z = -40; z <= 40; z += 2) {
        if (in_lake(w.village.lake, x, z, 1.0f)) continue;
        lo = std::min(lo, w.ground.height(x, z));
        hi = std::max(hi, w.ground.height(x, z));
      }
    }
    CHECK(hi - lo > 4.0f);
    CHECK(hi - lo < 20.0f);
  }
}

TEST_CASE("every slope of the playable area can be walked up") {
  for (uint64_t seed = 0; seed < 20; ++seed) {
    CAPTURE(seed);
    const World w = new_game(seed);
    CHECK(steepest_slope(w.ground, rules::kWorldHalfSize) < rules::kMaxWalkSlope * 0.9f);
  }
}

TEST_CASE("the village and its shore are flat, the lake bed is sunken") {
  for (uint64_t seed = 0; seed < 10; ++seed) {
    CAPTURE(seed);
    const World w = new_game(seed);
    const Vec2 c = w.village.center;
    for (float dx = -rules::kVillageRadius; dx <= rules::kVillageRadius; dx += 1) {
      for (float dz = -rules::kVillageRadius; dz <= rules::kVillageRadius; dz += 1) {
        if (dx * dx + dz * dz > rules::kVillageRadius * rules::kVillageRadius) continue;
        if (in_lake(w.village.lake, c.x + dx, c.z + dz, 1.0f)) continue;
        CHECK(std::fabs(w.ground.height(c.x + dx, c.z + dz)) < 1e-6f);
      }
    }
    const Lake& lake = w.village.lake;
    CHECK(w.ground.height(lake.center.x, lake.center.z) < rules::kWaterLevel - 0.5f);
    CHECK(w.ground.height(lake.center.x + lake.radius_x + 0.5f, lake.center.z) > rules::kWaterLevel);
  }
}

TEST_CASE("the ground is drawn where bodies walk") {
  const World w = new_game(2);
  const MeshData ground = build_ground_mesh(w);
  size_t checked = 0;
  for (size_t t = 0; t < ground.vertices.size(); t += 3 * MeshData::kFloatsPerVertex) {
    const float* a = &ground.vertices[t];
    const float* b = &ground.vertices[t + MeshData::kFloatsPerVertex];
    const float* c = &ground.vertices[t + 2 * MeshData::kFloatsPerVertex];
    if (std::fabs(a[0]) > rules::kWorldHalfSize || std::fabs(a[2]) > rules::kWorldHalfSize) continue;
    if (t % (97 * 3 * MeshData::kFloatsPerVertex) != 0) continue;  // a sample is enough
    // The middle of the triangle is on the plane through its corners.
    const float mx = (a[0] + b[0] + c[0]) / 3, my = (a[1] + b[1] + c[1]) / 3, mz = (a[2] + b[2] + c[2]) / 3;
    CHECK(w.ground.height(mx, mz) == Approx(my).epsilon(1e-4));
    CHECK(w.ground.height(a[0], a[2]) == Approx(a[1]));
    CHECK(a[4] > 0.6f);  // normals point up
    ++checked;
  }
  CHECK(checked > 50);
}

TEST_CASE("walking up and down a gentle slope keeps the feet on the ground") {
  World w = world_on([](float x, float) { return std::max(0.0f, -x) * 0.5f; });  // rises to the west
  w.player.pos = {10, w.ground.height(10, 0), 0};
  const Input west = move(-127, 0);
  for (int i = 0; i < 4 * rules::kTicksPerSecond; ++i) {
    step(w, west);
    CHECK(w.player.on_ground);
    CHECK(w.player.pos.y == Approx(w.ground.height(w.player.pos.x, w.player.pos.z)));
  }
  CHECK(w.player.pos.x < -5);
  CHECK(w.player.pos.y > 2.5f);
  const Input east = move(127, 0);
  for (int i = 0; i < 4 * rules::kTicksPerSecond; ++i) {
    step(w, east);
    CHECK(w.player.on_ground);  // walking down: no hopping
  }
  CHECK(w.player.pos.y == Approx(0));
}

TEST_CASE("a slope steeper than 45 degrees stops walkers, a jump gets over a small one") {
  // A ledge rising 1.6 m over one meter.
  World w = world_on([](float x, float) { return x <= -2 ? 1.6f : 0.0f; });
  w.player.pos = {0, 0, 0};
  run(w, move(-127, 0), 2 * rules::kTicksPerSecond);
  CHECK(w.player.pos.x > -1.2f);  // stopped at the foot
  CHECK(w.player.pos.y < 0.5f);
  CHECK(w.player.on_ground);

  // 1.15 m over one meter: too steep to walk up, low enough to jump onto.
  World low = world_on([](float x, float) { return x <= -2 ? 1.15f : 0.0f; });
  low.player.pos = {0, 0, 0};
  run(low, move(-127, 0), rules::kTicksPerSecond / 2);
  CHECK(low.player.pos.x > -1.2f);
  // With a short run-up, a jump lands on top.
  low.player.pos = {-0.3f, 0, 0};
  step(low, move(-127, 0, 0, true));
  run(low, move(-127, 0), rules::kTicksPerSecond);
  CHECK(low.player.pos.x < -3);
  CHECK(low.player.pos.y == Approx(1.15f));
}

TEST_CASE("creatures and the companion stay on the hills") {
  for (uint64_t seed = 0; seed < 3; ++seed) {
    CAPTURE(seed);
    World w = new_game(seed);
    // Walk around in circles for a while, through hills.
    for (int i = 0; i < 20 * rules::kTicksPerSecond; ++i) {
      step(w, move(90, 90, static_cast<Angle16>(i * 40)));
      if (i % 30 == 0) check_invariants(w);
    }
    for (const Creature& c : w.creatures) {
      if (c.species == Species::Fish || c.species == Species::Crow || !c.body.on_ground) continue;
      CHECK(c.body.pos.y >= w.ground.height(c.body.pos.x, c.body.pos.z));
    }
  }
}
