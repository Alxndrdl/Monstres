#include "monstres/core/world.hpp"

#include "monstres/core/battle.hpp"
#include "monstres/core/interior.hpp"
#include "monstres/core/landfix.hpp"
#include "monstres/core/north.hpp"
#include "monstres/core/continent.hpp"
#include "monstres/core/quests.hpp"
#include "monstres/core/roads.hpp"
#include "monstres/core/routines.hpp"
#include "monstres/core/sites.hpp"
#include "monstres/core/variants.hpp"
#include "monstres/core/west.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <span>
#include <tuple>
#include <utility>

namespace monstres {

namespace {

// Random value in [lo, hi] with a 1 cm step: integers from the RNG converted
// exactly, so the layout is identical on every platform.
float random_range(Rng& rng, float lo, float hi) {
  const auto steps = static_cast<uint32_t>((hi - lo) * 100.0f + 0.5f);
  return lo + static_cast<float>(rng.below(steps + 1)) / 100.0f;
}

Angle16 any_yaw(Rng& rng) { return static_cast<Angle16>(rng.below(65536)); }
Angle16 quarter_yaw(Rng& rng) { return static_cast<Angle16>(rng.below(4) << 14); }

ObjectKind pick_kind(Rng& rng) {
  const uint32_t r = rng.below(100);
  if (r < 30) return ObjectKind::Tree;
  if (r < 45) return ObjectKind::Pine;
  if (r < 68) return ObjectKind::Rock;
  if (r < 78) return ObjectKind::Log;
  if (r < 88) return ObjectKind::Stump;
  if (r < 96) return ObjectKind::Bush;
  return ObjectKind::Crate;
}

// In the forest: mostly trees.
ObjectKind pick_forest_kind(Rng& rng) {
  const uint32_t r = rng.below(100);
  if (r < 46) return ObjectKind::Tree;
  if (r < 83) return ObjectKind::Pine;
  if (r < 90) return ObjectKind::Bush;
  if (r < 95) return ObjectKind::Log;
  return ObjectKind::Stump;
}

Object random_object(Rng& rng, float x, float z, bool forest = false) {
  Object o;
  o.kind = forest ? pick_forest_kind(rng) : pick_kind(rng);
  o.pos = {x, 0, z};
  switch (o.kind) {
    case ObjectKind::Tree: {
      const float s = random_range(rng, 0.8f, 1.4f);
      o.size = {s, s, s};
      o.yaw = any_yaw(rng);
      break;
    }
    case ObjectKind::Pine: {
      const float s = random_range(rng, 0.8f, 1.5f);
      o.size = {s, s, s};
      o.yaw = any_yaw(rng);
      break;
    }
    case ObjectKind::Rock:
      o.size = {random_range(rng, 0.4f, 1.6f), random_range(rng, 0.3f, 1.4f), random_range(rng, 0.4f, 1.6f)};
      o.yaw = quarter_yaw(rng);
      break;
    case ObjectKind::Log:  // lying any way (see add_log_pieces)
      o.size = {random_range(rng, 1.0f, 2.0f), random_range(rng, 0.25f, 0.4f), 0};
      o.yaw = any_yaw(rng);
      break;
    case ObjectKind::Stump:
      o.size = {random_range(rng, 0.3f, 0.5f), random_range(rng, 0.3f, 0.7f), 0};
      o.yaw = any_yaw(rng);
      break;
    case ObjectKind::Bush:
      o.size = {random_range(rng, 0.5f, 0.9f), random_range(rng, 0.5f, 0.9f), 0};
      o.yaw = any_yaw(rng);
      break;
    default: {  // Crate
      o.kind = ObjectKind::Crate;
      const float h = random_range(rng, 0.4f, 0.7f);
      o.size = {h, h, h};
      o.yaw = quarter_yaw(rng);
      break;
    }
  }
  o.variant = static_cast<uint8_t>(rng.below(256));
  return o;
}

// Space an object takes for placement: its collision box, widened to the
// foliage for trees so that crowns do not grow into each other.
Box object_extent(const Object& o);

Box footprint(const Object& o) {
  Box b = object_extent(o);
  float r = 0;
  if (o.kind == ObjectKind::Tree) r = 1.2f * o.size.x;
  if (o.kind == ObjectKind::Pine || o.kind == ObjectKind::Larch) r = 1.0f * o.size.x;
  if (r > 0) {
    b.min.x = o.pos.x - r;
    b.max.x = o.pos.x + r;
    b.min.z = o.pos.z - r;
    b.max.z = o.pos.z + r;
  }
  return b;
}

// Footprints of placed objects, by cells of a grid: a new object is tested
// against its neighbours only.
class PlacementGrid {
public:
  static constexpr float kCell = 8.0f;
  static constexpr float kHalf = rules::kReliefHalfSize + 8.0f;
  static constexpr float kNorth = rules::kReliefNorth + 8.0f;
  static constexpr int kCellsX = static_cast<int>(2 * kHalf / kCell) + 1;
  static constexpr int kCellsZ = static_cast<int>((kHalf + kNorth) / kCell) + 1;

  explicit PlacementGrid(const std::vector<Object>& objects) : cells_(static_cast<size_t>(kCellsX * kCellsZ)) {
    for (const Object& o : objects) add(o);
  }
  // Seen from above: heights do not matter (objects settle on the ground afterwards).
  static Box flat(Box b) {
    b.min.y = -1e9f;
    b.max.y = 1e9f;
    return b;
  }
  void add(const Object& o) {
    const Box b = flat(footprint(o));
    each_cell(b, [&](std::vector<Box>& cell) { cell.push_back(b); });
  }
  bool overlaps_any(Box b) {
    b = flat(b);
    bool hit = false;
    each_cell(b, [&](std::vector<Box>& cell) {
      hit = hit || std::any_of(cell.begin(), cell.end(), [&](const Box& other) { return overlaps(b, other); });
    });
    return hit;
  }

private:
  static int cell_x(float v) { return std::clamp(static_cast<int>((v + kHalf) / kCell), 0, kCellsX - 1); }
  static int cell_z(float v) { return std::clamp(static_cast<int>((v + kNorth) / kCell), 0, kCellsZ - 1); }
  template <typename F>
  void each_cell(const Box& b, F f) {
    for (int i = cell_x(b.min.x); i <= cell_x(b.max.x); ++i) {
      for (int j = cell_z(b.min.z); j <= cell_z(b.max.z); ++j) f(cells_[static_cast<size_t>(i * kCellsZ + j)]);
    }
  }
  std::vector<std::vector<Box>> cells_;
};

// Ground too steep to stand a tree or a rock on (the terrace's cliffs, the river's cut banks).
bool steep_here(const World& w, float x, float z) {
  if (w.ground.empty()) return false;
  const float h = w.ground.height(x, z);
  float most = 0;
  for (const auto& [dx, dz] : {std::pair{1.6f, 0.0f}, {-1.6f, 0.0f}, {0.0f, 1.6f}, {0.0f, -1.6f}}) {
    most = std::max(most, std::fabs(w.ground.height(x + dx, z + dz) - h));
  }
  return most > 1.1f;
}

}  // namespace

bool under_water(const World& w, float x, float z, float margin) {
  if (w.ground.empty()) return false;
  const float g = w.ground.height(x, z);
  float level = 0, width = 0;
  if (w.west.valid() && river_surface(w.west, x, z, g, 16.0f, level, width) < width + 2.0f && g < level + margin) return true;
  if (in_lake(w.village.lake, x, z, margin)) return true;
  // The sea, drawn over squares where some of the relief is the sea
  // (build_water_mesh): around there, the ground below its level is under it.
  if (g >= rules::kSeaLevel + margin) return false;
  for (float dx = -8; dx <= 8; dx += 8) {
    for (float dz = -8; dz <= 8; dz += 8) {
      if (w.ground.sea(x + dx, z + dz)) return true;
    }
  }
  return false;
}

namespace {

// In the water or right at its edge (no tree or rock there).
bool wet_here(const World& w, float x, float z) { return under_water(w, x, z, 0.3f); }

// Whether (x, z) belongs to a place built by hand, or the forest's clearing
// (no random trees or rocks there).
bool in_a_place(const World& w, float x, float z) {
  return in_village(w.village, x, z, 2.0f) || in_lake(w.village.lake, x, z, 2.5f) || in_ruins(w.ruins, x, z, 2.0f) ||
         in_castle(w.castle, x, z, 2.0f) || in_cave_hill(w.cave, x, z, 3.0f) || in_west_place(w.west, x, z, 1.0f) ||
         in_north_place(w.north, x, z, 1.0f) || in_site(w.sites, x, z, 1.0f) ||
         steep_here(w, x, z) || wet_here(w, x, z);
}

// A number in [0, 1) for a spot of the world (the same each time).
float spot01(uint64_t seed, float x, float z, uint64_t salt) {
  uint64_t h = seed ^ (static_cast<uint64_t>(std::bit_cast<uint32_t>(x)) << 32) ^ std::bit_cast<uint32_t>(z) ^
               (salt * 0x9E3779B97F4A7C15ULL);
  h = (h ^ (h >> 30)) * 0xBF58476D1CE4E5B9ULL;
  h = (h ^ (h >> 27)) * 0x94D049BB133111EBULL;
  h ^= h >> 31;
  return static_cast<float>(h >> 40) / static_cast<float>(1ULL << 24);
}

// The forest's ways, for the valley's objects as for the wilds'. Its
// clearing has what the meadows have, as many (`forest_pass`: the forest's
// own trees, none there), but no trees; around it, the trees thin out, with
// no logs or stumps. Its trees are taller than elsewhere, some much taller
// (normal, taller and tallest ones mixed). False: nothing there.
bool forest_ways(const World& w, Object& o, bool forest_pass) {
  const float x = o.pos.x, z = o.pos.z;
  if (!in_forest(w.forest, x, z)) return true;
  const bool tree = o.kind == ObjectKind::Tree || o.kind == ObjectKind::Pine;
  const float clearing = clearing_distance(w.forest, x, z);
  if (clearing < 1) return !forest_pass && !tree;
  const float fringe = (clearing - 1) / rules::kClearingFringe;
  if (fringe < 1) {
    if (o.kind == ObjectKind::Log || o.kind == ObjectKind::Stump) return false;
    if (tree && spot01(w.seed, x, z, 1) >= fringe) return false;
    if (!tree && forest_pass) return false;
  }
  if (tree) {
    const float roll = spot01(w.seed, x, z, 2);
    o.size = o.size * (roll < 0.4f ? 1.0f : roll < 0.8f ? rules::kForestTreeScale : 1.75f);
  }
  return true;
}

// A fallen trunk lying any way: its collision is a row of small boxes along
// it (Colliders right after it in World::objects), its own box only says
// where it lies. One along an axis is solid by itself.
bool turned_log(const Object& o) { return o.kind == ObjectKind::Log && (o.yaw & 0x3FFF) != 0; }

// Where an object lies (its collision box; a log turned any way: the box around it).
Box object_extent(const Object& o) {
  if (!turned_log(o)) return object_box(o);
  const SinCos sc = sincos16(o.yaw);
  const float c = std::fabs(sc.cos), n = std::fabs(sc.sin), r = o.size.y;
  const Vec3 half{c * o.size.x + n * r, r, n * o.size.x + c * r};
  const Vec3 center = o.pos + Vec3{0, r, 0};
  return {center - half, center + half};
}

void add_log_pieces(std::vector<Object>& objects, const Object& log) {
  if (!turned_log(log)) return;
  const float half = log.size.x, r = log.size.y;
  const SinCos sc = sincos16(log.yaw);
  // The log's axis: its local +X turned by its yaw (as the model is drawn).
  const float ax = sc.cos, az = -sc.sin;
  // In pairs from its middle outwards, not touching each other (boxes along
  // the axes: farther apart when it lies across them), and leaving its
  // middle free (its own box stands there).
  const float across = std::max(std::fabs(ax), std::fabs(az));
  const float step = 1.45f * r / across, first = 0.75f * r / across, last = half - 0.7f * r;
  for (float u = first; u <= std::max(last, first); u += step) {
    for (const float t : {-u, u}) {
      Object piece;
      piece.kind = ObjectKind::Collider;
      piece.pos = {log.pos.x + ax * t, log.pos.y, log.pos.z + az * t};
      piece.size = {0.7f * r, r, 0.7f * r};
      piece.yaw = 0;
      piece.variant = 0;
      objects.push_back(piece);
    }
  }
}

// The valley's trees and rocks (from the world's random stream, as before
// the world grew). `forest_only`: only in the forest (old saves, given a new one).
void generate_objects(World& w, Rng& rng, bool forest_only = false) {
  const float limit = rules::kValleyHalfSize - 3.0f;
  const float gap = rules::kObjectGap;
  PlacementGrid grid(w.objects);
  std::vector<Vec2> teleporters;  // a clearing around each
  for (const Object& o : w.objects) {
    if (o.kind == ObjectKind::Teleporter) teleporters.push_back({o.pos.x, o.pos.z});
  }
  // First everywhere, then more trees in the forest.
  const struct {
    uint32_t count;
    int attempts;
    bool forest;
  } passes[] = {{rules::kObjectCount, 60000, false}, {rules::kForestTrees, rules::kForestAttempts, true}};
  for (const auto& pass : passes) {
    if (pass.forest && w.forest.radius_x <= 0) continue;
    uint32_t nature = 0;  // the village is already there
    for (int attempt = 0; attempt < pass.attempts && nature < pass.count; ++attempt) {
      float x = random_range(rng, -limit, limit);
      float z = random_range(rng, -limit, limit);
      if (pass.forest) {  // somewhere in the forest (beyond the valley: the wilds', generate_wilds)
        const float u = random_range(rng, -1, 1), v = random_range(rng, -1, 1);
        x = w.forest.center.x + u * w.forest.radius_x;
        z = w.forest.center.z + v * w.forest.radius_z;
        if (std::fabs(x) > limit || std::fabs(z) > limit || !in_forest(w.forest, x, z)) continue;
      }
      if (forest_only && !in_forest(w.forest, x, z)) continue;
      Object o = random_object(rng, x, z, pass.forest);
      const bool tree = o.kind == ObjectKind::Tree || o.kind == ObjectKind::Pine;
      if (pass.forest && tree && rng.below(60) == 0) {
        o.size = o.size * 1.7f;  // an old giant
      }
      if (!forest_ways(w, o, pass.forest)) continue;

      // Keep the spawn area clear.
      const Box b = footprint(o);
      const float dx = std::max({b.min.x, -b.max.x, 0.0f});
      const float dz = std::max({b.min.z, -b.max.z, 0.0f});
      if (dx * dx + dz * dz < rules::kSpawnClearRadius * rules::kSpawnClearRadius) continue;

      // Not on a slope too steep for its size (a flat rock would float).
      if (!w.ground.empty()) {
        const Box box = object_extent(o);
        const float center = w.ground.height(o.pos.x, o.pos.z);
        float lowest = center;
        for (float cx : {box.min.x, box.max.x}) {
          for (float cz : {box.min.z, box.max.z}) lowest = std::min(lowest, w.ground.height(cx, cz));
        }
        if (center - lowest > 0.6f * (box.max.y - box.min.y) + 0.1f) continue;
      }

      // Keep the village, the lake, the ruins, the castle, the cave and the teleporters clear.
      if (in_a_place(w, o.pos.x, o.pos.z)) continue;
      const bool by_teleporter = std::any_of(teleporters.begin(), teleporters.end(), [&](Vec2 t) {
        return (t.x - o.pos.x) * (t.x - o.pos.x) + (t.z - o.pos.z) * (t.z - o.pos.z) < 4.5f * 4.5f;
      });
      if (by_teleporter) continue;

      // Keep the path clear.
      const float reach = std::max(b.max.x - b.min.x, b.max.z - b.min.z) * 0.5f;
      const float keep_off = rules::kPathHalfWidth + reach + 0.3f;
      if (distance_to_path(w.path, o.pos.x, o.pos.z, keep_off + 1) < keep_off) continue;

      // Keep a gap between objects so the player can always walk between them.
      Box wide = b;
      wide.min = wide.min - Vec3{gap, 0, gap};
      wide.max = wide.max + Vec3{gap, 0, gap};
      if (!grid.overlaps_any(wide)) {
        w.objects.push_back(o);
        grid.add(o);
        add_log_pieces(w.objects, o);
        ++nature;
      }
    }
  }
}

}  // namespace

void generate_wilds(World& w) {
  if (w.ground.empty() || w.wild_objects > 0) return;  // hand-made worlds; already there
  // The west's buildings first (the valley's trees and rocks in their way go), then the north's.
  clear_lanes(w);
  size_t first = build_west(w);
  build_north(w);
  build_roads(w);
  build_sites(w);
  if (!w.ground.seated) {
    // Their buildings' ground made flat (landfix.hpp), then all built again
    // over it: the same as when the wilds are drawn again over a ground already seated.
    seat_buildings(w, first);
    w.objects.resize(first);
    first = build_west(w);
    build_north(w);
    build_roads(w);
    build_sites(w);
  }
  Rng rng(w.seed ^ 0x57494C44535F3031ULL, 3);  // "WILDS_01": its own stream
  PlacementGrid grid(w.objects);
  const float gap = rules::kObjectGap;
  constexpr float kStep = 5.0f;  // one try per square of 5 m (jittered)
  const float limit = rules::kReliefHalfSize - 20.0f;
  const int n = static_cast<int>(2 * limit / kStep);
  // What grows at (x, z), drawn there (if anything stays).
  const auto grow = [&](float x, float z, uint32_t roll, Object o) {
    if (std::max(std::fabs(x), std::fabs(z)) < rules::kValleyHalfSize - 2.0f) return;  // the valley's own
    // The west's places stay clear; its woods have their own trees (below).
    if (in_west_place(w.west, x, z, 1.5f) || in_woods(w.west, x, z) || in_roadside_place(w.roads, x, z, 1.5f) ||
        in_site(w.sites, x, z, 1.5f)) return;
    if (steep_here(w, x, z) || wet_here(w, x, z)) return;  // not on the cliffs, not in the water
    const float h = w.ground.height(x, z);
    // How likely an object is here: woods in patches, the continent's forest
    // dense, the hills rocky with pines, the beach almost bare, none at sea;
    // the north's pastures, larch woods and rocky slopes (north_wilds).
    const float patches = value_noise(w.seed + 700, x, z, 90.0f);
    float chance = 0.3f + 0.55f * std::clamp((patches - 0.55f) / 0.15f, 0.0f, 1.0f);
    const Zone zone = zone_at(w, x, z);
    if (region_at(x, z) == Region::Forest) chance = 0.75f;
    if (zone == Zone::Hills) chance = std::max(chance, 0.3f);
    if (w.ground.land_height(x, z) < rules::kSeaLevel + 1.0f && h < rules::kSeaLevel + 1.5f) chance = 0;
    else if (zone == Zone::Coast) chance = 0.04f;
    if (o.kind == ObjectKind::Crate) return;  // not in the wild
    if (zone == Zone::Hills && o.kind == ObjectKind::Tree) o.kind = ObjectKind::Pine;
    if (chance > 0 && zone != Zone::Coast) chance = north_wilds(w, o, x, z, chance);
    if (static_cast<float>(roll) >= chance * 1000.0f) return;
    if (!forest_ways(w, o, false)) return;
    if (zone == Zone::Coast && (o.kind == ObjectKind::Tree || o.kind == ObjectKind::Pine)) {
      o.kind = ObjectKind::Rock;  // its size will do; turned by quarters, as rocks are
      o.yaw = static_cast<Angle16>(o.yaw & 0xC000);
    }
    // Not on a slope too steep for its size, not across the path, not on
    // another one (tried at the height of the ground, as those around it).
    o.pos.y = h;
    const Box box = object_extent(o);
    float lowest = h;
    for (float cx : {box.min.x, box.max.x}) {
      for (float cz : {box.min.z, box.max.z}) lowest = std::min(lowest, w.ground.height(cx, cz));
    }
    if (h - lowest > 0.6f * (box.max.y - box.min.y) + 0.1f) return;
    const Box b = footprint(o);
    const float reach = std::max(b.max.x - b.min.x, b.max.z - b.min.z) * 0.5f;
    const float keep_off = rules::kPathHalfWidth + reach + 0.3f;
    if (distance_to_path(w.path, x, z, keep_off + 1) < keep_off) return;
    const float lane_off = rules::kLaneHalfWidth + reach + 0.3f;
    if (distance_to_lanes(w.roads, x, z, lane_off + 1) < lane_off) return;
    Box wide = b;
    wide.min = wide.min - Vec3{gap, 0, gap};
    wide.max = wide.max + Vec3{gap, 0, gap};
    if (grid.overlaps_any(wide)) return;
    w.objects.push_back(o);
    grid.add(o);
    add_log_pieces(w.objects, o);
  };
  for (int i = 0; i < n; ++i) {
    for (int j = 0; j < n; ++j) {
      // Every draw first (the same number whatever happens): stable layouts.
      const float x = -limit + kStep * (static_cast<float>(i) + random_range(rng, 0.1f, 0.9f));
      const float z = -limit + kStep * (static_cast<float>(j) + random_range(rng, 0.1f, 0.9f));
      const uint32_t roll = rng.below(1000);
      grow(x, z, roll, random_object(rng, x, z, false));
    }
  }
  // Further north (the world grew north): its own stream, the same tries per square meter.
  {
    Rng north(w.seed ^ 0x57494C44534E4F52ULL, 4);  // "WILDSNOR"
    const float north_limit = rules::kReliefNorth - 20.0f;
    const int rows = static_cast<int>((north_limit - limit) / kStep);
    for (int i = 0; i < n; ++i) {
      for (int j = 0; j < rows; ++j) {
        const float x = -limit + kStep * (static_cast<float>(i) + random_range(north, 0.1f, 0.9f));
        const float z = -limit - kStep * (static_cast<float>(j + 1) - random_range(north, 0.1f, 0.9f));
        const uint32_t roll = north.below(1000);
        grow(x, z, roll, random_object(north, x, z, false));
      }
    }
  }
  // The valley's forest past the valley's edge: as dense as inside it (the
  // same tries per square meter as generate_objects' forest pass).
  if (w.forest.radius_x > 0) {
    Rng trees(w.seed ^ 0x464F524553543231ULL, 5);  // "FOREST21": its own stream
    const float inner = rules::kValleyHalfSize - 2.0f;
    for (int attempt = 0; attempt < rules::kForestAttempts; ++attempt) {
      const float u = random_range(trees, -1, 1), v = random_range(trees, -1, 1);
      Object o = random_object(trees, 0, 0, true);
      const float x = w.forest.center.x + u * w.forest.radius_x, z = w.forest.center.z + v * w.forest.radius_z;
      if (std::max(std::fabs(x), std::fabs(z)) < inner || !in_forest(w.forest, x, z)) continue;
      o.pos = {x, 0, z};
      if (!forest_ways(w, o, true) || in_west_place(w.west, x, z, 1.5f)) continue;
      const float h = w.ground.height(x, z);
      o.pos.y = h;
      const Box box = object_extent(o);
      float lowest = h;
      for (float cx : {box.min.x, box.max.x}) {
        for (float cz : {box.min.z, box.max.z}) lowest = std::min(lowest, w.ground.height(cx, cz));
      }
      if (h - lowest > 0.6f * (box.max.y - box.min.y) + 0.1f) continue;
      const Box b = footprint(o);
      const float reach = std::max(b.max.x - b.min.x, b.max.z - b.min.z) * 0.5f;
      const float keep_off = rules::kPathHalfWidth + reach + 0.3f;
      if (distance_to_path(w.path, x, z, keep_off + 1) < keep_off) continue;
      if (distance_to_lanes(w.roads, x, z, keep_off + 1) < keep_off - 0.15f) continue;
      Box wide = b;
      wide.min = wide.min - Vec3{gap, 0, gap};
      wide.max = wide.max + Vec3{gap, 0, gap};
      if (grid.overlaps_any(wide)) continue;
      w.objects.push_back(o);
      grid.add(o);
      add_log_pieces(w.objects, o);
    }
  }
  // The west's woods: dense, tall trees around its clearings.
  if (w.west.valid()) {
    Rng trees(w.seed ^ 0x574F4F44535F3031ULL, 6);  // "WOODS_01": its own stream
    const WestLayout& west = w.west;
    for (int attempt = 0; attempt < 6000; ++attempt) {
      const float u = random_range(trees, -1, 1), v = random_range(trees, -1, 1);
      Object o = random_object(trees, 0, 0, true);
      const float roll = random_range(trees, 0, 1);
      const float x = west.woods.x + u * west.woods_rx, z = west.woods.z + v * west.woods_rz;
      if (!in_woods(west, x, z) || in_west_place(west, x, z, 1.5f) || steep_here(w, x, z) || wet_here(w, x, z)) continue;
      o.pos = {x, 0, z};
      if (o.kind == ObjectKind::Tree || o.kind == ObjectKind::Pine) {
        o.size = o.size * (roll < 0.4f ? 1.0f : roll < 0.85f ? rules::kForestTreeScale : 1.8f);
      }
      const float h = w.ground.height(x, z);
      o.pos.y = h;
      const Box box = object_extent(o);
      float lowest = h;
      for (float cx : {box.min.x, box.max.x}) {
        for (float cz : {box.min.z, box.max.z}) lowest = std::min(lowest, w.ground.height(cx, cz));
      }
      if (h - lowest > 0.6f * (box.max.y - box.min.y) + 0.1f) continue;
      const Box b = footprint(o);
      const float lane_off = rules::kLaneHalfWidth + 0.5f * std::max(b.max.x - b.min.x, b.max.z - b.min.z) + 0.3f;
      if (distance_to_lanes(w.roads, x, z, lane_off + 1) < lane_off) continue;
      Box wide = b;
      wide.min = wide.min - Vec3{gap, 0, gap};
      wide.max = wide.max + Vec3{gap, 0, gap};
      if (grid.overlaps_any(wide)) continue;
      w.objects.push_back(o);
      grid.add(o);
      add_log_pieces(w.objects, o);
    }
  }
  settle_objects(w, first);
  w.wild_objects = static_cast<uint32_t>(w.objects.size() - first);
}

namespace {

// What bodies collide with during a tick: the objects (through their
// index), the ground, the lake, which walkers cannot enter and swimmers
// cannot leave, and the sea, which walkers do not enter either.
struct Surroundings {
  const World& world;
  const Lake& lake;
  const Heightmap& ground;
  const Interior* interior = nullptr;  // inside: its walls

  // Calls f(box) for every object box that may touch `area` (a box may be
  // visited more than once; callers do not mind).
  template <typename F>
  void near(const Box& area, F f) const {
    for_objects_near(world, area, [&](uint32_t, const Box& b) { f(b); });
  }

  bool any_overlap(const Box& area) const {
    bool hit = false;
    near(area, [&](const Box& b) { hit = hit || overlaps(area, b); });
    return hit;
  }
};

enum class Medium { Land, Water, Air };  // Air: a flyer, over the lake too

bool wrong_medium(const Surroundings& s, const Body& b, const BodyShape& shape, Medium medium) {
  if (medium == Medium::Air) return false;
  if (medium == Medium::Water) return !in_lake(s.lake, b.pos.x, b.pos.z, -(shape.half_width + 0.6f));
  // The lake, and the sea (the ground below its level: a few steps into the waves, no more).
  if (in_lake(s.lake, b.pos.x, b.pos.z, shape.half_width * 0.5f)) return true;
  if (!(s.ground.height(b.pos.x, b.pos.z) < rules::kSeaLevel - 0.3f && s.ground.sea(b.pos.x, b.pos.z))) return false;
  // Over the sea: only on a deck (a pier's), standing on it.
  if (b.pos.y < rules::kSeaLevel + 0.2f) return true;
  const float hw = shape.half_width;
  const Box feet{{b.pos.x - hw, b.pos.y - 0.4f, b.pos.z - hw}, {b.pos.x + hw, b.pos.y + 0.05f, b.pos.z + hw}};
  bool deck = false;
  s.near(feet, [&](const Box& o) { deck = deck || (overlaps(feet, o) && o.max.y <= b.pos.y + 0.05f && o.max.y > rules::kSeaLevel); });
  return !deck;
}

constexpr float kStepUp = 0.01f;     // rise always allowed in one step (rounding)
constexpr float kGroundSnap = 0.25f; // walking downhill: stay on the ground if it is this close

// Moves a body along one axis, then pushes it out of any object it entered.
void move_axis(const Surroundings& s, Body& b, const BodyShape& shape, int axis, float delta,
               Medium medium = Medium::Land) {
  if (delta == 0) return;  // not moving: nothing to resolve
  float* coord = axis == 0 ? &b.pos.x : &b.pos.z;
  const float start = *coord;
  *coord += delta;
  const float h = shape.half_width;
  Box area = body_box(b, shape);
  area.min.x = std::min(area.min.x, area.min.x - (axis == 0 ? delta : 0));
  area.max.x = std::max(area.max.x, area.max.x - (axis == 0 ? delta : 0));
  area.min.z = std::min(area.min.z, area.min.z - (axis == 2 ? delta : 0));
  area.max.z = std::max(area.max.z, area.max.z - (axis == 2 ? delta : 0));
  // Walking into something low (a rock, a log, a bush): step up onto it,
  // if there is room on top.
  const float y0 = b.pos.y;
  bool stepped = false;
  if (medium == Medium::Land && b.on_ground) {
    float top = b.pos.y;
    bool low = false, high = false;
    s.near(area, [&](const Box& ob) {
      if (!overlaps(body_box(b, shape), ob)) return;
      if (ob.max.y - b.pos.y <= rules::kStepHeight) {
        low = true;
        top = std::max(top, ob.max.y);
      } else {
        high = true;
      }
    });
    if (low && !high) {
      b.pos.y = top;
      stepped = !s.any_overlap(body_box(b, shape));  // up on it (move_vertical keeps it there)
      if (!stepped) b.pos.y = y0;
    }
  }
  if (!stepped) s.near(area, [&](const Box& ob) {
    if (!overlaps(body_box(b, shape), ob)) return;
    const float lo = axis == 0 ? ob.min.x : ob.min.z;
    const float hi = axis == 0 ? ob.max.x : ob.max.z;
    *coord = delta > 0 ? lo - h : hi + h;
  });
  *coord = axis == 0 ? rules::clamp_world_x(*coord, h) : rules::clamp_world_z(*coord, h);
  // Walkers cannot go up a slope steeper than kMaxWalkSlope (but can jump
  // onto it: the ground then is below their feet).
  const bool too_steep = medium == Medium::Land &&
                         s.ground.height(b.pos.x, b.pos.z) - b.pos.y >
                             std::fabs(*coord - start) * rules::kMaxWalkSlope + kStepUp;
  // Inside: the walls (a body against one may still move away from it). The
  // floor curves up into them: walkers keep a little away.
  bool walled = false;
  if (s.interior) {
    const float margin = shape.half_width + 0.35f;
    const float now = wall_distance(*s.interior, b.pos.x, b.pos.z);
    if (now < margin) {  // moving deeper into it?
      const float bx = axis == 0 ? start : b.pos.x, bz = axis == 2 ? start : b.pos.z;
      walled = now < wall_distance(*s.interior, bx, bz);
    }
  }
  // Pushed out of one object into another (a gap narrower than the body):
  // do not move along this axis at all.
  if (walled || too_steep || s.any_overlap(body_box(b, shape)) || wrong_medium(s, b, shape, medium)) {
    *coord = start;
    b.pos.y = y0;
  }
}

void move_vertical(const Surroundings& s, Body& b, const BodyShape& shape) {  // walkers only
  const float previous_y = b.pos.y;
  const bool was_on_ground = b.on_ground;
  const float ground = s.ground.height(b.pos.x, b.pos.z);
  b.pos.y += b.velocity_y * rules::kDt;
  b.on_ground = false;
  // On the ground, or walking down a slope (not jumping): follow the ground.
  if (b.pos.y <= ground || (was_on_ground && b.velocity_y <= 0 && b.pos.y - ground < kGroundSnap)) {
    b.pos.y = ground;
    b.velocity_y = 0;
    b.on_ground = true;
  }
  constexpr float kEpsilon = 1e-4f;
  s.near(body_box(b, shape), [&](const Box& ob) {
    if (!overlaps(body_box(b, shape), ob)) return;
    if (previous_y >= ob.max.y - kEpsilon) {                    // landing on top
      b.pos.y = ob.max.y;
      b.on_ground = true;
    } else if (previous_y + shape.height <= ob.min.y + kEpsilon) {  // head hits the underside
      b.pos.y = ob.min.y - shape.height;
    } else {
      // Already touching it from the side (squeezed between two objects):
      // stay at the same height rather than being pushed through anything.
      b.pos.y = previous_y;
      b.on_ground = b.on_ground || previous_y <= ground;
    }
    b.velocity_y = 0;
  });
}

// One tick of walking and jumping. (dir_x, dir_z) has a length of at most 1.
void move_body(const Surroundings& s, Body& b, const BodyShape& shape, float dir_x, float dir_z,
               float speed, bool jump, float jump_speed) {
  const float len2 = dir_x * dir_x + dir_z * dir_z;
  if (len2 > 0.0f) {
    const float len = std::sqrt(len2);
    b.facing_x = dir_x / len;
    b.facing_z = dir_z / len;
  }
  if (jump && b.on_ground) b.velocity_y = jump_speed;
  b.velocity_y = std::max(b.velocity_y - rules::kGravity * rules::kDt, -rules::kMaxFallSpeed);

  const float x0 = b.pos.x, z0 = b.pos.z;
  move_axis(s, b, shape, 0, dir_x * speed * rules::kDt);
  move_axis(s, b, shape, 2, dir_z * speed * rules::kDt);
  move_vertical(s, b, shape);
  const float dx = b.pos.x - x0, dz = b.pos.z - z0;
  b.speed = std::sqrt(dx * dx + dz * dz) / rules::kDt;
}

// ---- Creatures ---------------------------------------------------------------

// Shapes match the drawn size (rig scale in src/render/characters.cpp).
constexpr SpeciesInfo kSpecies[] = {
    {{0.23f, 0.58f}, 4.0f, 6.5f, 6.5f},  // Companion
    {{0.45f, 0.72f}, 2.6f, 2.6f, 4.8f},  // Slime: moves only while hopping
    {{0.36f, 0.92f}, 1.5f, 1.5f, 4.2f},  // Mushroom
    {{0.28f, 0.70f}, 2.3f, 6.0f, 4.2f},  // Bird
    {{0.25f, 1.10f}, 1.4f, 1.4f, 0.0f},  // Villager
    {{0.18f, 0.40f}, 0.0f, 6.0f, 0.0f},  // Crow: flies at run_speed
    {{0.30f, 0.30f}, 1.3f, 1.3f, 5.0f},  // Fish: swims, jumps out of the water
    {{0.24f, 0.55f}, 2.6f, 5.8f, 5.5f},  // Spark
    {{0.28f, 0.42f}, 1.7f, 3.4f, 4.0f},  // Axolotl
    {{0.27f, 0.5f}, 2.2f, 5.0f, 4.0f},   // Cristi: climbs too (interior.hpp)
    {{0.28f, 0.55f}, 2.4f, 5.6f, 5.2f},  // Raccoon: Chipou, quick on its paws
    {{0.22f, 0.48f}, 0.0f, 5.5f, 0.0f},  // Owl: Hulou, flies at run_speed (as a crow)
    {{0.36f, 0.66f}, 1.6f, 4.2f, 4.5f},  // Sheep: Laini
    {{0.26f, 0.38f}, 1.8f, 3.6f, 3.6f},  // Mole: Taupi, low and stubby
    {{0.34f, 0.62f}, 1.3f, 2.8f, 3.0f},  // Golem: Caillou, heavy
    {{0.30f, 0.78f}, 2.2f, 6.2f, 7.0f},  // Goat: Cabri, leaps high
    {{0.20f, 0.36f}, 2.8f, 6.6f, 5.5f},  // Ermine: Givri, quick
};

// A companion that flies: it moves like a walker (objects stop it) but stays
// `hover` meters above the ground, rising and sinking smoothly. The lake
// stops a fish swimming in the air, not a crow (`over_water`): it flies above
// the water.
void move_flyer(const Surroundings& s, Body& b, const BodyShape& shape, float dir_x, float dir_z, float speed,
                float hover, bool over_water) {
  const float len2 = dir_x * dir_x + dir_z * dir_z;
  if (len2 > 0.0f) {
    const float len = std::sqrt(len2);
    b.facing_x = dir_x / len;
    b.facing_z = dir_z / len;
  }
  const float x0 = b.pos.x, z0 = b.pos.z;
  const Medium medium = over_water ? Medium::Air : Medium::Land;
  move_axis(s, b, shape, 0, dir_x * speed * rules::kDt, medium);
  move_axis(s, b, shape, 2, dir_z * speed * rules::kDt, medium);
  // Over the lake: above the water, not its bed.
  const bool lake = in_lake(s.lake, b.pos.x, b.pos.z);
  const float floor = std::max(s.ground.height(b.pos.x, b.pos.z), lake ? rules::kWaterLevel : -1e9f);
  // A bird going nowhere, not made to climb, comes down and perches on the
  // ground (not on the water); it takes off again as soon as it moves.
  const bool perch = over_water && !lake && speed <= 0.0f && hover <= companion_hover(Species::Crow) + 1e-3f;
  const float wanted = perch ? floor : floor + hover;
  const float climb = 4.0f * rules::kDt;
  b.pos.y += std::clamp(wanted - b.pos.y, -climb, climb);
  b.pos.y = std::max(b.pos.y, floor);
  b.velocity_y = 0;
  b.on_ground = perch && b.pos.y <= floor + 1e-3f;
  const float dx = b.pos.x - x0, dz = b.pos.z - z0;
  b.speed = std::sqrt(dx * dx + dz * dz) / rules::kDt;
}

// A crow in a battle: it flies over everything, `lift` meters above the
// ground, rising and sinking smoothly.
void fly_free(const Surroundings& s, Body& b, const Steer& st) {
  const float len2 = st.dir_x * st.dir_x + st.dir_z * st.dir_z;
  if (len2 > 0.0f) {
    const float len = std::sqrt(len2);
    b.facing_x = st.dir_x / len;
    b.facing_z = st.dir_z / len;
  }
  const float x0 = b.pos.x, z0 = b.pos.z;
  b.pos.x = rules::clamp_world_x(b.pos.x + st.dir_x * st.speed * rules::kDt, 1.0f);
  b.pos.z = rules::clamp_world_z(b.pos.z + st.dir_z * st.speed * rules::kDt, 1.0f);
  const float floor = s.ground.height(b.pos.x, b.pos.z);
  const float climb = 4.0f * rules::kDt;
  b.pos.y += std::clamp(floor + st.lift - b.pos.y, -climb, climb);
  b.pos.y = std::max(b.pos.y, floor);
  b.velocity_y = 0;
  b.on_ground = false;
  const float dx = b.pos.x - x0, dz = b.pos.z - z0;
  b.speed = std::sqrt(dx * dx + dz * dz) / rules::kDt;
}

Vec2 clamp_to_world(Vec2 p, float margin) { return {rules::clamp_world_x(p.x, margin), rules::clamp_world_z(p.z, margin)}; }


// Unit direction from (x, z) towards the target, and the distance to it.
float direction_to(float x, float z, Vec2 target, float& dir_x, float& dir_z) {
  const float dx = target.x - x, dz = target.z - z;
  const float d = std::sqrt(dx * dx + dz * dz);
  dir_x = d > 0 ? dx / d : 0;
  dir_z = d > 0 ? dz / d : 0;
  return d;
}

void step_companion(World& w, const Surroundings& boxes, Creature& c) {
  const SpeciesInfo& info = species_info(c.species);
  Body& b = c.body;
  const Body& p = w.player;

  float to_player_x, to_player_z;
  const float player_distance = direction_to(b.pos.x, b.pos.z, {p.pos.x, p.pos.z}, to_player_x, to_player_z);
  if (player_distance > rules::kCompanionTeleportDistance) {
    // Lost behind: catch up instantly, just behind the player.
    b.pos = {p.pos.x - p.facing_x * rules::kCompanionFollowDistance, p.pos.y,
             p.pos.z - p.facing_z * rules::kCompanionFollowDistance};
    b.pos.y = std::max(b.pos.y, w.ground.height(b.pos.x, b.pos.z));
    if (inside_any_object(w, b, info.shape)) b.pos = p.pos;
    b.velocity_y = 0;
    c.timer = 0;
  }

  // Follow a point behind the player; or, helping, a few meters from it
  // towards what the player looks for (a lost lamb, a bundle, the way out).
  Vec2 goal;
  bool guiding = companion_guide(w, goal);
  float look_x = to_player_x, look_z = to_player_z;
  if (guiding) {
    float gx, gz;
    const float far = direction_to(p.pos.x, p.pos.z, goal, gx, gz);
    guiding = far > 0.3f;
    if (guiding) {
      if (!inside(w)) {
        const float r = std::min(far, rules::kCompanionGuideDistance);
        goal = {p.pos.x + gx * r, p.pos.z + gz * r};
      }
      look_x = gx;
      look_z = gz;
    }
  }
  c.target = guiding ? clamp_to_world(goal, info.shape.half_width)
                     : clamp_to_world({p.pos.x - p.facing_x * rules::kCompanionFollowDistance,
                                       p.pos.z - p.facing_z * rules::kCompanionFollowDistance}, info.shape.half_width);
  float dir_x, dir_z;
  const float d = direction_to(b.pos.x, b.pos.z, c.target, dir_x, dir_z);
  float speed = 0;
  if (d > 0.6f) {
    c.mind = Mind::Follow;
    speed = d > 4.0f ? std::max(info.run_speed, rules::kCompanionMinRunSpeed) : std::max(info.walk_speed, 3.0f);
  } else {
    c.mind = Mind::Idle;
    dir_x = dir_z = 0;
    if (player_distance > 0.1f || guiding) {  // look at the player, or the way it shows
      b.facing_x = look_x;
      b.facing_z = look_z;
    }
  }
  // Jump when stuck against something, or to follow the player up onto
  // something (heights above the ground: a slope is not a reason to jump).
  const float player_up = p.pos.y - w.ground.height(p.pos.x, p.pos.z);
  const float own_up = b.pos.y - w.ground.height(b.pos.x, b.pos.z);
  bool jump = b.on_ground && (c.timer >= 8 || (player_up > own_up + 0.4f && player_distance < 2.5f));
  if (jump) c.timer = 0;
  if (const float hover = companion_hover(c.species); hover > 0) {
    move_flyer(boxes, b, info.shape, dir_x, dir_z, speed, hover, flies(c.species));
    return;
  }
  if (c.species == Species::Slime && speed > 0) {  // a slime follows by hopping
    jump = jump || b.on_ground;
    if (b.on_ground) speed = 0;
  }
  move_body(boxes, b, info.shape, dir_x, dir_z, speed, jump, info.jump_speed);
  c.timer = (speed > 0 && b.on_ground && b.speed < 0.3f * speed) ? static_cast<uint16_t>(c.timer + 1) : 0;
}

void step_wild(World& w, const Surroundings& boxes, Creature& c) {
  const SpeciesInfo& info = species_info(c.species);
  Body& b = c.body;
  const Body& p = w.player;

  // Its life (routines.hpp): asleep, after its prey, playing, away from a hunter...
  wild_watch(w, c);
  float away_x, away_z;
  const float player_distance = direction_to(p.pos.x, p.pos.z, {b.pos.x, b.pos.z}, away_x, away_z);
  if (c.species == Species::Bird && !asleep(c) && player_distance < rules::kBirdFleeDistance && c.mind != Mind::Flee) {
    c.mind = Mind::Flee;
    c.act = Activity::None;
    c.timer = 2 * rules::kTicksPerSecond;
    c.target = clamp_to_world({b.pos.x + away_x * 6, b.pos.z + away_z * 6}, 1.0f);
  }

  float dir_x = 0, dir_z = 0, speed = 0;
  bool jump = false;
  if (c.mind == Mind::Idle) {
    if (c.timer > 0) {
      --c.timer;
      // Birds hop on the spot from time to time.
      if (c.species == Species::Bird && c.act == Activity::None && b.on_ground && w.rng.below(200) == 0) jump = true;
    } else if (!wild_decide(w, c)) {
      c.mind = Mind::Wander;
      c.timer = 8 * rules::kTicksPerSecond;  // give up after 8 s
      // The shepherd's flock stays in its pen, the farm's boarders in their paddock.
      const float r = wander_reach(w, c);
      c.target = clamp_to_world({c.home.x + random_range(w.rng, -r, r), c.home.z + random_range(w.rng, -r, r)}, 1.0f);
    }
  } else {
    const float d = direction_to(b.pos.x, b.pos.z, c.target, dir_x, dir_z);
    if (d < 0.4f || c.timer == 0) {
      // There (or it gave up): what it came for, a while; else a rest.
      c.mind = Mind::Idle;
      dir_x = dir_z = 0;
      const auto secs = [](float s) { return static_cast<uint16_t>(s * rules::kTicksPerSecond); };
      switch (c.act) {
        case Activity::Shelter: c.timer = secs(12) + static_cast<uint16_t>(w.rng.below(8 * rules::kTicksPerSecond)); break;
        case Activity::Warm: c.timer = secs(20); break;
        case Activity::Sniff: c.timer = secs(3); break;
        case Activity::Drink: {
          Vec2 edge, facing;
          if (water_edge(w, {b.pos.x, b.pos.z}, 3.0f, edge, facing)) {
            b.facing_x = facing.x;
            b.facing_z = facing.z;
          }
          c.timer = secs(4) + static_cast<uint16_t>(w.rng.below(3 * rules::kTicksPerSecond));
          break;
        }
        default:
          c.act = Activity::None;
          c.other = 0;
          c.timer = static_cast<uint16_t>(rules::kTicksPerSecond + w.rng.below(3 * rules::kTicksPerSecond));
          break;
      }
    } else {
      --c.timer;
      speed = wild_runs(c) ? info.run_speed : info.walk_speed;
    }
  }

  if (c.species == Species::Slime && speed > 0) {
    // Slimes hop: they jump as soon as they land and only move in the air.
    jump = b.on_ground;
    if (b.on_ground) speed = 0;
  }
  move_body(boxes, b, info.shape, dir_x, dir_z, speed, jump, info.jump_speed);
  if (speed > 0 && b.speed > 0) {  // keep facing the way it goes
    b.facing_x = dir_x;
    b.facing_z = dir_z;
  }
}

void step_villager(World& w, const Surroundings& s, Creature& c) {
  const SpeciesInfo& info = species_info(c.species);
  Body& b = c.body;
  const Body& p = w.player;
  float to_x, to_z;
  const float player_distance = direction_to(b.pos.x, b.pos.z, {p.pos.x, p.pos.z}, to_x, to_z);

  float dir_x = 0, dir_z = 0, speed = 0;
  if (player_distance < rules::kVillagerGreetDistance && std::fabs(p.pos.y - b.pos.y) < 1.5f) {
    // Stop, turn to the player and wave.
    c.mind = Mind::Greet;
    b.facing_x = to_x;
    b.facing_z = to_z;
    c.timer = static_cast<uint16_t>(rules::kTicksPerSecond / 2);
  } else if (c.act == Activity::GoHome) {
    // Home (routines.hpp): to its door, round what stands in the way.
    const float d = direction_to(b.pos.x, b.pos.z, c.target, dir_x, dir_z);
    if (d < 0.5f || c.timer == 0) {
      c.act = Activity::Home;
      c.mind = Mind::Idle;
      c.other = 0;
      b.pos = {c.target.x, w.ground.empty() ? b.pos.y : w.ground.height(c.target.x, c.target.z), c.target.z};
      return;
    }
    --c.timer;
    c.mind = Mind::Wander;
    uint32_t stuck = c.other & 0xFFu, aside = (c.other >> 8) & 0xFFu, left = (c.other >> 16) & 1u;
    if (aside > 0) {  // a moment along the obstacle
      const float sx = left ? -dir_z : dir_z, sz = left ? dir_x : -dir_x;
      dir_x = sx;
      dir_z = sz;
      --aside;
    }
    move_body(s, b, info.shape, dir_x, dir_z, info.walk_speed, false, info.jump_speed);
    b.facing_x = dir_x;
    b.facing_z = dir_z;
    stuck = b.speed < 0.25f * info.walk_speed ? stuck + 1 : 0;
    if (stuck > 30) {
      stuck = 0;
      aside = 50;
      left ^= 1u;
    }
    c.other = std::min(stuck, 255u) | (aside << 8) | (left << 16);
    return;
  } else if (c.mind == Mind::Greet || c.mind == Mind::Idle) {
    if (c.timer > 0) {
      --c.timer;
    } else {
      c.mind = Mind::Wander;
      c.timer = 10 * rules::kTicksPerSecond;
      // Gaspard stays by the teleporter, Bastien at the edge of the village, Isidore on the square.
      const float r = inside(w)                                        ? 1.0f  // in its house
                      : c.npc == npc::kGaspard || c.npc == npc::kBastien ? 2.5f
                      : c.npc == npc::kMentor || c.npc > npc::kMentor ? 1.5f  // the west's stay at their place
                                                                      : rules::kVillageRadius - 4.0f;
      c.target = {c.home.x + random_range(w.rng, -r, r), c.home.z + random_range(w.rng, -r, r)};
    }
  } else {
    const float d = direction_to(b.pos.x, b.pos.z, c.target, dir_x, dir_z);
    if (d < 0.4f || c.timer == 0) {
      c.mind = Mind::Idle;
      c.timer = static_cast<uint16_t>(2 * rules::kTicksPerSecond + w.rng.below(4 * rules::kTicksPerSecond));
      dir_x = dir_z = 0;
    } else {
      --c.timer;
      speed = info.walk_speed;
    }
  }
  move_body(s, b, info.shape, dir_x, dir_z, speed, false, info.jump_speed);
}

// What a flyer keeps above at (x, z): the ground, what stands there, and the
// hollow tree's crown of leaves (much wider than its trunk's box).
float flight_floor(const World& w, float x, float z) {
  float top = w.ground.height(x, z);
  constexpr float kCrown = 10.0f;  // the leaves' reach from the trunk's middle
  const Box area{{x - kCrown, -1e9f, z - kCrown}, {x + kCrown, 1e9f, z + kCrown}};
  for_objects_near(w, area, [&](uint32_t k, const Box& b) {
    const Object& o = w.objects[k];
    if (o.kind == ObjectKind::HollowTree) {
      if (std::hypot(x - o.pos.x, z - o.pos.z) < kCrown) top = std::max(top, o.pos.y + rules::kHollowTreeTop + 4.5f);
    } else if (x > b.min.x - 0.6f && x < b.max.x + 0.6f && z > b.min.z - 0.6f && z < b.max.z + 0.6f) {
      top = std::max(top, b.max.y + 0.4f);
    }
  });
  return top;
}

void step_crow(World& w, const std::vector<Vec3>& perches, Creature& c) {
  const SpeciesInfo& info = species_info(c.species);
  Body& b = c.body;
  const Body& p = w.player;
  const float dx = p.pos.x - b.pos.x, dz = p.pos.z - b.pos.z;
  const bool player_near = dx * dx + dz * dz < rules::kCrowFleeDistance * rules::kCrowFleeDistance &&
                           std::fabs(p.pos.y - b.pos.y) < 7.0f;

  auto take_off = [&](bool flee) {
    if (perches.size() < 2) return;
    // Another perch; when fleeing, one far from the player.
    for (int attempt = 0; attempt < 8; ++attempt) {
      const Vec3 t = perches[w.rng.below(static_cast<uint32_t>(perches.size()))];
      const float tx = t.x - p.pos.x, tz = t.z - p.pos.z;
      const float mx = t.x - b.pos.x, mz = t.z - b.pos.z;
      if (mx * mx + mz * mz < 1.0f) continue;  // the same perch
      if (flee && tx * tx + tz * tz < 64.0f && attempt < 7) continue;
      c.target = {t.x, t.z};
      c.target_y = t.y;
      c.mind = Mind::Fly;
      b.on_ground = false;
      return;
    }
  };

  b.speed = 0;
  if (c.mind != Mind::Fly) {
    if (player_near) {
      take_off(true);
    } else if (c.timer > 0) {
      --c.timer;
    } else {
      take_off(false);
    }
    if (c.mind != Mind::Fly) return;
  }

  // Flying: cruise above the target, then glide down onto it.
  float dir_x, dir_z;
  const float d = direction_to(b.pos.x, b.pos.z, c.target, dir_x, dir_z);
  const float step = info.run_speed * rules::kDt;
  float horizontal = std::min(step, d);
  // Over what is in the way (the hills, the hollow tree's crown), never
  // through it: up first while it is too high, here or a little ahead. (Not
  // in the last glide, down onto the perch: that is on top of it.)
  const bool cruising = d > 2.5f;
  const float above = cruising ? std::max(flight_floor(w, b.pos.x, b.pos.z),
                                          flight_floor(w, b.pos.x + dir_x * 3.0f, b.pos.z + dir_z * 3.0f))
                               : 0.0f;
  if (cruising && b.pos.y < above - 0.2f) horizontal = 0;
  b.pos.x += dir_x * horizontal;
  b.pos.z += dir_z * horizontal;
  const float floor = w.ground.height(b.pos.x, b.pos.z);
  const float wanted_y = cruising ? std::max({c.target_y + 3.0f, floor + 3.0f, above + 1.0f}) : c.target_y;
  const float climb = 3.0f * rules::kDt;
  b.pos.y += std::clamp(wanted_y - b.pos.y, -climb, climb);
  b.pos.y = std::max(b.pos.y, floor);
  if (d > 0) {
    b.facing_x = dir_x;
    b.facing_z = dir_z;
  }
  b.speed = horizontal / rules::kDt;
  if (d < 0.05f && std::fabs(b.pos.y - c.target_y) < 0.05f) {  // landed
    b.pos = {c.target.x, c.target_y, c.target.z};
    b.on_ground = true;
    c.mind = Mind::Idle;
    c.timer = static_cast<uint16_t>(4 * rules::kTicksPerSecond + w.rng.below(8 * rules::kTicksPerSecond));
  }
}

void step_fish(World& w, const Surroundings& s, Creature& c) {
  const SpeciesInfo& info = species_info(c.species);
  Body& b = c.body;
  float dir_x = 0, dir_z = 0, speed = 0;
  if (c.mind == Mind::Idle) {
    if (c.timer > 0) {
      --c.timer;
    } else {
      c.mind = Mind::Wander;
      c.timer = 8 * rules::kTicksPerSecond;
      for (int attempt = 0; attempt < 8; ++attempt) {
        const Lake& l = s.lake;
        c.target = {l.center.x + random_range(w.rng, -l.radius_x, l.radius_x),
                    l.center.z + random_range(w.rng, -l.radius_z, l.radius_z)};
        if (in_lake(l, c.target.x, c.target.z, -1.5f)) break;
      }
    }
  } else {
    const float d = direction_to(b.pos.x, b.pos.z, c.target, dir_x, dir_z);
    if (d < 0.4f || c.timer == 0) {
      c.mind = Mind::Idle;
      c.timer = static_cast<uint16_t>(rules::kTicksPerSecond + w.rng.below(3 * rules::kTicksPerSecond));
    } else {
      --c.timer;
      speed = info.walk_speed;
    }
  }
  // Swim on the surface; now and then, jump out of the water.
  if (b.on_ground && w.rng.below(500) == 0) {
    b.velocity_y = info.jump_speed;
    b.on_ground = false;
  }
  if (speed > 0) {
    b.facing_x = dir_x;
    b.facing_z = dir_z;
  }
  const float x0 = b.pos.x, z0 = b.pos.z;
  move_axis(s, b, info.shape, 0, dir_x * speed * rules::kDt, Medium::Water);
  move_axis(s, b, info.shape, 2, dir_z * speed * rules::kDt, Medium::Water);
  const float mx = b.pos.x - x0, mz = b.pos.z - z0;
  b.speed = std::sqrt(mx * mx + mz * mz) / rules::kDt;
  if (!b.on_ground) {
    b.velocity_y -= rules::kGravity * rules::kDt;
    b.pos.y += b.velocity_y * rules::kDt;
    if (b.pos.y <= rules::kSwimLevel) {
      b.pos.y = rules::kSwimLevel;
      b.velocity_y = 0;
      b.on_ground = true;  // back in the water
    }
  }
}

}  // namespace

Box body_box(const Body& b, const BodyShape& shape) {
  const float h = shape.half_width;
  return {{b.pos.x - h, b.pos.y, b.pos.z - h}, {b.pos.x + h, b.pos.y + shape.height, b.pos.z + h}};
}

Box player_box(const Player& p) { return body_box(p, kPlayerShape); }

bool steep_ground(const World& w, float x, float z) { return steep_here(w, x, z); }

float companion_hover(Species species) {
  switch (species) {
    case Species::Crow:
    case Species::Owl: return 0.7f;  // low (and on the ground when it stops: move_flyer)
    case Species::Fish: return 0.7f;
    default: return 0;
  }
}

const SpeciesInfo& species_info(Species species) {
  const auto i = static_cast<size_t>(species);
  return kSpecies[i < static_cast<size_t>(Species::Count) ? i : 0];
}

namespace {

// Levels of the wild monsters, by where they live (stronger far from the
// start: in the wilds, one more every 110 m beyond the valley).
uint8_t level_in(Zone zone, Rng& rng, float x = 0, float z = 0) {
  uint8_t base = 1;
  switch (zone) {
    case Zone::Forest: base = 2; break;
    case Zone::Ruins: base = 3; break;
    case Zone::Cave: base = 4; break;
    case Zone::Castle: base = 5; break;
    case Zone::Farm: base = 3; break;
    case Zone::Woods: base = 4; break;
    case Zone::Crater: base = 6; break;
    case Zone::Coast:
    case Zone::Hills:
    case Zone::Plains:
    // The north's, further and further up to the mountains (a level every 110 m, as everywhere).
    case Zone::Alpine:
    case Zone::Larches:
    case Zone::Frost:
    case Zone::FrozenLake: {
      const float beyond = std::max(std::fabs(x), std::fabs(z)) - rules::kValleyHalfSize;
      base = static_cast<uint8_t>(2 + std::clamp(static_cast<int>(beyond / 110.0f), 0, 10));
      break;
    }
    case Zone::Mine: base = 7; break;
    default: break;
  }
  return static_cast<uint8_t>(base + rng.below(3));
}

// Which monster lives in a zone, with its chances out of 100 (crows and
// fish have their own places). Zapou like the open hills and old stones,
// Ploufi the water's edge; Pompon too, now and then, in the open (a starter
// that may not have been chosen).
struct Odds {
  uint32_t chance;
  Species species;
};
std::span<const Odds> zone_odds(Zone zone) {
  using enum Species;
  static constexpr Odds kForest[] = {{45, Mushroom}, {25, Bird}, {15, Slime}, {15, Axolotl}};
  static constexpr Odds kCave[] = {{50, Slime}, {30, Mushroom}, {20, Spark}};
  static constexpr Odds kCastle[] = {{40, Bird}, {25, Slime}, {20, Mushroom}, {15, Spark}};
  static constexpr Odds kCoast[] = {{45, Slime}, {35, Axolotl}, {20, Bird}};  // slimes like the wet sand
  static constexpr Odds kHills[] = {{38, Bird}, {25, Mushroom}, {17, Spark}, {10, Slime}, {10, Sheep}};
  static constexpr Odds kRuins[] = {{30, Slime}, {30, Mushroom}, {20, Bird}, {20, Spark}};
  static constexpr Odds kLake[] = {{40, Axolotl}, {30, Slime}, {30, Bird}};
  static constexpr Odds kOpen[] = {{27, Slime}, {27, Mushroom}, {27, Bird}, {7, Spark}, {7, Axolotl}, {5, Companion}};
  // The west: Chipous raid the farms, Taupis dig the fields; the woods' own; the crater's arcane stones.
  static constexpr Odds kPlains[] = {{24, Slime}, {22, Mushroom}, {22, Bird}, {10, Raccoon}, {7, Spark},
                                     {6, Axolotl}, {5, Mole}, {4, Companion}};
  static constexpr Odds kFarm[] = {{40, Raccoon}, {25, Bird}, {20, Slime}, {15, Mole}};
  static constexpr Odds kWoods[] = {{30, Mushroom}, {22, Raccoon}, {20, Bird}, {14, Slime}, {14, Axolotl}};
  static constexpr Odds kCrater[] = {{45, Cristi}, {30, Spark}, {25, Slime}};
  static constexpr Odds kRiver[] = {{55, Axolotl}, {20, Slime}, {25, Bird}};  // Ploufis along the river
  // The north: Cabri in the pastures and up the slopes, Caillou among the rocks, Givri in the snow.
  static constexpr Odds kAlpine[] = {{26, Goat}, {22, Bird}, {16, Sheep}, {14, Mushroom}, {8, Golem}, {8, Spark}, {6, Slime}};
  static constexpr Odds kLarches[] = {{24, Goat}, {22, Mushroom}, {18, Golem}, {16, Bird}, {12, Raccoon}, {8, Ermine}};
  static constexpr Odds kFrost[] = {{34, Ermine}, {30, Golem}, {26, Goat}, {10, Bird}};
  static constexpr Odds kFrozenLake[] = {{55, Ermine}, {25, Slime}, {20, Golem}};
  static constexpr Odds kMine[] = {{65, Golem}, {35, Mole}};  // inside (north.cpp: spawn_miners)
  switch (zone) {
    case Zone::Forest: return kForest;
    case Zone::Cave: return kCave;
    case Zone::Castle: return kCastle;
    case Zone::Coast: return kCoast;
    case Zone::Hills: return kHills;
    case Zone::Ruins: return kRuins;
    case Zone::Lake: return kLake;
    case Zone::Plains: return kPlains;
    case Zone::Farm: return kFarm;
    case Zone::Woods: return kWoods;
    case Zone::Crater: return kCrater;
    case Zone::River: return kRiver;
    case Zone::Alpine: return kAlpine;
    case Zone::Larches: return kLarches;
    case Zone::Frost: return kFrost;
    case Zone::FrozenLake: return kFrozenLake;
    case Zone::Mine: return kMine;
    default: return kOpen;
  }
}

Species species_in(Zone zone, Rng& rng) {
  const uint32_t r = rng.below(100);
  uint32_t sum = 0;
  const std::span<const Odds> odds = zone_odds(zone);
  for (const Odds& o : odds) {
    sum += o.chance;
    if (r < sum) return o.species;
  }
  return odds.front().species;
}

// Chances (out of 100) that a try puts a monster there.
uint32_t density_in(Zone zone) {
  switch (zone) {
    case Zone::Village: return 0;  // villagers only
    case Zone::Lake: return 0;     // fish, see below
    case Zone::Meadow: return 35;
    case Zone::Coast: case Zone::Hills: case Zone::Plains: return 30;
    case Zone::Alpine: case Zone::Larches: case Zone::Frost: return 30;
    default: return 50;
  }
}

}  // namespace

uint32_t species_chance(Species species, Zone zone) {
  if (species == Species::Fish) return zone == Zone::Lake ? 100 : 0;  // the lake's fish
  if (species == Species::Cristi && zone == Zone::Cave) return 100;  // inside the crystal cave (interior.cpp)
  if (species == Species::Owl) return zone == Zone::Woods ? 100 : 0;  // on the hollow tree's crown
  if (species == Species::Crow) {  // on the roofs, the tower, the castle's towers
    return zone == Zone::Village || zone == Zone::Ruins || zone == Zone::Castle ? 100 : 0;
  }
  if (density_in(zone) == 0 && zone != Zone::Lake) return 0;  // nothing wild lives there
  for (const Odds& o : zone_odds(zone)) {
    if (o.species == species) return o.chance;
  }
  return 0;
}

uint32_t species_habitat(Species species) {
  uint32_t zones = 0;
  for (size_t z = 0; z < static_cast<size_t>(Zone::Count); ++z) {
    if (species_chance(species, static_cast<Zone>(z)) > 0) zones |= 1u << z;
  }
  return zones;
}

namespace {

Box cell_rect(int index, float& x0, float& z0) {
  const int i = index / rules::kPopulationCellsZ, j = index % rules::kPopulationCellsZ;
  x0 = -rules::kWorldHalfSize + rules::kPopulationCell * static_cast<float>(i);
  z0 = -rules::kWorldNorth + rules::kPopulationCell * static_cast<float>(j);
  return {{x0, 0, z0}, {x0 + rules::kPopulationCell, 0, z0 + rules::kPopulationCell}};
}

int cell_index(float x, float z) {
  const int nx = rules::kPopulationCellsX, nz = rules::kPopulationCellsZ;
  const int i = std::clamp(static_cast<int>((x + rules::kWorldHalfSize) / rules::kPopulationCell), 0, nx - 1);
  const int j = std::clamp(static_cast<int>((z + rules::kWorldNorth) / rules::kPopulationCell), 0, nz - 1);
  return i * nz + j;
}

// A square's own random streams: as numbered when the world was a square of
// 29 by 29 (the same monsters there as before it grew north), the new ones
// beyond those numbers.
uint64_t cell_stream(int index) {
  const int i = index / rules::kPopulationCellsZ, j = index % rules::kPopulationCellsZ;
  constexpr int kOld = static_cast<int>(2 * rules::kWorldHalfSize / rules::kPopulationCell);
  constexpr int kNorthRows = rules::kPopulationCellsZ - kOld;
  if (j >= kNorthRows) return static_cast<uint64_t>(i * kOld + (j - kNorthRows));
  return static_cast<uint64_t>(kOld * kOld + i * kNorthRows + j);
}

float distance_to_rect(const Box& r, float x, float z) {
  const float dx = std::max({r.min.x - x, 0.0f, x - r.max.x});
  const float dz = std::max({r.min.z - z, 0.0f, z - r.max.z});
  return std::sqrt(dx * dx + dz * dz);
}

bool near_player(const World& w, float x, float z, float distance) {
  const float dx = x - w.player.pos.x, dz = z - w.player.pos.z;
  return dx * dx + dz * dz < distance * distance;
}

// Now and then, a trainer passing by in this square (once Bastien taught the
// player to fight): its team drawn from what lives there, a little stronger;
// some are aggressive (never close to a safe place, see west.hpp). Its own
// draws: the rest of the square stays the same.
void add_wanderer(World& w, int index, float x0, float z0) {
  if (w.quests[static_cast<size_t>(QuestId::Combat)].stage != QuestStage::Done) return;
  const auto k = static_cast<size_t>(index);
  Rng rng(w.seed ^ 0x545241494E4552ULL, cell_stream(index) * 65536u + w.population.visits[k]);  // "TRAINER"
  if (rng.below(100) >= rules::kWandererChance) return;
  uint32_t here = 0;
  int free_slot = -1;
  for (uint8_t n = 0; n < npc::kWandererCount; ++n) {
    if (npc_index(w, static_cast<uint8_t>(npc::kWanderer + n)) > 0) ++here;
    else if (free_slot < 0) free_slot = n;
  }
  if (free_slot < 0 || here >= rules::kWanderersAtOnce) return;
  const uint8_t n = static_cast<uint8_t>(npc::kWanderer + free_slot);
  for (int attempt = 0; attempt < 8; ++attempt) {
    const float x = random_range(rng, x0 + 2, x0 + rules::kPopulationCell - 2);
    const float z = random_range(rng, z0 + 2, z0 + rules::kPopulationCell - 2);
    const bool aggressive = rng.below(100) < rules::kAggressiveWanderers;
    const Zone zone = zone_at(w, x, z);
    if (density_in(zone) == 0 || !rules::in_world(x, z, 4.0f) ||
        near_player(w, x, z, rules::kMonsterSpawnMinDistance + 6) || in_village(w.village, x, z, 8.0f) ||
        in_lake(w.village.lake, x, z, 3.0f) || in_cave_hill(w.cave, x, z, 2.0f) || w.ground.sea(x, z) ||
        (aggressive && !far_from_safety(w, x, z))) {
      continue;
    }
    Creature t;
    t.species = Species::Villager;
    t.npc = n;
    t.variant = static_cast<uint8_t>(rng.below(256));
    t.body.pos = {x, w.ground.height(x, z), z};
    t.home = {x, z};
    t.timer = static_cast<uint16_t>(rng.below(3 * rules::kTicksPerSecond));
    if (aggressive) t.tag = Tag::Raider;
    if (inside_any_object(w, t.body, species_info(t.species).shape)) continue;
    // Its team: one to three of the square's monsters.
    Wanderer& team = w.wanderers[static_cast<size_t>(free_slot)];
    team = {};
    team.count = static_cast<uint8_t>(1 + rng.below(kRivalTeamSize));
    for (uint8_t m = 0; m < team.count; ++m) {
      Species sp = Species::Slime;
      for (int tries = 0; tries < 8; ++tries) {
        const Species s = species_in(zone, rng);
        if (monster_stats(s).is_monster && s != Species::Fish) {
          sp = s;
          break;
        }
      }
      RivalMonster& r = team.team[m];
      r.species = sp;
      r.genes = genetics(sp).gene_count > 0 ? roll_genes(sp, zone, rng) : Genes{};
      r.level = static_cast<uint8_t>(std::min(99, level_in(zone, rng, x, z) + 1 + static_cast<int>(rng.below(2))));
    }
    add_creature(w, t);
    // Its first monster, beside it.
    Creature c;
    c.species = team.team[0].species;
    c.genes = team.team[0].genes;
    c.level = team.team[0].level;
    c.tag = Tag::Rival;
    c.npc = n;
    c.body.pos = {x + 1.0f, w.ground.height(x + 1.0f, z), z};
    c.home = {x, z};
    add_creature(w, c);
    return;
  }
}

// The wild monsters of one square, new ones every time it is lived in again.
void populate_cell(World& w, int index) {
  float x0, z0;
  cell_rect(index, x0, z0);
  const auto k = static_cast<size_t>(index);
  Rng rng(w.seed ^ 0x504F50554C415445ULL, cell_stream(index) * 65536u + w.population.visits[k]);
  Rng aggro(w.seed ^ 0x524F414D494E47ULL, cell_stream(index) * 65536u + w.population.visits[k]);  // "ROAMING"
  auto random_in_cell = [&](float& x, float& z) {
    x = random_range(rng, x0, x0 + rules::kPopulationCell);
    z = random_range(rng, z0, z0 + rules::kPopulationCell);
  };
  auto finish = [&](Creature& c, Zone zone) {
    c.home = {c.body.pos.x, c.body.pos.z};
    c.genes = genetics(c.species).gene_count > 0 ? roll_genes(c.species, zone, rng) : Genes{};
    c.level = level_in(zone, rng, c.body.pos.x, c.body.pos.z);
    c.variant = static_cast<uint8_t>(rng.below(256));
    add_creature(w, c);
  };

  // Birds, slimes and mushrooms, on the ground.
  for (int attempt = 0; attempt < 12; ++attempt) {
    float x, z;
    random_in_cell(x, z);
    const Zone zone = zone_at(w, x, z);
    const uint32_t roll = rng.below(100);
    const Species species = species_in(zone, rng);
    const SinCos sc = sincos16(static_cast<Angle16>(rng.below(65536)));
    const uint16_t timer = static_cast<uint16_t>(rng.below(3 * rules::kTicksPerSecond));
    if (roll >= density_in(zone) || !rules::in_world(x, z, 3.0f)) continue;
    if (near_player(w, x, z, rules::kMonsterSpawnMinDistance)) continue;
    if (in_village(w.village, x, z, 2.0f) || in_lake(w.village.lake, x, z, 2.0f) || in_cave_hill(w.cave, x, z, 1.0f) ||
        (w.ground.sea(x, z) && w.ground.height(x, z) < rules::kSeaLevel + 0.5f)) {  // not in the sea
      continue;
    }
    Creature c;
    c.species = species;
    c.body.pos = {x, w.ground.height(x, z), z};
    c.body.facing_x = sc.sin;
    c.body.facing_z = sc.cos;
    c.timer = timer;
    if (inside_any_object(w, c.body, species_info(species).shape)) continue;
    // After the orchard, now and then an aggressive one (its own draw: the others stay the same).
    if (monster_stats(species).is_monster && roaming_raider(w, x, z, aggro.below(100))) c.tag = Tag::Raider;
    finish(c, zone);
  }

  // Fish, where the lake is (tries all over it: some fall in this square).
  const Lake& lake = w.village.lake;
  for (int attempt = 0, fish = 0; lake.radius_x > 0 && attempt < 10 && fish < 3; ++attempt) {
    const float x = random_range(rng, lake.center.x - lake.radius_x, lake.center.x + lake.radius_x);
    const float z = random_range(rng, lake.center.z - lake.radius_z, lake.center.z + lake.radius_z);
    const uint16_t timer = static_cast<uint16_t>(rng.below(3 * rules::kTicksPerSecond));
    if (cell_index(x, z) != index || !in_lake(lake, x, z, -1.5f) || near_player(w, x, z, 3.0f)) continue;
    Creature c;
    c.species = Species::Fish;
    c.body.pos = {x, rules::kSwimLevel, z};
    c.timer = timer;
    finish(c, Zone::Lake);
    ++fish;
  }

  // Crows, on the roofs and towers of the square.
  int crows = 0;
  for (const Vec3& perch : crow_perches(w)) {
    if (cell_index(perch.x, perch.z) != index || crows >= 3) continue;
    const bool here = rng.below(100) < 40;
    const uint16_t timer = static_cast<uint16_t>(3 * rules::kTicksPerSecond + rng.below(8 * rules::kTicksPerSecond));
    if (!here || near_player(w, perch.x, perch.z, 6.0f)) continue;
    Creature c;
    c.species = Species::Crow;
    c.body.pos = perch;
    c.target = {perch.x, perch.z};
    c.target_y = perch.y;
    c.timer = timer;
    finish(c, zone_at(w, perch.x, perch.z));  // home: where it was born (its variants), not where it goes
    ++crows;
  }

  // Owls, on the hollow tree's crown (and the woodcutter's roof).
  int owls = 0;
  for (const Vec3& perch : owl_perches(w)) {
    if (cell_index(perch.x, perch.z) != index || owls >= 3) continue;
    const bool here = rng.below(100) < 55;
    const uint16_t timer = static_cast<uint16_t>(4 * rules::kTicksPerSecond + rng.below(8 * rules::kTicksPerSecond));
    if (!here || near_player(w, perch.x, perch.z, 5.0f)) continue;
    Creature c;
    c.species = Species::Owl;
    c.body.pos = perch;
    c.target = {perch.x, perch.z};
    c.target_y = perch.y;
    c.timer = timer;
    finish(c, Zone::Woods);
    ++owls;
  }

  add_wanderer(w, index, x0, z0);
}

}  // namespace

namespace {

// Mirabelle's lost Piou roams near the village until it is caught: it is
// there whenever its square is lived in (a new one each time it was gone).
void keep_lost_piou(World& w) {
  if (w.village.lake.radius_x <= 0 || w.quests[static_cast<size_t>(QuestId::LostPiou)].stage == QuestStage::Done) return;
  const Vec2 lost = lost_piou_home(w);
  const auto k = static_cast<size_t>(cell_index(lost.x, lost.z));
  if (!w.population.active[k] || lost_piou_exists(w)) return;
  Creature c = make_lost_piou(w, w.population.visits[k]);
  // On free ground near its spot.
  for (int attempt = 0; attempt < 16; ++attempt) {
    const SinCos sc = sincos16(static_cast<Angle16>(attempt * 4096));
    const float r = 1.5f * static_cast<float>(attempt);
    c.body.pos = {lost.x + sc.cos * r, 0, lost.z + sc.sin * r};
    c.body.pos.y = w.ground.height(c.body.pos.x, c.body.pos.z);
    if (!inside_any_object(w, c.body, species_info(c.species).shape) &&
        !in_lake(w.village.lake, c.body.pos.x, c.body.pos.z, 1.0f)) {
      break;
    }
  }
  c.home = {c.body.pos.x, c.body.pos.z};
  add_creature(w, c);
}

// The giant Champi and its group live in the heart of the forest, whenever
// its square is lived in, until the giant is caught (see quests.hpp).
void keep_giant(World& w) {
  if (!giant_wanted(w)) return;
  const Vec2 home = giant_home(w);
  const auto k = static_cast<size_t>(cell_index(home.x, home.z));
  if (!w.population.active[k]) return;
  std::array<Creature, 5> group = make_giant_group(w, w.population.visits[k]);
  int spot = 0;
  for (Creature& c : group) {
    // On free ground around the middle: the giant there, the others around it.
    for (; spot < 64; ++spot) {
      const SinCos sc = sincos16(static_cast<Angle16>(spot * 10923));  // a sixth of a turn
      const float r = spot == 0 ? 0.0f : 2.5f + 1.2f * static_cast<float>(spot / 6);
      c.body.pos = {home.x + sc.cos * r, 0, home.z + sc.sin * r};
      c.body.pos.y = w.ground.height(c.body.pos.x, c.body.pos.z);
      if (!inside_any_object(w, c.body, species_info(c.species).shape)) break;
    }
    ++spot;
    c.home = {c.body.pos.x, c.body.pos.z};
    add_creature(w, c);
  }
}

// A trainer's monster stands beside it (the first of its team); between
// battles it is the first one again (a game saved during a battle). Its
// villager number says whose it is (none: Bastien's, from older saves).
void keep_rival(World& w, uint8_t trainer) {
  const int owner = npc_index(w, trainer);
  if (owner < 0) return;
  int index = -1;
  for (size_t i = 1; i < w.creatures.size(); ++i) {
    Creature& c = w.creatures[i];
    if (c.tag != Tag::Rival) continue;
    if (c.npc == 0) c.npc = npc::kBastien;
    if (c.npc == trainer) index = static_cast<int>(i);
  }
  uint8_t count = 0;
  const RivalMonster& first = rival_team(w, trainer, count)[0];
  if (index > 0) {
    Creature& c = w.creatures[static_cast<size_t>(index)];
    if (w.battle.rival.npc != trainer && (c.species != first.species || c.level != first.level)) {
      c.species = first.species;
      c.genes = first.genes;
      c.level = first.level;
      c.mind = Mind::Idle;
    }
    return;
  }
  const Body& o = w.creatures[static_cast<size_t>(owner)].body;
  Creature c;
  c.species = first.species;
  c.genes = first.genes;
  c.level = first.level;
  c.tag = Tag::Rival;
  c.npc = trainer;
  for (int k = 0; k < 8; ++k) {  // beside him, where there is room
    const SinCos sc = sincos16(static_cast<Angle16>(k * 8192));
    const float x = o.pos.x + sc.cos * 0.9f, z = o.pos.z + sc.sin * 0.9f;
    c.body.pos = {x, w.ground.height(x, z), z};
    if (!inside_any_object(w, c.body, species_info(c.species).shape) && !in_lake(w.village.lake, x, z, 1.0f)) break;
    c.body.pos = o.pos;
  }
  c.home = {o.pos.x, o.pos.z};
  add_creature(w, c);
}

}  // namespace

void keep_trainer_monster(World& w, uint8_t trainer) { keep_rival(w, trainer); }

size_t add_creature(World& w, Creature c) {
  c.id = w.next_id++;
  w.creatures.push_back(c);
  return w.creatures.size() - 1;
}

void remove_creature(World& w, size_t index) {
  if (index == 0 || index >= w.creatures.size()) return;
  w.creatures.erase(w.creatures.begin() + static_cast<std::ptrdiff_t>(index));
  for (Foe& f : w.battle.foes) {
    if (f.index > index) --f.index;
  }
}

int find_creature(const World& w, uint32_t id) {
  for (size_t i = 0; i < w.creatures.size(); ++i) {
    if (w.creatures[i].id == id) return static_cast<int>(i);
  }
  return -1;
}

void update_population(World& w) {
  if (w.ground.empty()) return;  // hand-made worlds (tests) have only the creatures put there
  for (int index = 0; index < static_cast<int>(kPopulationCellCount); ++index) {
    float x0, z0;
    const Box rect = cell_rect(index, x0, z0);
    const float d = distance_to_rect(rect, w.player.pos.x, w.player.pos.z);
    const auto k = static_cast<size_t>(index);
    if (!w.population.active[k] && d < rules::kSpawnRadius) {
      w.population.active[k] = 1;
      ++w.population.visits[k];
      populate_cell(w, index);
    } else if (w.population.active[k] && d > rules::kDespawnRadius) {
      // Far away: its wild monsters are gone (not one in a battle).
      w.population.active[k] = 0;
      for (size_t i = w.creatures.size(); i-- > 1;) {
        const Creature& c = w.creatures[i];
        const bool fighting = foe_slot(w, i) >= 0;
        const bool passing = npc::wanderer(c.npc) && w.battle.rival.npc != c.npc;  // a trainer passing by, and its monster
        if (((monster_stats(c.species).is_monster && !owned(w, c)) || passing) && !fighting &&
            cell_index(c.home.x, c.home.z) == index) {
          remove_creature(w, i);
        }
      }
    }
  }
  keep_lost_piou(w);
  keep_giant(w);
  keep_rival(w, npc::kBastien);
  keep_rival(w, npc::kRomane);
  if (w.tournament.on) keep_rival(w, npc::kContender);
  keep_rival(w, npc::kTristan);
  keep_west(w);
}

bool place_trainer(const World& w, Creature& c) {
  // Bastien waits at the edge of the village, on the side of the place where he fights.
  Vec2 center, axis;
  if (!rival_arena(w, npc::kBastien, center, axis)) return false;
  const Vec2 vc = w.village.center;
  const float dx = center.x - vc.x, dz = center.z - vc.z;
  const float d = std::sqrt(dx * dx + dz * dz);
  // A spot wide enough for him and his monster (a fence, a house may be in the way).
  const BodyShape room{0.75f, 1.2f};
  for (int attempt = 0; attempt < 120; ++attempt) {
    const float r = rules::kVillageRadius - 3.0f - static_cast<float>(attempt / 12);
    const int k = attempt % 12;
    const float t = 1.5f * static_cast<float>(k % 2 == 0 ? k / 2 : -(k + 1) / 2);
    const float x = vc.x + dx / d * r + axis.x * t;
    const float z = vc.z + dz / d * r + axis.z * t;
    Body b = c.body;
    b.pos = {x, w.ground.height(x, z), z};
    if (!inside_any_object(w, b, room) && !in_lake(w.village.lake, x, z, 1.5f)) {
      c.body.pos = b.pos;
      c.home = {x, z};
      return true;
    }
  }
  return false;  // nowhere free: anywhere in the village
}

WeatherField weather_field(const World& w) {
  WeatherField f;
  f.seed = w.seed;
  f.tick = w.tick;
  f.season = w.sky.season;
  f.center = {w.player.pos.x, w.player.pos.z};
  f.forced = w.sky.forced;
  f.forced_amount = w.sky.forced_amount;
  f.afar = w.sky.afar;
  f.afar_at = w.sky.afar_at;
  return f;
}

void spawn_creatures(World& w) {
  Rng rng(w.seed ^ 0x4352454154555245ULL, 11);  // own stream: objects stay the same
  w.creatures.clear();
  w.population = {};

  Creature companion;
  companion.species = Species::Companion;
  companion.body.pos = {0.8f, w.ground.height(0.8f, 1.2f), 1.2f};  // just behind the player, on the right
  companion.home = {0.8f, 1.2f};
  add_creature(w, companion);

  const bool has_village = w.village.lake.radius_x > 0;
  if (has_village) {
    // Villagers, around the village square.
    const Vec2 center = w.village.center;
    for (uint32_t n = 0, attempt = 0; n < rules::kVillagers && attempt < 2000; ++attempt) {
      Creature c;
      c.species = Species::Villager;
      const float r = rules::kVillageRadius - 4.0f;
      c.body.pos = {center.x + random_range(rng, -r, r), 0, center.z + random_range(rng, -r, r)};
      c.body.pos.y = w.ground.height(c.body.pos.x, c.body.pos.z);
      c.variant = static_cast<uint8_t>(rng.below(256));
      c.home = center;
      c.timer = static_cast<uint16_t>(rng.below(4 * rules::kTicksPerSecond));
      c.npc = static_cast<uint8_t>(n + 1);
      Vec3 teleporter;
      if (c.npc == npc::kGaspard && teleporter_position(w, TeleporterId::Village, teleporter)) {
        // Gaspard stands by the teleporter, wondering what it is.
        const SinCos sc = sincos16(static_cast<Angle16>(attempt * 9000));
        const float x = teleporter.x + sc.cos * 2.4f, z = teleporter.z + sc.sin * 2.4f;
        c.home = {teleporter.x, teleporter.z};
        c.body.pos = {x, w.ground.height(x, z), z};
      }
      if (c.npc == npc::kBastien && place_trainer(w, c)) {
        add_creature(w, c);
        ++n;
        continue;
      }
      if (inside_any_object(w, c.body, species_info(c.species).shape)) continue;
      if (in_lake(w.village.lake, c.body.pos.x, c.body.pos.z, 1.0f)) continue;
      add_creature(w, c);
      ++n;
    }
  }
  if (has_village) add_mentor(w);
  add_west_npcs(w);
  add_north_npcs(w);
  add_road_npcs(w);
  add_site_npcs(w);
  // The wild monsters around the player.
  update_population(w);
}

void add_mentor(World& w) {
  if (w.village.lake.radius_x <= 0 || npc_index(w, npc::kMentor) >= 0) return;
  // On the village square, a few steps from its middle, away from the lake.
  const Vec2 c = w.village.center;
  const float away = w.village.lake.center.x > c.x ? -1.0f : 1.0f;
  Creature m;
  m.species = Species::Villager;
  m.npc = npc::kMentor;
  m.home = c;
  m.body.facing_x = 0;
  m.body.facing_z = 1;  // towards the south, where the player comes from
  const BodyShape& shape = species_info(Species::Villager).shape;
  for (int k = 0; k < 24; ++k) {
    const SinCos sc = sincos16(static_cast<Angle16>(k * 2731));
    const float r = 4.0f + 0.25f * static_cast<float>(k);
    const float x = c.x + away * std::fabs(sc.cos) * r, z = c.z + sc.sin * r;
    m.body.pos = {x, w.ground.height(x, z), z};
    if (!inside_any_object(w, m.body, shape) && !in_lake(w.village.lake, x, z, 1.5f)) break;
  }
  m.home = {m.body.pos.x, m.body.pos.z};
  add_creature(w, m);
}

namespace {

Vec2 local_to_world(const Object& o, float lx, float lz) {
  // Same rotation as the renderer: yaw turns +X towards -Z.
  const SinCos sc = sincos16(o.yaw);
  return {o.pos.x + lx * sc.cos + lz * sc.sin, o.pos.z - lx * sc.sin + lz * sc.cos};
}

// Front direction (where the door is) of a quarter yaw: local -Z.
Vec2 front_of(Angle16 yaw) {
  const SinCos sc = sincos16(yaw);
  return {-sc.sin, -sc.cos};
}

bool can_place(const World& w, const Object& o, float gap, bool keep_off_path) {
  Box b = object_box(o);
  for (float x : {b.min.x, b.max.x}) {
    for (float z : {b.min.z, b.max.z}) {
      if (in_lake(w.village.lake, x, z, 1.0f)) return false;
      if (!rules::in_world(x, z, 1.0f)) return false;
    }
  }
  if (keep_off_path) {
    const float reach = std::max(b.max.x - b.min.x, b.max.z - b.min.z) * 0.5f;
    if (distance_to_path(w.path, o.pos.x, o.pos.z) < rules::kPathHalfWidth + reach + 0.4f) return false;
  }
  b.min = b.min - Vec3{gap, 0, gap};
  b.max = b.max + Vec3{gap, 0, gap};
  return std::none_of(w.objects.begin(), w.objects.end(),
                      [&](const Object& other) { return overlaps(b, object_box(other)); });
}

Object village_object(ObjectKind kind, Vec2 pos, Angle16 yaw, uint8_t variant = 0) {
  Object o;
  o.kind = kind;
  o.pos = {pos.x, 0, pos.z};
  o.size = {1, 1, 1};
  o.yaw = yaw;
  o.variant = variant;
  return o;
}

}  // namespace

void build_village(World& w) {
  const VillageLayout& v = w.village;
  if (v.lake.radius_x <= 0) return;
  Rng rng(w.seed ^ 0x56494C4C41474521ULL, 13);  // own stream
  std::erase_if(w.objects, [&](const Object& o) {
    return in_village(v, o.pos.x, o.pos.z, 2.0f) || in_lake(v.lake, o.pos.x, o.pos.z, 2.5f);
  });
  const Vec2 c = v.center;
  const float lake_side = v.lake.center.x > c.x ? 1.0f : -1.0f;

  // The well, on the square, away from the lake.
  w.objects.push_back(village_object(ObjectKind::Well, {c.x - lake_side * 4.0f, c.z}, 0));

  // Six houses around the square, doors towards it.
  ObjectKind types[] = {ObjectKind::HouseCottage, ObjectKind::HouseChimney, ObjectKind::HouseHut,
                        ObjectKind::HouseCottage, ObjectKind::HouseChimney, ObjectKind::HouseHut};
  for (int i = 5; i > 0; --i) std::swap(types[i], types[rng.below(static_cast<uint32_t>(i + 1))]);
  std::vector<Object> houses;
  // Candidate places around the square, away from the path, which runs
  // north-south through the middle (angles in degrees from +X towards +Z).
  constexpr int kSlots[] = {0, 40, 140, 180, 220, 320, 20, 160, 200, 340, 60, 120, 240, 300};
  int next_type = 0;
  for (int degrees : kSlots) {
    if (houses.size() == 6) break;
    const auto angle = static_cast<Angle16>((degrees * 65536 / 360 + static_cast<int>(rng.below(2185)) - 1092) & 0xFFFF);
    const SinCos sc = sincos16(angle);
    for (float radius : {9.5f + random_range(rng, 0, 1.5f), 12.5f}) {
      const Vec2 pos{c.x + sc.cos * radius, c.z + sc.sin * radius};
      // The quarter turn whose door faces the square best.
      Angle16 best = 0;
      float best_dot = -2;
      for (Angle16 yaw : {Angle16{0}, Angle16{16384}, Angle16{32768}, Angle16{49152}}) {
        const Vec2 f = front_of(yaw);
        const float d = f.x * (c.x - pos.x) + f.z * (c.z - pos.z);
        if (d > best_dot) {
          best_dot = d;
          best = yaw;
        }
      }
      const Object house = village_object(types[next_type], pos, best, static_cast<uint8_t>(rng.below(256)));
      if (can_place(w, house, 1.0f, true)) {
        w.objects.push_back(house);
        houses.push_back(house);
        ++next_type;
        break;
      }
    }
  }

  // A fenced yard behind the cottages and chimney houses, a barrel by some doors.
  for (const Object& h : houses) {
    const Box b = object_box(h);
    const float depth = (h.yaw >> 14) & 1 ? (b.max.x - b.min.x) / 2 : (b.max.z - b.min.z) / 2;
    if (h.kind != ObjectKind::HouseHut) {
      for (float along : {-2.0f, 0.0f, 2.0f}) {
        const Vec2 pos = local_to_world(h, along, depth + 2.2f);  // behind: local +Z
        const Object fence = village_object(ObjectKind::Fence, pos, h.yaw);
        if (can_place(w, fence, 0.05f, true)) w.objects.push_back(fence);
      }
    }
    if (rng.below(2) == 0) {
      const Vec2 pos = local_to_world(h, 1.6f, -(depth + 0.8f));  // beside the door
      const Object barrel = village_object(ObjectKind::Barrel, pos, static_cast<Angle16>(rng.below(65536)));
      if (can_place(w, barrel, 0.3f, true)) w.objects.push_back(barrel);
    }
  }

  // Lanterns along the path through the village, a signpost at each end.
  int lamp = 0;
  for (size_t i = 1; i + 1 < w.path.points.size(); i += 8) {
    const Vec2 p = w.path.points[i];
    if (std::fabs(p.z - c.z) > rules::kVillageRadius - 2) continue;
    const Vec2 q = w.path.points[i + 1];
    float tx = q.x - p.x, tz = q.z - p.z;
    const float len = std::sqrt(tx * tx + tz * tz);
    tx /= len;
    tz /= len;
    const float side = (lamp++ % 2) ? 1.0f : -1.0f;
    const float offset = rules::kPathHalfWidth + 0.8f;
    const Object o = village_object(ObjectKind::Lamp, {p.x - tz * offset * side, p.z + tx * offset * side}, 0);
    if (can_place(w, o, 0.3f, false)) w.objects.push_back(o);
  }
  for (float dz : {rules::kVillageRadius + 1.0f, -(rules::kVillageRadius + 1.0f)}) {
    const Vec2* nearest = &w.path.points.front();
    for (const Vec2& p : w.path.points) {
      if (std::fabs(p.z - (c.z + dz)) < std::fabs(nearest->z - (c.z + dz))) nearest = &p;
    }
    const Object sign = village_object(ObjectKind::Sign, {nearest->x + rules::kPathHalfWidth + 0.9f, nearest->z},
                                       dz > 0 ? Angle16{32768} : Angle16{0});
    if (can_place(w, sign, 0.3f, false)) w.objects.push_back(sign);
  }
}

void settle_objects(World& w, size_t first) {
  constexpr float kSink = 0.05f;
  if (w.ground.empty()) return;  // flat: objects stay where they are
  for (size_t k = first; k < w.objects.size(); ++k) {
    Object& o = w.objects[k];
    const Box b = object_extent(o);
    const float center = w.ground.height(o.pos.x, o.pos.z);
    float lowest = center;
    for (float x : {b.min.x, b.max.x}) {
      for (float z : {b.min.z, b.max.z}) lowest = std::min(lowest, w.ground.height(x, z));
    }
    if (o.kind == ObjectKind::Bridge || o.kind == ObjectKind::Pier) continue;  // its deck spans the stream, the sea (build_west, build_sites)
    if (o.kind == ObjectKind::Boat) {  // afloat
      o.pos.y = rules::kSeaLevel - 0.3f;
      continue;
    }
    if (o.kind == ObjectKind::HotSpring) {  // its water at the level of the ground around its pool
      o.pos.y = w.ground.height(o.pos.x + o.size.x * 1.25f, o.pos.z);
      continue;
    }
    if (seated_kind(o.kind) && std::max(std::fabs(o.pos.x), std::fabs(o.pos.z)) >= rules::kValleyHalfSize + 12.0f) {
      o.pos.y = center - kSink;  // on its flat ground (seat_buildings), its sill at the ground's level
      continue;
    }
    if (o.kind == ObjectKind::Collider) {  // from its lowest point, raised by its variant (decimeters)
      o.pos.y = lowest + static_cast<float>(o.variant) / 10.0f;
      continue;
    }
    // A low object on a slope (a flat rock) would vanish into the ground:
    // sink it at most 40 % of its height, an edge may then float a little.
    o.pos.y = std::max(lowest - kSink, center - kSink - 0.4f * (b.max.y - b.min.y));
  }
}

void build_ruins(World& w) {
  const RuinsLayout& r = w.ruins;
  if (r.radius <= 0) return;
  // Own random stream. Every draw goes into a variable first: the order in
  // which a call's arguments are evaluated differs between compilers (GCC,
  // Clang), and the layout must be the same natively and in WebAssembly.
  Rng rng(w.seed ^ 0x5255494E53212121ULL, 17);
  auto any_yaw16 = [&] { return static_cast<Angle16>(rng.below(65536)); };
  auto quarter_yaw16 = [&] { return static_cast<Angle16>(rng.below(4) << 14); };
  auto variant8 = [&] { return static_cast<uint8_t>(rng.below(256)); };
  std::erase_if(w.objects, [&](const Object& o) { return in_ruins(r, o.pos.x, o.pos.z, 2.0f); });
  const Vec2 c = r.center;
  auto place = [&](ObjectKind kind, Vec2 pos, Angle16 yaw, Vec3 size, uint8_t variant, float gap) {
    Object o = village_object(kind, pos, yaw, variant);
    o.size = size;
    if (!can_place(w, o, gap, true)) return false;
    w.objects.push_back(o);
    return true;
  };

  // The tower, its door towards the path.
  const float to_path = w.path.points.empty() ? -1.0f : (distance_to_path(w.path, c.x - 1, c.z) <
                                                         distance_to_path(w.path, c.x + 1, c.z) ? -1.0f : 1.0f);
  const Angle16 door_yaw = to_path < 0 ? Angle16{16384} : Angle16{49152};  // front (-Z) turned to -X / +X
  place(ObjectKind::Tower, c, door_yaw, {1, 1, 1}, variant8(), 0.5f);

  // What is left of the square enclosure: wall pieces, an opening towards the path.
  constexpr float kHalf = 9.0f;
  for (int side = 0; side < 4; ++side) {
    for (int k = 0; k < 6; ++k) {
      const float along = -7.5f + 3.0f * static_cast<float>(k);
      const bool x_side = side < 2;  // the wall runs along Z, at x = ±kHalf
      const float sign = side % 2 ? 1.0f : -1.0f;
      const Vec2 pos = x_side ? Vec2{c.x + sign * kHalf, c.z + along} : Vec2{c.x + along, c.z + sign * kHalf};
      const bool entrance = x_side && sign == to_path && (k == 2 || k == 3);
      if (entrance || rng.below(100) < 40) continue;  // fallen long ago
      const Angle16 yaw = x_side ? Angle16{16384} : Angle16{0};
      place(ObjectKind::RuinWall, pos, yaw, {1, 1, 1}, variant8(), 0.0f);
    }
  }

  // A colonnade from the entrance towards the path, some columns fallen.
  for (int row = 0; row < 3; ++row) {
    for (float across : {-2.4f, 2.4f}) {
      const Vec2 pos{c.x + to_path * (11.0f + 2.8f * static_cast<float>(row)), c.z + across};
      if (rng.below(100) < 25) {
        const Angle16 yaw = quarter_yaw16();
        const uint8_t variant = variant8();
        place(ObjectKind::FallenPillar, {pos.x + to_path * 0.5f, pos.z + across * 0.3f}, yaw, {1, 1, 1}, variant, 0.2f);
      } else {
        const Angle16 yaw = any_yaw16();
        const float height = random_range(rng, 1.2f, 3.4f);
        const uint8_t variant = variant8();
        place(ObjectKind::Pillar, pos, yaw, {1, height, 1}, variant, 0.2f);
      }
    }
  }

  // Four rune stones in the courtyard, still glowing.
  for (int k = 0; k < 4; ++k) {
    const SinCos sc = sincos16(static_cast<Angle16>(8192 + k * 16384));
    place(ObjectKind::RuneStone, {c.x + sc.cos * 5.6f, c.z + sc.sin * 5.6f}, static_cast<Angle16>((k % 2) << 14),
          {1, 1, 1}, variant8(), 0.3f);
  }

  // Fallen columns and rubble scattered around.
  for (int n = 0, attempt = 0; n < 9 && attempt < 200; ++attempt) {
    const SinCos sc = sincos16(static_cast<Angle16>(rng.below(65536)));
    const float d = random_range(rng, 3.5f, r.radius - 1);
    const Vec2 pos{c.x + sc.cos * d, c.z + sc.sin * d};
    const bool rubble = n % 3 != 0;
    const Angle16 yaw = rubble ? any_yaw16() : quarter_yaw16();
    const uint8_t variant = variant8();
    const bool ok = place(rubble ? ObjectKind::Rubble : ObjectKind::FallenPillar, pos, yaw, {1, 1, 1}, variant, 0.4f);
    n += ok ? 1 : 0;
  }
}

namespace {

// The quarter turn whose front (local -Z) faces `dir` (a unit vector along X or Z).
Angle16 yaw_facing(Vec2 dir) {
  Angle16 best = 0;
  float best_dot = -2;
  for (Angle16 yaw : {Angle16{0}, Angle16{16384}, Angle16{32768}, Angle16{49152}}) {
    const Vec2 f = front_of(yaw);
    if (f.x * dir.x + f.z * dir.z > best_dot) {
      best_dot = f.x * dir.x + f.z * dir.z;
      best = yaw;
    }
  }
  return best;
}

// An invisible solid box: center (lx, lz) and half extents in the space of
// `o` (turned with it), from `bottom` meters above the ground.
Object collider(const Object& o, float lx, float lz, Vec3 half, float bottom = 0) {
  const Vec2 p = local_to_world(o, lx, lz);
  Object c = village_object(ObjectKind::Collider, p, o.yaw, static_cast<uint8_t>(std::lround(bottom * 10)));
  c.size = half;
  return c;
}

}  // namespace

void build_castle(World& w) {
  const CastleLayout& k = w.castle;
  if (k.radius <= 0) return;
  Rng rng(w.seed ^ 0x4B45455054555252ULL, 31);  // own stream
  std::erase_if(w.objects, [&](const Object& o) { return in_castle(k, o.pos.x, o.pos.z, 1.0f); });
  const Vec2 c = k.center;
  const Vec2 f = k.facing;            // through the gate (along X)
  const Vec2 side{-f.z, f.x};         // along the gate's wall
  constexpr float h = rules::kCastleHalf;
  auto at = [&](float along_f, float along_side) {
    return Vec2{c.x + f.x * along_f + side.x * along_side, c.z + f.z * along_f + side.z * along_side};
  };
  // Four corner towers, a little out of the corners (clear of the walls).
  for (float a : {-h - 0.3f, h + 0.3f}) {
    for (float b : {-h - 0.3f, h + 0.3f}) {
      w.objects.push_back(village_object(ObjectKind::CastleTower, at(a, b), 0, static_cast<uint8_t>(rng.below(256))));
    }
  }
  // The walls, 4 m stretches between the towers; the gatehouse in the middle of the front.
  const Angle16 front_yaw = yaw_facing(f);
  for (int wall = 0; wall < 4; ++wall) {
    for (int n = 0; n < 6; ++n) {
      const float along = -10.0f + 4.0f * static_cast<float>(n);
      const bool gate_side = wall == 0;
      if (gate_side && (n == 2 || n == 3)) continue;
      const float sign = wall % 2 == 0 ? 1.0f : -1.0f;
      const bool across = wall < 2;  // the front and back walls run along `side`
      const Vec2 pos = across ? at(sign * h, along) : at(along, sign * h);
      const Vec2 outward = across ? Vec2{f.x * sign, f.z * sign} : Vec2{side.x * sign, side.z * sign};
      w.objects.push_back(village_object(ObjectKind::CastleWall, pos, yaw_facing(outward), static_cast<uint8_t>(rng.below(256))));
    }
  }
  const Object gate = village_object(ObjectKind::CastleGate, at(h, 0), front_yaw);
  w.objects.push_back(gate);
  // The gatehouse's solid parts: both sides, and above the passage (see castle_gate in tools/make_models.py).
  w.objects.push_back(collider(gate, -2.75f, 0, {1.13f, 3.75f, 1.5f}));
  w.objects.push_back(collider(gate, 2.75f, 0, {1.13f, 3.75f, 1.5f}));
  w.objects.push_back(collider(gate, 0, 0, {1.58f, 1.65f, 1.5f}, 4.2f));
  // The keep, its door towards the gate, and a few things in the courtyard.
  w.objects.push_back(village_object(ObjectKind::CastleKeep, at(-3.0f, 0), front_yaw));
  for (int n = 0; n < 4; ++n) {
    const float along = n < 2 ? 5.5f : -9.5f;
    const float across = (n % 2 ? 1.0f : -1.0f) * (n < 2 ? 9.5f : 10.0f);
    const Object barrel = village_object(ObjectKind::Barrel, at(along, across), static_cast<Angle16>(rng.below(65536)));
    w.objects.push_back(barrel);
  }
  for (float across : {-2.6f, 2.6f}) w.objects.push_back(village_object(ObjectKind::Lamp, at(h + 3.0f, across), 0));
}

void build_cave(World& w) {
  const CaveLayout& cave = w.cave;
  if (cave.radius <= 0) return;
  Rng rng(w.seed ^ 0x47524F5454452121ULL, 37);  // own stream
  std::erase_if(w.objects, [&](const Object& o) { return in_cave_hill(cave, o.pos.x, o.pos.z, 3.0f); });
  // The mouth, its back in the cliff, and its solid rocks (see cave_mouth in tools/make_models.py).
  const Object mouth = village_object(ObjectKind::CaveMouth, cave.mouth, yaw_facing(cave.facing));
  w.objects.push_back(mouth);
  w.objects.push_back(collider(mouth, -3.4f, 2.0f, {1.3f, 3.3f, 3.2f}));
  w.objects.push_back(collider(mouth, 3.4f, 2.0f, {1.3f, 3.3f, 3.2f}));
  w.objects.push_back(collider(mouth, 0, 5.0f, {2.05f, 1.85f, 0.45f}));
  w.objects.push_back(collider(mouth, 0, 1.95f, {2.05f, 1.4f, 2.5f}, 3.8f));
  // Glowing crystals inside, and around the hill.
  // Not on ground sloping too much under it (it would float on one side).
  const auto level_enough = [&](const Object& o) {
    const Box box = object_box(o);
    float lowest = w.ground.height(o.pos.x, o.pos.z);
    for (float cx : {box.min.x, box.max.x}) {
      for (float cz : {box.min.z, box.max.z}) lowest = std::min(lowest, w.ground.height(cx, cz));
    }
    return w.ground.height(o.pos.x, o.pos.z) - lowest < 0.15f * (box.max.y - box.min.y);
  };
  auto crystal = [&](Vec2 p, float scale) {
    const auto yaw = static_cast<Angle16>(rng.below(65536));  // one draw per statement: same order everywhere
    const auto variant = static_cast<uint8_t>(rng.below(256));
    Object o = village_object(ObjectKind::Crystal, p, yaw, variant);
    o.size = {scale, scale, scale};
    if (!level_enough(o) || !can_place(w, o, 0.2f, true)) return false;
    w.objects.push_back(o);
    return true;
  };
  // Inside: against the walls, clear of the back (its collider starts at 4.1 m).
  for (const auto& [lx, lz, scale] : {std::tuple{-1.45f, 3.3f, 0.8f}, std::tuple{1.55f, 2.9f, 0.6f},
                                       std::tuple{-1.6f, 1.2f, 0.45f}}) {
    const auto yaw = static_cast<Angle16>(rng.below(65536));
    const auto variant = static_cast<uint8_t>(rng.below(256));
    Object o = village_object(ObjectKind::Crystal, local_to_world(mouth, lx, lz), yaw, variant);
    o.size = {scale, scale, scale};
    if (steep_ground(w, o.pos.x, o.pos.z) || !level_enough(o)) continue;  // (the ravine's floor can slope down to the river)
    w.objects.push_back(o);
  }
  // Around the hill: along the foot of the terrace's cliffs about the cave
  // (the round hill of hand-made worlds: around it).
  const bool cliffs = w.west.valid() && !w.west.scarp.points.empty();
  const auto at_foot = [&](Vec2 p, float lo, float hi) {
    if (!cliffs) return true;
    const float sd = scarp_side(w.west, p.x, p.z);
    return sd > lo && sd < hi && !steep_ground(w, p.x, p.z);
  };
  for (int n = 0, attempt = 0; n < 10 && attempt < 200; ++attempt) {
    const SinCos sc = sincos16(static_cast<Angle16>(rng.below(65536)));
    const float d = cliffs ? random_range(rng, 12.0f, 40.0f) : cave.radius + random_range(rng, 0.5f, 3.0f);
    const float scale = random_range(rng, 0.4f, 0.9f);
    const Vec2 p{cave.center.x + sc.cos * d, cave.center.z + sc.sin * d};
    if (sc.cos * cave.facing.x + sc.sin * cave.facing.z > 0.85f) continue;  // keep the ravine free
    if (!at_foot(p, 1.5f, 6.0f)) continue;
    if (crystal(p, scale)) ++n;
  }
  // Big rocks fallen from the cliffs.
  for (int n = 0, attempt = 0; n < 16 && attempt < 200; ++attempt) {
    const SinCos sc = sincos16(static_cast<Angle16>(rng.below(65536)));
    const float d = cliffs ? random_range(rng, 12.0f, 50.0f) : cave.radius + random_range(rng, 1.5f, 6.0f);
    const auto yaw = static_cast<Angle16>(rng.below(4) << 14);
    const auto variant = static_cast<uint8_t>(rng.below(256));
    Object o = village_object(ObjectKind::Rock, {cave.center.x + sc.cos * d, cave.center.z + sc.sin * d}, yaw, variant);
    const float sx = random_range(rng, 1.0f, 2.2f), sy = random_range(rng, 0.8f, 1.8f), sz = random_range(rng, 1.0f, 2.2f);
    o.size = {sx, sy, sz};
    if (!at_foot({o.pos.x, o.pos.z}, 2.0f, 10.0f)) continue;
    if (sc.cos * cave.facing.x + sc.sin * cave.facing.z > 0.8f) continue;  // keep the ravine free
    // Not on a slope too steep for it (it would float).
    const Box box = object_box(o);
    float lowest = w.ground.height(o.pos.x, o.pos.z);
    for (float cx : {box.min.x, box.max.x}) {
      for (float cz : {box.min.z, box.max.z}) lowest = std::min(lowest, w.ground.height(cx, cz));
    }
    if (w.ground.height(o.pos.x, o.pos.z) - lowest > 0.6f * (box.max.y - box.min.y) + 0.1f) continue;
    if (!can_place(w, o, 0.6f, true)) continue;
    w.objects.push_back(o);
    ++n;
  }
}

void build_teleporters(World& w, TeleporterId only) {
  struct Spot {
    TeleporterId id;
    Vec2 spot;   // where it should be
    Vec2 away;   // where to look further if that is taken
    bool exists;
  };
  const Vec2 vc = w.village.center;
  const float lake_side = w.village.lake.center.x > vc.x ? 1.0f : -1.0f;
  const float ruins_side = w.ruins.center.x > vc.x ? 1.0f : -1.0f;
  const float forest_side = w.forest.center.x > 0 ? 1.0f : -1.0f;  // the forest's side of the path
  const float gate = rules::kClearingRadius * outline_scale(w.forest.center, kClearingOutline, forest_side > 0 ? static_cast<float>(kPi) : 0.0f) - 5.0f;
  // To the centimeter: the outline's sine may differ in its last digits between platforms.
  const Vec2 forest_gate{std::round((w.forest.center.x - forest_side * gate) * 100.0f) / 100.0f, w.forest.center.z};
  // Before the cave: out of its notch, down on the valley's ground (not up
  // on the cliff beside it).
  Vec2 cave_spot{w.cave.mouth.x + w.cave.facing.x * 12.0f, w.cave.mouth.z + 4.0f};
  if (w.cave.radius > 0 && w.west.valid()) {
    const auto level_around = [&](Vec2 p) {  // its stones' ring, and a few meters on (where it may move to)
      for (float k = -2.0f; k <= 6.0f; k += 2.0f) {
        if (steep_ground(w, p.x + w.cave.facing.x * k, p.z + w.cave.facing.z * k) || under_water(w, p.x, p.z, 0.3f)) return false;
      }
      return true;
    };
    for (float a = 8.0f; a < 60.0f; a += 1.0f) {
      const Vec2 p{w.cave.mouth.x + w.cave.facing.x * a, w.cave.mouth.z + w.cave.facing.z * a + 4.0f};
      if (scarp_side(w.west, p.x, p.z) > 6.0f && level_around(p)) {
        cave_spot = p;
        break;
      }
    }
  }
  const Spot places[] = {
      // On the village square, on the side away from the lake.
      {TeleporterId::Village, {vc.x - lake_side * 7.0f, vc.z + 6.0f}, {-lake_side, 0}, w.village.lake.radius_x > 0},
      // Before the ruins' entrance, towards the path.
      {TeleporterId::Ruins, {w.ruins.center.x - ruins_side * 20.0f, w.ruins.center.z - 5.0f}, {0, -1}, w.ruins.radius > 0},
      // At the edge of the clearing, on the side of the path (the giant lives in its middle).
      {TeleporterId::Forest, forest_gate, {forest_side, 0}, w.forest.radius_x > 0},
      // Out of the ravine of the cave.
      {TeleporterId::Cave, cave_spot, w.cave.facing,
       w.cave.radius > 0},
      // Before the castle's gate, beside the way in.
      {TeleporterId::Castle, {w.castle.center.x + w.castle.facing.x * (rules::kCastleHalf + 8.0f), w.castle.center.z + 5.0f},
       w.castle.facing, w.castle.radius > 0},
  };
  for (const Spot& p : places) {
    if (!p.exists || (only != TeleporterId::Count && p.id != only)) continue;
    // The first free spot along `away` (its stones need about 2 m around);
    // in the village, around the square, between the well and the houses.
    Vec2 at = p.spot;
    for (int step = 0; step < 24; ++step) {
      Vec2 candidate{p.spot.x + p.away.x * 2.0f * static_cast<float>(step % 12),
                     p.spot.z + p.away.z * 2.0f * static_cast<float>(step % 12)};
      if (p.id == TeleporterId::Village) {  // two rings around the square, every 30°
        const SinCos sc = sincos16(static_cast<Angle16>((step % 12) * 5461));
        const float r = step < 12 ? 6.0f : 7.5f;
        candidate = {vc.x - lake_side * sc.cos * r, vc.z + sc.sin * r};
      }
      Object probe = village_object(ObjectKind::Collider, candidate, 0);
      probe.size = {1.5f, 0.5f, 1.5f};
      if (can_place(w, probe, 0.2f, true)) {
        at = candidate;
        break;
      }
    }
    // Trees and rocks around it give way.
    std::erase_if(w.objects, [&](const Object& o) {
      const float dx = o.pos.x - at.x, dz = o.pos.z - at.z;
      return o.kind <= ObjectKind::Crate && dx * dx + dz * dz < 4.5f * 4.5f;
    });
    w.objects.push_back(village_object(ObjectKind::Teleporter, at, 0, static_cast<uint8_t>(p.id)));
  }
}

bool teleporter_position(const World& w, TeleporterId id, Vec3& at) {
  // Inside (interior.hpp), the world outside as it was left: its teleporters
  // are still there (not found anew on coming out).
  const bool in = w.place.kind != PlaceKind::Outside;
  const Heightmap& ground = in ? w.outdoors.ground : w.ground;
  for (const Object& o : in ? w.outdoors.objects : w.objects) {
    if (o.kind == ObjectKind::Teleporter && o.variant == static_cast<uint8_t>(id)) {
      at = {o.pos.x, ground.height(o.pos.x, o.pos.z), o.pos.z};
      return true;
    }
  }
  return false;
}

Zone zone_at(const World& w, float x, float z) {
  switch (w.place.kind) {
    case PlaceKind::Cave: return Zone::Cave;
    case PlaceKind::House: return Zone::Village;
    case PlaceKind::Tower: return Zone::Ruins;
    case PlaceKind::Burrow: return Zone::Plains;
    case PlaceKind::HollowTree: return Zone::Woods;
    case PlaceKind::Lighthouse: return Zone::Coast;
    case PlaceKind::Mine: return Zone::Mine;
    case PlaceKind::Temple: return Zone::Frost;
    default: break;
  }
  if (in_lake(w.village.lake, x, z, 1.5f)) return Zone::Lake;
  if (w.village.lake.radius_x > 0 && in_village(w.village, x, z, 2.0f)) return Zone::Village;
  if (in_ruins(w.ruins, x, z, 3.0f)) return Zone::Ruins;
  if (in_castle(w.castle, x, z, 3.0f)) return Zone::Castle;
  if (in_cave_hill(w.cave, x, z, 10.0f)) return Zone::Cave;
  if (in_forest(w.forest, x, z)) return Zone::Forest;
  // The west's places.
  if (in_farm(w.west, x, z, 8.0f)) return Zone::Farm;
  if (w.west.valid() && distance_to_stream(w.west.stream, x, z, rules::kStreamChannel + 4.0f) < rules::kStreamChannel + 3.0f) {
    return Zone::River;  // on its banks, in its water
  }
  if (in_crater(w.west, x, z, 6.0f)) return Zone::Crater;
  if (in_woods(w.west, x, z)) return Zone::Woods;
  // Beyond the valley: the wilds.
  if (std::max(std::fabs(x), std::fabs(z)) < rules::kValleyHalfSize + 10) return Zone::Meadow;
  // The north (north.hpp), its belts wavy (the continent's ground is low up
  // there, near the sea: the terrace lifts it; the shore only where it is low).
  const bool shore = w.ground.land_height(x, z) < rules::kSeaLevel + 1.0f;
  if (w.north.valid() && z < rules::kNorthFrom && (!shore || w.ground.height(x, z) > rules::kSeaLevel + 3.0f)) {
    if (in_frozen_lake(w.north, x, z, 6.0f)) return Zone::FrozenLake;
    const float wave = 40.0f * (value_noise(w.seed + 820, x, z, 120.0f) - 0.5f);
    if (z + wave > rules::kLarchesFrom) return Zone::Alpine;
    if (z + wave > rules::kFrostFrom) return Zone::Larches;
    return Zone::Frost;
  }
  if (shore) return Zone::Coast;
  if (z < -260.0f) return Zone::Hills;
  return Zone::Plains;
}

WizardStats wizard_stats(const Wizard& wiz) {
  const auto l = static_cast<uint16_t>(std::max<uint16_t>(wiz.level, 1) - 1);  // levels gained
  return {static_cast<uint16_t>(30 + 6 * l),  static_cast<uint16_t>(20 + 5 * l), static_cast<uint16_t>(6 + 2 * l),
          static_cast<uint16_t>(5 + 2 * l),   static_cast<uint16_t>(5 + l),     static_cast<uint16_t>(5 + l),
          100u * (l + 1u)};
}

const MonsterStats& monster_stats(Species species) {
  //                               hp atk def spd mag element
  static constexpr MonsterStats kStats[] = {
      {32, 9, 6, 11, 8, Element::Fire, true},     // Companion: Pompon, fox of embers
      {40, 5, 9, 4, 6, Element::Water, true},     // Slime
      {34, 6, 8, 3, 9, Element::Nature, true},    // Mushroom
      {22, 7, 4, 13, 5, Element::Air, true},      // Bird
      {0, 0, 0, 0, 0, Element::Light, false},     // Villager: not a monster
      {24, 8, 5, 12, 9, Element::Shadow, true, Element::Air},  // Crow
      {28, 6, 6, 8, 7, Element::Water, true},     // Fish
      {26, 8, 5, 12, 9, Element::Electric, true}, // Spark: Zapou
      {36, 6, 8, 7, 8, Element::Water, true},     // Axolotl: Ploufi
      {30, 8, 9, 9, 10, Element::Arcane, true},   // Cristi: Cristi
      {28, 9, 6, 12, 7, Element::Shadow, true},   // Raccoon: Chipou, a masked thief
      {26, 7, 6, 11, 11, Element::Air, true},     // Owl: Hulou
      {42, 6, 10, 6, 7, Element::Normal, true},   // Sheep: Laini, a woolly cushion
      {34, 8, 9, 6, 6, Element::Nature, true},    // Mole: Taupi
      {46, 9, 13, 4, 6, Element::Earth, true},    // Golem: Caillou, a little golem of stones
      {36, 11, 8, 11, 6, Element::Earth, true, Element::Ice},  // Goat: Cabri, of the snowy slopes
      {27, 8, 6, 14, 11, Element::Ice, true},     // Ermine: Givri, of frost
  };
  static_assert(std::size(kStats) == static_cast<size_t>(Species::Count));
  const auto i = static_cast<size_t>(species);
  return kStats[i < std::size(kStats) ? i : 0];
}

std::vector<Vec3> crow_perches(const World& w) {
  std::vector<Vec3> perches;
  auto add = [&](const Object& o, float lx, float y, float lz) {
    const Vec2 p = local_to_world(o, lx, lz);
    perches.push_back({p.x, o.pos.y + y, p.z});
  };
  for (const Object& o : w.objects) {
    switch (o.kind) {
      case ObjectKind::HouseCottage: add(o, -1.6f, 4.33f, 0); add(o, 1.6f, 4.33f, 0); break;
      case ObjectKind::HouseChimney: add(o, -1.1f, 5.63f, 0); add(o, 0.4f, 5.63f, 0); break;
      case ObjectKind::HouseHut: add(o, 0, 4.22f, 0); break;
      case ObjectKind::Well: add(o, 0, 2.45f, 0); break;
      case ObjectKind::Tower:  // three merlons left on the broken top
        add(o, 2.15f, 10.5f, 0);
        add(o, -1.075f, 10.5f, 1.862f);
        add(o, -1.075f, 10.5f, -1.862f);
        break;
      case ObjectKind::CastleTower: add(o, 1.75f, 9.6f, 0); add(o, -1.75f, 9.6f, 0); break;  // merlons
      case ObjectKind::CastleKeep: add(o, -4.0f, 12.6f, -4.0f); add(o, 4.0f, 12.6f, 4.0f); break;
      default: break;
    }
  }
  return perches;
}

bool is_round(ObjectKind kind) {
  return kind == ObjectKind::Tree || kind == ObjectKind::Pine || kind == ObjectKind::Stump ||
         kind == ObjectKind::Bush || kind == ObjectKind::HouseHut || kind == ObjectKind::Well ||
         kind == ObjectKind::Barrel || kind == ObjectKind::Lamp || kind == ObjectKind::Tower ||
         kind == ObjectKind::Pillar || kind == ObjectKind::Rubble || kind == ObjectKind::CastleTower ||
         kind == ObjectKind::Crystal || kind == ObjectKind::Teleporter || kind == ObjectKind::FruitTree ||
         kind == ObjectKind::Haystack || kind == ObjectKind::Windmill || kind == ObjectKind::Campfire ||
         kind == ObjectKind::HollowTree || kind == ObjectKind::Lighthouse || kind == ObjectKind::Menhir ||
         kind == ObjectKind::Meteorite || kind == ObjectKind::Larch || kind == ObjectKind::StoneWall ||
         kind == ObjectKind::Cairn || kind == ObjectKind::HotSpring || kind == ObjectKind::MineProp ||
         kind == ObjectKind::Beehive;
}

Box object_box(const Object& o) {
  const Vec3 s = o.size;
  Vec3 half;
  switch (o.kind) {
    case ObjectKind::Tree:  half = {0.28f * s.x, 1.6f * s.x, 0.28f * s.x}; break;  // trunk only
    case ObjectKind::Pine:  half = {0.25f * s.x, 1.6f * s.x, 0.25f * s.x}; break;
    case ObjectKind::Rock:  half = {0.85f * s.x, 0.5f * s.y, 0.85f * s.z}; break;
    case ObjectKind::Log:
      // Along its yaw; turned any way, solid through its pieces (add_log_pieces): walked onto.
      half = (o.yaw & 0x3FFF) != 0 ? Vec3{0, s.y, 0} : Vec3{s.x, s.y, s.y};
      break;
    case ObjectKind::Stump: half = {s.x, 0.5f * s.y, s.x}; break;
    case ObjectKind::Bush:  half = {0.8f * s.x, 0.5f * s.y, 0.8f * s.x}; break;
    // Village: fixed sizes, matching the models of tools/make_models.py.
    case ObjectKind::HouseCottage: half = {2.5f, 2.15f, 2.0f}; break;
    case ObjectKind::HouseChimney: half = {2.25f, 2.8f, 2.25f}; break;
    case ObjectKind::HouseHut:     half = {2.0f, 2.1f, 2.0f}; break;
    case ObjectKind::Fence:        half = {0.995f, 0.45f, 0.07f}; break;  // side by side: never overlapping
    case ObjectKind::Sign:         half = {0.12f, 0.9f, 0.12f}; break;
    case ObjectKind::Well:         half = {0.85f, 0.45f, 0.85f}; break;
    case ObjectKind::Barrel:       half = {0.35f, 0.45f, 0.35f}; break;
    case ObjectKind::Lamp:         half = {0.1f, 1.25f, 0.1f}; break;
    // Ruins: matching the models of tools/make_models.py.
    case ObjectKind::Tower:        half = {2.35f, 0.5f * rules::kRoofHeight, 2.35f}; break;  // up to its broken top
    case ObjectKind::RuinWall:     half = o.variant & 1 ? Vec3{1.5f, 1.1f, 0.28f} : Vec3{1.5f, 0.5f, 0.28f}; break;
    case ObjectKind::Pillar:       half = {0.4f, 0.5f * s.y, 0.4f}; break;
    case ObjectKind::FallenPillar: half = {1.5f, 0.38f, 0.38f}; break;
    case ObjectKind::Rubble:       half = {0.75f, 0.3f, 0.75f}; break;
    case ObjectKind::RuneStone:    half = {0.35f, 0.95f, 0.18f}; break;
    // Castle and cave: matching the models of tools/make_models.py.
    case ObjectKind::CastleWall:   half = {1.99f, 2.5f, 0.6f}; break;  // stretches side by side do not overlap
    case ObjectKind::CastleTower:  half = {2.2f, 4.5f, 2.2f}; break;
    case ObjectKind::CastleKeep:   half = {4.5f, 6.0f, 4.5f}; break;
    case ObjectKind::CastleGate:   // solid through its Colliders
    case ObjectKind::CaveMouth:
    case ObjectKind::Teleporter:   half = {0, 0, 0}; break;  // walked onto
    case ObjectKind::Crystal:      half = {0.35f * s.x, 0.5f * s.x, 0.35f * s.x}; break;
    // Inside (interior.cpp): matching the models of tools/make_models.py.
    case ObjectKind::Table:        half = {0.7f, 0.375f, 0.4f}; break;
    case ObjectKind::Chair:        half = {0.23f, 0.45f, 0.23f}; break;
    case ObjectKind::Bed:          half = {0.5f, 0.3f, 1.0f}; break;
    case ObjectKind::Shelf:        half = {0.6f, 0.9f, 0.2f}; break;
    case ObjectKind::Hearth:       half = {0.8f, 0.7f, 0.35f}; break;
    case ObjectKind::Rug:
    case ObjectKind::Hatch:        half = {0, 0, 0}; break;  // walked over
    // The west (west.hpp): matching the models of tools/make_models.py.
    case ObjectKind::Cart:         half = {1.5f, 0.75f, 0.95f}; break;
    case ObjectKind::Bridge:       half = s; break;  // its deck on top (build_west)
    case ObjectKind::Farmhouse:    half = {3.6f, 3.1f, 3.0f}; break;
    case ObjectKind::Barn:         half = {4.5f, 3.4f, 3.6f}; break;
    case ObjectKind::FruitTree:    half = {0.22f * s.x, 1.3f * s.x, 0.22f * s.x}; break;
    case ObjectKind::Crops:        half = {0, 0, 0}; break;  // walked through
    case ObjectKind::Haystack:     half = {0.9f, 0.7f, 0.9f}; break;
    case ObjectKind::Windmill:     half = {2.6f, 4.6f, 2.6f}; break;
    case ObjectKind::Cabin:        half = {3.0f, 2.1f, 2.5f}; break;
    case ObjectKind::Tent:         half = {1.4f, 0.9f, 1.6f}; break;
    case ObjectKind::Campfire:     half = {0.55f, 0.18f, 0.55f}; break;
    case ObjectKind::Woodpile:     half = {1.4f, 0.55f, 0.45f}; break;
    case ObjectKind::HollowTree:   half = {4.2f, 0.5f * rules::kHollowTreeTop, 4.2f}; break;  // up to its crown
    case ObjectKind::Burrow:       half = {2.0f, 0.75f, 2.0f}; break;
    case ObjectKind::Lighthouse:   half = {2.6f, 0.5f * rules::kLighthouseGallery, 2.6f}; break;  // up to its gallery
    case ObjectKind::Menhir:       half = {0.45f, 0.5f * s.y, 0.45f}; break;
    case ObjectKind::Meteorite:    half = {0.9f * s.x, 0.7f * s.x, 0.9f * s.x}; break;
    case ObjectKind::WaterMill:    half = {3.0f, 2.4f, 3.0f}; break;  // turned along the river: square
    case ObjectKind::HiddenCave:   half = {0, 0, 0}; break;  // a cleft in the cliff: walked into
    // The north (north.hpp): matching the models of tools/make_models.py.
    case ObjectKind::Chalet:       half = {3.6f, 2.5f, 3.0f}; break;
    case ObjectKind::MineEntrance: half = {0, 0, 0}; break;  // walked into (its rocks: Colliders)
    case ObjectKind::Minecart:     half = {0.75f, 0.5f, 0.5f}; break;
    case ObjectKind::Larch:        half = {0.22f * s.x, 1.6f * s.x, 0.22f * s.x}; break;
    case ObjectKind::StoneWall:    half = {0.37f, 0.5f * rules::kWallHeight, 0.37f}; break;  // turned any way: square, stretches never overlapping
    case ObjectKind::Cairn:        half = {0.4f * s.x, 0.65f * s.x, 0.4f * s.x}; break;
    case ObjectKind::HotSpring:    half = {0, 0, 0}; break;  // walked into
    case ObjectKind::MineProp:     half = {0.13f, 1.35f, 0.13f}; break;  // its post
    case ObjectKind::Arena:        // solid through its Colliders
    case ObjectKind::Grotto:       half = {0, 0, 0}; break;  // walked into (its rocks: Colliders)
    case ObjectKind::Shrine:       half = {0.4f, 0.95f, 0.4f}; break;
    case ObjectKind::Bench:        half = {0.8f, 0.25f, 0.25f}; break;
    case ObjectKind::Stall:        half = {1.3f, 1.2f, 0.8f}; break;
    // The sites (sites.hpp): matching the models of tools/make_models.py.
    case ObjectKind::Pier:         half = s; break;  // its deck (build_sites sets it at the quay's level)
    case ObjectKind::Boat:         half = {2.4f, 0.55f, 0.95f}; break;
    case ObjectKind::FishRack:     half = {1.3f, 0.8f, 0.3f}; break;
    case ObjectKind::AbbeyWall:    half = {1.99f, 2.1f, 0.5f}; break;  // stretches side by side do not overlap
    case ObjectKind::AbbeyGate:    half = {0, 0, 0}; break;  // walked through (its sides: Colliders)
    case ObjectKind::Temple:       half = {12.0f, 6.5f, 9.5f}; break;
    case ObjectKind::Belfry:       half = {2.5f, 8.0f, 2.5f}; break;
    case ObjectKind::Dorter:       half = {11.0f, 3.2f, 3.6f}; break;
    case ObjectKind::Gravestone:   half = {0.3f, 0.45f, 0.12f}; break;
    case ObjectKind::Chapel:       half = {3.2f, 3.0f, 4.6f}; break;
    case ObjectKind::Crane:        half = {0.9f, 3.0f, 0.9f}; break;  // its mast
    case ObjectKind::Block:        half = s; break;
    case ObjectKind::Beehive:      half = {0.35f, 0.55f, 0.35f}; break;
    case ObjectKind::Dolmen:       half = {2.0f, 1.0f, 1.2f}; break;
    case ObjectKind::Wreck:        half = {6.0f, 1.6f, 2.0f}; break;
    case ObjectKind::Aqueduct:     half = {0, 0, 0}; break;  // walked under (its piers: Colliders)
    default:                half = s; break;  // Crate, Collider, Step
  }
  if (o.mount != 0) half = {0, 0, 0};  // on a wall or the ceiling: out of the way
  if (!is_round(o.kind) && ((o.yaw >> 14) & 1)) std::swap(half.x, half.z);  // turned 90° or 270°
  const Vec3 center = o.pos + Vec3{0, half.y, 0};
  return {center - half, center + half};
}

bool overlaps(const Box& a, const Box& b) {
  return a.min.x < b.max.x && a.max.x > b.min.x && a.min.y < b.max.y && a.max.y > b.min.y &&
         a.min.z < b.max.z && a.max.z > b.min.z;
}

const ObjectIndex& objects_index(const World& w) {
  ObjectIndex& ix = w.object_index;
  const Vec3 last = w.objects.empty() ? Vec3{} : w.objects.back().pos;
  if (ix.count == w.objects.size() && ix.data == w.objects.data() && ix.last == last) return ix;
  ix.count = w.objects.size();
  ix.data = w.objects.data();
  ix.last = last;
  ix.boxes.resize(w.objects.size());
  for (size_t i = 0; i < w.objects.size(); ++i) ix.boxes[i] = object_box(w.objects[i]);
  // Counting sort: how many per cell, then where each cell's start, then fill.
  constexpr size_t kCellCount = static_cast<size_t>(ObjectIndex::kCellsX) * ObjectIndex::kCellsZ;
  const auto each_cell = [](const Box& b, auto f) {
    for (int cx = ObjectIndex::cell_x(b.min.x); cx <= ObjectIndex::cell_x(b.max.x); ++cx) {
      for (int cz = ObjectIndex::cell_z(b.min.z); cz <= ObjectIndex::cell_z(b.max.z); ++cz) {
        f(static_cast<size_t>(cx * ObjectIndex::kCellsZ + cz));
      }
    }
  };
  ix.cell_start.assign(kCellCount + 1, 0);
  for (const Box& b : ix.boxes) each_cell(b, [&](size_t c) { ++ix.cell_start[c + 1]; });
  for (size_t c = 1; c <= kCellCount; ++c) ix.cell_start[c] += ix.cell_start[c - 1];
  ix.items.resize(ix.cell_start.back());
  std::vector<uint32_t> fill(ix.cell_start.begin(), ix.cell_start.end() - 1);
  for (size_t i = 0; i < ix.boxes.size(); ++i) {
    each_cell(ix.boxes[i], [&](size_t c) { ix.items[fill[c]++] = static_cast<uint32_t>(i); });
  }
  return ix;
}

bool inside_any_object(const World& w, const Body& b, const BodyShape& shape) {
  const Box body = body_box(b, shape);
  bool hit = false;
  for_objects_near(w, body, [&](uint32_t, const Box& o) { hit = hit || overlaps(body, o); });
  return hit;
}

void build_layouts(World& w) {
  w.path = make_path(w.seed);
  w.village = make_village_layout(w.seed, w.path);
  w.ruins = make_ruins_layout(w.seed, w.path);
  w.forest = make_forest_layout(w.seed, w.path, w.ruins);
  w.cave = make_cave_layout(w.seed, w.path);
  w.castle = make_castle_layout(w.seed, w.path);
  w.west = make_west_layout(w.seed, w.path, w.village, w.ruins, w.forest, w.cave, w.castle);
  w.north = make_north_layout(w.seed, w.path, w.west);
  w.roads = make_roads(w.seed, w.path, w.village, w.ruins, w.forest, w.cave, w.castle, w.west, w.north, w.sites);  // (and the sites)
  w.ground = make_heightmap(w.seed, w.path, {w.village, w.ruins, w.cave, w.castle, &w.west, &w.roads});
  shape_north(w.ground, w.north, w.path, w.seed);
  shape_sites(w.ground, w.sites, w.seed, w.path, w.north);
  if (w.sites.abbey_way.size() >= 6) set_lane(w.roads, LaneTo::AbbeyWay, w.sites.abbey_way);  // (found again over the shaped ground)
  join_waters(w);  // no water shut in inland (landfix.hpp)
}

void regrow_forest(World& w) {
  if (w.forest.radius_x <= 0 || w.ground.empty()) return;
  // Its trees, rocks, logs... and its teleporter go (the wilds are not there yet).
  std::erase_if(w.objects, [&](const Object& o) {
    const bool nature = o.kind <= ObjectKind::Crate;
    const bool its_teleporter = o.kind == ObjectKind::Teleporter && o.variant == static_cast<uint8_t>(TeleporterId::Forest);
    return (nature && in_forest(w.forest, o.pos.x, o.pos.z, 1.0f)) || its_teleporter;
  });
  build_teleporters(w, TeleporterId::Forest);
  const size_t first = w.objects.size();
  Rng rng(w.seed ^ 0x524547524F575321ULL, 41);  // "REGROWS!": its own stream
  generate_objects(w, rng, true);
  settle_objects(w, first);
  for (Object& o : w.objects) {
    if (o.kind == ObjectKind::Teleporter) o.pos.y = w.ground.height(o.pos.x, o.pos.z) - 0.05f;
  }
}

World new_game(uint64_t seed) {
  World w;
  w.seed = seed;
  w.rng = Rng(seed);
  build_layouts(w);
  // Objects are placed as if on flat ground, then set on the relief.
  build_village(w);
  build_ruins(w);
  build_castle(w);
  build_cave(w);
  build_teleporters(w);
  generate_objects(w, w.rng);
  settle_objects(w);
  generate_wilds(w);
  w.player.pos.y = w.ground.height(0, 0);
  spawn_creatures(w);
  capture(w, 0);  // the companion is the player's own
  w.team = {starting_companion(w.creatures[0].genes)};
  give_traits(w.team[0], roll_traits(w.seed, w.creatures[0].id, Species::Companion, w.team[0].genes), 0);
  return w;
}

bool valid_look(const WizardLook& l) {
  return l.skin < look::kSkins && l.hair < look::kHairStyles && l.hair_color < look::kHairColors &&
         l.eyes < look::kEyeColors && l.staff <= look::kWand && l.robe < look::kRobeColors &&
         l.headwear <= look::kBareHead;
}

uint32_t look_code(const WizardLook& l) {
  return (l.woman ? 1u : 0u) | (uint32_t{l.skin} & 7u) << 1 | (uint32_t{l.hair} & 3u) << 4 |
         (uint32_t{l.hair_color} & 7u) << 6 | (uint32_t{l.eyes} & 7u) << 9 | (uint32_t{l.staff} & 1u) << 12 |
         (uint32_t{l.robe} & 7u) << 13 | (uint32_t{l.headwear} & 3u) << 16;
}

WizardLook look_from_code(uint32_t c) {
  WizardLook l;
  l.woman = (c & 1u) != 0;
  l.skin = static_cast<uint8_t>((c >> 1) & 7u);
  l.hair = static_cast<uint8_t>((c >> 4) & 3u);
  l.hair_color = static_cast<uint8_t>((c >> 6) & 7u);
  l.eyes = static_cast<uint8_t>((c >> 9) & 7u);
  l.staff = static_cast<uint8_t>((c >> 12) & 1u);
  l.robe = static_cast<uint8_t>((c >> 13) & 7u);
  l.headwear = static_cast<uint8_t>((c >> 16) & 3u);
  return l;
}

bool valid_starter(Species species) {
  return std::find(std::begin(kStarters), std::end(kStarters), species) != std::end(kStarters);
}

World new_game(uint64_t seed, const NewGame& choices) {
  World w = new_game(seed);
  w.wizard.name = choices.name;
  if (valid_look(choices.look)) w.wizard.look = choices.look;
  const Species starter = valid_starter(choices.starter) ? choices.starter : Species::Companion;
  // The first monster: of the magician's sex, the most common of its kind.
  Genes genes;
  genes.allele[0] = w.wizard.look.woman ? 1 : 0;
  Creature& c = w.creatures[0];
  c.species = starter;
  c.genes = genes;
  w.dex[static_cast<size_t>(Species::Companion)] = {};
  capture(w, 0);
  Monster m = starting_companion(genes);
  m.species = starter;
  m.hp = monster_fight_stats(m).hp;
  give_traits(m, roll_traits(w.seed, c.id, starter, genes), 0);
  w.team = {m};
  return w;
}

namespace {

// A beaten monster lies on the ground (a crow falls from the air, a fish
// floats), then gets up and runs home, or flies away.
void step_knocked_out(World& w, const Surroundings& s, Creature& c) {
  if (c.species != Species::Fish) move_body(s, c.body, species_info(c.species).shape, 0, 0, 0, false, 0);
  c.body.speed = 0;
  if (c.timer > 0 && --c.timer > 0) return;
  switch (c.species) {
    case Species::Crow:
    case Species::Owl:
      c.mind = Mind::Idle;  // takes off at once
      c.timer = 0;
      break;
    case Species::Fish:
      c.mind = Mind::Idle;
      c.timer = rules::kTicksPerSecond;
      break;
    default:
      c.mind = Mind::Flee;
      c.target = c.home;
      c.timer = 4 * rules::kTicksPerSecond;
      break;
  }
  (void)w;
}

}  // namespace

namespace {

// Mirabelle's Piou, back with her: it stays at her side; Bastien's monster too.
void step_pet(World& w, const Surroundings& s, Creature& c, uint8_t owner_npc) {
  const int owner = npc_index(w, owner_npc);
  if (owner < 0) return;
  const Body& o = w.creatures[static_cast<size_t>(owner)].body;
  const float side = owner_npc == npc::kMirabelle ? 1.0f : -1.0f;  // on her left, on the trainer's right
  const Vec2 spot{o.pos.x - o.facing_z * 0.9f * side, o.pos.z + o.facing_x * 0.9f * side};
  float dx, dz;
  const float d = direction_to(c.body.pos.x, c.body.pos.z, spot, dx, dz);
  const SpeciesInfo& info = species_info(c.species);
  c.home = {o.pos.x, o.pos.z};
  if (d > 0.5f) {
    c.mind = Mind::Follow;
    move_body(s, c.body, info.shape, dx, dz, d > 4 ? info.run_speed : info.walk_speed, false, info.jump_speed);
  } else {
    c.mind = Mind::Idle;
    move_body(s, c.body, info.shape, 0, 0, 0, false, info.jump_speed);
    float tx, tz;
    if (direction_to(c.body.pos.x, c.body.pos.z, {o.pos.x, o.pos.z}, tx, tz) > 0.1f) {
      c.body.facing_x = tx;
      c.body.facing_z = tz;
    }
  }
  if (d > rules::kCompanionTeleportDistance) {  // left behind: back at once
    c.body.pos = {spot.x, w.ground.height(spot.x, spot.z), spot.z};
    if (inside_any_object(w, c.body, info.shape)) c.body.pos = o.pos;
  }
}

// A lost lamb found by the player trots after it (west.hpp), a little behind.
void step_lamb(World& w, const Surroundings& s, Creature& c) {
  const Body& p = w.player;
  const SpeciesInfo& info = species_info(c.species);
  const Vec2 spot{p.pos.x - p.facing_x * 2.0f + p.facing_z * 0.8f, p.pos.z - p.facing_z * 2.0f - p.facing_x * 0.8f};
  float dx, dz;
  const float d = direction_to(c.body.pos.x, c.body.pos.z, spot, dx, dz);
  if (d > 30.0f) {  // left far behind: it waits there
    c.mind = Mind::Idle;
    move_body(s, c.body, info.shape, 0, 0, 0, false, info.jump_speed);
    return;
  }
  const bool go = d > 0.8f;
  const bool stuck = go && c.body.on_ground && c.timer >= 10;
  move_body(s, c.body, info.shape, go ? dx : 0, go ? dz : 0, d > 4 ? info.run_speed : info.walk_speed, stuck,
            info.jump_speed);
  c.timer = (go && c.body.on_ground && c.body.speed < 0.3f) ? static_cast<uint16_t>(c.timer + 1) : 0;
  if (go) {
    c.body.facing_x = dx;
    c.body.facing_z = dz;
  }
}

}  // namespace

void step(World& w, const Input& in) {
  ++w.tick;
  if (sealing(w)) {  // capturing: the world holds still, but the runes
    seal_step(w, in);
    return;
  }
  w.clock = (w.clock + 1) % rules::kTicksPerDay;
  quest_command(w, in);   // teleports, quests accepted or done
  west_command(w, in);    // the bag, the merchant, the farm
  if (in.command == Command::Door) use_door(w);  // in, out (interior.hpp)
  battle_command(w, in);  // may start or end a battle, or remove a captured monster
  const BattlePhase phase = w.battle.phase;
  // The companion is steered by the joystick (fighting or training).
  const bool steered = phase == BattlePhase::Fight || phase == BattlePhase::Training;

  // Joystick -> direction on the ground, relative to the camera (in a fight:
  // to the line from the companion to its enemy).
  float mx = static_cast<float>(std::max<int>(in.move_x, -127)) / 127.0f;
  float my = static_cast<float>(std::max<int>(in.move_y, -127)) / 127.0f;
  const float len2 = mx * mx + my * my;
  if (len2 > 1.0f) {
    const float len = std::sqrt(len2);
    mx /= len;
    my /= len;
  }
  float forward_x, forward_z;
  const bool aimed = battle_forward(w, forward_x, forward_z);
  bool climbed = false;  // Cristi steered on the walls (World::climbing)
  if (!aimed) {
    const SinCos sc = sincos16(in.yaw);
    forward_x = -sc.sin;  // forward = (-sin, 0, -cos), right = (cos, 0, -sin)
    forward_z = -sc.cos;
  }
  const float dir_x = -mx * forward_z + my * forward_x;
  const float dir_z = mx * forward_x + my * forward_z;
  static const Lake kNoLake{};
  Surroundings s{w, inside(w) ? kNoLake : w.village.lake, w.ground, inside(w) ? &w.interior : nullptr};

  // During a battle the magician waits: the joystick moves the companion
  // (before a trainer's battle, the magician walks to its place).
  if (phase == BattlePhase::None) {
    move_body(s, w.player, kPlayerShape, dir_x, dir_z, rules::kWalkSpeed, in.jump, rules::kJumpSpeed);
  } else {
    const Steer st = battle_prepare(w, Stand::Magician);
    move_body(s, w.player, kPlayerShape, st.dir_x, st.dir_z, st.speed, false, 0);
  }
  std::vector<Vec3> perches, owls;
  for (size_t i = 0; i < w.creatures.size(); ++i) {
    Creature& c = w.creatures[i];
    if (i == 0 && w.active < 0) {  // in the crystal: it waits by the player, unseen
      c.body.pos = w.player.pos;
      continue;
    }
    const int foe = i > 0 ? foe_slot(w, i) : -1;
    const bool preparing = phase == BattlePhase::Prepare && (i == 0 || foe >= 0);
    // Cristi climbs inside (steered, or wild and waiting); else it lets go and drops to the floor.
    // Cristi climbs on everything when steered; wild ones, in the cave.
    const bool climbs = c.species == Species::Cristi &&
                        ((i == 0 && steered) || (inside(w) && i > 0 && foe < 0 && c.mind != Mind::KnockedOut));
    if (!climbs && clinging(c.body)) {
      c.body.up = {0, 1, 0};
      c.body.facing_y = 0;
      c.body.on_ground = false;
      const float l = std::sqrt(c.body.facing_x * c.body.facing_x + c.body.facing_z * c.body.facing_z);
      c.body.facing_x = l > 1e-3f ? c.body.facing_x / l : 0.0f;
      c.body.facing_z = l > 1e-3f ? c.body.facing_z / l : -1.0f;
    }
    if (climbs && i == 0) {
      const Steer st = battle_ally(w, in, dir_x, dir_z);
      // On the floor it jumps as the others do (and falls back before it climbs again).
      if (!clinging(c.body) && (st.jump || !c.body.on_ground)) {
        const SpeciesInfo& info = species_info(c.species);
        move_body(s, c.body, info.shape, st.dir_x, st.dir_z, st.speed, st.jump, info.jump_speed);
        continue;
      }
      // The joystick's way, along the wall or the ceiling it clings to.
      const float f = st.dir_x * forward_x + st.dir_z * forward_z;
      const float r = -st.dir_x * forward_z + st.dir_z * forward_x;
      climb_steered(w, c.body, forward_x, forward_z, aimed, in.yaw, r, f, st.speed);
      climbed = true;
      continue;
    }
    if ((steered && i == 0) || foe >= 0 || preparing) {
      if (i > 0) c.act = Activity::None;  // awake, fighting
      const Steer st = preparing ? battle_prepare(w, i == 0 ? Stand::Ally : Stand::Foe)
                       : i == 0  ? battle_ally(w, in, dir_x, dir_z)
                                 : battle_foe(w, static_cast<size_t>(foe));
      const SpeciesInfo& info = species_info(c.species);
      if (i != 0 && holds_its_place(c.species)) continue;  // in the lake
      if (i != 0 && st.fly) {
        fly_free(s, c.body, st);
      } else if (const float hover = i == 0 ? companion_hover(c.species) : 0.0f; hover > 0) {
        move_flyer(s, c.body, info.shape, st.dir_x, st.dir_z, st.speed, hover + (steered ? w.battle.ally.lift : 0.0f),
                   flies(c.species));
      } else if (c.species == Species::Slime && st.speed > 0) {  // a slime goes by hopping, steered too
        move_body(s, c.body, info.shape, st.dir_x, st.dir_z, c.body.on_ground ? 0.0f : st.speed, true, info.jump_speed);
      } else {
        move_body(s, c.body, info.shape, st.dir_x, st.dir_z, st.speed, st.jump, info.jump_speed);
      }
      continue;
    }
    if (i == 0 && c.mind == Mind::KnockedOut) {
      // Knocked out: it lies there until healed; too far behind, it goes back into the crystal.
      move_body(s, c.body, species_info(c.species).shape, 0, 0, 0, false, 0);
      c.body.speed = 0;
      const float dx = c.body.pos.x - w.player.pos.x, dz = c.body.pos.z - w.player.pos.z;
      if (dx * dx + dz * dz > rules::kKnockedOutRecall * rules::kKnockedOutRecall && w.active >= 0) {
        const Monster& m = w.team[static_cast<size_t>(w.active)];
        w.went_in = {w.tick, false, m.species, m.genes, c.body};
        w.active = -1;
        c.mind = Mind::Idle;
      }
      continue;
    }
    if (i == 0) {
      step_companion(w, s, c);
      continue;
    }
    if (c.mind == Mind::KnockedOut) {
      step_knocked_out(w, s, c);
      continue;
    }
    if (c.tag == Tag::LostPiou && w.quests[static_cast<size_t>(QuestId::LostPiou)].stage == QuestStage::Done) {
      // Mirabelle's Piou goes home with her.
      const int owner = npc_index(w, npc::kMirabelle);
      const bool home = owner > 0 && !about(w.creatures[static_cast<size_t>(owner)]);
      if (home) c.body.pos = w.creatures[static_cast<size_t>(owner)].body.pos;
      c.act = home ? Activity::Home : Activity::None;
      if (!home) step_pet(w, s, c, npc::kMirabelle);
      continue;
    }
    if (c.tag == Tag::Rival) {
      step_pet(w, s, c, c.npc ? c.npc : npc::kBastien);
      continue;
    }
    if (c.tag == Tag::Lamb && c.mind == Mind::Follow) {
      step_lamb(w, s, c);
      continue;
    }
    if (c.species == Species::Villager && w.battle.rival.npc && c.npc == w.battle.rival.npc) {
      // The trainer goes to its place, and watches.
      const Steer st = battle_prepare(w, Stand::Trainer);
      move_body(s, c.body, species_info(c.species).shape, st.dir_x, st.dir_z, st.speed, false, 0);
      continue;
    }
    switch (c.species) {
      case Species::Villager:
        villager_routine(w, c);  // home at night, in the rain... (routines.hpp)
        if (!about(c)) break;
        // Far from the player, the village stands still.
        if (near_player(w, c.body.pos.x, c.body.pos.z, rules::kSimulationRadius)) {
          step_villager(w, s, c);
        }
        break;
      case Species::Crow:
        if (perches.empty()) perches = crow_perches(w);
        step_crow(w, perches, c);
        break;
      case Species::Owl: {
        if (owls.empty()) owls = owl_perches(w);
        if (owls.size() < 2) step_wild(w, s, c);
        else step_crow(w, owls, c);
        break;
      }
      case Species::Fish: step_fish(w, s, c); break;
      case Species::Cristi:
        if (climbs) step_cristi(w, c);
        else step_wild(w, s, c);
        break;
      default: step_wild(w, s, c); break;
    }
  }
  if (!climbed) w.climbing = false;
  battle_resolve(w);
  spells_step(w);
  discover(w);
  quests_step(w);
  west_step(w);
  north_step(w);
  tournament_step(w);
  if (w.tick % rules::kPopulationEveryTicks == 0) update_population(w);
}

}  // namespace monstres
