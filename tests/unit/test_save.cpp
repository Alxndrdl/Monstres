#include "monstres/core/save.hpp"
#include "test_helpers.hpp"

using namespace monstres;

namespace {
World some_game() {
  World w = new_game(2024);
  for (int i = 0; i < 300; ++i) step(w, static_cast<Action>(i % 6));
  return w;
}
}  // namespace

TEST_CASE("a world survives serialize + deserialize unchanged") {
  const World w = some_game();
  const auto bytes = serialize(w);
  LoadError error = LoadError::Corrupt;
  const auto loaded = deserialize(bytes, &error);
  REQUIRE(loaded.has_value());
  CHECK(error == LoadError::None);
  CHECK(*loaded == w);
}

TEST_CASE("a loaded world continues exactly like the original") {
  World original = some_game();
  World copy = *deserialize(serialize(original));
  for (int i = 0; i < 500; ++i) {
    step(original, Action::Attack);
    step(copy, Action::Attack);
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
}

TEST_CASE("a save with a position outside the map is rejected") {
  World w = empty_world();
  add_monster(w, {3, 3});
  w.monsters[0].pos = {rules::kWidth, 0};
  LoadError error = LoadError::None;
  CHECK_FALSE(deserialize(serialize(w), &error));
  CHECK(error == LoadError::Corrupt);
}

TEST_CASE("state_hash tells worlds apart") {
  const World a = some_game();
  World b = a;
  CHECK(state_hash(a) == state_hash(b));
  b.player.score += 1;
  CHECK(state_hash(a) != state_hash(b));
}
