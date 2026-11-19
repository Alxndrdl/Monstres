#include "monstres/render/scene.hpp"
#include "monstres/core/continent.hpp"
#include "test_helpers.hpp"

#include <cmath>

using namespace monstres;
using doctest::Approx;

namespace {

// Applies a column-major matrix to a point, returns normalized device coordinates.
Vec3 project(const Mat4& m, Vec3 p) {
  const float x = m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12];
  const float y = m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13];
  const float z = m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14];
  const float w = m.m[3] * p.x + m.m[7] * p.y + m.m[11] * p.z + m.m[15];
  return {x / w, y / w, z / w};
}

const std::vector<Instance>& batch(const DrawList& list, MeshId id) {
  return list.batches[static_cast<size_t>(id)];
}

size_t count_inside_play_area(const std::vector<Instance>& instances) {
  size_t n = 0;
  for (const Instance& in : instances) {
    if (std::fabs(in.x) <= rules::kWorldHalfSize && std::fabs(in.z) <= rules::kWorldHalfSize) ++n;
  }
  return n;
}

}  // namespace

TEST_CASE("each object is drawn with the mesh of its kind, with a shadow") {
  World w = empty_world();
  w.objects.push_back(make_object(ObjectKind::Tree, {5, 0, 5}, {1.2f, 1.2f, 1.2f}));
  w.objects.push_back(make_object(ObjectKind::Pine, {-5, 0, 5}, {1, 1, 1}));
  w.objects.push_back(make_object(ObjectKind::Log, {0, 0, 8}, {2, 0.3f, 0}, 16384));
  w.objects.push_back(make_object(ObjectKind::Rock, {8, 0, 0}, {1, 0.5f, 0.8f}));
  w.objects.back().variant = 6;  // -> Rock2
  add_box(w, {-8, 0, 0}, {0.5f, 0.5f, 0.5f});

  DrawList list;
  build_landscape(w, list);
  REQUIRE(list.batches.size() == kMeshCount);

  CHECK(count_inside_play_area(batch(list, MeshId::Tree)) == 1);
  CHECK(count_inside_play_area(batch(list, MeshId::Pine)) == 1);
  CHECK(batch(list, MeshId::Log).size() == 1);
  CHECK(count_inside_play_area(batch(list, MeshId::Rock2)) == 1);
  CHECK(batch(list, MeshId::Crate).size() == 1);
  CHECK(count_inside_play_area(batch(list, MeshId::Shadow)) == 5);
  CHECK(batch(list, MeshId::Ground).empty());  // the ground, the path and the water: the terrain list (terrain_mesh.hpp)
  CHECK(batch(list, MeshId::Path).empty());

  const Instance& log = batch(list, MeshId::Log)[0];
  CHECK(log.rotation.y == Approx(std::sin(kPi / 4)));  // turned 90° around Y
  CHECK(log.rotation.w == Approx(std::cos(kPi / 4)));
  CHECK(log.sx == Approx(2));
  CHECK(log.sy == Approx(0.3f));
  CHECK(batch(list, MeshId::Crate)[0].y == Approx(0.5f));  // the crate mesh is centered
}

TEST_CASE("lighter grass: fewer tufts, simpler sooner; the rest of the landscape the same") {
  const World& w = cached_game(42);
  DrawList usual, lighter;
  build_landscape(w, usual);
  set_lighter_grass(true);
  build_landscape(w, lighter);
  set_lighter_grass(false);
  const auto count = [](const DrawList& l, std::initializer_list<MeshId> ids) {
    size_t n = 0;
    for (MeshId id : ids) n += l.batches[static_cast<size_t>(id)].size();
    return n;
  };
  const auto grass = {MeshId::GrassShort, MeshId::GrassTall, MeshId::GrassShortFar, MeshId::GrassTallFar};
  CHECK(count(lighter, grass) < count(usual, grass) * 9 / 10);
  CHECK(count(lighter, {MeshId::GrassShort, MeshId::GrassTall}) < count(usual, {MeshId::GrassShort, MeshId::GrassTall}) / 2);
  CHECK(count(lighter, {MeshId::Crate}) == count(usual, {MeshId::Crate}));
  CHECK(count(lighter, {MeshId::Shadow}) == count(usual, {MeshId::Shadow}));
}

TEST_CASE("the landscape has grass, flowers, a forest beyond the edges and mountains") {
  const World& w = cached_game(42);
  DrawList list;
  build_landscape(w, list);

  // Near the center in full, simpler farther (and thinning out).
  std::vector<Instance> short_grass = batch(list, MeshId::GrassShort), tall_grass = batch(list, MeshId::GrassTall);
  short_grass.insert(short_grass.end(), batch(list, MeshId::GrassShortFar).begin(), batch(list, MeshId::GrassShortFar).end());
  tall_grass.insert(tall_grass.end(), batch(list, MeshId::GrassTallFar).begin(), batch(list, MeshId::GrassTallFar).end());
  CHECK(short_grass.size() > 1500);
  CHECK(tall_grass.size() > 300);
  CHECK(batch(list, MeshId::Flower).size() + batch(list, MeshId::FlowerFar).size() > 100);
  CHECK(batch(list, MeshId::GrassShortFar).size() > batch(list, MeshId::GrassShort).size());

  // No grass in the middle of the path or inside an object.
  for (const auto* grass : {&short_grass, &tall_grass}) {
    for (const Instance& g : *grass) {
      REQUIRE(distance_to_path(w.path, g.x, g.z) >= 0.75f * rules::kPathHalfWidth);
      for (const Object& o : w.objects) {
        const Box b = object_box(o);
        REQUIRE_FALSE((g.x > b.min.x && g.x < b.max.x && g.z > b.min.z && g.z < b.max.z));
      }
    }
  }

  // The trees around the center are drawn (simplified far from the player), not the others.
  const Vec2 center = landscape_center(w.player.pos.x, w.player.pos.z);
  size_t around = 0;
  for (const Object& o : w.objects) {
    if (o.kind != ObjectKind::Tree && o.kind != ObjectKind::Pine) continue;
    around += std::hypot(o.pos.x - center.x, o.pos.z - center.z) <= landscape::kObjectsRadius;
  }
  size_t drawn = 0;
  for (MeshId id : {MeshId::Tree, MeshId::Pine, MeshId::TreeMid, MeshId::TreeFar, MeshId::PineFar}) {
    for (const Instance& t : batch(list, id)) {
      ++drawn;
      CHECK(std::hypot(t.x - center.x, t.z - center.z) <= landscape::kObjectsRadius + 1e-3f);
      CHECK(t.material == kFadeTree);  // they shrink away before the edge
    }
  }
  CHECK(drawn == around);
  CHECK(around < w.objects.size() / 3);  // far from all of them
  for (MeshId id : {MeshId::Tree, MeshId::Pine, MeshId::TreeMid}) {  // full trees near the player only
    for (const Instance& t : batch(list, id)) {
      CHECK(std::hypot(t.x - w.player.pos.x, t.z - w.player.pos.z) < landscape::kDetailRadius + landscape::kStep);
    }
  }
  for (const Instance& t : batch(list, MeshId::Tree)) {  // in full very near only, then simpler
    CHECK(std::hypot(t.x - center.x, t.z - center.z) < landscape::kFullRadius + 1e-3f);
  }
  CHECK(batch(list, MeshId::TreeFar).size() > batch(list, MeshId::Tree).size());

  // No painted mountains any more: the continent's relief is drawn instead (far lists).
  for (size_t i = 0; i < kMountainVariants; ++i) CHECK(batch(list, mesh_offset(MeshId::Mountain0, i)).empty());
  CHECK_FALSE(list.far);
}

TEST_CASE("the continent is drawn in the far pass: its ground, waters, lava, and the volcano's smoke") {
  DrawList far;
  build_far_landscape(far);
  CHECK(far.far);
  CHECK(far.cached);
  CHECK(batch(far, MeshId::FarGround).size() == 1);
  CHECK(batch(far, MeshId::FarWater).size() == 1);
  CHECK(batch(far, MeshId::FarWater)[0].material == kWater);
  CHECK(batch(far, MeshId::Lava)[0].material == kGlowing);
  CHECK(batch(far, MeshId::Ground).empty());
  DrawList smoke;
  const World& w = cached_game(3);
  build_far_dynamic(w, 12.0f, smoke);
  CHECK(smoke.far);
  const Crater crater = volcano_crater();
  REQUIRE(batch(smoke, MeshId::Smoke).size() == 16);
  for (const Instance& puff : batch(smoke, MeshId::Smoke)) {
    CHECK(std::hypot(puff.x - crater.center.x, puff.z - crater.center.z) < 600.0f);
    CHECK(puff.y > crater.lava_level);
  }
  // The near pass reaches beyond the playable area; the far one to the horizon, with a fog thick enough there.
  const View v = build_view(w, Camera{}, 1.5f, 0);
  CHECK(view_range::kNearFar > rules::kReliefHalfSize * 2.9f);  // the whole relief, from one of its corners
  CHECK(v.fog_end <= view_range::kFarFar);
  CHECK(v.fog_half < 3000.0f);
}

TEST_CASE("grass grows in the valley's hollows below the sea's level (dry land), not on the shore") {
  // Seed 42 starts in a hollow below the sea's level.
  const World& w = cached_game(42);
  REQUIRE(w.ground.height(0, 0) < rules::kSeaLevel);
  DrawList list;
  build_landscape(w, list);
  size_t low = 0, shore = 0;
  for (MeshId id : {MeshId::GrassShort, MeshId::GrassShortFar, MeshId::GrassTall, MeshId::GrassTallFar}) {
    for (const Instance& g : list.batches[static_cast<size_t>(id)]) {
      low += g.y < rules::kSeaLevel;
      shore += g.y < rules::kSeaLevel + 0.5f && w.ground.land_height(g.x, g.z) < rules::kSeaLevel + 1.0f;
    }
  }
  CHECK(low > 500);
  CHECK(shore == 0);
  // On the west coast: no grass on the wet sand nor in the sea.
  World coast = cached_game(3);
  float x = -150;
  while (x > -rules::kWorldHalfSize && !(coast.ground.sea(x, 0) && coast.ground.height(x, 0) < rules::kSeaLevel)) x -= 2;
  REQUIRE(x > -rules::kWorldHalfSize);
  coast.player.pos = {x + 10, coast.ground.height(x + 10, 0), 0};
  DrawList beach;
  build_landscape(coast, beach);
  for (const Instance& g : beach.batches[static_cast<size_t>(MeshId::GrassShort)]) {
    CHECK_FALSE((coast.ground.sea(g.x, g.z) && coast.ground.height(g.x, g.z) < rules::kSeaLevel + 0.5f));
  }
}

TEST_CASE("the landscape depends on the world only") {
  const World& w = cached_game(9);
  DrawList a, b;
  build_landscape(w, a);
  build_landscape(w, b);
  for (size_t i = 0; i < kMeshCount; ++i) {
    REQUIRE(a.batches[i].size() == b.batches[i].size());
    for (size_t j = 0; j < a.batches[i].size(); ++j) CHECK(a.batches[i][j].x == b.batches[i][j].x);
  }
}

TEST_CASE("the player's shadow shrinks while jumping and falls on what is below") {
  World w = empty_world();
  add_box(w, {0, 0, 0}, {1, 0.5f, 1});
  w.player.pos.y = 1.0f;  // standing on the crate, 1 m high
  DrawList list;
  build_dynamic(w, 0, list);
  const Instance on_crate = batch(list, MeshId::Shadow)[0];
  CHECK(on_crate.y > 1.0f);

  w.player.pos.y = 2.5f;  // in the air above it
  build_dynamic(w, 0, list);
  const Instance jumping = batch(list, MeshId::Shadow)[0];
  CHECK(jumping.y == Approx(on_crate.y));
  CHECK(jumping.sx < on_crate.sx);
  CHECK(jumping.tint.a < on_crate.tint.a);
}

TEST_CASE("the characters' list has no clouds anymore (the weather has them)") {
  const World& w = cached_game(1);
  DrawList list;
  build_dynamic(w, 0, list);
  for (size_t v = 0; v < kCloudVariants; ++v) CHECK(batch(list, mesh_offset(MeshId::Cloud0, v)).empty());
}

TEST_CASE("the camera looks at the player from behind") {
  World w = empty_world();
  w.player.pos = {3, 0, -4};
  for (float yaw : {0.0f, 1.0f, 2.5f, -2.0f}) {
    CAPTURE(yaw);
    Camera cam;
    cam.yaw = yaw;
    const View v = build_view(w, cam, 16.0f / 9.0f, 0);
    const Vec3 center = project(v.view_projection, {3, 1, -4});
    CHECK(center.x == Approx(0).epsilon(1e-4));
    CHECK(center.y == Approx(0).epsilon(1e-4));
    const Vec3 ahead = project(v.view_projection, {3 - std::sin(yaw) * 5, 1, -4 - std::cos(yaw) * 5});
    CHECK(ahead.z > center.z);
    CHECK(v.camera_y > 0.1f);
  }
}

TEST_CASE("the inverse view-projection undoes the view-projection") {
  World w = empty_world();
  const View v = build_view(w, Camera{}, 1.5f, 0);
  const Mat4 id = multiply(v.inverse_view_projection, v.view_projection);
  for (int i = 0; i < 16; ++i) CHECK(id.m[i] == Approx(i % 5 == 0 ? 1.0f : 0.0f).epsilon(1e-3));
}

TEST_CASE("the sun is above the horizon, the light comes from above") {
  const View v = build_view(empty_world(), Camera{}, 1.5f, 0);
  CHECK(v.sun_y > 0.1f);
  CHECK(v.light_y > v.sun_y);
  CHECK(std::sqrt(v.sun_x * v.sun_x + v.sun_y * v.sun_y + v.sun_z * v.sun_z) == Approx(1));
}

TEST_CASE("the camera keeps its distance when an object is between it and the player: the object shows through") {
  World w = empty_world();
  Camera cam;  // behind the player: towards +Z
  const View free = build_view(w, cam, 1.5f, 0);
  CHECK(free.camera_z > 5.0f);

  w.objects.push_back(make_object(ObjectKind::Tree, {0, 0, 3}, {1, 1, 1}));  // trunk 3 m behind
  const View blocked = build_view(w, cam, 1.5f, 0);
  CHECK(blocked.camera_z == Approx(free.camera_z));
  // What lies between the camera and the player fades (dithered by the renderer).
  CHECK(blocked.fade_radius > 0);
  CHECK(blocked.fade_from_z == Approx(blocked.camera_z));
  CHECK(blocked.fade_to_z == Approx(w.player.pos.z));
  CHECK(camera_distance(w, 0, 1, 0, 0, 0, 1, 6) == Approx(6));
}

namespace {

World world_with_slime_ahead() {
  World w = empty_world();  // the player at the origin, the camera looks north (-Z)
  Creature c;
  c.species = Species::Slime;
  c.body.pos = {0, 0, -5};
  w.creatures.push_back(c);
  return w;
}

}  // namespace

TEST_CASE("clicking on a creature picks it, clicking on the sky picks nothing") {
  World w = world_with_slime_ahead();
  const View v = build_view(w, Camera{}, 1.5f, 0);
  float x = 0, y = 0;
  REQUIRE(creature_on_screen(w, v, 0, x, y));
  CHECK(std::fabs(x) < 0.05f);  // straight ahead
  CHECK(pick_creature(w, v, x, y) == 0);
  CHECK(pick_creature(w, v, x + 0.03f, y + 0.03f) == 0);  // a little off: still easy to hit
  CHECK(pick_creature(w, v, x, 0.9f) == -1);              // the sky
  CHECK(pick_creature(w, v, 0.9f, y) == -1);              // far to the right
  CHECK_FALSE(creature_on_screen(w, v, 1, x, y));         // no such creature
}

TEST_CASE("a creature hidden behind an object cannot be picked") {
  World w = world_with_slime_ahead();
  const View v = build_view(w, Camera{}, 1.5f, 0);
  float x = 0, y = 0;
  REQUIRE(creature_on_screen(w, v, 0, x, y));
  add_box(w, {0, 0, -3.5f}, {1, 1.5f, 0.2f});  // a wall in between
  CHECK(pick_creature(w, v, x, y) == -1);
}

TEST_CASE("the nearest of two creatures in line is picked") {
  World w = world_with_slime_ahead();
  Creature far = w.creatures[0];
  far.body.pos.z = -9;
  w.creatures.insert(w.creatures.begin(), far);  // index 0 far, index 1 near
  const View v = build_view(w, Camera{}, 1.5f, 0);
  float x = 0, y = 0;
  REQUIRE(creature_on_screen(w, v, 1, x, y));
  CHECK(pick_creature(w, v, x, y) == 1);
}

TEST_CASE("the selected creature has a ring at its feet") {
  const World w = world_with_slime_ahead();
  DrawList none, marked;
  build_dynamic(w, 0, none);
  build_dynamic(w, 0, marked, 0);
  const auto& rings = batch(marked, MeshId::Ripple);
  REQUIRE(rings.size() == batch(none, MeshId::Ripple).size() + 1);
  CHECK(rings.back().x == Approx(0));
  CHECK(rings.back().z == Approx(-5));
  CHECK(rings.back().y > 0);
  build_dynamic(w, 0, marked, 7);  // out of range: no ring, no crash
  CHECK(batch(marked, MeshId::Ripple).size() == batch(none, MeshId::Ripple).size());
}
