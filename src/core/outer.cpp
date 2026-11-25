#include "monstres/core/outer.hpp"

#include "monstres/core/heightmap.hpp"
#include "monstres/core/roads.hpp"
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
    default: return 0.0f;  // the open sea, the capital (empty for now), the valley's
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
// by big patches (woods and clearings), then by clumps; near the relief, as
// thick as the world's nature just inside it, thinning out (heightmap.hpp:
// edge_density).
float nature_density(const World& w, Region region, float x, float z) {
  // (Read through a warped field, turned: never the noise's square cells.)
  const float wx = x + 90.0f * (value_noise(w.seed + 3003, x, z, 210.0f) - 0.5f) + 30.0f * (value_noise(w.seed + 3005, x, z, 70.0f) - 0.5f);
  const float wz = z + 90.0f * (value_noise(w.seed + 3004, x, z, 210.0f) - 0.5f) + 30.0f * (value_noise(w.seed + 3006, x, z, 70.0f) - 0.5f);
  const float ax = 0.80f * wx - 0.60f * wz, az = 0.60f * wx + 0.80f * wz;  // turned by 37°
  const float bx = 0.39f * wx + 0.92f * wz, bz = -0.92f * wx + 0.39f * wz;  // and by -67°
  const float patch = 0.6f * value_noise(w.seed + 3001, ax, az, 150.0f) + 0.4f * value_noise(w.seed + 3007, bx, bz, 65.0f);
  const float patches = 0.15f + 1.6f * smooth(0.3f, 0.7f, patch);
  const float clumps = 0.4f + 1.2f * value_noise(w.seed + 3002, bx, bz, 26.0f);
  const float edge = std::max(0.0f, w.ground.edge_density(x, z)) * clumps;
  return std::min(0.95f, std::max(density_of(region) * patches * clumps, edge));
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
  // The world's own (and right by its edge). Beyond it, between it and the
  // relief's edge too: the continent's nature, as anywhere (but on the
  // world's ways and in its stream, going on to the relief's edge).
  if (rules::world_inset(x, z) > -3.0f) return c;
  const Region region = outer_region(w.seed, x, z);
  if (roll >= nature_density(w, region, x, z)) return c;
  const Heightmap& g = w.ground;
  const float ground = g.height(x, z);
  if (ground < rules::kSeaLevel + 0.6f || (g.sea(x, z) && ground < rules::kSeaLevel + 1.0f)) return c;  // the water, the beach
  for (const Pond& p : continent_ponds()) {  // the lake's and the marsh's water
    if (pond_shape(p, x, z) > -0.05f && ground < p.level + 0.6f) return c;
  }
  if (RiverSpot r; nearest_river(g.rivers, x, z, 20.0f, r) && r.distance < r.width + 1.5f) return c;  // a river's water, its edge
  Object o;
  o.kind = palette(region, ground, pick);
  if (g.snow(x, z, ground) > 0.5f && o.kind != ObjectKind::Rock) return c;  // up in the snow, bare rock
  const float steep = g.normal(x, z).y;
  if (steep < (o.kind == ObjectKind::Rock ? 0.68f : 0.82f)) return c;
  if (rules::relief_inset(x, z) > -12.0f) {  // (between the world's edge and the relief's: its ways, its stream)
    if (distance_to_path(w.path, x, z, 8.0f) < 6.0f || distance_to_lanes(w.roads, x, z, 8.0f) < 5.0f) return c;
    if (w.west.valid() && (distance_to_road(w.west.road, x, z, 8.0f) < 5.0f || distance_to_stream(w.west.stream, x, z, 12.0f) < 7.0f)) return c;
  }
  o.pos = {x, ground, z};
  o.yaw = yaw;
  o.variant = variant;
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
      if (c.any && kept(w, ci, cj, c)) out.push_back(c.object);
    }
  }
  return out;
}

}  // namespace

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
