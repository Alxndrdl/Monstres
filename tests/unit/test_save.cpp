#include "monstres/core/battle.hpp"
#include "monstres/core/debug.hpp"
#include "monstres/core/save.hpp"
#include "monstres/core/variants.hpp"
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
  World w = cached_game(2024);
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

TEST_CASE("every new world can be saved and loaded back") {
  for (uint64_t seed = 0; seed < 25; ++seed) {
    CAPTURE(seed);
    const World& w = cached_game(seed);
    LoadError error = LoadError::None;
    const auto loaded = deserialize(serialize(w), &error);
    CHECK(error == LoadError::None);
    REQUIRE(loaded.has_value());
    CHECK(*loaded == w);
  }
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
    const size_t objects = some_game().objects.size() - some_game().wild_objects;  // the wilds' are not saved
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
  const size_t objects_end = kFirstObject + (original.objects.size() - original.wild_objects) * kObjectSize;
  v4.insert(v4.end(), v5.begin() + static_cast<std::ptrdiff_t>(objects_start),
            v5.begin() + static_cast<std::ptrdiff_t>(objects_end));

  LoadError error = LoadError::None;
  const auto loaded = deserialize(v4, &error);
  REQUIRE(loaded.has_value());
  const World fresh = new_game(original.seed);
  CHECK(loaded->player.pos == fresh.player.pos);  // the world was built anew: back at the start
  CHECK(loaded->creatures[0].species == Species::Companion);
  CHECK(fresh.creatures == loaded->creatures);  // the same creatures as a new game with this seed
  CHECK(loaded->tick == original.tick);
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

TEST_CASE("a save with a teleporter that is none of the world's is rejected") {
  World w = some_game();
  bool found = false;
  for (Object& o : w.objects) {
    if (o.kind != ObjectKind::Teleporter) continue;
    o.variant = 200;  // would be a bit of World::teleporters far beyond its eight
    found = true;
    break;
  }
  REQUIRE(found);
  LoadError error = LoadError::None;
  CHECK_FALSE(deserialize(serialize(w), &error));
  CHECK(error == LoadError::Corrupt);
}

TEST_CASE("a save with absurd speeds or targets is rejected") {
  LoadError error = LoadError::None;
  SUBCASE("the player's speed") {
    World w = some_game();
    w.player.speed = 3e38f;
    CHECK_FALSE(deserialize(serialize(w), &error));
  }
  SUBCASE("a fall faster than anything falls") {
    World w = some_game();
    w.player.velocity_y = -1e30f;
    CHECK_FALSE(deserialize(serialize(w), &error));
  }
  SUBCASE("a creature going nowhere near the world") {
    World w = some_game();
    REQUIRE(w.creatures.size() > 1);
    w.creatures[1].target.x = 3e38f;
    CHECK_FALSE(deserialize(serialize(w), &error));
  }
  CHECK(error == LoadError::Corrupt);
}

// An imported file may be anything: whatever is read is either refused, or
// a world that can be played, shown and saved again (run with the
// sanitizers, this finds what a damaged file could break).
TEST_CASE("damaged saves are refused or load into a world that works") {
  const World game = some_game();
  const std::vector<uint8_t> good = serialize(game);
  // Most of a save is its objects: two times out of three, the damage is
  // after them (creatures, team, bestiary, quests...), where more is checked.
  const size_t after_objects = kFirstObject + (game.objects.size() - game.wild_objects) * kObjectSize;
  REQUIRE(after_objects < good.size());
  Rng rng(7);
  int loaded = 0;
  for (int k = 0; k < 36; ++k) {  // a load builds the wilds again: not too many
    std::vector<uint8_t> bytes = good;
    const size_t from = k % 3 == 0 ? 8 : after_objects;  // never the magic and the version
    const uint32_t changes = 1 + rng.below(3);
    for (uint32_t c = 0; c < changes; ++c) {
      const size_t at = from + rng.below(static_cast<uint32_t>(bytes.size() - from));
      bytes[at] = static_cast<uint8_t>(rng.below(256));
    }
    if (k % 9 == 8) bytes.resize(from + rng.below(static_cast<uint32_t>(bytes.size() - from)));  // cut short
    auto w = deserialize(bytes);
    if (!w) continue;
    ++loaded;
    for (int i = 0; i < 30; ++i) step(*w, move(60, 127, static_cast<Angle16>(i * 500), i % 10 == 0));
    CHECK(to_json(*w).size() > 2);
    CHECK(team_json(*w).size() > 2);
    CHECK(battle_json(*w).size() > 2);
    CHECK(wizard_json(*w).size() > 2);
    CHECK(serialize(*w).size() > after_objects / 2);
  }
  CHECK(loaded > 0);
}

TEST_CASE("a save can be looked at without building its world") {
  World w = some_game();
  w.wizard.name = "Zoé";
  w.wizard.level = 7;
  w.wizard.mana = std::min(w.wizard.mana, wizard_stats(w.wizard).mana);
  std::vector<uint8_t> bytes = serialize(w);
  const auto summary = peek_save(bytes);
  REQUIRE(summary.has_value());
  CHECK(summary->name == "Zoé");
  CHECK(summary->level == 7);
  CHECK(summary->seed == w.seed);
  CHECK(summary->tick == w.tick);
  // Checked as a load is: what would not load is not shown as a save.
  LoadError error = LoadError::None;
  bytes.pop_back();
  CHECK_FALSE(peek_save(bytes, &error));
  CHECK(error == LoadError::Corrupt);
  CHECK_FALSE(peek_save({}, &error));
}
