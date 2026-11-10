#pragma once

// Replays: a seed plus a list of actions, played tick by tick.
//
// File format (tests/regression/data/replays/*.replay), one command per line:
//   # comment
//   seed 42                  start a new game with this seed (first line)
//   actions right*3 attack   actions, "name*count" repeats one
//   random 1234 500          500 actions drawn from Rng(1234)

#include "monstres/core/debug.hpp"
#include "monstres/core/rng.hpp"
#include "monstres/core/save.hpp"
#include "monstres/core/world.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace monstres::test {

struct Replay {
  uint64_t seed = 0;
  std::vector<Action> actions;
};

inline void append_actions(Replay& r, const std::string& token) {
  const auto star = token.find('*');
  const std::string name = token.substr(0, star);
  const int count = star == std::string::npos ? 1 : std::stoi(token.substr(star + 1));
  const auto action = parse_action(name);
  if (!action) throw std::runtime_error("unknown action: " + name);
  r.actions.insert(r.actions.end(), static_cast<size_t>(count), *action);
}

inline void append_random(Replay& r, uint64_t seed, int count) {
  Rng rng(seed);
  for (int i = 0; i < count; ++i) r.actions.push_back(static_cast<Action>(rng.below(6)));
}

inline Replay parse_replay(std::istream& in) {
  Replay r;
  std::string line;
  while (std::getline(in, line)) {
    line = line.substr(0, line.find('#'));
    std::istringstream words(line);
    std::string command;
    if (!(words >> command)) continue;
    if (command == "seed") {
      words >> r.seed;
    } else if (command == "actions") {
      for (std::string token; words >> token;) append_actions(r, token);
    } else if (command == "random") {
      uint64_t seed = 0;
      int count = 0;
      words >> seed >> count;
      append_random(r, seed, count);
    } else {
      throw std::runtime_error("unknown replay command: " + command);
    }
  }
  return r;
}

inline Replay load_replay(const std::string& path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("cannot open " + path);
  return parse_replay(in);
}

inline World play(const Replay& r) {
  World w = new_game(r.seed);
  for (Action a : r.actions) step(w, a);
  return w;
}

// Short human-readable summary, stored in the golden .expected files so a
// failing diff says what changed, not only that the hash changed.
inline std::string summary(const World& w) {
  char hash[17];
  std::snprintf(hash, sizeof hash, "%016llx", static_cast<unsigned long long>(state_hash(w)));
  std::ostringstream out;
  out << "hash " << hash << '\n'
      << "tick " << w.tick << '\n'
      << "player " << w.player.pos.x << ' ' << w.player.pos.y << " hp " << w.player.hp
      << " score " << w.player.score << '\n'
      << "monsters " << w.monsters.size() << '\n';
  return out.str();
}

inline bool update_golden() {
  const char* v = std::getenv("MONSTRES_UPDATE_GOLDEN");
  return v && *v && std::string(v) != "0";
}

inline std::optional<std::string> read_text(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  std::ostringstream s;
  s << in.rdbuf();
  return s.str();
}

inline void write_text(const std::string& path, const std::string& text) {
  std::ofstream(path, std::ios::binary) << text;
}

}  // namespace monstres::test
