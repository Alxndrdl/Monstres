#include "monstres/core/debug.hpp"

#include <array>

namespace monstres {

namespace {
constexpr std::array<std::string_view, 6> kActionNames = {"none", "up", "down", "left", "right", "attack"};
}

std::string to_json(const World& w) {
  std::string s;
  s += "{\"tick\":" + std::to_string(w.tick);
  s += ",\"gameOver\":" + std::string(w.game_over() ? "true" : "false");
  s += ",\"player\":{\"x\":" + std::to_string(w.player.pos.x) +
       ",\"y\":" + std::to_string(w.player.pos.y) +
       ",\"hp\":" + std::to_string(w.player.hp) +
       ",\"score\":" + std::to_string(w.player.score) + "}";
  s += ",\"monsters\":[";
  for (size_t i = 0; i < w.monsters.size(); ++i) {
    const Monster& m = w.monsters[i];
    if (i) s += ',';
    s += "{\"id\":" + std::to_string(m.id) + ",\"x\":" + std::to_string(m.pos.x) +
         ",\"y\":" + std::to_string(m.pos.y) + ",\"hp\":" + std::to_string(m.hp) + "}";
  }
  s += "]}";
  return s;
}

std::string_view action_name(Action a) { return kActionNames[static_cast<size_t>(a)]; }

std::optional<Action> parse_action(std::string_view name) {
  for (size_t i = 0; i < kActionNames.size(); ++i) {
    if (kActionNames[i] == name) return static_cast<Action>(i);
  }
  return std::nullopt;
}

}  // namespace monstres
