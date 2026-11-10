#include "monstres/core/save.hpp"
#include "test_helpers.hpp"

using namespace monstres;

namespace {
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
  SUBCASE("old versions 1 (2D prototype) and 2 (boxes only)") {
    for (uint8_t old : {1, 2}) {
      bytes[4] = old;
      CHECK_FALSE(deserialize(bytes, &error));
      CHECK(error == LoadError::UnsupportedVersion);
    }
  }
  SUBCASE("unknown object kind") {
    // First object: after magic, version, tick, RNG, player (12 + 4 + 1 + 8) and count.
    constexpr size_t first_object = 4 + 4 + 8 + 8 + 8 + 12 + 4 + 1 + 8 + 4;
    bytes[first_object] = 200;
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
    // Player x is right after magic, version, tick and the RNG (4+4+8+8+8 bytes).
    bytes[32] = 0xff; bytes[33] = 0xff; bytes[34] = 0xff; bytes[35] = 0x7f;
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
