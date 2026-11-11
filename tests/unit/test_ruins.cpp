#include "monstres/core/debug.hpp"
#include "monstres/core/save.hpp"
#include "monstres/core/world.hpp"
#include "monstres/render/scene.hpp"
#include "test_helpers.hpp"

#include <algorithm>
#include <cmath>
#include <string>

using namespace monstres;
using doctest::Approx;

namespace {
size_t count(const World& w, ObjectKind kind) {
  return static_cast<size_t>(std::count_if(w.objects.begin(), w.objects.end(), [&](const Object& o) { return o.kind == kind; }));
}
}  // namespace

TEST_CASE("the ruins stand beside the path, south of the start, with their tower") {
  for (uint64_t seed = 0; seed < 12; ++seed) {
    CAPTURE(seed);
    const World w = new_game(seed);
    const RuinsLayout& r = w.ruins;
    REQUIRE(r.radius > 0);
    CHECK(r.center.z == Approx(rules::kRuinsZ));
    CHECK(std::fabs(r.center.x) + r.radius < rules::kWorldHalfSize);
    CHECK(distance_to_path(w.path, r.center.x, r.center.z) > 10.0f);
    REQUIRE(count(w, ObjectKind::Tower) == 1);
    CHECK(count(w, ObjectKind::RuinWall) >= 8);
    CHECK(count(w, ObjectKind::Pillar) + count(w, ObjectKind::FallenPillar) >= 4);
    CHECK(count(w, ObjectKind::RuneStone) == 4);
    CHECK(count(w, ObjectKind::Rubble) >= 3);
    const Object& tower = *std::find_if(w.objects.begin(), w.objects.end(),
                                        [](const Object& o) { return o.kind == ObjectKind::Tower; });
    CHECK(tower.pos.x == Approx(r.center.x));
    CHECK(tower.pos.z == Approx(r.center.z));
    // On the flat top of its knoll.
    const float top = w.ground.height(r.center.x, r.center.z);
    for (float dx = -6; dx <= 6; dx += 1.5f) {
      for (float dz = -6; dz <= 6; dz += 1.5f) CHECK(w.ground.height(r.center.x + dx, r.center.z + dz) == Approx(top).epsilon(0.002));
    }
    // Nothing of the meadow inside the ruins.
    for (const Object& o : w.objects) {
      if (o.kind <= ObjectKind::Crate) CHECK_FALSE(in_ruins(r, o.pos.x, o.pos.z));
    }
  }
}

TEST_CASE("the tower's door faces the path, the courtyard opens towards it") {
  const World w = new_game(5);
  const Object& tower = *std::find_if(w.objects.begin(), w.objects.end(),
                                      [](const Object& o) { return o.kind == ObjectKind::Tower; });
  const SinCos sc = sincos16(tower.yaw);
  const Vec2 front{-sc.sin, -sc.cos};  // local -Z
  const Vec2 c = w.ruins.center;
  CHECK(distance_to_path(w.path, c.x + front.x * 8, c.z + front.z * 8) <
        distance_to_path(w.path, c.x - front.x * 8, c.z - front.z * 8));
  // No wall in the way between the door and the path.
  for (const Object& o : w.objects) {
    if (o.kind != ObjectKind::RuinWall) continue;
    const float along = (o.pos.x - c.x) * front.x + (o.pos.z - c.z) * front.z;
    const float across = (o.pos.x - c.x) * front.z - (o.pos.z - c.z) * front.x;
    CHECK_FALSE((along > 7 && std::fabs(across) < 2.5f));
  }
}

TEST_CASE("crows can land on the tower") {
  const World w = new_game(3);
  const auto perches = crow_perches(w);
  const size_t high = static_cast<size_t>(std::count_if(perches.begin(), perches.end(), [&](const Vec3& p) {
    return p.y > w.ground.height(p.x, p.z) + 9;
  }));
  CHECK(high == 3);
}

TEST_CASE("places of the world are named") {
  const World w = new_game(4);
  CHECK(zone_at(w, w.village.center.x, w.village.center.z) == Zone::Village);
  CHECK(zone_at(w, w.village.lake.center.x, w.village.lake.center.z) == Zone::Lake);
  CHECK(zone_at(w, w.ruins.center.x, w.ruins.center.z) == Zone::Ruins);
  CHECK(zone_at(w, 0, 0) == Zone::Meadow);
  CHECK(std::string(zone_name(Zone::Ruins)) == "ruins");
  World empty = empty_world();
  CHECK(zone_at(empty, 0, 0) == Zone::Meadow);  // no village in a hand-made world
}

TEST_CASE("walking south along the path leads to the ruins") {
  World w = new_game(7);
  for (int i = 0; i < 12 * rules::kTicksPerSecond && zone_at(w, w.player.pos.x, w.player.pos.z) != Zone::Ruins; ++i) {
    // Steer towards the tower, like a player would.
    const float dx = w.ruins.center.x - w.player.pos.x, dz = w.ruins.center.z - w.player.pos.z;
    const auto yaw = angle16_from_radians(std::atan2(-dx, -dz));
    step(w, move(0, 127, yaw));
  }
  CHECK(zone_at(w, w.player.pos.x, w.player.pos.z) == Zone::Ruins);
}

TEST_CASE("the magician's name, level and experience are saved") {
  World w = new_game(9);
  w.wizard.name = "Élian l'Étoilé";
  w.wizard.level = 3;
  w.wizard.xp = 42;
  const auto loaded = deserialize(serialize(w));
  REQUIRE(loaded);
  CHECK(loaded->wizard == w.wizard);

  SUBCASE("a name longer than allowed is cut when saving") {
    w.wizard.name = std::string(40, 'a');
    CHECK(deserialize(serialize(w))->wizard.name.size() == kMaxNameBytes);
  }
  SUBCASE("a level 0 or a broken name length is refused") {
    auto bytes = serialize(w);
    bytes[bytes.size() - 6 - 1 - w.wizard.name.size()] = 200;  // the name length
    LoadError error = LoadError::None;
    CHECK_FALSE(deserialize(bytes, &error));
    CHECK(error == LoadError::Corrupt);
    w.wizard.level = 0;
    CHECK_FALSE(deserialize(serialize(w), &error));
  }
}

TEST_CASE("characteristics grow with the level") {
  Wizard wiz;
  const WizardStats one = wizard_stats(wiz);
  CHECK(one.hp == 30);
  CHECK(one.mana == 20);
  CHECK(one.xp_to_next == 100);
  wiz.level = 5;
  const WizardStats five = wizard_stats(wiz);
  CHECK(five.hp > one.hp);
  CHECK(five.power > one.power);
  CHECK(five.xp_to_next > one.xp_to_next);
}

TEST_CASE("every monster has stats, villagers are not monsters") {
  for (size_t i = 0; i < static_cast<size_t>(Species::Count); ++i) {
    const auto s = static_cast<Species>(i);
    CAPTURE(i);
    const MonsterStats& st = monster_stats(s);
    if (s == Species::Villager) {
      CHECK_FALSE(st.is_monster);
      continue;
    }
    CHECK(st.is_monster);
    CHECK(st.hp > 0);
    CHECK(st.attack > 0);
    CHECK(st.element < Element::Count);
  }
}

TEST_CASE("the menus get their data as JSON") {
  World w = new_game(11);
  w.wizard.name = "Zoé \"la Brave\"";
  const std::string wiz = wizard_json(w);
  CHECK(wiz.find("\"name\":\"Zoé \\\"la Brave\\\"\"") != std::string::npos);  // quotes escaped
  CHECK(wiz.find("\"level\":1") != std::string::npos);
  CHECK(wiz.find("\"mana\":20") != std::string::npos);

  const std::string best = bestiary_json(w);
  CHECK(best.find("\"species\":\"villager\"") == std::string::npos);
  CHECK(best.find("\"species\":\"slime\"") != std::string::npos);
  CHECK(best.find("\"elements\":[\"water\"],\"family\":\"ooze\"") != std::string::npos);
  CHECK(best.find("\"elements\":[\"shadow\",\"air\"],\"family\":\"bird\"") != std::string::npos);  // the crow
  CHECK(best.find("\"id\":\"ears.pointed\"") != std::string::npos);
  CHECK(best.find("\"count\":1}") != std::string::npos);  // the companion

  const std::string map = map_json(w);
  CHECK(map.find("\"points\":201") != std::string::npos);
  CHECK(map.find("\"ruins\":{") != std::string::npos);
  CHECK(map.find("[\"tower\",") != std::string::npos);
  CHECK(map.find("\"lake\":{") != std::string::npos);
}

TEST_CASE("portraits show one figure, framed, without sky") {
  for (const PortraitSubject subject : {PortraitSubject{true}, PortraitSubject{false, Species::Slime},
                                        PortraitSubject{false, Species::Crow}, PortraitSubject{false, Species::Fish}}) {
    CAPTURE(static_cast<int>(subject.species));
    const Portrait p = build_portrait(subject, 1.0f);
    CHECK_FALSE(p.view.sky);
    size_t parts = 0;
    float lo = 1, hi = -1;
    for (const auto& batch : p.list.batches) {
      for (const Instance& in : batch) {
        ++parts;
        const float* m = p.view.view_projection.m;
        const float w = m[3] * in.x + m[7] * in.y + m[11] * in.z + m[15];
        const float y = (m[1] * in.x + m[5] * in.y + m[9] * in.z + m[13]) / w;
        lo = std::min(lo, y);
        hi = std::max(hi, y);
      }
    }
    CHECK(parts >= 3);
    CHECK(lo > -1);  // every part on screen
    CHECK(hi < 1);
    CHECK(hi - lo > 0.2f);  // and not tiny (joints of the parts, not their ends)
  }
  CHECK_FALSE(build_portrait({true}, 1).list.batches[static_cast<size_t>(MeshId::PlayerStaff)].empty());
}
