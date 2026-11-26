#include "monstres/core/outer.hpp"
#include "monstres/core/water.hpp"

#include "monstres/core/heightmap.hpp"
#include "monstres/core/north.hpp"
#include "monstres/core/roads.hpp"
#include "monstres/core/sites.hpp"
#include "monstres/core/village.hpp"
#include "monstres/core/west.hpp"
#include "monstres/core/rng.hpp"
#include "monstres/core/variants.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace monstres {

// ---- The cache ----

struct OuterCache::Store {
  struct Entry {
    uint64_t used = 0;
    std::vector<Object> objects;
  };
  std::unordered_map<int64_t, Entry> tiles;
  uint64_t clock = 0;
};
OuterCache::OuterCache() = default;
OuterCache::OuterCache(const OuterCache&) {}
OuterCache& OuterCache::operator=(const OuterCache& other) {
  if (this != &other) store_.reset();
  return *this;
}
OuterCache::OuterCache(OuterCache&&) noexcept = default;
OuterCache& OuterCache::operator=(OuterCache&&) noexcept = default;
OuterCache::~OuterCache() = default;
OuterCache::Store& OuterCache::store() const {
  if (!store_) store_ = std::make_unique<Store>();
  return *store_;
}
size_t OuterCache::size() const { return store_ ? store_->tiles.size() : 0; }

namespace {

uint64_t mix64(uint64_t x) {
  x += 0x9E3779B97F4A7C15ULL;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
  return x ^ (x >> 31);
}

uint64_t cell_hash(uint64_t seed, int ci, int cj, uint64_t salt) {
  return mix64(seed ^ salt ^ (static_cast<uint64_t>(static_cast<uint32_t>(ci)) << 32) ^ static_cast<uint32_t>(cj));
}

float unit(Rng& r) { return static_cast<float>(r.below(1u << 24)) / static_cast<float>(1u << 24); }

float smooth(float e0, float e1, float x) {
  const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
  return t * t * (3 - 2 * t);
}

constexpr float kTile = rules::kTileCells * Heightmap::kCellSize;

int floor_int(float v) { return static_cast<int>(std::floor(v)); }

// How much grows in a region (out of 1, before its patches and clumps).
float density_of(Region r) {
  switch (r) {
    case Region::Forest: return 0.62f;
    case Region::Plains: return 0.08f;
    case Region::Desert: return 0.025f;
    case Region::Mountains: return 0.15f;
    case Region::Volcano: return 0.04f;
    case Region::Marsh: return 0.14f;
    case Region::Lake: return 0.10f;
    case Region::LoneMountain: return 0.14f;
    case Region::Start: return 0.08f;  // (around the valley: as the plains)
    default: return 0.0f;  // the open sea, the capital (empty for now)
  }
}

struct Pick {
  ObjectKind kind;
  float weight;
};

// What grows in a region (at `height`: up high, rocks rather than pines).
ObjectKind palette(Region r, float height, float roll) {
  static constexpr Pick kForest[] = {{ObjectKind::Tree, 0.55f}, {ObjectKind::Pine, 0.2f}, {ObjectKind::Bush, 0.12f}, {ObjectKind::Stump, 0.05f}, {ObjectKind::Rock, 0.08f}};
  static constexpr Pick kPlains[] = {{ObjectKind::Tree, 0.35f}, {ObjectKind::Bush, 0.4f}, {ObjectKind::Rock, 0.2f}, {ObjectKind::Stump, 0.05f}};
  static constexpr Pick kDesert[] = {{ObjectKind::Rock, 0.6f}, {ObjectKind::Bush, 0.4f}};
  static constexpr Pick kMountains[] = {{ObjectKind::Pine, 0.5f}, {ObjectKind::Rock, 0.4f}, {ObjectKind::Larch, 0.1f}};
  static constexpr Pick kHigh[] = {{ObjectKind::Pine, 0.25f}, {ObjectKind::Rock, 0.75f}};
  static constexpr Pick kVolcano[] = {{ObjectKind::Rock, 0.75f}, {ObjectKind::Stump, 0.25f}};
  static constexpr Pick kMarsh[] = {{ObjectKind::Bush, 0.5f}, {ObjectKind::Tree, 0.25f}, {ObjectKind::Stump, 0.1f}, {ObjectKind::Rock, 0.15f}};
  static constexpr Pick kLake[] = {{ObjectKind::Tree, 0.4f}, {ObjectKind::Bush, 0.4f}, {ObjectKind::Rock, 0.2f}};
  static constexpr Pick kLone[] = {{ObjectKind::Pine, 0.5f}, {ObjectKind::Rock, 0.5f}};
  std::span<const Pick> picks;
  switch (r) {
    case Region::Forest: picks = kForest; break;
    case Region::Plains: picks = kPlains; break;
    case Region::Desert: picks = kDesert; break;
    case Region::Mountains: picks = height > 120.0f ? std::span<const Pick>(kHigh) : std::span<const Pick>(kMountains); break;
    case Region::Volcano: picks = kVolcano; break;
    case Region::Marsh: picks = kMarsh; break;
    case Region::Lake: picks = kLake; break;
    case Region::Start: picks = kPlains; break;
    default: picks = kLone; break;
  }
  float total = 0;
  for (const Pick& p : picks) total += p.weight;
  float at = roll * total;
  for (const Pick& p : picks) {
    if (at < p.weight) return p.kind;
    at -= p.weight;
  }
  return picks.back().kind;
}

// How likely a cell of 5 m at (x, z) is to have something: its region's,
// by big patches (woods and clearings), then by clumps (the same within the
// playable world as beyond it).
float nature_density(const World& w, Region region, float x, float z) {
  // (Read through a warped field, turned: never the noise's square cells.)
  const float wx = x + 90.0f * (value_noise(w.seed + 3003, x, z, 210.0f) - 0.5f) + 30.0f * (value_noise(w.seed + 3005, x, z, 70.0f) - 0.5f);
  const float wz = z + 90.0f * (value_noise(w.seed + 3004, x, z, 210.0f) - 0.5f) + 30.0f * (value_noise(w.seed + 3006, x, z, 70.0f) - 0.5f);
  const float ax = 0.80f * wx - 0.60f * wz, az = 0.60f * wx + 0.80f * wz;  // turned by 37°
  const float bx = 0.39f * wx + 0.92f * wz, bz = -0.92f * wx + 0.39f * wz;  // and by -67°
  const float patch = 0.6f * value_noise(w.seed + 3001, ax, az, 150.0f) + 0.4f * value_noise(w.seed + 3007, bx, bz, 65.0f);
  const float patches = 0.15f + 1.6f * smooth(0.3f, 0.7f, patch);
  const float clumps = 0.4f + 1.2f * value_noise(w.seed + 3002, bx, bz, 26.0f);
  return std::min(0.95f, density_of(region) * patches * clumps);
}

// Within the playable world: whether its nature may grow at (x, z) (not
// its valley, its places, its forest or woods: their own).
bool free_for_nature(const World& w, float x, float z) {
  if (std::max(std::fabs(x), std::fabs(z)) < rules::kValleyHalfSize - 2.0f) return false;
  if (in_west_place(w.west, x, z, 1.5f) || in_woods(w.west, x, z) || in_roadside_place(w.roads, x, z, 1.5f) ||
      in_site(w.sites, x, z, 1.5f) || in_forest(w.forest, x, z)) {
    return false;
  }
  return !(w.north.valid() && in_north_place(w.north, x, z, 1.5f));
}

// Something built there in its way (the world's objects but its nature: the
// valley's, the forest's and the woods' trees and rocks do not move it).
bool in_the_way(const World& w, const Object& o) {
  const Box b = object_box(o);
  const float r = std::max(0.6f, 0.5f * std::max(b.max.x - b.min.x, b.max.z - b.min.z)) + rules::kObjectGap;
  const Box area{{o.pos.x - r, -1e4f, o.pos.z - r}, {o.pos.x + r, 1e4f, o.pos.z + r}};
  bool hit = false;
  for_objects_near(w, area, [&](uint32_t i, const Box& ob) {
    const ObjectKind k = w.objects[i].kind;
    const bool nature = k == ObjectKind::Tree || k == ObjectKind::Pine || k == ObjectKind::Larch || k == ObjectKind::Bush ||
                        k == ObjectKind::Rock || k == ObjectKind::Stump || k == ObjectKind::Log || k == ObjectKind::Seaweed;
    hit = hit || (!nature && ob.max.x > area.min.x && ob.min.x < area.max.x && ob.max.z > area.min.z && ob.min.z < area.max.z);
  });
  return hit;
}

// Not on a slope too steep for its size: its foot's lowest corner not too
// far under its middle (it would float on one side).
bool on_its_ground(const World& w, const Object& o, float ground) {
  const Box e = object_box(o);  // (no logs here)
  float lowest = ground;
  for (const float cx : {e.min.x, e.max.x}) {
    for (const float cz : {e.min.z, e.max.z}) lowest = std::min(lowest, w.ground.height(cx, cz));
  }
  return ground - lowest <= 0.6f * (e.max.y - e.min.y) + 0.1f;
}

// What a cell of 5 m would have (before its neighbours have their say).
struct Candidate {
  bool any = false;
  Object object;
  float reach = 0;        // how far its foot goes from its middle
  uint32_t priority = 0;  // who stays when two are in each other's way
};

Candidate candidate(const World& w, int ci, int cj) {
  Candidate c;
  const uint64_t h = cell_hash(w.seed, ci, cj, 0x4F55544552ULL);  // "OUTER"
  Rng rng(h, 23);
  // Every draw first (the same whatever happens).
  const float u = unit(rng), v = unit(rng);  // anywhere in it: no rows
  const float roll = unit(rng), pick = unit(rng);
  const float s1 = unit(rng), s2 = unit(rng), s3 = unit(rng);
  const auto yaw = static_cast<Angle16>(rng.below(65536));
  const auto variant = static_cast<uint8_t>(rng.below(256));
  c.priority = static_cast<uint32_t>(h >> 32);
  const float x = (static_cast<float>(ci) + u) * rules::kOuterCell, z = (static_cast<float>(cj) + v) * rules::kOuterCell;
  // Within the playable world too, as anywhere (world_nature), but not in its
  // valley, its places, its forest and woods (their own), on its ways and in
  // its stream, nor in the way of what is built there.
  const bool in_world = rules::world_inset(x, z) > -12.0f;
  if (in_world && !free_for_nature(w, x, z)) return c;
  const Region region = outer_region(w.seed, x, z);
  const Heightmap& g = w.ground;
  // The bottom of the water (water.hpp): seaweed in clumps, rocks here and there.
  if (WaterSpot ws; water_at(w, x, z, ws)) {
    const float weed = 0.08f + 0.55f * smooth(0.45f, 0.65f, value_noise(w.seed + 4700, x, z, 30.0f));
    if (ws.depth() < 1.2f || roll >= weed + 0.05f) return c;
    Object o;
    o.yaw = yaw;
    o.variant = variant;
    if (roll < 0.05f && !in_lake(w.village.lake, x, z, 2.0f)) {  // (the village's lake: its seaweed only)
      o.kind = ObjectKind::Rock;
      o.size = {0.4f + 0.9f * s1, 0.3f + 0.7f * s2, 0.4f + 0.9f * s3};
      o.yaw = static_cast<Angle16>(o.yaw & 0xC000);
      c.reach = std::max(o.size.x, o.size.z);
    } else {
      o.kind = ObjectKind::Seaweed;
      o.size = {0.7f + 0.8f * s1, std::min(ws.depth() - 0.3f, 0.8f + 1.6f * s2), 0};
      c.reach = 0.3f;
    }
    o.pos = {x, ws.ground - 0.05f, z};
    if (o.kind == ObjectKind::Rock && (steep_ground(w, x, z) || !on_its_ground(w, o, ws.ground))) return c;
    if (in_world && in_the_way(w, o)) return c;
    c.any = true;
    c.object = o;
    return c;
  }
  Object o;
  const float high = g.height(x, z);
  o.kind = palette(region, high, pick);
  // Up the hills (anywhere: within the world as beyond it), pines rather than leafy trees.
  if (o.kind == ObjectKind::Tree && high > 22.0f + 10.0f * value_noise(w.seed + 3020, x, z, 90.0f)) o.kind = ObjectKind::Pine;
  // The north's: its pastures, its larch woods, its rocky slopes (north.hpp).
  float chance = nature_density(w, region, x, z);
  if (w.north.valid() && z < rules::kNorthFrom + 40.0f) {
    chance = north_wilds(w, o, x, z, chance);
  }
  if (roll >= chance) return c;
  const float ground = g.height(x, z);
  if (ground < rules::kSeaLevel + 0.6f || (g.sea(x, z) && ground < rules::kSeaLevel + 1.0f)) return c;  // the water, the beach
  for (const Pond& p : continent_ponds()) {  // the lake's and the marsh's water
    if (pond_shape(p, x, z) > -0.05f && ground < p.level + 0.6f) return c;
  }
  if (RiverSpot r; nearest_river(g.rivers, x, z, 20.0f, r) && r.distance < r.width + 1.5f) return c;  // a river's water, its edge
  if (g.snow(x, z, ground) > 0.5f && o.kind != ObjectKind::Rock) return c;  // up in the snow, bare rock
  const float steep = g.normal(x, z).y;
  if (steep < (o.kind == ObjectKind::Rock ? 0.68f : 0.82f) || steep_ground(w, x, z)) return c;
  if (rules::relief_inset(x, z) > -12.0f) {  // (between the world's edge and the relief's: its ways, its stream)
    if (distance_to_path(w.path, x, z, 8.0f) < 6.0f || distance_to_lanes(w.roads, x, z, 8.0f) < 5.0f) return c;
    if (w.west.valid() && (distance_to_road(w.west.road, x, z, 8.0f) < 5.0f || distance_to_stream(w.west.stream, x, z, 12.0f) < 7.0f)) return c;
  }
  o.pos = {x, ground, z};
  o.yaw = yaw;
  o.variant = variant;
  if (in_world && in_the_way(w, o)) return c;
  switch (o.kind) {
    case ObjectKind::Tree:
    case ObjectKind::Pine:
    case ObjectKind::Larch: {
      const float big = region == Region::Forest ? 1.25f : 1.0f;  // the forest's old trees
      const float s = (0.75f + 0.7f * s1 * s1) * big;
      o.size = {s, s, s};
      c.reach = 0.45f * s;
      o.pos.y = ground - 0.05f;
      break;
    }
    case ObjectKind::Rock: {
      const float k = region == Region::Desert || region == Region::Volcano || ground > 120.0f ? 1.4f : 1.0f;  // bigger out in the bare land
      o.size = {(0.4f + 1.3f * s1) * k, (0.3f + 1.1f * s2) * k, (0.4f + 1.3f * s3) * k};
      o.yaw = static_cast<Angle16>(o.yaw & 0xC000);
      c.reach = std::max(o.size.x, o.size.z);
      // On a slope it sinks into it (at most 40 % of its height).
      float lowest = ground;
      for (const float a : {-1.0f, 1.0f}) {
        for (const float b : {-1.0f, 1.0f}) lowest = std::min(lowest, g.height(x + a * o.size.x, z + b * o.size.z));
      }
      o.pos.y = std::max(lowest - 0.05f, ground - 0.05f - 0.4f * o.size.y);
      break;
    }
    case ObjectKind::Bush:
      o.size = {0.5f + 0.45f * s1, 0.5f + 0.45f * s2, 0};
      c.reach = o.size.x;
      o.pos.y = ground - 0.05f;
      break;
    default:  // a stump
      o.kind = ObjectKind::Stump;
      o.size = {0.3f + 0.2f * s1, 0.3f + 0.4f * s2, 0};
      c.reach = o.size.x;
      o.pos.y = ground - 0.05f;
      break;
  }
  if (!on_its_ground(w, o, ground)) return c;  // a slope too steep for its size
  c.any = true;
  c.object = o;
  return c;
}

// Whether a cell keeps what it drew: no neighbour drawn before it in its way.
bool kept(const World& w, int ci, int cj, const Candidate& c) {
  const int reach = c.reach > 1.6f ? 2 : 1;
  for (int a = -reach; a <= reach; ++a) {
    for (int b = -reach; b <= reach; ++b) {
      if (!a && !b) continue;
      const Candidate n = candidate(w, ci + a, cj + b);
      if (!n.any) continue;
      const float d = std::hypot(n.object.pos.x - c.object.pos.x, n.object.pos.z - c.object.pos.z);
      if (d >= n.reach + c.reach + 0.4f) continue;
      if (n.priority > c.priority || (n.priority == c.priority && (a < 0 || (a == 0 && b < 0)))) return false;
    }
  }
  return true;
}

std::vector<Object> make_tile(const World& w, int ti, int tj) {
  std::vector<Object> out;
  const float x0 = Heightmap::kMinX + kTile * static_cast<float>(ti), z0 = Heightmap::kMinZ + kTile * static_cast<float>(tj);
  // The cells whose corner is in the tile.
  const int c0 = floor_int(x0 / rules::kOuterCell), c1 = floor_int((x0 + kTile) / rules::kOuterCell);
  const int d0 = floor_int(z0 / rules::kOuterCell), d1 = floor_int((z0 + kTile) / rules::kOuterCell);
  for (int ci = c0; ci <= c1; ++ci) {
    const float cx = static_cast<float>(ci) * rules::kOuterCell;
    if (cx < x0 || cx >= x0 + kTile) continue;
    for (int cj = d0; cj <= d1; ++cj) {
      const float cz = static_cast<float>(cj) * rules::kOuterCell;
      if (cz < z0 || cz >= z0 + kTile) continue;
      const Candidate c = candidate(w, ci, cj);
      // (Within the world: its own, World::objects, drawn alike: world_nature.)
      if (c.any && rules::world_inset(c.object.pos.x, c.object.pos.z) <= -3.0f && kept(w, ci, cj, c)) out.push_back(c.object);
    }
  }
  return out;
}

}  // namespace

std::vector<Object> world_nature(const World& w) {
  std::vector<Object> out;
  if (w.ground.empty()) return out;
  // The cells over the world (and two more around: their neighbours'
  // candidates, looked up once each).
  const float c = rules::kOuterCell;
  const int c0 = floor_int((-rules::kWorldHalfSize - 3.0f) / c) - 2, c1 = floor_int((rules::kWorldHalfSize + 3.0f) / c) + 2;
  const int d0 = floor_int((-rules::kWorldNorth - 3.0f) / c) - 2, d1 = floor_int((rules::kWorldHalfSize + 3.0f) / c) + 2;
  const int nx = c1 - c0 + 1, nz = d1 - d0 + 1;
  std::vector<Candidate> grid(static_cast<size_t>(nx * nz));
  for (int i = 0; i < nx; ++i) {
    for (int j = 0; j < nz; ++j) grid[static_cast<size_t>(i * nz + j)] = candidate(w, c0 + i, d0 + j);
  }
  const auto at = [&](int ci, int cj) -> Candidate {
    const int i = ci - c0, j = cj - d0;
    return i >= 0 && j >= 0 && i < nx && j < nz ? grid[static_cast<size_t>(i * nz + j)] : candidate(w, ci, cj);
  };
  for (int i = 2; i + 2 < nx; ++i) {
    for (int j = 2; j + 2 < nz; ++j) {
      const Candidate& k = grid[static_cast<size_t>(i * nz + j)];
      if (!k.any || rules::world_inset(k.object.pos.x, k.object.pos.z) <= -3.0f) continue;
      // Kept: no neighbour drawn before it in its way (as kept()).
      const int ci = c0 + i, cj = d0 + j;
      const int reach = k.reach > 1.6f ? 2 : 1;
      bool keep = true;
      for (int a = -reach; a <= reach && keep; ++a) {
        for (int b = -reach; b <= reach && keep; ++b) {
          if (!a && !b) continue;
          const Candidate n = at(ci + a, cj + b);
          if (!n.any) continue;
          const float d = std::hypot(n.object.pos.x - k.object.pos.x, n.object.pos.z - k.object.pos.z);
          if (d >= n.reach + k.reach + 0.4f) continue;
          if (n.priority > k.priority || (n.priority == k.priority && (a < 0 || (a == 0 && b < 0)))) keep = false;
        }
      }
      if (keep) out.push_back(k.object);
    }
  }
  return out;
}

float outer_nature(const World& w, float x, float z) {
  if (w.ground.empty()) return 0.0f;
  return nature_density(w, outer_region(w.seed, x, z), x, z);
}

Region outer_region(uint64_t seed, float x, float z) {
  const float wx = x + 110.0f * (value_noise(seed + 3010, x, z, 230.0f) - 0.5f) + 30.0f * (value_noise(seed + 3012, x, z, 60.0f) - 0.5f);
  const float wz = z + 110.0f * (value_noise(seed + 3011, x, z, 230.0f) - 0.5f) + 30.0f * (value_noise(seed + 3013, x, z, 60.0f) - 0.5f);
  return region_at(wx, wz);
}

void outer_tile_of(float x, float z, int& ti, int& tj) {
  ti = floor_int((x - Heightmap::kMinX) / kTile);
  tj = floor_int((z - Heightmap::kMinZ) / kTile);
}

const std::vector<Object>& outer_objects(const World& w, int ti, int tj) {
  static const std::vector<Object> kNone;
  if (w.ground.empty()) return kNone;
  // Within the world: its own objects (a tile all inside it has none of the continent's).
  const float x0 = Heightmap::kMinX + kTile * static_cast<float>(ti), z0 = Heightmap::kMinZ + kTile * static_cast<float>(tj);
  if (std::min({rules::world_inset(x0, z0), rules::world_inset(x0 + kTile, z0), rules::world_inset(x0, z0 + kTile),
                rules::world_inset(x0 + kTile, z0 + kTile)}) > 3.0f) {
    return kNone;
  }
  OuterCache::Store& store = w.outer.store();
  const int64_t key = (static_cast<int64_t>(ti) << 32) ^ static_cast<int64_t>(static_cast<uint32_t>(tj));
  auto it = store.tiles.find(key);
  if (it == store.tiles.end()) {
    if (store.tiles.size() >= rules::kOuterTilesKept) {
      auto oldest = store.tiles.begin();
      for (auto k = store.tiles.begin(); k != store.tiles.end(); ++k) oldest = k->second.used < oldest->second.used ? k : oldest;
      store.tiles.erase(oldest);
    }
    OuterCache::Store::Entry entry;
    entry.objects = make_tile(w, ti, tj);
    it = store.tiles.emplace(key, std::move(entry)).first;
  }
  it->second.used = ++store.clock;
  return it->second.objects;
}

// ---- The monsters ----

std::vector<Species> outer_species(Region region) {
  switch (region) {
    case Region::Plains: return {Species::Bird, Species::Sheep, Species::Slime, Species::Spark, Species::Raccoon};
    case Region::Forest: return {Species::Mushroom, Species::Raccoon, Species::Bird, Species::Mole, Species::Slime};
    case Region::Marsh: return {Species::Slime, Species::Axolotl, Species::Mushroom};
    case Region::Lake: return {Species::Axolotl, Species::Slime, Species::Bird};
    case Region::Mountains: return {Species::Goat, Species::Golem, Species::Ermine};
    case Region::LoneMountain: return {Species::Goat, Species::Golem, Species::Bird};
    case Region::Volcano: return {Species::Golem, Species::Spark};
    case Region::Desert: return {Species::Spark, Species::Mole, Species::Golem};
    default: return {};
  }
}

Zone outer_zone(Region region) {
  switch (region) {
    case Region::Forest: return Zone::Woods;
    case Region::Marsh: return Zone::River;
    case Region::Lake: return Zone::Lake;
    case Region::Mountains: return Zone::Frost;
    case Region::LoneMountain: return Zone::Alpine;
    case Region::Volcano: return Zone::Crater;
    default: return Zone::Plains;
  }
}

uint8_t outer_level(float x, float z) {
  const float beyond = std::max(0.0f, std::hypot(x, z) - rules::kValleyHalfSize);
  return static_cast<uint8_t>(std::clamp(3 + static_cast<int>(beyond / 140.0f), 3, 45));
}

std::vector<Creature> outer_creatures(const World& w, int ti, int tj, uint32_t visit) {
  std::vector<Creature> out;
  if (w.ground.empty()) return out;
  const uint64_t h = cell_hash(w.seed, ti, tj, 0x4D4F4E5354ULL ^ (static_cast<uint64_t>(visit) << 40));  // "MONST"
  Rng rng(h, 29);
  const float x0 = Heightmap::kMinX + kTile * static_cast<float>(ti), z0 = Heightmap::kMinZ + kTile * static_cast<float>(tj);
  for (uint32_t k = 0; k < rules::kOuterMonsterTries; ++k) {
    // Every draw first.
    const float x = x0 + kTile * unit(rng), z = z0 + kTile * unit(rng);
    const uint32_t here = rng.below(100), which = rng.below(1000);
    const auto facing = static_cast<Angle16>(rng.below(65536));
    const auto variant = static_cast<uint8_t>(rng.below(256));
    const uint32_t extra = rng.below(3);
    Rng genes(h ^ (k * 0x9E3779B97F4A7C15ULL), 31);
    if (rules::in_world(x, z, -3.0f)) continue;  // the playable ground's squares have their own
    const Region region = outer_region(w.seed, x, z);
    // Under water (water.hpp): Bloup and Ploufi, at any depth.
    if (WaterSpot ws; water_at(w, x, z, ws)) {
      if (here >= 40 || frozen(w, ws) || ws.depth() < 2.5f) continue;
      Creature c;
      c.species = which % 100 < 65 ? Species::Fish : Species::Axolotl;
      const BodyShape& shape = species_info(c.species).shape;
      c.body.pos = {x, ws.ground + 0.4f + (ws.depth() - shape.height - 0.8f) * static_cast<float>(variant) / 255.0f, z};
      c.body.on_ground = false;
      const SinCos sc = sincos16(facing);
      c.body.facing_x = sc.sin;
      c.body.facing_z = sc.cos;
      c.home = {x, z};
      c.variant = variant;
      c.level = static_cast<uint8_t>(outer_level(x, z) + extra);
      const Zone zone = ws.kind == WaterKind::Sea ? Zone::Coast : ws.kind == WaterKind::River ? Zone::River : Zone::Lake;
      c.genes = genetics(c.species).gene_count > 0 ? roll_genes(c.species, zone, genes) : Genes{};
      out.push_back(c);
      continue;
    }
    const std::vector<Species> kinds = outer_species(region);
    if (kinds.empty() || here >= 55) continue;
    const float ground = w.ground.height(x, z);
    if (ground < rules::kSeaLevel + 0.6f || w.ground.normal(x, z).y < 0.75f) continue;
    Creature c;
    c.species = kinds[which % kinds.size()];
    c.body.pos = {x, ground, z};
    const SinCos sc = sincos16(facing);
    c.body.facing_x = sc.sin;
    c.body.facing_z = sc.cos;
    c.home = {x, z};
    c.variant = variant;
    c.level = static_cast<uint8_t>(outer_level(x, z) + extra);
    c.genes = genetics(c.species).gene_count > 0 ? roll_genes(c.species, outer_zone(region), genes) : Genes{};
    out.push_back(c);
  }
  return out;
}

}  // namespace monstres
