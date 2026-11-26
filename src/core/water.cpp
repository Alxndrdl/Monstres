#include "monstres/core/water.hpp"

#include "monstres/core/continent.hpp"
#include "monstres/core/interior.hpp"
#include "monstres/core/rivers.hpp"
#include "monstres/core/village.hpp"
#include "monstres/core/west.hpp"

#include <algorithm>
#include <cmath>

namespace monstres {

bool water_at(const World& w, float x, float z, WaterSpot& spot, float margin) {
  if (w.ground.empty() || inside(w)) return false;
  return water_at_ground(w, x, z, w.ground.height(x, z), spot, margin);
}

bool water_at_ground(const World& w, float x, float z, float g, WaterSpot& spot, float margin) {
  if (w.ground.empty() || inside(w)) return false;
  WaterKind kind = WaterKind::None;
  float best = -1e9f;
  const auto take = [&](float level, WaterKind k) {
    if (g < level + margin && level > best) {
      best = level;
      kind = k;
    }
  };
  // The sea: where the continent's ground (as the relief's rim raises it) is below it.
  if (g < rules::kSeaLevel + margin && w.ground.sea(x, z)) take(rules::kSeaLevel, WaterKind::Sea);
  // The village's lake, the continent's lake and marsh.
  if (in_lake(w.village.lake, x, z, std::max(margin, 0.0f))) take(rules::kWaterLevel, WaterKind::Lake);
  for (const Pond& p : continent_ponds()) {
    if (std::fabs(x - p.center.x) > 1.6f * p.radius_x + margin || std::fabs(z - p.center.z) > 1.6f * p.radius_z + margin) continue;
    if (g < p.level + margin && pond_shape(p, x, z) > -0.02f - std::max(margin, 0.0f) / std::min(p.radius_x, p.radius_z)) take(p.level, WaterKind::Lake);
  }
  // The west's stream (within the relief), the continent's rivers.
  if (w.west.valid()) {
    float level = 0, width = 0;
    if (river_surface(w.west, x, z, g, 16.0f, level, width) < width + 2.0f) take(level, WaterKind::River);
  }
  if (RiverSpot r; !w.ground.rivers.empty() && nearest_river(w.ground.rivers, x, z, 14.0f, r) && r.distance < r.width + 2.5f) {
    take(r.level, WaterKind::River);
  }
  if (kind == WaterKind::None) return false;
  spot = {kind, best, g};
  return true;
}

bool frozen(const World& w, const WaterSpot& spot) {
  return spot.kind != WaterKind::None && spot.kind != WaterKind::Sea && w.sky.season == Season::Winter;
}

float floor_height(const World& w, float x, float z) {
  const float g = w.ground.empty() ? 0.0f : w.ground.height(x, z);
  if (w.sky.season != Season::Winter) return g;
  WaterSpot s;
  return water_at(w, x, z, s) && frozen(w, s) ? std::max(g, s.level) : g;
}

bool submerged(const World& w, const Body& b, const BodyShape& shape, WaterSpot* out) {
  WaterSpot s;
  if (!water_at(w, b.pos.x, b.pos.z, s) || frozen(w, s)) return false;
  if (s.depth() < shape.height || s.level < b.pos.y + 0.5f * shape.height) return false;
  if (out) *out = s;
  return true;
}

bool swimming(const World& w) {
  WaterSpot s;
  if (!water_at(w, w.player.pos.x, w.player.pos.z, s) || frozen(w, s)) return false;
  return s.depth() >= rules::kSwimFrom && s.level - w.player.pos.y >= rules::kSwimFrom;
}

bool diving(const World& w) {
  WaterSpot s;
  if (!water_at(w, w.player.pos.x, w.player.pos.z, s) || frozen(w, s)) return false;
  return s.level > w.player.pos.y + rules::kPlayerHeight;
}

bool may_come_out(const World& w, size_t slot) {
  return slot < w.team.size() && (aquatic(w.team[slot].species) || !swimming(w));
}

}  // namespace monstres
