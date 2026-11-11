#include "monstres/render/scene.hpp"
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
  CHECK(batch(list, MeshId::Ground).size() == 1);
  CHECK(batch(list, MeshId::Path).size() == 1);

  const Instance& log = batch(list, MeshId::Log)[0];
  CHECK(log.rotation.y == Approx(std::sin(kPi / 4)));  // turned 90° around Y
  CHECK(log.rotation.w == Approx(std::cos(kPi / 4)));
  CHECK(log.sx == Approx(2));
  CHECK(log.sy == Approx(0.3f));
  CHECK(batch(list, MeshId::Crate)[0].y == Approx(0.5f));  // the crate mesh is centered
}

TEST_CASE("the landscape has grass, flowers, a forest beyond the edges and mountains") {
  const World w = new_game(42);
  DrawList list;
  build_landscape(w, list);

  const auto& short_grass = batch(list, MeshId::GrassShort);
  const auto& tall_grass = batch(list, MeshId::GrassTall);
  CHECK(short_grass.size() > 2000);
  CHECK(tall_grass.size() > 300);
  CHECK(batch(list, MeshId::Flower).size() > 100);

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

  // Extra trees only outside the playable area.
  size_t world_trees = 0;
  for (const Object& o : w.objects) world_trees += o.kind == ObjectKind::Tree || o.kind == ObjectKind::Pine;
  size_t border_trees = 0;
  for (MeshId id : {MeshId::Tree, MeshId::Pine}) {
    for (const Instance& t : batch(list, id)) {
      if (std::max(std::fabs(t.x), std::fabs(t.z)) > rules::kWorldHalfSize + 1) ++border_trees;
    }
  }
  CHECK(border_trees > 60);
  CHECK(batch(list, MeshId::Tree).size() + batch(list, MeshId::Pine).size() == world_trees + border_trees);

  size_t mountains = 0;
  for (size_t i = 0; i < kMountainVariants; ++i) {
    for (const Instance& m : batch(list, mesh_offset(MeshId::Mountain0, i))) {
      CHECK(std::sqrt(m.x * m.x + m.z * m.z) > 190.0f);
      ++mountains;
    }
  }
  CHECK(mountains == 18);
}

TEST_CASE("the landscape depends on the world only") {
  const World w = new_game(9);
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

TEST_CASE("clouds drift with time and stay in the sky") {
  const World w = new_game(1);
  DrawList at0, at10;
  build_dynamic(w, 0, at0);
  build_dynamic(w, 10, at10);
  size_t clouds = 0;
  for (size_t v = 0; v < kCloudVariants; ++v) {
    const auto& a = batch(at0, mesh_offset(MeshId::Cloud0, v));
    const auto& b = batch(at10, mesh_offset(MeshId::Cloud0, v));
    REQUIRE(a.size() == b.size());
    for (size_t i = 0; i < a.size(); ++i) {
      CHECK(a[i].y > 30.0f);
      CHECK(a[i].x != b[i].x);
      CHECK(a[i].z == b[i].z);
      CHECK(std::fabs(b[i].x) <= 220.0f);
      ++clouds;
    }
  }
  CHECK(clouds == 16);
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

TEST_CASE("the camera comes closer when an object is between it and the player") {
  World w = empty_world();
  Camera cam;  // behind the player: towards +Z
  const View free = build_view(w, cam, 1.5f, 0);
  CHECK(free.camera_z > 5.0f);

  w.objects.push_back(make_object(ObjectKind::Tree, {0, 0, 3}, {1, 1, 1}));  // trunk 3 m behind
  const View blocked = build_view(w, cam, 1.5f, 0);
  CHECK(blocked.camera_z < 3.0f - 0.28f);
  CHECK(blocked.camera_z > 1.0f);

  CHECK(camera_distance(w, 0, 1, 0, 0, 0, 1, 6) < 3.0f);
  CHECK(camera_distance(w, 0, 1, 0, 0, 0, -1, 6) == Approx(6));  // nothing in front
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
