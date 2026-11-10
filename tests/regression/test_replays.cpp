// Non-regression: every replay in data/replays/ must still produce exactly the
// state recorded in its .expected file.
//
// After an intended change of the game rules, regenerate the references and
// review the diff before committing:
//   MONSTRES_UPDATE_GOLDEN=1 ctest --preset native-debug -L regression

#include "replay.hpp"

#include <doctest.h>

#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;
using namespace monstres::test;

TEST_CASE("replays produce their recorded state") {
  const fs::path dir = fs::path(MONSTRES_TEST_DATA_DIR) / "replays";
  std::vector<fs::path> files;
  for (const auto& entry : fs::directory_iterator(dir)) {
    if (entry.path().extension() == ".replay") files.push_back(entry.path());
  }
  std::sort(files.begin(), files.end());
  REQUIRE_MESSAGE(!files.empty(), "no replay in ", dir.string());

  for (const fs::path& file : files) {
    CAPTURE(file.filename().string());
    const std::string actual = summary(play(load_replay(file.string())));
    fs::path expected_path = file;
    expected_path.replace_extension(".expected");

    if (update_golden()) {
      write_text(expected_path.string(), actual);
      MESSAGE("updated ", expected_path.filename().string());
      continue;
    }
    const auto expected = read_text(expected_path.string());
    REQUIRE_MESSAGE(expected.has_value(), "missing ", expected_path.filename().string(),
                    ": run with MONSTRES_UPDATE_GOLDEN=1 to create it");
    CHECK(actual == *expected);
  }
}
