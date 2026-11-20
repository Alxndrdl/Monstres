// Interiors (interior.hpp): going in and out through doors, the cave, the
// houses, the tower's stair and roof, Cristi on the walls, saves made inside.

#include "monstres/core/battle.hpp"
#include "monstres/core/debug.hpp"
#include "monstres/core/interior.hpp"
#include "monstres/core/save.hpp"
#include "test_helpers.hpp"

#include <doctest.h>

#include <algorithm>
#include <cmath>

using namespace monstres;
namespace {

float distance_xz(const Body& a, const Body& b) {
  const float dx = a.pos.x - b.pos.x, dz = a.pos.z - b.pos.z;
  return std::sqrt(dx * dx + dz * dz);
}

// Where a spot in an object's space is in the world (as interior.cpp).
Vec2 local(const Object& o, float lx, float lz) {
  const SinCos sc = sincos16(o.yaw);
  return {o.pos.x + lx * sc.cos + lz * sc.sin, o.pos.z - lx * sc.sin + lz * sc.cos};
}

int find_kind(const World& w, ObjectKind kind, int skip = 0) {
  for (size_t i = 0; i < w.objects.size(); ++i) {
    if (w.objects[i].kind == kind && skip-- == 0) return static_cast<int>(i);
  }
  return -1;
}

// Puts the player where it can use the door of objects[index] (its local spot).
void stand_at(World& w, int index, float lx, float lz) {
  const Object& o = w.objects[static_cast<size_t>(index)];
  const Vec2 at = local(o, lx, lz);
  w.player.pos = {at.x, w.ground.height(at.x, at.z), at.z};
  w.player.velocity_y = 0;
  w.player.on_ground = true;
}

void door(World& w) {
  Input in;
  in.command = Command::Door;
  step(w, in);
}

void walk(World& w, int8_t x, int8_t y, int ticks, Angle16 yaw = 0) {
  Input in;
  in.move_x = x;
  in.move_y = y;
  in.yaw = yaw;
  for (int t = 0; t < ticks; ++t) step(w, in);
}

World at_cave() {
  World w = cached_game(5);
  const int mouth = find_kind(w, ObjectKind::CaveMouth);
  REQUIRE(mouth >= 0);
  stand_at(w, mouth, 0, 2.0f);
  return w;
}

}  // namespace

TEST_CASE("the cave's mouth leads into the crystal cave, and back out") {
  World w = at_cave();
  const auto objects = w.objects;
  const size_t creatures = w.creatures.size();
  REQUIRE(door_near(w).action == DoorAction::Enter);
  CHECK(door_near(w).place == PlaceKind::Cave);
  door(w);
  REQUIRE(inside(w));
  CHECK(w.place.kind == PlaceKind::Cave);
  CHECK(w.ground.empty());  // a flat floor
  CHECK(w.outdoors.objects == objects);
  CHECK(zone_at(w, w.player.pos.x, w.player.pos.z) == Zone::Cave);
  // Rocks and crystals, standing, hanging from the ceiling, out of the walls.
  int rocks = 0, standing = 0, hanging = 0, on_walls = 0;
  for (const Object& o : w.objects) {
    rocks += o.kind == ObjectKind::Rock;
    standing += o.kind == ObjectKind::Crystal && o.mount == 0;
    hanging += o.kind == ObjectKind::Crystal && o.mount == Object::kHanging;
    on_walls += o.kind == ObjectKind::Crystal && o.mount == Object::kOnWall;
  }
  CHECK(rocks >= 10);
  CHECK(standing >= 10);
  CHECK(hanging >= 10);
  CHECK(on_walls >= 10);
  // Cristis, on the floor, the walls and the ceiling; the companion along.
  int cristis = 0, clinging_ones = 0;
  for (size_t i = 1; i < w.creatures.size(); ++i) {
    const Creature& c = w.creatures[i];
    CHECK(c.species == Species::Cristi);
    ++cristis;
    clinging_ones += clinging(c.body);
    CHECK(std::fabs(open_distance(w.interior, c.body.pos)) < 0.1f);  // on a surface
  }
  CHECK(cristis >= static_cast<int>(rules::kCaveCristisMin));
  CHECK(cristis < static_cast<int>(rules::kCaveCristisMin + rules::kCaveCristisMore));
  CHECK(clinging_ones > 0);
  CHECK(distance_xz(w.creatures[0].body, w.player) < 2.0f);
  // Out again: the world outside as it was, in front of the mouth.
  const Exit out = w.interior.exits[0];
  w.player.pos = out.at;
  REQUIRE(door_near(w).action == DoorAction::Leave);
  door(w);
  CHECK_FALSE(inside(w));
  CHECK(w.objects == objects);
  CHECK(w.creatures.size() == creatures);
  CHECK(w.outdoors.objects.empty());
  CHECK(w.player.pos.y == doctest::Approx(w.ground.height(w.player.pos.x, w.player.pos.z)));
  check_invariants(w);
}

TEST_CASE("inside, the walls hold the player in, whichever way it walks") {
  World w = at_cave();
  door(w);
  REQUIRE(inside(w));
  for (const Angle16 yaw : {Angle16{0}, Angle16{16384}, Angle16{32768}, Angle16{49152}, Angle16{8000}}) {
    walk(w, 0, 127, 6 * rules::kTicksPerSecond, yaw);
    CHECK(wall_distance(w.interior, w.player.pos.x, w.player.pos.z) > kPlayerShape.half_width);
    CHECK(w.player.pos.y >= 0);
  }
}

TEST_CASE("every house of the village has its furniture, and its door back out") {
  const World& game = cached_game(2);
  int houses = 0;
  for (size_t i = 0; i < game.objects.size(); ++i) {
    const ObjectKind k = game.objects[i].kind;
    if (k != ObjectKind::HouseCottage && k != ObjectKind::HouseChimney && k != ObjectKind::HouseHut) continue;
    CAPTURE(i);
    ++houses;
    World w = game;
    const float front = k == ObjectKind::HouseCottage ? 2.0f : k == ObjectKind::HouseChimney ? 2.25f : 2.1f;
    stand_at(w, static_cast<int>(i), 0, -front - 0.7f);
    REQUIRE(door_near(w).action == DoorAction::Enter);
    door(w);
    REQUIRE(w.place.kind == PlaceKind::House);
    int tables = 0, chairs = 0, beds = 0;
    for (const Object& o : w.objects) {
      tables += o.kind == ObjectKind::Table;
      chairs += o.kind == ObjectKind::Chair;
      beds += o.kind == ObjectKind::Bed;
      // Within the walls, and not in the way of the door.
      const Box b = object_box(o);
      CHECK(wall_distance(w.interior, (b.min.x + b.max.x) / 2, (b.min.z + b.max.z) / 2) > 0);
    }
    for (size_t a = 0; a < w.objects.size(); ++a) {  // no piece of furniture in another
      for (size_t c = a + 1; c < w.objects.size(); ++c) {
        const Box ba = object_box(w.objects[a]), bc = object_box(w.objects[c]);
        if (ba.min == ba.max || bc.min == bc.max) continue;  // the rug: walked over
        if (overlaps(ba, bc)) {
          FAIL_CHECK("objects " << static_cast<int>(w.objects[a].kind) << " and " << static_cast<int>(w.objects[c].kind) << " overlap");
        }
      }
    }
    // From its far side, the way out leads to the door.
    w.player.pos = {w.interior.pieces[0].a.x, 0, w.interior.pieces[0].a.z - 1.5f};
    Vec2 way;
    REQUIRE(way_out(w, way));
    CHECK(way.z > 0.3f);  // towards the door, at z = 0
    w.player.pos = w.interior.entry;
    CHECK(tables == 1);
    CHECK(chairs >= 2);
    CHECK(beds >= 1);
    CHECK_FALSE(inside_any_object(w, w.player, kPlayerShape));
    walk(w, 0, 127, 3 * rules::kTicksPerSecond);  // into the room
    walk(w, 0, -127, 4 * rules::kTicksPerSecond);  // back to the door
    CHECK(door_near(w).action == DoorAction::Leave);
    door(w);
    CHECK_FALSE(inside(w));
    CHECK(w.objects == game.objects);
  }
  CHECK(houses >= 3);
}

TEST_CASE("the tower's stair goes up to its trapdoor, out onto its roof, and back") {
  World w = cached_game(4);
  const int tower = find_kind(w, ObjectKind::Tower);
  REQUIRE(tower >= 0);
  const Object t = w.objects[static_cast<size_t>(tower)];
  stand_at(w, tower, 0, -2.9f);
  REQUIRE(door_near(w).action == DoorAction::Enter);
  door(w);
  REQUIRE(w.place.kind == PlaceKind::Tower);
  // Each step a little higher than the one before, close enough to step onto
  // (its blocks: by the wall, inside; the landing last).
  std::vector<Object> steps;
  for (const Object& o : w.objects) {
    if (o.kind == ObjectKind::Step && o.variant != kLandingVariant) {
      REQUIRE(o.variant == steps.size() / 2);
      steps.push_back(o);
    }
  }
  for (const Object& o : w.objects) {
    if (o.kind == ObjectKind::Step && o.variant == kLandingVariant) steps.push_back(o);
  }
  REQUIRE(steps.size() > 40);
  const auto touching = [](const Box& a, const Box& b) {
    return std::max({a.min.x - b.max.x, b.min.x - a.max.x, a.min.z - b.max.z, b.min.z - a.max.z}) < 0.05f;
  };
  for (size_t k = 0; k + 1 < steps.size(); k += 2) {
    const Box in = object_box(steps[k]), out = object_box(steps[k + 1]);
    CHECK(in.max.y == doctest::Approx(out.max.y));  // the two blocks of one step, side by side
    CHECK(touching(in, out));
    CHECK(wall_distance(w.interior, steps[k].pos.x, steps[k].pos.z) > 0);
    CHECK(wall_distance(w.interior, steps[k + 1].pos.x, steps[k + 1].pos.z) > 0);
    const size_t next = k + 2 < steps.size() ? k + 2 : steps.size() - 1;
    const Box b = object_box(steps[next]);
    CHECK(b.max.y - in.max.y <= rules::kStepHeight);
    CHECK(b.max.y > in.max.y);
    CHECK((touching(in, b) || touching(out, b) || touching(in, object_box(steps[std::min(next + 1, steps.size() - 1)]))));
  }
  // Climbing it, step by step: the player walks from one onto the next.
  w.player.pos = {steps[0].pos.x, object_box(steps[0]).max.y, steps[0].pos.z};
  for (size_t k = 1; k < steps.size(); ++k) {
    for (int tick = 0; tick < 40; ++tick) {
      const float dx = steps[k].pos.x - w.player.pos.x, dz = steps[k].pos.z - w.player.pos.z;
      const float d = std::sqrt(dx * dx + dz * dz);
      if (d < 0.15f) break;
      // The joystick towards the next step (camera yaw 0: forward is -Z, right is +X).
      Input in;
      in.move_x = static_cast<int8_t>(std::lround(127 * dx / d));
      in.move_y = static_cast<int8_t>(std::lround(-127 * dz / d));
      step(w, in);
    }
  }
  CHECK(w.player.pos.y >= object_box(steps.back()).max.y - 0.01f);
  // Up to the landing, under the trapdoor: out onto the roof.
  w.player.pos = w.interior.top;
  REQUIRE(door_near(w).action == DoorAction::Climb);
  door(w);
  REQUIRE_FALSE(inside(w));
  CHECK(w.player.pos.y == doctest::Approx(t.pos.y + rules::kRoofHeight));
  CHECK(std::hypot(w.player.pos.x - t.pos.x, w.player.pos.z - t.pos.z) < 2.0f);
  walk(w, 0, 0, 30);  // it stands there (on top of the tower)
  CHECK(w.player.pos.y == doctest::Approx(t.pos.y + rules::kRoofHeight));
  // And down again through the trapdoor, onto the landing.
  REQUIRE(door_near(w).action == DoorAction::Descend);
  door(w);
  REQUIRE(w.place.kind == PlaceKind::Tower);
  CHECK(w.player.pos.y == doctest::Approx(w.interior.top.y));
}

TEST_CASE("a game saved inside loads inside, where the player stood") {
  World w = at_cave();
  door(w);
  REQUIRE(inside(w));
  walk(w, 0, 127, 60);
  const auto loaded = deserialize(serialize(w));
  REQUIRE(loaded.has_value());
  CHECK(loaded->place == w.place);
  CHECK(loaded->interior == w.interior);
  CHECK(loaded->player.pos.x == doctest::Approx(w.player.pos.x));
  CHECK(loaded->player.pos.z == doctest::Approx(w.player.pos.z));
  CHECK(loaded->outdoors.objects == w.outdoors.objects);
  // On the map, at the mouth.
  CHECK(map_body(*loaded).pos.x == doctest::Approx(w.outdoors.player.pos.x));
  const World out = outside_world(w);
  CHECK_FALSE(inside(out));
  CHECK(out.objects == w.outdoors.objects);
}

TEST_CASE("Cristi climbs: into a wall it goes up it, on over the ceiling, and down the other side") {
  World w = at_cave();
  door(w);
  Body b;
  b.pos = {0, 0, -17.0f};  // the first chamber's middle
  b.facing_x = 1;
  b.facing_z = 0;
  // Forward is +X (the camera's yaw, a quarter turn): it reaches a wall,
  // climbs it, goes on over the ceiling and comes down the other side.
  constexpr Angle16 kTowardsX = 49152;
  float lowest_up = 1;
  bool down_again = false;
  for (int t = 0; t < 30 * rules::kTicksPerSecond; ++t) {
    climb_steered(w, b, 1, 0, false, kTowardsX, 0, 1, 3.0f);
    CHECK(std::fabs(surface_distance(w, b.pos)) < 0.25f);  // on the cave or on a rock
    lowest_up = std::min(lowest_up, b.up.y);
    down_again = down_again || (lowest_up < -0.8f && b.up.y > 0.9f);
  }
  CHECK(lowest_up < -0.8f);  // upside down: on the ceiling
  CHECK(down_again);         // and back on the floor, not stuck in a corner
}

TEST_CASE("Cristi climbs anything outside too: up a rock's side and onto its top") {
  World w;
  Object rock;
  rock.kind = ObjectKind::Crate;  // a plain box, 2 m high
  rock.pos = {3, 0, 0};
  rock.size = {1, 1, 1};
  w.objects.push_back(rock);
  Body b;
  b.facing_x = 1;
  b.facing_z = 0;
  float highest = 0;
  bool on_its_side = false;
  for (int t = 0; t < 4 * rules::kTicksPerSecond; ++t) {
    climb_steered(w, b, 1, 0, false, 49152, 0, 1, 2.0f);  // forward: +X, towards it
    highest = std::max(highest, b.pos.y);
    on_its_side = on_its_side || std::fabs(b.up.x + 1) < 0.1f;
  }
  CHECK(on_its_side);
  CHECK(highest > 1.9f);  // up on its top
  CHECK(highest < 2.1f);
}

TEST_CASE("a Cristi steered in a training jumps on the spot, then climbs again") {
  World w;
  w.rng = Rng(1);
  Creature cristi;
  cristi.species = Species::Cristi;
  cristi.body.pos = {0, 0, 1};
  add_creature(w, cristi);
  Monster m = starting_companion({});
  m.species = Species::Cristi;
  m.hp = monster_fight_stats(m).hp;
  w.team = {m};
  Input train;
  train.command = Command::Train;
  step(w, train);
  REQUIRE(w.battle.phase == BattlePhase::Training);
  Input jump;
  jump.jump = true;
  step(w, jump);
  float highest = 0;
  for (int t = 0; t < rules::kTicksPerSecond; ++t) {
    step(w, Input{});
    highest = std::max(highest, w.creatures[0].body.pos.y);
  }
  CHECK(highest > 0.3f);  // up in the air
  CHECK(w.creatures[0].body.on_ground);  // and down again
  CHECK(w.creatures[0].body.pos.y < 0.05f);
}

TEST_CASE("wild Cristis wait still, and scuttle away when the player comes close") {
  World w = at_cave();
  door(w);
  size_t k = 1;
  while (k < w.creatures.size() && clinging(w.creatures[k].body)) ++k;  // one on the floor
  REQUIRE(k < w.creatures.size());
  const Vec3 before = w.creatures[k].body.pos;
  walk(w, 0, 0, 30);
  if (distance_xz(w.creatures[k].body, w.player) > rules::kCristiWake + 1) {
    CHECK(w.creatures[k].body.pos == before);
  }
  // The player right next to it.
  w.player.pos = before;
  walk(w, 0, 0, 20);
  CHECK(w.creatures[k].mind == Mind::Flee);
  CHECK(w.creatures[k].body.pos != before);
}

TEST_CASE("the Way out spell, free, leads back to the door from the far end of the cave, round its walls") {
  World w = at_cave();
  w.adventure_spells = {SpellId::WayOut, SpellId::None, SpellId::None, SpellId::None};
  CHECK(std::string(cannot_cast(w, 0)) == "outside");
  door(w);
  REQUIRE(inside(w));
  REQUIRE(cannot_cast(w, 0) == nullptr);
  const uint16_t mana = w.wizard.mana;
  Input cast;
  cast.command = Command::Cast;
  cast.target = 0;
  step(w, cast);
  CHECK(w.way_out_ticks > 0);
  CHECK(w.wizard.mana == mana);  // free
  // The chamber farthest from the door (by the way, not as the crow flies).
  const RoomPiece* far = nullptr;
  float farthest = 0;
  for (const RoomPiece& p : w.interior.pieces) {
    const float d = std::sqrt(p.a.x * p.a.x + p.a.z * p.a.z);
    if (p.a == p.b && d > farthest) {
      farthest = d;
      far = &p;
    }
  }
  REQUIRE(far != nullptr);
  w.player.pos = {far->a.x, 0, far->a.z};
  w.creatures.resize(1);  // no Cristi in the way
  bool out = false;
  for (int t = 0; t < 90 * rules::kTicksPerSecond && !out; ++t) {
    if (w.way_out_ticks < 100) w.way_out_ticks = 1000;  // it lasts a minute: cast again
    Vec2 dir;
    REQUIRE(way_out(w, dir));
    Input in;  // camera yaw 0: forward is -Z, right is +X
    in.move_x = static_cast<int8_t>(std::lround(127 * dir.x));
    in.move_y = static_cast<int8_t>(std::lround(-127 * dir.z));
    step(w, in);
    out = door_near(w).action == DoorAction::Leave;
  }
  CHECK(out);
}

TEST_CASE("inside, the teleporters outside are still listed (not found anew on coming out)") {
  World w = at_cave();
  Vec3 at;
  REQUIRE(teleporter_position(w, TeleporterId::Village, at));
  door(w);
  REQUIRE(inside(w));
  CHECK(teleporter_position(w, TeleporterId::Village, at));
  CHECK(teleporters_json(w).find("\"place\":\"village\"") != std::string::npos);
}
