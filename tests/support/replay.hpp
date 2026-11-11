#pragma once

// Replays: a seed plus a list of inputs, played tick by tick.
//
// File format (tests/regression/data/replays/*.replay), one command per line:
//   # comment
//   seed 42                    start a new game with this seed (first line)
//   walk <x> <y> <yaw°> <ticks>  joystick x/y in -127..127, camera yaw in degrees
//   jump [<x> <y> <yaw°>]      one tick with a jump press (optionally while moving)
//   idle <ticks>               no input
//   random <seed> <ticks>      random inputs: segments of 10-60 ticks, some jumps

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
  std::vector<Input> inputs;
};

inline Angle16 yaw_from_degrees(int degrees) {
  const int64_t d = ((degrees % 360) + 360) % 360;
  return static_cast<Angle16>((d * 65536 + 180) / 360);
}

inline int8_t axis(int v) {
  if (v < -127 || v > 127) throw std::runtime_error("joystick value out of -127..127");
  return static_cast<int8_t>(v);
}

inline void append_random(Replay& r, uint64_t seed, int ticks) {
  Rng rng(seed);
  int produced = 0;
  while (produced < ticks) {
    const int length = 10 + static_cast<int>(rng.below(51));
    Input in;
    in.move_x = static_cast<int8_t>(static_cast<int>(rng.below(255)) - 127);
    in.move_y = static_cast<int8_t>(static_cast<int>(rng.below(255)) - 127);
    in.yaw = static_cast<Angle16>(rng.below(65536));
    const bool jump = rng.below(4) == 0;
    for (int i = 0; i < length && produced < ticks; ++i, ++produced) {
      in.jump = jump && i == 0;
      r.inputs.push_back(in);
    }
  }
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
    } else if (command == "walk") {
      int x = 0, y = 0, yaw = 0, ticks = 0;
      if (!(words >> x >> y >> yaw >> ticks)) throw std::runtime_error("walk needs: x y yaw ticks");
      r.inputs.insert(r.inputs.end(), static_cast<size_t>(ticks), Input{axis(x), axis(y), yaw_from_degrees(yaw), false});
    } else if (command == "jump") {
      int x = 0, y = 0, yaw = 0;
      words >> x >> y >> yaw;  // optional
      r.inputs.push_back({axis(x), axis(y), yaw_from_degrees(yaw), true});
    } else if (command == "idle") {
      int ticks = 0;
      words >> ticks;
      r.inputs.insert(r.inputs.end(), static_cast<size_t>(ticks), Input{});
    } else if (command == "random") {
      uint64_t seed = 0;
      int ticks = 0;
      words >> seed >> ticks;
      append_random(r, seed, ticks);
    } else {
      throw std::runtime_error("unknown replay command: " + command);
    }
  }
  return r;
}

inline Replay parse_replay(const std::string& text) {
  std::istringstream in(text);
  return parse_replay(in);
}

inline Replay load_replay(const std::string& path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("cannot open " + path);
  return parse_replay(in);
}

inline World play(const Replay& r) {
  World w = new_game(r.seed);
  for (const Input& in : r.inputs) step(w, in);
  return w;
}

// Short human-readable summary, stored in the golden .expected files so a
// failing diff says what changed, not only that the hash changed.
inline std::string summary(const World& w) {
  char buf[512];
  int n = std::snprintf(buf, sizeof buf,
                        "hash %016llx\ntick %llu\nplayer %.4f %.4f %.4f %s\nobjects %zu\ncreatures %zu\n",
                        static_cast<unsigned long long>(state_hash(w)),
                        static_cast<unsigned long long>(w.tick), static_cast<double>(w.player.pos.x),
                        static_cast<double>(w.player.pos.y), static_cast<double>(w.player.pos.z),
                        w.player.on_ground ? "on-ground" : "in-air", w.objects.size(), w.creatures.size());
  if (!w.creatures.empty() && n > 0 && static_cast<size_t>(n) < sizeof buf) {
    const Body& c = w.creatures[0].body;
    std::snprintf(buf + n, sizeof buf - static_cast<size_t>(n), "companion %.4f %.4f %.4f\n",
                  static_cast<double>(c.pos.x), static_cast<double>(c.pos.y), static_cast<double>(c.pos.z));
  }
  return buf;
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
