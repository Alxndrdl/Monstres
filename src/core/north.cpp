#include "monstres/core/north.hpp"

#include "monstres/core/battle.hpp"
#include "monstres/core/continent.hpp"
#include "monstres/core/interior.hpp"
#include "monstres/core/quests.hpp"
#include "monstres/core/variants.hpp"
#include "monstres/core/west.hpp"

#include <algorithm>
#include <cmath>

namespace monstres {

namespace {

Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.z + b.z}; }
Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.z - b.z}; }
Vec2 operator*(Vec2 a, float k) { return {a.x * k, a.z * k}; }
float length(Vec2 v) { return std::sqrt(v.x * v.x + v.z * v.z); }
float distance(Vec2 a, Vec2 b) { return length(a - b); }
Vec2 dir16(Angle16 a) {  // (cos, sin)
  const SinCos sc = sincos16(a);
  return {sc.cos, sc.sin};
}

float smooth(float e0, float e1, float x) {
  const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
  return t * t * (3 - 2 * t);
}

uint64_t mix(uint64_t z) {
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}

// A number in [0, 1) for (seed, salt), the same each time.
float hash01(uint64_t seed, uint64_t salt) {
  return static_cast<float>(mix(seed ^ (0x4E4F525448ULL + salt * 0x9E3779B97F4A7C15ULL)) >> 40) /
         static_cast<float>(1ULL << 24);
}

// Where the path is at z (its points go by increasing z).
float path_x_at(const Path& path, float z) {
  const auto& pts = path.points;
  if (pts.empty()) return 0;
  const auto it = std::lower_bound(pts.begin(), pts.end(), z, [](const Vec2& p, float v) { return p.z < v; });
  if (it == pts.begin()) return pts.front().x;
  if (it == pts.end()) return pts.back().x;
  const Vec2 b = *it, a = *(it - 1);
  const float t = b.z - a.z > 1e-6f ? (z - a.z) / (b.z - a.z) : 0.0f;
  return a.x + (b.x - a.x) * t;
}

float catmull_rom(float p0, float p1, float p2, float p3, float t) {
  const float t2 = t * t, t3 = t2 * t;
  return 0.5f * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 + (-p0 + 3 * p1 - 3 * p2 + p3) * t3);
}

// Through the control points, a point every `step` meters (about).
std::vector<Vec2> spline(const std::vector<Vec2>& c, float step) {
  std::vector<Vec2> out;
  if (c.size() < 2) return c;
  for (size_t i = 0; i + 1 < c.size(); ++i) {
    const Vec2 p0 = c[i > 0 ? i - 1 : 0], p1 = c[i], p2 = c[i + 1], p3 = c[std::min(i + 2, c.size() - 1)];
    const int n = std::max(1, static_cast<int>(distance(p1, p2) / step));
    for (int k = 0; k < n; ++k) {
      const float t = static_cast<float>(k) / static_cast<float>(n);
      out.push_back({catmull_rom(p0.x, p1.x, p2.x, p3.x, t), catmull_rom(p0.z, p1.z, p2.z, p3.z, t)});
    }
  }
  out.push_back(c.back());
  return out;
}

// Beside the path at z, `off` meters across it (right of its way north for off > 0).
Vec2 beside_path(const Path& path, float z, float off) {
  Vec2 t{path_x_at(path, z - 1.0f) - path_x_at(path, z + 1.0f), -2.0f};
  t = t * (1.0f / length(t));
  return Vec2{path_x_at(path, z), z} + Vec2{-t.z, t.x} * off;
}

// The valley's floor up to the col, at z (going up gently towards it).
float valley_floor(const NorthLayout& n, float z) {
  return n.floor_start + rules::kValleyRise * std::max(0.0f, rules::kFrostFrom - z);
}

}  // namespace

// ---- Layout ----------------------------------------------------------------------

NorthLayout make_north_layout(uint64_t seed, const Path& path, const WestLayout& west) {
  NorthLayout n;
  if (path.points.size() < 2 || !west.valid()) return n;
  // Firm ground all over a disc: land, away from the terrace's cliffs and the
  // path, within the world.
  const auto firm = [&](Vec2 c, float r) {
    if (!rules::in_world(c.x, c.z, r + 14.0f)) return false;
    if (distance_to_path(path, c.x, c.z, r + 5.0f) < r + 4.0f) return false;
    if (west.scarp.points.size() >= 2 && std::fabs(scarp_side(west, c.x, c.z)) < r + 30.0f) return false;
    // (The continent's ground is low up there, near the sea: the terrace inland lifts it.)
    for (int k = 0; k <= 8; ++k) {
      const Vec2 p = k == 8 ? c : c + dir16(static_cast<Angle16>(k * 8192)) * r;
      const bool terrace = west.scarp.points.size() >= 2 && scarp_side(west, p.x, p.z) < -30.0f;
      if (natural_height(seed, p.x, p.z) + (terrace ? rules::kTerraceHeight : 0.0f) < rules::kSeaLevel + 4.0f) return false;
    }
    return true;
  };
  // Looks for a spot about z, `off` meters from the path on side `side` (or the other one).
  const auto find = [&](float z0, float spread, float off0, float off_more, float side, float r, uint64_t salt, Vec2& at) {
    for (int k = 0; k < 60; ++k) {
      const float z = z0 + (hash01(seed, salt + static_cast<uint64_t>(k)) - 0.5f) * spread;
      const float off = off0 + off_more * hash01(seed, salt + 100 + static_cast<uint64_t>(k)) + 0.6f * static_cast<float>(k);
      for (const float s : {side, -side}) {
        const Vec2 p{path_x_at(path, z) + s * off, z};
        if (firm(p, r)) {
          at = p;
          return s;
        }
      }
    }
    at = {path_x_at(path, z0) + side * off0, z0};
    return side;
  };
  const float side = hash01(seed, 1) < 0.5f ? -1.0f : 1.0f;
  // The refuge by the path, its door towards it; its teleporter beside it.
  const float rs = find(-760.0f, 50.0f, 24.0f, 6.0f, side, rules::kRefugeRadius, 10, n.refuge);
  n.refuge_front = {-rs, 0};
  n.refuge_teleporter = n.refuge + Vec2{-rs * 2.5f, hash01(seed, 2) < 0.5f ? 8.5f : -8.5f};
  // The mine across the path, looking towards it.
  const float ms = find(-665.0f, 60.0f, 80.0f, 40.0f, -side, rules::kMineRiseRadius, 300, n.mine);
  n.mine_front = {-ms, 0};
  // Rochebrune, on the path at the foot of the mountains: the arena on one
  // side, the breeding farm on the other, houses along the path.
  {
    const float z = rules::kHamletZ + 16.0f * (hash01(seed, 20) - 0.5f);
    n.hamlet = {path_x_at(path, z), z};
    const float ahead = path_x_at(path, z - 8.0f), behind = path_x_at(path, z + 8.0f);
    Vec2 along{ahead - behind, -16.0f};
    along = along * (1.0f / length(along));
    n.hamlet_along = along;
    const Vec2 across{-along.z, along.x};
    // The arena on the side with more room in the world.
    float as = hash01(seed, 21) < 0.5f ? -1.0f : 1.0f;
    if (!rules::in_world(n.hamlet.x + across.x * as * 60.0f, z, 0.0f)) as = -as;
    // (The path bends: what stands beside it is moved away from it as far as needed.)
    const auto off_path = [&](Vec2 c, Vec2 away, float need) {
      for (int k = 0; k < 60 && distance_to_path(path, c.x, c.z, need + 1.0f) < need; ++k) c = c + away;
      return c;
    };
    n.arena = n.hamlet + across * (as * rules::kArenaFromPath) + along * (6.0f * (hash01(seed, 22) - 0.5f));
    n.arena = off_path(n.arena, across * as, rules::kArenaRadius + 7.0f);
    n.arena_front = across * -as;
    // The breeding farm across the path: its barn, its paddock beside it (further from the path).
    Vec2 ranch_at = n.hamlet - across * (as * rules::kRanchFromPath) + along * 4.0f;
    ranch_at = off_path(ranch_at, across * -as, 22.0f);
    {  // its doors look towards the path, along an axis
      const Vec2 to_path = n.hamlet - ranch_at;
      n.ranch_front = std::fabs(to_path.x) >= std::fabs(to_path.z) ? Vec2{to_path.x > 0 ? 1.0f : -1.0f, 0}
                                                                   : Vec2{0, to_path.z > 0 ? 1.0f : -1.0f};
    }
    n.ranch = ranch_at + along * 9.0f;
    n.paddock_hx = 9.0f;
    n.paddock_hz = 7.0f;
    if (std::fabs(n.ranch_front.z) > 0.5f) std::swap(n.paddock_hx, n.paddock_hz);
    n.paddock = ranch_at - along * 9.0f - n.ranch_front * 3.0f;
    // The houses along the path, their doors towards it; the well and the market by it.
    const struct {
      float u, v;
    } spots[] = {{18.0f, 13.0f}, {-17.0f, 14.0f}, {30.0f, -12.5f}, {-28.0f, -13.0f}, {-2.0f, -15.5f}};
    for (const auto& sp : spots) {
      const float hs = sp.v > 0 ? as : -as;
      const Vec2 c = off_path(n.hamlet + along * sp.u + across * (hs * std::fabs(sp.v)), across * hs, 9.5f);
      if (distance(c, n.arena) < rules::kArenaRadius + 6.0f || distance(c, n.ranch) < 10.0f ||
          distance(c, n.paddock) < std::max(n.paddock_hx, n.paddock_hz) + 5.0f) {
        continue;
      }
      const Vec2 to_path = Vec2{path_x_at(path, c.z), c.z} - c;
      n.houses.push_back(c);
      n.house_fronts.push_back(std::fabs(to_path.x) >= std::fabs(to_path.z) ? Vec2{to_path.x > 0 ? 1.0f : -1.0f, 0}
                                                                            : Vec2{0, to_path.z > 0 ? 1.0f : -1.0f});
    }
    n.well = off_path(n.hamlet + across * (as * 5.5f) - along * 3.0f, across * as, 4.0f);
    n.market = off_path(n.hamlet - across * (as * 6.5f) + along * 10.0f, across * -as, 5.0f);
    n.hamlet_teleporter = off_path(n.hamlet - along * 36.0f + across * (as * 7.0f), across * as, 6.0f);
  }
  // The springs further down, in the larches, on either side.
  find(-872.0f, 40.0f, 38.0f, 25.0f, hash01(seed, 3) < 0.5f ? -1.0f : 1.0f, rules::kSpringsRadius + 4.0f, 600, n.springs);
  // The col, where the path leaves the world; the frozen lake in the valley below it.
  n.col = {path_x_at(path, -1185.0f), -1185.0f};
  const float cs = hash01(seed, 4) < 0.5f ? -1.0f : 1.0f;
  n.col_teleporter = beside_path(path, n.col.z + 4.0f, cs * 7.0f);
  n.lake_rx = 26.0f + 8.0f * hash01(seed, 5);
  n.lake_rz = 17.0f + 6.0f * hash01(seed, 6);
  {
    const float z = -1105.0f + 20.0f * (hash01(seed, 7) - 0.5f);
    const float off = n.lake_rx + 9.0f + 6.0f * hash01(seed, 8);
    const float ls = -cs;  // across from the col's teleporter
    n.lake = {path_x_at(path, z) + ls * off, z};
    if (!rules::in_world(n.lake.x, n.lake.z, n.lake_rx + 15.0f)) n.lake.x = path_x_at(path, z) - ls * off;
  }
  // The cairns along the path, from the larches up to the col (not in the village).
  for (int k = 0; k < 7; ++k) {
    const float z = -880.0f - 52.0f * static_cast<float>(k);
    const float s = k % 2 == 0 ? 1.0f : -1.0f;
    const Vec2 c = beside_path(path, z, s * (rules::kPathHalfWidth + 1.7f));
    if (distance(c, n.hamlet) > rules::kHamletRadius + 6.0f && distance(c, n.springs) > rules::kSpringsRadius + 6.0f) n.cairns.push_back(c);
  }
  // The grotto: the natural cave the mine leads into opens here, on the
  // valley's side up to the col, across from the lake.
  {
    const float z = -1048.0f + 14.0f * (hash01(seed, 23) - 0.5f);
    const float gs = n.lake.x > path_x_at(path, n.lake.z) ? -1.0f : 1.0f;
    n.grotto = {path_x_at(path, z) + gs * (rules::kValleyHalf + 4.0f), z};
    if (!rules::in_world(n.grotto.x, n.grotto.z, 30.0f)) n.grotto.x = path_x_at(path, z) - gs * (rules::kValleyHalf + 4.0f);
    n.grotto_front = {n.grotto.x > path_x_at(path, z) ? -1.0f : 1.0f, 0};
  }
  // The pastures' dry-stone walls: a few curving lines off the path.
  Rng rng(seed ^ 0x4D555245545321ULL, 61);  // "MURETS!"
  for (int attempt = 0; attempt < 40 && n.walls.size() < 8; ++attempt) {
    const float z = -600.0f - 230.0f * static_cast<float>(rng.below(1000)) / 1000.0f;
    const float off = 14.0f + 120.0f * static_cast<float>(rng.below(1000)) / 1000.0f;
    const float s = rng.below(2) ? 1.0f : -1.0f;
    const Vec2 start{path_x_at(path, z) + s * off, z};
    const Vec2 d = dir16(static_cast<Angle16>(rng.below(65536)));
    const Vec2 across{-d.z, d.x};
    const float len = 16.0f + 26.0f * static_cast<float>(rng.below(1000)) / 1000.0f;
    const float bend = (static_cast<float>(rng.below(1000)) / 1000.0f - 0.5f) * 0.5f * len;
    const float bend2 = (static_cast<float>(rng.below(1000)) / 1000.0f - 0.5f) * 0.3f * len;
    const std::vector<Vec2> c{start, start + d * (len / 3) + across * bend, start + d * (2 * len / 3) + across * (bend + bend2),
                              start + d * len + across * (0.5f * bend)};
    std::vector<Vec2> pts = spline(c, 1.1f);
    bool ok = true;
    for (const Vec2& p : pts) {
      ok = ok && rules::in_world(p.x, p.z, 20.0f) && distance_to_path(path, p.x, p.z, 6.0f) >= 5.0f &&
           distance(p, n.refuge) > rules::kRefugeRadius + 8.0f && distance(p, n.mine) > rules::kMineRiseRadius + 6.0f &&
           distance(p, n.hamlet) > rules::kHamletRadius + 8.0f &&
           natural_height(seed, p.x, p.z) + (west.scarp.points.size() >= 2 && scarp_side(west, p.x, p.z) < -30.0f ? rules::kTerraceHeight : 0.0f) >
               rules::kSeaLevel + 4.0f &&
           (west.scarp.points.size() < 2 || std::fabs(scarp_side(west, p.x, p.z)) > 12.0f);
      for (const auto& other : n.walls) {
        for (const Vec2& q : other) ok = ok && distance(p, q) > 6.0f;
      }
      if (!ok) break;
    }
    if (ok) n.walls.push_back(std::move(pts));
  }
  return n;
}

// ---- The relief -------------------------------------------------------------------

void shape_north(Heightmap& m, NorthLayout& n, const Path& path, uint64_t seed) {
  if (!n.valid() || m.empty()) return;
  // Over a square around c (half `reach`), each grid point once.
  const auto each_point = [&](Vec2 c, float reach, auto&& f) {
    const int i0 = Heightmap::i_of(c.x - reach), i1 = std::min(Heightmap::i_of(c.x + reach) + 1, Heightmap::kPointsX - 1);
    const int j0 = Heightmap::j_of(c.z - reach), j1 = std::min(Heightmap::j_of(c.z + reach) + 1, Heightmap::kPointsZ - 1);
    for (int i = i0; i <= i1; ++i) {
      for (int j = j0; j <= j1; ++j) f(Heightmap::x_of(i), Heightmap::z_of(j), m.heights[Heightmap::index(i, j)]);
    }
  };
  // The ground's height around c (the middle and eight points r away).
  const auto mean = [&](Vec2 c, float r) {
    float sum = m.height(c.x, c.z);
    for (int k = 0; k < 8; ++k) {
      const Vec2 p = c + dir16(static_cast<Angle16>(k * 8192)) * r;
      sum += m.height(p.x, p.z);
    }
    return sum / 9.0f;
  };
  // Flat ground of radius `flat` at `top`, blending back into the ground out to `outer`.
  const auto level = [&](Vec2 c, float top, float flat, float outer) {
    each_point(c, outer, [&](float x, float z, float& h) {
      const float k = 1 - smooth(flat, outer, std::hypot(x - c.x, z - c.z));
      h += (top - h) * k;
    });
  };
  // The refuge.
  level(n.refuge, mean(n.refuge, 6.0f), rules::kRefugeRadius, rules::kRefugeRadius + 16.0f);
  // The springs: flat ground, its pool dug in the middle.
  {
    const float top = mean(n.springs, 4.0f);
    level(n.springs, top, rules::kSpringsRadius, rules::kSpringsRadius + 12.0f);
    each_point(n.springs, rules::kSpringsPool + 1.0f, [&](float x, float z, float& h) {
      const float r = std::hypot(x - n.springs.x, z - n.springs.z) / rules::kSpringsPool;
      if (r < 1) h = std::min(h, top - rules::kSpringsDepth * (1 - r * r));
    });
  }
  // The mine: its yard flat before the entrance, a rocky rise behind it.
  {
    const Vec2 f = n.mine_front;
    const Vec2 yard = n.mine + f * 4.0f;
    const float top = mean(yard, 3.0f);
    level(yard, top, rules::kMineYard, rules::kMineYard + 10.0f);
    const Vec2 c = n.mine - f * 15.0f;
    each_point(c, rules::kMineRiseRadius + 4.0f, [&](float x, float z, float& h) {
      const float wobble = 3.0f * (value_noise(seed + 810, x, z, 9.0f) - 0.5f) + 1.2f * (value_noise(seed + 811, x, z, 3.0f) - 0.5f);
      const float d = std::hypot(x - c.x, z - c.z) + wobble;
      const float along = (x - n.mine.x) * f.x + (z - n.mine.z) * f.z;  // out of the entrance
      const float behind = smooth(-0.3f, -2.6f, along);  // a face right behind the entrance (its sill at the yard's level)
      const float rise = rules::kMineRise * (1 - smooth(rules::kMineRiseRadius * 0.3f, rules::kMineRiseRadius, d)) * behind;
      h = std::max(h, top + rise);
    });
  }
  // The valley up to the col: the mountains' slopes cut back around the path
  // (a flat floor going up gently, walkable sides), and around the lake.
  // Rochebrune's flat ground: at the height of the ground around its middle;
  // the valley's floor goes on up from it.
  n.hamlet_level = std::max(mean(n.hamlet, 22.0f), rules::kSeaLevel + 4.0f);
  n.floor_start = std::max(m.height(path_x_at(path, rules::kFrostFrom), rules::kFrostFrom), rules::kSeaLevel + 4.0f);
  if (n.hamlet.z - rules::kHamletRadius < rules::kFrostFrom + 10.0f) n.floor_start = n.hamlet_level;
  n.lake_level = valley_floor(n, n.lake.z) + 0.2f;
  // (From Rochebrune's middle on: past its flat ground, the mountains are cut back all through.)
  const float from = std::max(rules::kFrostFrom + 50.0f, n.hamlet.z + 10.0f);
  // The distance to the path every kCoarse points (far from it, between
  // them is near enough: it changes by at most a meter a meter).
  constexpr int kCoarse = 4;
  const int rows = static_cast<int>((from - Heightmap::kMinZ) / Heightmap::kCellSize) + 1;
  const int cx = (Heightmap::kPointsX - 1) / kCoarse + 2, cz = rows / kCoarse + 2;
  std::vector<float> coarse(static_cast<size_t>(cx * cz));
  for (int a = 0; a < cx; ++a) {
    for (int b = 0; b < cz; ++b) {
      coarse[static_cast<size_t>(a * cz + b)] = distance_to_path(path, Heightmap::x_of(a * kCoarse), Heightmap::z_of(b * kCoarse), 170.0f);
    }
  }
  for (int j = 0; j < Heightmap::kPointsZ && Heightmap::z_of(j) < from; ++j) {
    const float z = Heightmap::z_of(j);
    const float k = smooth(from, rules::kFrostFrom, z);
    const float floor = valley_floor(n, z);
    for (int i = 0; i < Heightmap::kPointsX; ++i) {
      const float x = Heightmap::x_of(i);
      const int a = i / kCoarse, b = j / kCoarse;
      const float tx = static_cast<float>(i % kCoarse) / kCoarse, tz = static_cast<float>(j % kCoarse) / kCoarse;
      const auto at = [&](int p, int q) { return coarse[static_cast<size_t>(p * cz + q)]; };
      const float between = (at(a, b) * (1 - tx) + at(a + 1, b) * tx) * (1 - tz) + (at(a, b + 1) * (1 - tx) + at(a + 1, b + 1) * tx) * tz;
      // (Farther than 160 m, the cut is far above the ground: none.)
      const float d = between < 50.0f ? distance_to_path(path, x, z, 60.0f) : std::min(between, 160.0f);
      const auto side = [](float dd) { return 0.03f * std::min(dd, rules::kValleyHalf) + rules::kValleySlope * std::max(0.0f, dd - rules::kValleyHalf); };
      float cap = floor + side(d);
      // Around the lake: its shore at its ice's level.
      const float dl = (frozen_lake_distance(n, x, z) - 1.0f) * std::min(n.lake_rx, n.lake_rz);
      cap = std::min(cap, n.lake_level + side(std::max(0.0f, dl) + 4.0f));
      // Where the ground is lower (a hollow, the continent going down to the
      // sea by the coast), the valley's floor fills it, its sides going down gently.
      const float bumps = 0.8f * (value_noise(seed + 830, x, z, 9.0f) - 0.5f);
      const float fill = floor + bumps - rules::kValleySlope * std::max(0.0f, d - rules::kValleyHalf);
      float& h = m.heights[Heightmap::index(i, j)];
      h += (std::max(std::min(h, cap + bumps), fill) - h) * k;
    }
  }
  // The frozen lake: flat ice at the lowest of its shore (in a hollow: never
  // above the ground around), its shore rising gently.
  {
    float lowest = n.lake_level;
    for (int k = 0; k < 32; ++k) {
      const Vec2 d = dir16(static_cast<Angle16>(k * 2048));
      const float r = outline_scale(n.lake, kNorthLakeOutline, std::atan2(d.z, d.x));
      const Vec2 p = n.lake + Vec2{d.x * n.lake_rx * r * 1.05f, d.z * n.lake_rz * r * 1.05f};
      lowest = std::min(lowest, m.height(p.x, p.z));
    }
    n.lake_level = lowest - 0.05f;
  }
  // Rochebrune, flat (over the valley's start).
  {
    const Vec2 c = n.hamlet;
    each_point(c, rules::kHamletRadius + 34.0f, [&](float x, float z, float& h) {
      const float wobble = 6.0f * (value_noise(seed + 840, x, z, 17.0f) - 0.5f);
      const float k = 1 - smooth(rules::kHamletRadius, rules::kHamletRadius + 30.0f, std::hypot(x - c.x, z - c.z) + wobble);
      h += (n.hamlet_level - h) * k;
    });
  }
  // The grotto: a flat spot before its opening, a rocky slope rising behind it.
  {
    const Vec2 f = n.grotto_front;
    const Vec2 pad = n.grotto + f * 3.5f;
    const float top = m.height(pad.x, pad.z);
    level(pad, top, 4.5f, 10.0f);
    const Vec2 c = n.grotto - f * 12.0f;
    each_point(c, 24.0f, [&](float x, float z, float& h) {
      const float wobble = 2.5f * (value_noise(seed + 850, x, z, 8.0f) - 0.5f) + 1.0f * (value_noise(seed + 851, x, z, 3.0f) - 0.5f);
      const float d = std::hypot(x - c.x, z - c.z) + wobble;
      const float along = (x - n.grotto.x) * f.x + (z - n.grotto.z) * f.z;
      const float behind = smooth(-0.3f, -2.4f, along);
      const float rise = rules::kGrottoRise * (1 - smooth(6.0f, 20.0f, d)) * behind;
      if (rise > 0) h = std::max(h, top + rise);  // (only behind it: before it the ground stays)
    });
  }
  // Its banks walkable all around (cut back where the ground rose steeply).
  const float near = std::min(n.lake_rx, n.lake_rz);
  each_point(n.lake, std::max(n.lake_rx, n.lake_rz) * 1.5f + 45.0f, [&](float x, float z, float& h) {
    const float d = frozen_lake_distance(n, x, z);
    if (d < 1) {
      h = n.lake_level;
      return;
    }
    const float out = (d - 1.0f) * near;
    const float bank = n.lake_level + 0.08f * std::min(out, 2.0f) + rules::kValleySlope * std::max(0.0f, out - 2.0f);
    h = std::min(h, bank);
  });
}

// ---- Where things are ----------------------------------------------------------------

float frozen_lake_distance(const NorthLayout& n, float x, float z) {
  if (!n.valid()) return 1e9f;
  return outline_distance(n.lake, n.lake_rx, n.lake_rz, kNorthLakeOutline, x, z);
}

bool in_frozen_lake(const NorthLayout& n, float x, float z, float margin) {
  if (!n.valid()) return false;
  return outline_distance(n.lake, n.lake_rx + margin, n.lake_rz + margin, kNorthLakeOutline, x, z) < 1.0f;
}

bool in_springs(const NorthLayout& n, float x, float z) {
  return n.valid() && std::hypot(x - n.springs.x, z - n.springs.z) < rules::kSpringsPool - 0.3f;
}

bool in_hamlet(const NorthLayout& n, float x, float z, float margin) {
  return n.valid() && std::hypot(x - n.hamlet.x, z - n.hamlet.z) < rules::kHamletRadius + margin;
}

bool in_paddock(const NorthLayout& n, float x, float z, float margin) {
  return n.valid() && std::fabs(x - n.paddock.x) < n.paddock_hx + margin && std::fabs(z - n.paddock.z) < n.paddock_hz + margin;
}

bool in_north_place(const NorthLayout& n, float x, float z, float margin) {
  if (!n.valid()) return false;
  const Vec2 p{x, z};
  if (in_hamlet(n, x, z, margin)) return true;
  if (distance(p, n.grotto - n.grotto_front * 6.0f) < 9.0f + margin) return true;
  if (distance(p, n.refuge) < rules::kRefugeRadius + margin) return true;
  if (distance(p, n.springs) < rules::kSpringsRadius + margin) return true;
  if (distance(p, n.mine + n.mine_front * 4.0f) < rules::kMineYard + 3.0f + margin) return true;
  if (distance(p, n.mine - n.mine_front * 15.0f) < rules::kMineRiseRadius * 0.55f + margin) return true;  // its rocks, bare
  if (in_frozen_lake(n, x, z, 2.0f + margin)) return true;
  if (distance(p, n.col_teleporter) < 5.0f + margin || distance(p, n.refuge_teleporter) < 4.5f + margin) return true;
  for (const Vec2& c : n.cairns) {
    if (distance(p, c) < 1.5f + margin) return true;
  }
  for (const auto& wall : n.walls) {
    if (wall.empty()) continue;
    // (Far from its first point: not near any of it.)
    if (distance(p, wall.front()) > 50.0f + margin) continue;
    for (const Vec2& q : wall) {
      if (distance(p, q) < 1.2f + margin) return true;
    }
  }
  return false;
}

float north_snow(const NorthLayout& n, float x, float z, float h) {
  if (z > rules::kFrostFrom + 120.0f) return 0;
  if (in_frozen_lake(n, x, z, 0.5f)) return 0;
  // Further north, and higher above the valley: more snow; patches by the noise.
  const float north = smooth(rules::kFrostFrom + 120.0f, rules::kFrostFrom - 120.0f, z);
  const float above = n.valid() ? h - valley_floor(n, z) : h;
  const float high = smooth(3.0f, 22.0f, above);
  const float patches = value_noise(0x534E4F57ULL, x, z, 23.0f) * 0.6f + value_noise(0x534E4F58ULL, x, z, 7.0f) * 0.4f;
  // (Patches of old snow even on the valley's floor, the slopes white higher up.)
  return std::clamp(north * (0.85f * smooth(0.5f, 0.66f, patches) + high * 0.9f) - 0.2f * (1 - north), 0.0f, 1.0f);
}

// ---- The objects -------------------------------------------------------------------------

namespace {

Object make(ObjectKind kind, Vec2 pos, Angle16 yaw = 0, uint8_t variant = 0, Vec3 size = {1, 1, 1}) {
  Object o;
  o.kind = kind;
  o.pos = {pos.x, 0, pos.z};
  o.size = size;
  o.yaw = yaw;
  o.variant = variant;
  return o;
}

// The quarter turn whose front (local -Z) looks along `dir` (an axis).
Angle16 yaw_facing(Vec2 dir) {
  if (std::fabs(dir.x) >= std::fabs(dir.z)) return dir.x > 0 ? Angle16{49152} : Angle16{16384};
  return dir.z > 0 ? Angle16{32768} : Angle16{0};
}

Vec2 local_to_world(const Object& o, float lx, float lz) {
  const SinCos sc = sincos16(o.yaw);
  return {o.pos.x + lx * sc.cos + lz * sc.sin, o.pos.z - lx * sc.sin + lz * sc.cos};
}

// The yaw whose local +X runs along d (a unit vector), any angle.
Angle16 yaw_along_any(Vec2 d) {
  return static_cast<Angle16>(std::lround(std::atan2(-d.z, d.x) / 6.2831853f * 65536.0f) & 0xFFFF);
}

}  // namespace

void build_north(World& w) {
  const NorthLayout& n = w.north;
  if (!n.valid() || w.ground.empty()) return;
  const size_t first = w.objects.size();
  Rng rng(w.seed ^ 0x4E4F525448504345ULL, 47);  // "NORTHPCE": its own stream
  // One draw per statement (the order of a call's arguments differs between compilers).
  const auto v8 = [&]() { return static_cast<uint8_t>(rng.below(256)); };
  const auto any_yaw = [&]() { return static_cast<Angle16>(rng.below(65536)); };
  const auto add = [&](const Object& o) { w.objects.push_back(o); };
  // Stones and carts: only where nothing of the north stands yet.
  const auto add_if_free = [&](const Object& o) {
    // Not on a slope too steep for its size (it would float).
    {
      const Box box = object_box(o);
      const float center = w.ground.height(o.pos.x, o.pos.z);
      float lowest = center;
      for (float cx : {box.min.x, box.max.x}) {
        for (float cz : {box.min.z, box.max.z}) lowest = std::min(lowest, w.ground.height(cx, cz));
      }
      if (center - lowest > 0.6f * (box.max.y - box.min.y) + 0.1f) return;
    }
    Box b = object_box(o);
    b.min = b.min - Vec3{0.3f, 0, 0.3f};
    b.max = b.max + Vec3{0.3f, 0, 0.3f};
    b.min.y = -1e9f;
    b.max.y = 1e9f;
    for (size_t k = first; k < w.objects.size(); ++k) {
      Box other = object_box(w.objects[k]);
      other.min.y = -1e9f;
      other.max.y = 1e9f;
      if (overlaps(b, other)) return;
    }
    w.objects.push_back(o);
  };

  // The refuge, its door towards the path: a woodpile, barrels, a lantern, a signpost; its teleporter.
  {
    const Object chalet = make(ObjectKind::Chalet, n.refuge, yaw_facing(n.refuge_front), v8());
    add(chalet);
    add(make(ObjectKind::Woodpile, local_to_world(chalet, 0, 3.9f), chalet.yaw, v8()));
    add(make(ObjectKind::Barrel, local_to_world(chalet, 4.3f, -1.4f), 0, v8()));
    add(make(ObjectKind::Barrel, local_to_world(chalet, 4.35f, -0.5f), 0, v8()));
    add(make(ObjectKind::Lamp, local_to_world(chalet, -2.2f, -4.2f), 0));
    add(make(ObjectKind::Sign, n.refuge + n.refuge_front * 9.5f + Vec2{0, 2.0f}, chalet.yaw));
    add(make(ObjectKind::Teleporter, n.refuge_teleporter, 0, static_cast<uint8_t>(TeleporterId::Refuge)));
  }
  // The mine's entrance, carts in its yard, a few stones on its rise.
  {
    const Object mouth = make(ObjectKind::MineEntrance, n.mine, yaw_facing(n.mine_front), v8());
    add(mouth);
    // The rocks on both sides of its frame: solid (invisible boxes, from the ground).
    for (const float side : {-1.0f, 1.0f}) {
      Object c = make(ObjectKind::Collider, local_to_world(mouth, side * 2.4f, 0.7f), mouth.yaw, 0);
      c.size = {1.0f, 1.6f, 1.1f};
      add(c);
    }
    add(make(ObjectKind::Minecart, local_to_world(mouth, 3.6f, -4.6f), (mouth.yaw + 16384) & 0xFFFF, v8()));
    add_if_free(make(ObjectKind::Minecart, local_to_world(mouth, -4.2f, -6.0f), mouth.yaw, static_cast<uint8_t>(v8() | 1)));
    add(make(ObjectKind::Lamp, local_to_world(mouth, -2.6f, -2.2f), 0));
    for (int k = 0; k < 7; ++k) {
      const Vec2 d = dir16(any_yaw());
      const float r = 6.0f + static_cast<float>(rng.below(1200)) / 100.0f;
      const Vec2 at = n.mine - n.mine_front * 14.0f + d * r;
      const auto yaw = static_cast<Angle16>(rng.below(4) << 14);
      const uint8_t variant = v8();
      const float sx = 0.6f + static_cast<float>(rng.below(80)) / 100.0f;
      const float sy = 0.5f + static_cast<float>(rng.below(70)) / 100.0f;
      add_if_free(make(ObjectKind::Rock, at, yaw, variant, {sx, sy, sx}));
    }
  }
  // The hot springs: the pool, stones around it.
  add(make(ObjectKind::HotSpring, n.springs, any_yaw(), v8(), {rules::kSpringsPool, 1, rules::kSpringsPool}));
  for (int k = 0; k < 6; ++k) {
    const Vec2 d = dir16(static_cast<Angle16>(k * 10923 + static_cast<int>(rng.below(4000))));
    const float r = rules::kSpringsPool + 1.6f + static_cast<float>(rng.below(150)) / 100.0f;
    const auto yaw = static_cast<Angle16>(rng.below(4) << 14);
    const uint8_t variant = v8();
    const float s = 0.45f + static_cast<float>(rng.below(50)) / 100.0f;
    add_if_free(make(ObjectKind::Rock, n.springs + d * r, yaw, variant, {s, s * 0.8f, s}));
  }
  // The dry-stone walls: a stretch every 1.1 m along each line, turned along it.
  for (const auto& wall : n.walls) {
    Vec2 last{1e9f, 1e9f};
    for (size_t k = 0; k + 1 < wall.size(); ++k) {
      const bool gap = rng.below(100) < 4;  // a stretch fallen here and there
      const uint8_t variant = v8();
      const Vec2 d = wall[k + 1] - wall[k];
      const float len = length(d);
      const Vec2 at = (wall[k] + wall[k + 1]) * 0.5f;
      // Not into the last one (their boxes side by side, never overlapping), nor on a slope (it would float).
      if (gap || len < 1e-3f || (std::fabs(at.x - last.x) < 0.75f && std::fabs(at.z - last.z) < 0.75f)) continue;
      float lowest = 1e9f, highest = -1e9f;
      for (const float dx : {-0.37f, 0.37f}) {
        for (const float dz : {-0.37f, 0.37f}) {
          const float hh = w.ground.height(at.x + dx, at.z + dz);
          lowest = std::min(lowest, hh);
          highest = std::max(highest, hh);
        }
      }
      if (highest - lowest > 0.3f) continue;
      add(make(ObjectKind::StoneWall, at, yaw_along_any(d * (1 / len)), variant));
      last = at;
    }
  }
  // Rochebrune.
  {
    // The arena: its tiers solid all around (but its gate), benches and lanterns by the gate.
    const Vec2 g = std::fabs(n.arena_front.x) >= std::fabs(n.arena_front.z) ? Vec2{n.arena_front.x > 0 ? 1.0f : -1.0f, 0}
                                                                            : Vec2{0, n.arena_front.z > 0 ? 1.0f : -1.0f};
    const Object arena = make(ObjectKind::Arena, n.arena, yaw_facing(g), v8());
    add(arena);
    // (Two rings of boxes, never overlapping: 1.75 m apart along each, the rings 1.75 m apart.)
    for (const float r : {10.4f, 12.15f}) {
      const int count = static_cast<int>(6.2831853f * r / 1.75f);
      const int half_step = r > 11.0f ? 65536 / count / 2 : 0;
      for (int k = 0; k < count; ++k) {
        const Vec2 d = dir16(static_cast<Angle16>(k * 65536 / count + half_step));
        const float ahead = d.x * g.x + d.z * g.z, side = std::fabs(d.x * g.z - d.z * g.x);
        if (ahead > 0 && side * r < 2.4f) continue;  // the gate
        Object c = make(ObjectKind::Collider, n.arena + d * r, 0, 0);
        c.size = {0.6f, r < 11.0f ? 0.9f : 1.7f, 0.6f};
        add(c);
      }
    }
    const Vec2 gate = n.arena + g * (rules::kArenaRadius + 1.2f), gside{-g.z, g.x};
    add(make(ObjectKind::Lamp, gate + gside * 2.6f, 0));
    add(make(ObjectKind::Lamp, gate - gside * 2.6f, 0));
    add(make(ObjectKind::Sign, gate + gside * 3.6f + g * 1.5f, yaw_facing(g)));
    add_if_free(make(ObjectKind::Bench, n.arena + g * (rules::kArenaRadius + 3.2f) + gside * 6.0f, yaw_facing(g)));
    add_if_free(make(ObjectKind::Bench, n.arena + g * (rules::kArenaRadius + 3.2f) - gside * 6.0f, yaw_facing(g)));
    // The breeding farm: its barn, the paddock's fences (a gate towards the path), hay, a trough.
    const Object barn = make(ObjectKind::Barn, n.ranch, yaw_facing(n.ranch_front), v8());
    add(barn);
    {
      const Vec2 to_path = n.hamlet - n.paddock;
      for (int side = 0; side < 4; ++side) {
        const bool along_x = side < 2;
        const float sign = side % 2 == 0 ? -1.0f : 1.0f;
        const float half = along_x ? n.paddock_hx : n.paddock_hz;
        const int segments = static_cast<int>(half);  // 2 m each
        const bool gated = along_x ? (to_path.z * sign > 0 && std::fabs(to_path.z) >= std::fabs(to_path.x))
                                   : (to_path.x * sign > 0 && std::fabs(to_path.x) > std::fabs(to_path.z));
        for (int k = 0; k < segments; ++k) {
          const float t = -half + 1.0f + 2.0f * static_cast<float>(k);
          if (gated && std::fabs(t) < 1.5f) continue;
          // (The sides along Z just outside the others' ends: the corners never overlap.)
          const Vec2 at = along_x ? n.paddock + Vec2{t, sign * n.paddock_hz} : n.paddock + Vec2{sign * (n.paddock_hx + 0.08f), t};
          // (Not into the barn, where the paddock comes close to it: a gap in the fence there.)
          const Object fence = make(ObjectKind::Fence, at, along_x ? Angle16{0} : Angle16{16384}, v8());
          if (!overlaps(object_box(fence), object_box(barn))) add(fence);
        }
      }
    }
    add_if_free(make(ObjectKind::Haystack, n.paddock + Vec2{n.paddock_hx - 2.0f, n.paddock_hz - 2.0f}, 0, v8()));
    add_if_free(make(ObjectKind::Haystack, local_to_world(barn, -6.0f, 1.0f), 0, v8()));
    add_if_free(make(ObjectKind::Barrel, n.paddock + Vec2{-(n.paddock_hx - 1.5f), n.paddock_hz - 1.5f}, 0, v8()));
    add_if_free(make(ObjectKind::Lamp, local_to_world(barn, 5.4f, -3.0f), 0));
    // The houses, a barrel or a woodpile beside each.
    const ObjectKind kinds[] = {ObjectKind::Chalet, ObjectKind::HouseChimney, ObjectKind::HouseCottage, ObjectKind::Chalet,
                                ObjectKind::HouseChimney};
    for (size_t k = 0; k < n.houses.size(); ++k) {
      const Object house = make(kinds[k % std::size(kinds)], n.houses[k], yaw_facing(n.house_fronts[k]), v8());
      add_if_free(house);
      if (k % 2 == 0) {
        add_if_free(make(ObjectKind::Woodpile, local_to_world(house, 0, 3.9f), house.yaw, v8()));
      } else {
        add_if_free(make(ObjectKind::Barrel, local_to_world(house, 3.0f, -1.2f), 0, v8()));
      }
    }
    // The well; the market's stalls, crates and barrels around them; benches.
    add_if_free(make(ObjectKind::Well, n.well, 0, v8()));
    {
      const Vec2 to_path = n.hamlet - n.market;
      const Vec2 f = std::fabs(to_path.x) >= std::fabs(to_path.z) ? Vec2{to_path.x > 0 ? 1.0f : -1.0f, 0}
                                                                  : Vec2{0, to_path.z > 0 ? 1.0f : -1.0f};
      const Vec2 side{-f.z, f.x};
      for (const float u : {-1.6f, 1.6f}) add_if_free(make(ObjectKind::Stall, n.market + side * u, yaw_facing(f), v8()));
      Object crate = make(ObjectKind::Crate, n.market + side * 3.6f - f * 0.3f, 0, v8(), {0.35f, 0.35f, 0.35f});
      add_if_free(crate);
      add_if_free(make(ObjectKind::Barrel, n.market - side * 3.5f, 0, v8()));
    }
    add_if_free(make(ObjectKind::Bench, n.well + Vec2{2.4f, 0}, Angle16{16384}));
    // Lanterns along the path through the village.
    for (int k = -3; k <= 3; ++k) {
      const float z = n.hamlet.z + 13.0f * static_cast<float>(k);
      const float sx = k % 2 == 0 ? 1.0f : -1.0f;
      add_if_free(make(ObjectKind::Lamp, beside_path(w.path, z, sx * (rules::kPathHalfWidth + 1.3f)), 0));
    }
    add(make(ObjectKind::Teleporter, n.hamlet_teleporter, 0, static_cast<uint8_t>(TeleporterId::Hamlet)));
  }
  // The grotto's opening, its rocks solid on both sides.
  {
    const Object mouth = make(ObjectKind::Grotto, n.grotto, yaw_facing(n.grotto_front), v8());
    add(mouth);
    for (const float side : {-1.0f, 1.0f}) {
      Object c = make(ObjectKind::Collider, local_to_world(mouth, side * 2.6f, 0.6f), mouth.yaw, 0);
      c.size = {1.1f, 1.8f, 1.2f};
      add(c);
    }
  }
  // The cairns; the col's teleporter, a cairn on each side of the path there.
  for (const Vec2& c : n.cairns) {
    const float s = 0.8f + static_cast<float>(rng.below(50)) / 100.0f;
    add(make(ObjectKind::Cairn, c, any_yaw(), v8(), {s, s, s}));
  }
  add(make(ObjectKind::Teleporter, n.col_teleporter, 0, static_cast<uint8_t>(TeleporterId::Col)));
  for (const float s : {-1.0f, 1.0f}) {
    add(make(ObjectKind::Cairn, beside_path(w.path, n.col.z - 1.0f, s * (rules::kPathHalfWidth + 1.6f)), any_yaw(), v8(), {1.35f, 1.35f, 1.35f}));
  }
}

float north_wilds(const World& w, Object& o, float x, float z, float chance) {
  const NorthLayout& n = w.north;
  if (z > rules::kNorthFrom + 40.0f) return chance;
  if (in_north_place(n, x, z, 1.5f)) return 0;
  const float wave = 40.0f * (value_noise(w.seed + 820, x, z, 120.0f) - 0.5f);
  const bool tree = o.kind == ObjectKind::Tree || o.kind == ObjectKind::Pine;
  const float h = w.ground.height(x, z);
  if (z + wave > rules::kLarchesFrom) {
    // The pastures: open meadows, a few larches and pines, boulders.
    if (tree) {
      o.kind = value_noise(w.seed + 821, x, z, 40.0f) < 0.5f ? ObjectKind::Larch : ObjectKind::Pine;
      return 0.08f;
    }
    if (o.kind == ObjectKind::Log || o.kind == ObjectKind::Stump) return 0.05f;
    return 0.22f;
  }
  if (z + wave > rules::kFrostFrom) {
    // The larch woods, in patches; pines among them.
    const float patches = value_noise(w.seed + 822, x, z, 70.0f);
    if (tree) {
      o.kind = value_noise(w.seed + 823, x, z, 5.0f) < 0.68f ? ObjectKind::Larch : ObjectKind::Pine;
      return 0.25f + 0.6f * smooth(0.4f, 0.6f, patches);
    }
    return 0.3f;
  }
  // The mountains: rocks and scree, a few stunted pines, nothing high up.
  const float above = h - n.floor_start - rules::kValleyRise * std::max(0.0f, rules::kFrostFrom - z);
  if (tree) {
    o.kind = ObjectKind::Pine;
    o.size = o.size * 0.75f;
    return above > 18.0f ? 0.0f : 0.12f;
  }
  if (o.kind == ObjectKind::Rock) return 0.75f;
  return 0.12f;
}

// ---- The villagers -----------------------------------------------------------------------

void add_north_npcs(World& w) {
  const NorthLayout& n = w.north;
  if (!n.valid() || w.ground.empty() || inside(w)) return;
  struct Who {
    uint8_t npc;
    Vec2 at;
  };
  const Who people[] = {
      {npc::kSolene, n.refuge + n.refuge_front * 5.6f + Vec2{0, 1.6f}},
      {npc::kGaston, n.mine + n.mine_front * 5.0f + Vec2{n.mine_front.z, n.mine_front.x} * 2.0f},
      {npc::kBarnabe, n.arena + n.arena_front * (rules::kArenaRadius + 2.4f) + Vec2{-n.arena_front.z, n.arena_front.x} * 3.4f},
      {npc::kMargaux, n.paddock + (n.hamlet - n.paddock) * (std::max(n.paddock_hx, n.paddock_hz) / std::max(distance(n.hamlet, n.paddock), 1.0f)) * 1.15f},
      {npc::kAgathe, n.well + Vec2{-2.2f, 1.4f}},
  };
  const BodyShape& shape = species_info(Species::Villager).shape;
  for (const Who& who : people) {
    if (npc_index(w, who.npc) >= 0) continue;
    Creature c;
    c.species = Species::Villager;
    c.npc = who.npc;
    c.variant = static_cast<uint8_t>(mix(w.seed ^ (0x4E50430000ULL + who.npc)) & 0xFF);
    for (int k = 0; k < 40; ++k) {
      const Vec2 d = dir16(static_cast<Angle16>(k * 10923));
      const float r = k == 0 ? 0.0f : 0.8f + 0.35f * static_cast<float>(k / 6);
      const Vec2 p = who.at + d * r;
      c.body.pos = {p.x, w.ground.height(p.x, p.z), p.z};
      if (!inside_any_object(w, c.body, shape)) break;
    }
    c.home = {c.body.pos.x, c.body.pos.z};
    add_creature(w, c);
  }
}

// ---- Each tick -----------------------------------------------------------------------------

void north_step(World& w) {
  if (inside(w) || !w.north.valid() || w.battle.phase != BattlePhase::None) return;
  if (w.tick % rules::kSpringsHealEvery != 0 || !in_springs(w.north, w.player.pos.x, w.player.pos.z)) return;
  bool healed = false;
  for (Monster& m : w.team) {
    const uint16_t most = monster_fight_stats(m).hp;
    if (m.hp < most) {
      ++m.hp;
      healed = true;
    }
  }
  if (healed) w.healed_tick = w.tick;
}

// ---- The mine -------------------------------------------------------------------------------

void spawn_miners(World& w) {
  const Interior& in = w.interior;
  if (in.kind != PlaceKind::Mine || in.pieces.size() < 2) return;
  Rng& rng = w.rng;
  // The widest hall (the deepest one): the guardian, until it is beaten.
  const RoomPiece* deep = &in.pieces[1];
  for (const RoomPiece& p : in.pieces) {
    if (p.a == p.b && !p.natural && (!(deep->a == deep->b) || p.radius > deep->radius)) deep = &p;
  }
  const QuestState& q = w.quests[static_cast<size_t>(QuestId::MineGuardian)];
  const bool guardian = q.stage != QuestStage::Done && !(q.progress & kGuardianBeaten);
  const auto place = [&](Species species, Vec2 at, uint8_t level, Genes genes, Tag tag) {
    Creature c;
    c.species = species;
    c.body.pos = {at.x, floor_height(in, at.x, at.z), at.z};
    c.home = at;
    c.genes = genes;
    c.level = level;
    c.tag = tag;
    c.timer = static_cast<uint16_t>(rng.below(3 * rules::kTicksPerSecond));
    if (inside_any_object(w, c.body, species_info(species).shape)) return false;
    add_creature(w, c);
    return true;
  };
  if (guardian) {
    Genes g = roll_genes(Species::Golem, Zone::Mine, rng);
    g.form = Form::Omega;
    g.allele[0] = 0;  // a male (omegas are)
    g.chromatic = g.elemental = false;
    for (int k = 0; k < 12; ++k) {
      const Vec2 at{deep->b.x + (static_cast<float>(k % 4) - 1.5f) * 0.8f, deep->b.z + static_cast<float>(k / 4) * 0.9f};
      if (place(Species::Golem, at, rules::kGuardianLevel, g, Tag::None)) break;
    }
  }
  const uint32_t count = 8 + rng.below(4);
  for (uint32_t made = 0, attempt = 0; made < count && attempt < 150; ++attempt) {
    const RoomPiece& p = in.pieces[1 + rng.below(static_cast<uint32_t>(in.pieces.size() - 1))];
    const float t = static_cast<float>(rng.below(1000)) / 1000.0f;
    const Vec2 a = dir16(static_cast<Angle16>(rng.below(65536)));
    const float d = p.radius * 0.6f * static_cast<float>(rng.below(1000)) / 1000.0f;
    const Vec2 at{p.a.x + (p.b.x - p.a.x) * t + a.x * d, p.a.z + (p.b.z - p.a.z) * t + a.z * d};
    // (In the natural cave beyond the galleries: Givris of the cold rock, and Cailloux.)
    const bool natural = natural_at(in, at.x, at.z) > 0.5f;
    const Species species = natural ? (rng.below(100) < 55 ? Species::Ermine : Species::Golem)
                                    : rng.below(100) < 65 ? Species::Golem : Species::Mole;
    const Genes genes = roll_genes(species, Zone::Mine, rng);
    const auto level = static_cast<uint8_t>(7 + rng.below(4));
    if (wall_distance(in, at.x, at.z) < 0.8f || std::hypot(at.x, at.z) < 6.0f) continue;  // not by the way in
    if (guardian && std::hypot(at.x - deep->b.x, at.z - deep->b.z) < 6.0f) continue;     // the guardian's own
    if (place(species, at, level, genes, Tag::None)) ++made;
  }
}

void guardian_down(World& w, const Creature& c) {
  // The omega Caillou of the mine (the only one there is).
  if (w.place.kind != PlaceKind::Mine || c.species != Species::Golem || c.genes.form != Form::Omega) return;
  QuestState& q = w.quests[static_cast<size_t>(QuestId::MineGuardian)];
  q.progress |= kGuardianBeaten;
}

}  // namespace monstres
