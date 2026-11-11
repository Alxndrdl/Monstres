#include "monstres/core/save.hpp"
#include "test_helpers.hpp"

#include <algorithm>

using namespace monstres;

namespace {
// Layout of a version 5 save: magic, version, seed, tick, RNG (40 bytes), then
// the player body (position, vertical speed, on ground, facing, speed) and
// the object count.
constexpr size_t kFirstObject = 40 + 12 + 4 + 1 + 8 + 4 + 4;
constexpr size_t kObjectSize = 1 + 12 + 12 + 2 + 1;

World some_game() {
  World w = new_game(2024);
  for (int i = 0; i < 300; ++i) {
    step(w, move(static_cast<int8_t>(i % 255 - 127), 100, static_cast<Angle16>(i * 300), i % 50 == 0));
  }
  return w;
}
}  // namespace

TEST_CASE("a world survives serialize + deserialize unchanged") {
  const World w = some_game();
  LoadError error = LoadError::Corrupt;
  const auto loaded = deserialize(serialize(w), &error);
  REQUIRE(loaded.has_value());
  CHECK(error == LoadError::None);
  CHECK(*loaded == w);
}

TEST_CASE("a loaded world continues exactly like the original") {
  World original = some_game();
  World copy = *deserialize(serialize(original));
  for (int i = 0; i < 500; ++i) {
    const Input in = move(50, 127, static_cast<Angle16>(i * 97), i % 40 == 0);
    step(original, in);
    step(copy, in);
  }
  CHECK(copy == original);
}

TEST_CASE("invalid saves are rejected with the right error") {
  auto bytes = serialize(some_game());
  LoadError error = LoadError::None;

  SUBCASE("empty") {
    CHECK_FALSE(deserialize({}, &error));
    CHECK(error == LoadError::TooShort);
  }
  SUBCASE("bad magic") {
    bytes[0] = 'X';
    CHECK_FALSE(deserialize(bytes, &error));
    CHECK(error == LoadError::BadMagic);
  }
  SUBCASE("old versions 1 (2D prototype), 2 (boxes only) and 3 (no seed) are refused") {
    for (uint8_t old : {1, 2, 3}) {
      bytes[4] = old;
      CHECK_FALSE(deserialize(bytes, &error));
      CHECK(error == LoadError::UnsupportedVersion);
    }
  }
  SUBCASE("unknown object kind") {
    bytes[kFirstObject] = 200;
    CHECK_FALSE(deserialize(bytes, &error));
    CHECK(error == LoadError::Corrupt);
  }
  SUBCASE("unknown species") {
    const size_t objects = some_game().objects.size();
    bytes[kFirstObject + objects * kObjectSize + 4] = 200;  // after the objects and the creature count
    CHECK_FALSE(deserialize(bytes, &error));
    CHECK(error == LoadError::Corrupt);
  }
  SUBCASE("future version") {
    bytes[4] = 99;
    CHECK_FALSE(deserialize(bytes, &error));
    CHECK(error == LoadError::UnsupportedVersion);
  }
  SUBCASE("truncated") {
    bytes.resize(bytes.size() - 3);
    CHECK_FALSE(deserialize(bytes, &error));
    CHECK(error == LoadError::Corrupt);
  }
  SUBCASE("cut in the header") {
    bytes.resize(20);
    CHECK_FALSE(deserialize(bytes, &error));
    CHECK(error == LoadError::TooShort);
  }
  SUBCASE("trailing garbage") {
    bytes.push_back(0);
    CHECK_FALSE(deserialize(bytes, &error));
    CHECK(error == LoadError::Corrupt);
  }
  SUBCASE("not a number in a position") {
    // Player x is right after magic, version, seed, tick and the RNG (4+4+8+8+8+8 bytes).
    bytes[40] = 0xff; bytes[41] = 0xff; bytes[42] = 0xff; bytes[43] = 0x7f;
    CHECK_FALSE(deserialize(bytes, &error));
    CHECK(error == LoadError::Corrupt);
  }
}

TEST_CASE("a save with the player outside the world is rejected") {
  World w = empty_world();
  w.player.pos.x = rules::kWorldHalfSize + 10;
  LoadError error = LoadError::None;
  CHECK_FALSE(deserialize(serialize(w), &error));
  CHECK(error == LoadError::Corrupt);
}

TEST_CASE("state_hash tells worlds apart") {
  const World a = some_game();
  World b = a;
  CHECK(state_hash(a) == state_hash(b));
  b.player.pos.x += 0.001f;
  CHECK(state_hash(a) != state_hash(b));
}

TEST_CASE("a version 4 save (before creatures) loads and gets its creatures") {
  World original = some_game();
  // Rebuild the version 4 layout: no body speed, no creatures.
  std::vector<uint8_t> v5 = serialize(original);
  std::vector<uint8_t> v4(v5.begin(), v5.begin() + 40 + 12 + 4 + 1 + 8);  // up to the player's speed
  v4[4] = 4;
  const size_t objects_start = kFirstObject - 4;
  const size_t objects_end = kFirstObject + original.objects.size() * kObjectSize;
  v4.insert(v4.end(), v5.begin() + static_cast<std::ptrdiff_t>(objects_start),
            v5.begin() + static_cast<std::ptrdiff_t>(objects_end));

  LoadError error = LoadError::None;
  const auto loaded = deserialize(v4, &error);
  REQUIRE(loaded.has_value());
  CHECK(loaded->player.pos == original.player.pos);
  CHECK(loaded->creatures[0].species == Species::Companion);
  World fresh = *loaded;
  spawn_creatures(fresh);
  CHECK(fresh.creatures == loaded->creatures);  // the same creatures as a new game with this seed
  // The village is there too, and nothing else is in its way.
  const bool has_houses = std::any_of(loaded->objects.begin(), loaded->objects.end(),
                                      [](const Object& o) { return o.kind == ObjectKind::HouseCottage ||
                                                                   o.kind == ObjectKind::HouseChimney ||
                                                                   o.kind == ObjectKind::HouseHut; });
  CHECK(has_houses);
  for (const Object& o : loaded->objects) {
    if (o.kind <= ObjectKind::Crate) CHECK_FALSE(in_village(loaded->village, o.pos.x, o.pos.z));
  }
}
