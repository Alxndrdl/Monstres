// Non-regression of the save format: save files written by every past version
// of the game (data/saves/v<version>_*.sav) must still load.
//
// MONSTRES_UPDATE_GOLDEN=1 creates the reference file for the current version
// if it does not exist yet. Files of older versions are never rewritten.

#include "replay.hpp"

#include <doctest.h>

#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;
using namespace monstres;
using namespace monstres::test;

namespace {

std::vector<uint8_t> read_bytes(const fs::path& path) {
  const auto text = read_text(path.string());
  REQUIRE(text.has_value());
  return {text->begin(), text->end()};
}

World reference_game() {
  std::istringstream in("seed 2026\nactions right*3 attack*3 down*2\nrandom 8 700");
  return play(parse_replay(in));
}

}  // namespace

TEST_CASE("the current version has a reference save") {
  const fs::path dir = fs::path(MONSTRES_TEST_DATA_DIR) / "saves";
  const fs::path current = dir / ("v" + std::to_string(kSaveVersion) + "_reference.sav");
  if (update_golden() && !fs::exists(current)) {
    fs::create_directories(dir);
    const auto bytes = serialize(reference_game());
    write_text(current.string(), std::string(bytes.begin(), bytes.end()));
    write_text(fs::path(current).replace_extension(".expected").string(), summary(reference_game()));
    MESSAGE("created ", current.filename().string());
  }
  CHECK_MESSAGE(fs::exists(current), "missing ", current.filename().string(),
                ": run with MONSTRES_UPDATE_GOLDEN=1 to create it");
}

TEST_CASE("every past save still loads to the same state") {
  const fs::path dir = fs::path(MONSTRES_TEST_DATA_DIR) / "saves";
  if (!fs::exists(dir)) return;
  for (const auto& entry : fs::directory_iterator(dir)) {
    if (entry.path().extension() != ".sav") continue;
    CAPTURE(entry.path().filename().string());

    LoadError error = LoadError::None;
    const auto world = deserialize(read_bytes(entry.path()), &error);
    REQUIRE_MESSAGE(world.has_value(), "load error ", static_cast<int>(error));

    fs::path expected_path = entry.path();
    expected_path.replace_extension(".expected");
    const auto expected = read_text(expected_path.string());
    REQUIRE(expected.has_value());
    CHECK(summary(*world) == *expected);
  }
}
