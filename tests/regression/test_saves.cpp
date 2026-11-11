// Non-regression of the save format: save files written by every past version
// of the game (data/saves/v<version>_*.sav) must still load to the same state,
// or be refused cleanly when that version is not supported anymore.
//
// MONSTRES_UPDATE_GOLDEN=1 creates the reference save of the current version
// if it does not exist yet, and rewrites every .expected file. Save files of
// older versions are never rewritten.

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
  return play(parse_replay("seed 2026\nwalk 0 127 45 60\njump 0 127 45\nrandom 8 700"));
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

    fs::path expected_path = entry.path();
    expected_path.replace_extension(".expected");

    // The .expected file holds either the state summary, or
    // "rejected <error>" for saves that must be refused cleanly.
    LoadError error = LoadError::None;
    const auto world = deserialize(read_bytes(entry.path()), &error);
    const std::string actual =
        world ? summary(*world) : std::string("rejected ") + load_error_name(error) + "\n";
    if (update_golden()) {  // the .sav files themselves are never rewritten
      write_text(expected_path.string(), actual);
      continue;
    }
    const auto expected = read_text(expected_path.string());
    REQUIRE(expected.has_value());
    CHECK(actual == *expected);
  }
}
