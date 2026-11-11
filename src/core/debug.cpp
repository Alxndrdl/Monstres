#include "monstres/core/debug.hpp"

#include <cstdio>

namespace monstres {

namespace {
std::string num(float v) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.4f", static_cast<double>(v));
  return buf;
}
}  // namespace

std::string to_json(const World& w) {
  const Player& p = w.player;
  std::string s;
  s += "{\"tick\":" + std::to_string(w.tick);
  s += ",\"player\":{\"x\":" + num(p.pos.x) + ",\"y\":" + num(p.pos.y) + ",\"z\":" + num(p.pos.z) +
       ",\"onGround\":" + (p.on_ground ? "true" : "false") +
       ",\"facingX\":" + num(p.facing_x) + ",\"facingZ\":" + num(p.facing_z) + "}";
  s += ",\"objects\":" + std::to_string(w.objects.size());
  s += ",\"creatures\":[";
  static const char* kSpeciesNames[] = {"companion", "slime", "mushroom", "bird", "villager", "crow", "fish"};
  static const char* kMindNames[] = {"idle", "wander", "flee", "follow", "greet", "fly"};
  for (size_t i = 0; i < w.creatures.size(); ++i) {
    const Creature& c = w.creatures[i];
    if (i) s += ',';
    s += std::string("{\"species\":\"") + kSpeciesNames[static_cast<size_t>(c.species)] + "\"" +
         ",\"mind\":\"" + kMindNames[static_cast<size_t>(c.mind)] + "\"" +
         ",\"x\":" + num(c.body.pos.x) + ",\"y\":" + num(c.body.pos.y) + ",\"z\":" + num(c.body.pos.z) + "}";
  }
  s += "]}";
  return s;
}

}  // namespace monstres
