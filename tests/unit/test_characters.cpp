// Rigs and animations of the player, the companion and the wild monsters.

#include "monstres/render/characters.hpp"
#include "test_helpers.hpp"

#include <algorithm>
#include <cmath>

using namespace monstres;
using doctest::Approx;

namespace {

bool is_identity(const Quat& q) { return std::fabs(q.w) > 0.9999f; }

const std::vector<Instance>& batch(const DrawList& list, MeshId id) {
  return list.batches[static_cast<size_t>(id)];
}

DrawList empty_list() {
  DrawList list;
  list.batches.resize(kMeshCount);
  return list;
}

Creature make_creature(Species species) {
  Creature c;
  c.species = species;
  c.body.pos = {2, 0, 3};
  return c;
}

}  // namespace

TEST_CASE("every rig lists parents before their children") {
  std::vector<std::span<const RigPart>> rigs = {player_rig()};
  for (int s = 0; s < static_cast<int>(Species::Count); ++s) rigs.push_back(creature_rig(static_cast<Species>(s)));
  for (const auto& rig : rigs) {
    REQUIRE(!rig.empty());
    CHECK(rig[0].parent == -1);
    for (size_t i = 0; i < rig.size(); ++i) {
      CHECK(rig[i].parent < static_cast<int>(i));
      // A model file, not a runtime mesh (Count: an optional part, like a hat).
      CHECK((rig[i].mesh < MeshId::Ground || rig[i].mesh == MeshId::Count));
    }
  }
}

TEST_CASE("the player stands on its feet, head on top") {
  Body b;
  b.pos = {2, 0, 3};
  std::vector<PartPose> poses;
  pose_player(b, 0, poses);
  DrawList list = empty_list();
  add_rig(list, player_rig(), poses, b, {1, 1, 1, 1});

  REQUIRE(batch(list, MeshId::PlayerLeg).size() == 2);
  REQUIRE(batch(list, MeshId::PlayerArm).size() == 2);
  REQUIRE(batch(list, MeshId::Eye).size() == 2);
  const Instance& leg = batch(list, MeshId::PlayerLeg)[0];
  const Instance& head = batch(list, MeshId::PlayerHead)[0];
  CHECK(leg.y == Approx(0.38f));  // the leg mesh hangs 0.38 m down to the ground
  CHECK(head.y > 0.7f);
  CHECK(head.x == Approx(2));
  // Eyes on the front of the head (-Z when facing -Z).
  CHECK(batch(list, MeshId::Eye)[0].z < head.z);
}

TEST_CASE("the rig turns with the body") {
  Body b;
  b.facing_x = 1;  // facing +X: the left side is -Z
  b.facing_z = 0;
  std::vector<PartPose> poses;
  pose_player(b, 0, poses);
  DrawList list = empty_list();
  add_rig(list, player_rig(), poses, b, {1, 1, 1, 1});
  const Instance& left_arm = batch(list, MeshId::PlayerArm)[0];
  const Instance& right_arm = batch(list, MeshId::PlayerArm)[1];
  CHECK(left_arm.z < -0.15f);
  CHECK(right_arm.z > 0.15f);
  CHECK(std::fabs(left_arm.x) < 0.05f);
}

TEST_CASE("walking swings the legs in opposite directions, standing does not") {
  Body b;
  std::vector<PartPose> poses;
  pose_player(b, 0.1f, poses);
  for (size_t i = 0; i < 8; ++i) {   // body parts (not the staff, held forward)
    if (i == 5) continue;            // the right arm holds the staff
    CHECK(std::fabs(poses[i].rotation.x) < 0.05f);  // at most a small lean or breath
  }

  b.speed = rules::kWalkSpeed;
  pose_player(b, 0.1f, poses);
  const Quat left = poses[6].rotation, right = poses[7].rotation;  // legs
  CHECK(std::fabs(left.x) > 0.1f);
  CHECK(left.x == Approx(-right.x));
}

TEST_CASE("in the air the player raises its arms") {
  Body b;
  b.on_ground = false;
  b.velocity_y = rules::kJumpSpeed;
  std::vector<PartPose> poses;
  pose_player(b, 0, poses);
  CHECK(std::fabs(poses[4].rotation.z) > 0.3f);  // arms
  CHECK(poses[4].rotation.z == Approx(-poses[5].rotation.z));
  CHECK_FALSE(is_identity(poses[6].rotation));    // legs
}

TEST_CASE("characters blink from time to time") {
  Body b;
  std::vector<PartPose> poses;
  int closed = 0;
  for (int i = 0; i < 600; ++i) {
    pose_player(b, static_cast<float>(i) * 0.01f, poses);
    if (poses[2].scale.y < 0.5f) {
      ++closed;
      CHECK(poses[3].scale.y < 0.5f);  // both eyes
    }
  }
  CHECK(closed > 0);
  CHECK(closed < 60);
}

TEST_CASE("slimes stretch going up and squash coming down") {
  Creature c = make_creature(Species::Slime);
  c.body.on_ground = false;
  std::vector<PartPose> poses;
  c.body.velocity_y = 4;
  pose_creature(c, 0, 0, poses);
  CHECK(poses[0].scale.y > 1.1f);
  CHECK(poses[0].scale.x < 1.0f);
  CHECK(poses[1].offset.y > 0);  // the eyes move up with the surface
  c.body.velocity_y = -4;
  pose_creature(c, 0, 0, poses);
  CHECK(poses[0].scale.y < 0.9f);
}

TEST_CASE("birds flap their wings while fleeing") {
  Creature c = make_creature(Species::Bird);
  std::vector<PartPose> a, b;
  pose_creature(c, 0.00f, 0, a);
  pose_creature(c, 0.05f, 0, b);
  CHECK(is_identity(a[2].rotation));  // calm: wings folded
  c.mind = Mind::Flee;
  pose_creature(c, 0.00f, 0, a);
  pose_creature(c, 0.05f, 0, b);
  CHECK(a[2].rotation.z != Approx(b[2].rotation.z));
}

TEST_CASE("the companion wags its tail") {
  Creature c = make_creature(Species::Companion);
  std::vector<PartPose> a, b;
  pose_creature(c, 0.0f, 0, a);
  pose_creature(c, 0.15f, 0, b);
  CHECK(a[6].rotation.y != Approx(b[6].rotation.y));
}

TEST_CASE("add_characters draws everyone, each with a shadow") {
  World w = cached_game(3);
  DrawList list = empty_list();
  add_characters(w, 1.0f, list);
  CHECK(batch(list, MeshId::PlayerTorso).size() == 1);
  CHECK(batch(list, MeshId::CompanionBody).size() == 1);
  size_t fish = 0, walkers = 0;
  for (const Creature& c : w.creatures) {
    fish += c.species == Species::Fish;
    walkers += c.species == Species::Slime || c.species == Species::Mushroom || c.species == Species::Bird;
  }
  CHECK(fish > 0);
  CHECK(batch(list, MeshId::Shadow).size() == 1 + w.creatures.size() - fish);  // no shadow on water
  size_t wild = 0;
  for (MeshId id : {MeshId::SlimeBody, MeshId::MushroomStem, MeshId::BirdBody}) wild += batch(list, id).size();
  CHECK(wild == walkers);
}

TEST_CASE("wild monsters come in several colors") {
  World w = cached_game(3);
  DrawList list = empty_list();
  add_characters(w, 0, list);
  bool varied = false;
  for (MeshId id : {MeshId::SlimeBody, MeshId::MushroomCap, MeshId::BirdBody}) {
    const auto& parts = batch(list, id);
    for (const Instance& in : parts) varied |= in.tint.r != parts[0].tint.r;
  }
  CHECK(varied);
}

TEST_CASE("villagers look different from one another") {
  int children = 0, hats = 0;
  std::vector<float> shirts;
  for (int v = 0; v < 256; ++v) {
    const VillagerLook look = villager_look(static_cast<uint8_t>(v));
    children += look.child;
    hats += look.hat_mesh != MeshId::Count;
    shirts.push_back(look.shirt.r + 2 * look.shirt.g + 4 * look.shirt.b);
  }
  CHECK(children > 20);
  CHECK(hats > 100);
  CHECK(hats < 256);
  std::sort(shirts.begin(), shirts.end());
  CHECK(std::unique(shirts.begin(), shirts.end()) - shirts.begin() == 8);
}

TEST_CASE("a villager waves at the player") {
  Creature c = make_creature(Species::Villager);
  std::vector<PartPose> calm, greeting;
  pose_creature(c, 1.0f, 0, calm);
  c.mind = Mind::Greet;
  pose_creature(c, 1.0f, 0, greeting);
  CHECK(std::fabs(greeting[7].rotation.z) > 0.8f);  // right arm up
  CHECK(std::fabs(calm[7].rotation.z) < 0.2f);
}

TEST_CASE("crows flap only while flying, fish wag their tail") {
  Creature crow = make_creature(Species::Crow);
  std::vector<PartPose> a, b;
  pose_creature(crow, 0.0f, 0, a);
  pose_creature(crow, 0.05f, 0, b);
  CHECK(a[2].rotation.z == Approx(b[2].rotation.z));
  crow.body.on_ground = false;
  pose_creature(crow, 0.0f, 0, a);
  pose_creature(crow, 0.05f, 0, b);
  CHECK(a[2].rotation.z != Approx(b[2].rotation.z));

  Creature fish = make_creature(Species::Fish);
  pose_creature(fish, 0.0f, 0, a);
  pose_creature(fish, 0.1f, 0, b);
  CHECK(a[1].rotation.y != Approx(b[1].rotation.y));
}
