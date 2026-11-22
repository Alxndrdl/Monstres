#include "monstres/core/sites.hpp"

#include "monstres/core/continent.hpp"
#include "monstres/core/heightmap.hpp"
#include "monstres/core/interior.hpp"
#include "monstres/core/north.hpp"
#include "monstres/core/quests.hpp"
#include "monstres/core/roads.hpp"
#include "monstres/core/west.hpp"

#include <algorithm>
#include <cmath>

namespace monstres {

namespace {

Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.z + b.z}; }
Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.z - b.z}; }
Vec2 operator*(Vec2 a, float k) { return {a.x * k, a.z * k}; }
float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.z * b.z; }
float length(Vec2 v) { return std::sqrt(v.x * v.x + v.z * v.z); }
float distance(Vec2 a, Vec2 b) { return length(a - b); }
Vec2 axis_of(Vec2 d) {
  return std::fabs(d.x) >= std::fabs(d.z) ? Vec2{d.x >= 0 ? 1.0f : -1.0f, 0} : Vec2{0, d.z >= 0 ? 1.0f : -1.0f};
}
Vec2 dir16(Angle16 a) {
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

// Distance to a polyline (and the length along it to the nearest point).
float polyline_distance(const std::vector<Vec2>& pts, Vec2 p, float* along = nullptr) {
  float best = 1e30f, gone = 0, at = 0;
  for (size_t i = 0; i + 1 < pts.size(); ++i) {
    const Vec2 a = pts[i], ab = pts[i + 1] - a;
    const float len2 = dot(ab, ab), len = std::sqrt(len2);
    const float t = len2 > 0 ? std::clamp(dot(p - a, ab) / len2, 0.0f, 1.0f) : 0.0f;
    const float d = distance(p, a + ab * t);
    if (d < best) {
      best = d;
      at = gone + t * len;
    }
    gone += len;
  }
  if (along) *along = at;
  return best;
}

float catmull_rom(float p0, float p1, float p2, float p3, float t) {
  const float t2 = t * t, t3 = t2 * t;
  return 0.5f * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 + (-p0 + 3 * p1 - 3 * p2 + p3) * t3);
}
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

// Whether a site was placed (not at the origin: none found).
bool placed(Vec2 p) { return p.x != 0 || p.z != 0; }

// A point every meter along a line (its last one at its end).
std::vector<Vec2> every_meter(const std::vector<Vec2>& c) {
  if (c.size() < 2) return c;
  std::vector<Vec2> pts{c.front()};
  float gone = 0;
  for (size_t i = 0; i + 1 < c.size(); ++i) {
    const float d = distance(c[i], c[i + 1]);
    float t = 1.0f - gone;
    while (t <= d) {
      pts.push_back(c[i] + (c[i + 1] - c[i]) * (t / std::max(d, 1e-6f)));
      t += 1.0f;
    }
    gone = d - (t - 1.0f);
  }
  if (distance(pts.back(), c.back()) > 0.3f) pts.push_back(c.back());
  return pts;
}

// The yaw whose local +X runs along `d` (an axis); the one whose front (local -Z) looks along it.
Angle16 yaw_along(Vec2 d) {
  if (std::fabs(d.x) >= std::fabs(d.z)) return d.x > 0 ? Angle16{0} : Angle16{32768};
  return d.z > 0 ? Angle16{49152} : Angle16{16384};
}
Angle16 yaw_facing(Vec2 d) {
  if (std::fabs(d.x) >= std::fabs(d.z)) return d.x > 0 ? Angle16{49152} : Angle16{16384};
  return d.z > 0 ? Angle16{32768} : Angle16{0};
}

// The sea (where the continent goes below its level), its water `below` deep
// at least (the hills' ground under it); land: not the sea, or above it.
bool sea_at(uint64_t seed, float x, float z, float below = 1.0f) {
  return continent_height(seed, x, z) < rules::kSeaLevel && natural_height(seed, x, z) < rules::kSeaLevel - below;
}
bool land_at(uint64_t seed, float x, float z) {
  return continent_height(seed, x, z) >= rules::kSeaLevel || natural_height(seed, x, z) > rules::kSeaLevel + 0.3f;
}

// The abbey's way up from the foot of its slope to its gate (control points).
std::vector<Vec2> abbey_way_points(Vec2 c, float hz, float s) {
  const std::vector<Vec2> ctl{c + Vec2{-s * 58.0f, hz + 84.0f}, c + Vec2{-s * 12.0f, hz + 60.0f}, c + Vec2{s * 60.0f, hz + 34.0f},
                              c + Vec2{s * 6.0f, hz + 12.0f}, c + Vec2{0, hz + 1.0f}};
  return every_meter(spline(ctl, 0.5f));
}

}  // namespace

// ---- Layout ----------------------------------------------------------------------------

SitesLayout make_sites(uint64_t seed, const Path& path, const VillageLayout& village, const RuinsLayout& ruins,
                       const ForestLayout& forest, const CaveLayout& cave, const CastleLayout& castle, const WestLayout& west,
                       const NorthLayout& north, const SiteHints& hints) {
  SitesLayout s;
  const auto& reachable = hints.reachable;
  if (path.points.size() < 2 || !west.valid() || !north.valid()) return s;
  // The side of the terrace's edge and the ground, as make_roads measured them on its grid.
  const auto edge_side = [&](float x, float z) { return hints.edge(x, z); };
  const auto ground = [&](float x, float z) { return hints.ground(x, z); };
  struct Taken {
    Vec2 at;
    float radius;
  };
  std::vector<Taken> taken;
  for (const auto& [at, radius] : hints.keep_off) taken.push_back({at, radius});
  const auto free_of_others = [&](Vec2 c, float r) {
    for (const Taken& t : taken) {
      if (distance(c, t.at) < r + t.radius + 25.0f) return false;
    }
    return true;
  };
  // Away from what is there already: the valley, the path, the terrace's
  // edge, the river, the west's and the north's places.
  const auto clear = [&](Vec2 c, float r, float edge_room = 25.0f) {
    if (!rules::in_world(c.x, c.z, r + 14.0f)) return false;
    if (std::max(std::fabs(c.x), std::fabs(c.z)) < rules::kValleyHalfSize + r + 10.0f) return false;
    if (!free_of_others(c, r) || !reachable(c.x, c.z)) return false;
    if (distance_to_path(path, c.x, c.z, r + 8.0f) < r + 7.0f) return false;
    if (std::fabs(edge_side(c.x, c.z)) < r + edge_room) return false;
    if (distance_to_stream(west.stream, c.x, c.z, r + 20.0f) < r + 18.0f) return false;
    if (in_west_place(west, c.x, c.z, r + 10.0f) || in_woods(west, c.x, c.z, r + 6.0f) || in_north_place(north, c.x, c.z, r + 10.0f)) return false;
    if (in_village(village, c.x, c.z, r + 10.0f) || in_ruins(ruins, c.x, c.z, r + 10.0f) || in_castle(castle, c.x, c.z, r + 10.0f) ||
        in_cave_hill(cave, c.x, c.z, r + 10.0f) || in_forest(forest, c.x, c.z, r)) {
      return false;
    }
    return free_of_others(c, r);
  };
  // How uneven the ground is over a disc (and its height in the middle; lowest).
  const auto survey = [&](Vec2 c, float r, float& mid, float& lowest) {
    float hi = -1e9f;
    lowest = 1e9f;
    mid = ground(c.x, c.z);
    for (int k = 0; k <= 8; ++k) {
      const Vec2 p = k == 8 ? c : c + dir16(static_cast<Angle16>(k * 8192)) * r;
      const float h = k == 8 ? mid : ground(p.x, p.z);
      hi = std::max(hi, h);
      lowest = std::min(lowest, h);
    }
    return hi - lowest;
  };
  // Firm, gentle ground for a site of radius r in a box of the world (the
  // best one: the most even, then the nearest to `want`).
  const auto find = [&](float x0, float x1, float z0, float z1, float step, float r, Vec2 want, Vec2& at) {
    float best = 1e30f;
    for (float z = z0; z <= z1; z += step) {
      for (float x = x0; x <= x1; x += step) {
        const Vec2 c{x, z};
        if (!clear(c, r)) continue;
        float mid = 0, lowest = 0;
        const float spread = survey(c, r, mid, lowest);
        if (lowest < rules::kSeaLevel + 3.0f || spread > 0.35f * r + 2.0f) continue;
        const float score = spread + 0.02f * distance(c, want);
        if (score < best) {
          best = score;
          at = c;
        }
      }
    }
    return best < 1e29f;
  };

  // The fishing port, on the coast south of the lighthouse: a shore with the
  // sea out along an axis (deep enough for piers), low land behind it.
  {
    float best = 1e30f;
    const Vec2 axes[4] = {{-1, 0}, {0, 1}, {0, -1}, {1, 0}};
    for (int pass = 0; pass < 6 && best > 1e29f; ++pass) {
      const bool any_land = pass == 5;  // (at last: any shore within reach, its quay will be made)
      const bool loose = pass >= 2;  // (none found: a rougher coast will do)
      const float z0 = pass == 0 ? 90.0f : -700.0f, z1 = 545.0f;
      for (float z = z0; z <= z1; z += 8.0f) {
        for (float x = -570.0f; x <= -60.0f; x += 8.0f) {
          const Vec2 p{x, z};
          if (!rules::in_world(x, z, 40.0f)) continue;
          const float here = natural_height(seed, x, z);
          if (here < rules::kSeaLevel + 0.2f || here > rules::kSeaLevel + 6.0f) continue;
          if (distance(p, west.lighthouse) < 130.0f || distance_to_stream(west.stream, x, z, 50.0f) < 45.0f) continue;
          if (distance_to_path(path, x, z, 50.0f) < 45.0f || in_west_place(west, x, z, 30.0f) || in_woods(west, x, z, 20.0f)) continue;
          if (pass < 4 && edge_side(x, z) < 30.0f) continue;  // (below the terrace)
          if (any_land && (x > -100.0f || distance_to_path(path, x, z, 80.0f) < 70.0f)) continue;  // (not on a lake by the path)
          for (const Vec2 d : axes) {
            const Vec2 u{-d.z, d.x};
            bool ok = true;
            const float shore = pass >= 3 ? 14.0f : 8.0f;  // (the water right there)
            ok = ok && sea_at(seed, p.x + d.x * shore, p.z + d.z * shore, 0.2f);
            for (float t = shore + 4.0f; t <= shore + 18.0f && ok; t += 4.0f) {
              for (const float o : {-8.0f, 0.0f, 8.0f}) ok = ok && sea_at(seed, p.x + d.x * t + u.x * o * (loose ? 0.6f : 1.0f), p.z + d.z * t + u.z * o * (loose ? 0.6f : 1.0f));
            }
            for (float t = 4.0f; t <= rules::kPortDepth && ok && !any_land; t += 6.0f) {
              for (const float o : {-18.0f, -9.0f, 0.0f, 9.0f, 18.0f}) {
                const Vec2 q = p - d * t + u * (o * (loose ? 0.65f : 1.0f));
                const float h = ground(q.x, q.z);
                ok = ok && h > rules::kSeaLevel + 0.3f && h < rules::kSeaLevel + (loose ? 16.0f : 12.0f);
              }
            }
            {  // (its lane's start within reach: or, on a rough coast, near it)
              const Vec2 g = p - d * (rules::kPortDepth + 3.0f);
              bool near = reachable(g.x, g.z);
              for (int k = 0; k < 8 && !near && pass >= 3; ++k) {
                const Vec2 q = g + dir16(static_cast<Angle16>(k * 8192)) * 14.0f;
                near = reachable(q.x, q.z);
              }
              ok = ok && near;
            }
            // (A pier fits, out from its middle at least.)
            for (float t = shore + 1.0f; t <= shore + 5.0f && ok; t += 2.0f) {
              for (const float w : {-2.0f, 2.0f}) ok = ok && sea_at(seed, p.x + d.x * t + u.x * w, p.z + d.z * t + u.z * w, pass == 4 ? 0.3f : 0.5f);
            }
            // (On the mainland, not an island: land on and on inland.)
            for (float t = rules::kPortDepth + 10.0f; t <= (loose ? 80.0f : 130.0f) && ok && !any_land; t += 10.0f) {
              for (const float o : {-10.0f, 10.0f}) {
                const Vec2 q = p - d * t + u * o;
                ok = ok && ground(q.x, q.z) > rules::kSeaLevel + 1.0f;
              }
            }
            if (!ok) continue;
            const float score = std::fabs(z - 300.0f) * 0.3f;
            if (score < best) {
              best = score;
              s.port = p + d * (shore - 8.0f);  // (its quay out to 8 m from the water)
              s.port_sea = d;
            }
          }
        }
      }
    }
    if (best < 1e29f) {
      const Vec2 d = s.port_sea, u{-d.z, d.x};
      // Its piers: out over the sea, as far as it is deep.
      for (const float o : {-13.0f, 0.0f, 13.0f}) {
        float len = 0;
        for (float l = 28.0f; l >= 12.0f; l -= 4.0f) {
          bool ok = true;
          for (float t = 9.0f; t <= l + 1.0f && ok; t += 2.0f) {
            for (const float w : {-2.0f, 2.0f}) {
              const Vec2 q = s.port + d * t + u * (o + w);
              ok = ok && sea_at(seed, q.x, q.z, 0.5f);
            }
          }
          if (ok) {
            len = l;
            break;
          }
        }
        if (len > 0) {
          s.piers.push_back(s.port + u * o);
          s.pier_lengths.push_back(len);
        }
      }
      const float spots[4][2] = {{-21.0f, 21.0f}, {-8.0f, 24.0f}, {8.0f, 23.0f}, {21.0f, 20.0f}};
      for (const auto& sp : spots) {
        s.port_houses.push_back(s.port + u * sp[0] - d * sp[1]);
        s.port_house_fronts.push_back(d);
      }
      s.port_market = s.port - d * 10.0f + u * 0.5f;
      s.port_teleporter = s.port - d * 31.0f + u * 13.0f;
      taken.push_back({s.port - d * 15.0f, 40.0f});
    }
  }

  // The abbey, up in the mountains east of the col: the most even ground
  // there, high up, away from the path's valley.
  {
    const float hx = rules::kAbbeyHalfX, hz = rules::kAbbeyHalfZ;
    s.abbey_hx = hx;
    s.abbey_hz = hz;
    float best = 1e30f;
    for (float z = -rules::kWorldNorth + hz + 14.0f; z <= -1105.0f; z += 9.0f) {
      for (float x = -400.0f; x <= rules::kWorldHalfSize - hx - 70.0f; x += 15.0f) {
        const Vec2 c{x, z};
        if (std::fabs(x) < hx + 80.0f && false) continue;
        if (distance_to_path(path, x, z, 200.0f) < hx + 110.0f) continue;
        {  // (its way's foot within reach of a lane)
          const float side = north.hamlet.x < x ? 1.0f : -1.0f;
          if (!reachable(x - side * 58.0f, z + hz + 84.0f)) continue;
        }
        if (distance(c, north.lake) < 120.0f || distance(c, north.grotto) < 100.0f || distance(c, north.col_teleporter) < 140.0f) continue;
        if (x - hx - 70.0f < -rules::kWorldHalfSize + 12.0f) continue;
        float sum = 0, sum2 = 0;
        int n = 0;
        for (int a = -2; a <= 2; ++a) {
          for (int b = -2; b <= 2; ++b) {
            const float h = ground(x + a * hx * 0.5f, z + b * hz * 0.5f);
            sum += h;
            sum2 += h * h;
            ++n;
          }
        }
        const float mean = sum / n, spread = std::sqrt(std::max(0.0f, sum2 / n - mean * mean));
        const float score = 0.5f * spread + 0.8f * std::max(0.0f, 40.0f - mean) + 0.8f * std::max(0.0f, mean - 65.0f) + 0.03f * std::fabs(x - 360.0f);
        if (score < best) {
          best = score;
          s.abbey = c;
        }
      }
    }
    if (best > 1e29f) s.abbey = {380.0f, -1150.0f};
    const Vec2 c = s.abbey;
    s.abbey_front = {0, 1};
    // Its way up winds towards Rochebrune.
    const float side = north.hamlet.x < c.x ? 1.0f : -1.0f;
    s.abbey_way = abbey_way_points(c, hz, side);
    s.temple = c + Vec2{0, -hz + 2.5f + rules::kTempleHalfZ};
    s.belfry = s.temple + Vec2{rules::kTempleHalfX + 6.0f, -2.0f};
    s.dorter = c + Vec2{-hx + 6.5f, 3.0f};
    s.refectory = c + Vec2{hx - 6.5f, 3.0f};
    s.cloister = c + Vec2{0, 5.0f};
    s.abbey_teleporter = c + Vec2{-side * 10.0f, hz - 6.0f};
    taken.push_back({c, hx + 30.0f});
  }

  // The camp, in the plains near the sea (south of the port).
  {
    float best = 1e30f;
    const float r = rules::kSeaCampRadius;
    for (float z = 180.0f; z <= 540.0f; z += 12.0f) {
      for (float x = -520.0f; x <= -60.0f; x += 12.0f) {
        const Vec2 c{x, z};
        if (!clear(c, r)) continue;
        float mid = 0, lowest = 0;
        const float spread = survey(c, r, mid, lowest);
        if (lowest < rules::kSeaLevel + 3.0f || spread > 0.3f * r + 2.0f) continue;
        float sea = 1e9f;
        for (float t = 30.0f; t <= 160.0f && sea > 1e8f; t += 20.0f) {
          for (int k = 0; k < 8; ++k) {
            const Vec2 q = c + dir16(static_cast<Angle16>(k * 8192)) * t;
            if (sea_at(seed, q.x, q.z)) {
              sea = t;
              break;
            }
          }
        }
        if (sea < 40.0f || sea > 160.0f) continue;
        const float score = std::fabs(sea - 70.0f) * 0.2f + spread * 2.0f;
        if (score < best) {
          best = score;
          s.camp = c;
        }
      }
    }
    if (best > 1e29f && !find(-400.0f, -60.0f, 200.0f, 540.0f, 10.0f, r, {-250.0f, 380.0f}, s.camp)) {
      find(-560.0f, 560.0f, 180.0f, 560.0f, 14.0f, r, {-250.0f, 380.0f}, s.camp);  // (the coast out of reach: in the plains)
    }
    s.camp_front = axis_of(Vec2{path_x_at(path, s.camp.z), s.camp.z} - s.camp);
    s.camp_teleporter = s.camp + s.camp_front * (r + 6.0f);
    taken.push_back({s.camp, r});
  }

  // The old watchtower, on the terrace by its edge above the sea, in the north-west.
  {
    float best = 1e30f;
    for (float z = -660.0f; z <= -250.0f; z += 8.0f) {
      for (float x = -520.0f; x <= -40.0f; x += 8.0f) {
        const Vec2 c{x, z};
        const float side = edge_side(x, z);
        if (side > -16.0f || side < -36.0f || !rules::in_world(x, z, 30.0f)) continue;
        if (distance_to_path(path, x, z, 50.0f) < 45.0f || !free_of_others(c, rules::kLookoutRadius) || !reachable(x, z)) continue;
        if (in_north_place(north, x, z, 20.0f) || in_west_place(west, x, z, 20.0f)) continue;
        float mid = 0, lowest = 0;
        if (survey(c, rules::kLookoutRadius, mid, lowest) > 4.0f) continue;
        // The sea below: past the edge, within 100 m.
        bool sea = false;
        for (int k = 0; k < 16 && !sea; ++k) {
          const Vec2 d = dir16(static_cast<Angle16>(k * 4096));
          for (float t = 40.0f; t <= 100.0f && !sea; t += 15.0f) {
            const Vec2 q = c + d * t;
            sea = sea_at(seed, q.x, q.z) && edge_side(q.x, q.z) > 0;
          }
        }
        if (!sea) continue;
        const float score = std::fabs(z + 450.0f);
        if (score < best) {
          best = score;
          s.lookout = c;
        }
      }
    }
    if (best > 1e29f && !find(-450.0f, -100.0f, -650.0f, -250.0f, 10.0f, rules::kLookoutRadius, {-250.0f, -450.0f}, s.lookout)) {
      find(-560.0f, 560.0f, -900.0f, -220.0f, 14.0f, rules::kLookoutRadius, {-250.0f, -450.0f}, s.lookout);
    }
    s.lookout_front = axis_of(Vec2{path_x_at(path, s.lookout.z), s.lookout.z} - s.lookout);
    taken.push_back({s.lookout, rules::kLookoutRadius});
  }

  // The quarry, in the hills to the east.
  {
    const float r = rules::kQuarryRadius + rules::kQuarryRim + 3.0f;
    if (!find(240.0f, 520.0f, -540.0f, -300.0f, 14.0f, r, {380.0f, -420.0f}, s.quarry)) {
      if (!find(150.0f, 540.0f, -620.0f, -220.0f, 14.0f, r, {380.0f, -420.0f}, s.quarry)) {
        find(-560.0f, 560.0f, -900.0f, 560.0f, 16.0f, r, {380.0f, -420.0f}, s.quarry);
      }
    }
    s.quarry_front = axis_of(Vec2{path_x_at(path, s.quarry.z), s.quarry.z} - s.quarry);
    taken.push_back({s.quarry, r});
  }
  // The chapel, east of the valley.
  {
    if (!find(200.0f, 520.0f, -300.0f, -100.0f, 14.0f, rules::kChapelRadius, {330.0f, -200.0f}, s.chapel)) {
      if (!find(170.0f, 540.0f, -320.0f, 140.0f, 14.0f, rules::kChapelRadius, {330.0f, -100.0f}, s.chapel)) {
        find(-560.0f, 560.0f, -900.0f, 560.0f, 16.0f, rules::kChapelRadius, {330.0f, -100.0f}, s.chapel);
      }
    }
    s.chapel_front = axis_of(Vec2{path_x_at(path, s.chapel.z), s.chapel.z} - s.chapel);
    taken.push_back({s.chapel, rules::kChapelRadius});
  }
  // The apiary, beside the path south of the valley.
  {
    float best = 1e30f;
    for (float z = 220.0f; z <= 450.0f; z += 6.0f) {
      for (const float side : {-1.0f, 1.0f}) {
        for (float off = 30.0f; off <= 80.0f; off += 6.0f) {
          const Vec2 c{path_x_at(path, z) + side * off, z};
          if (!clear(c, rules::kApiaryRadius)) continue;
          float mid = 0, lowest = 0;
          const float spread = survey(c, rules::kApiaryRadius, mid, lowest);
          if (lowest < rules::kSeaLevel + 3.0f || spread > 4.0f) continue;
          const float score = spread + 0.05f * off + 0.01f * std::fabs(z - 320.0f);
          if (score < best) {
            best = score;
            s.apiary = c;
          }
        }
      }
    }
    if (best > 1e29f && !find(-200.0f, 200.0f, 200.0f, 540.0f, 10.0f, rules::kApiaryRadius, {0.0f, 330.0f}, s.apiary)) {
      find(-560.0f, 560.0f, 180.0f, 560.0f, 14.0f, rules::kApiaryRadius, {0.0f, 330.0f}, s.apiary);
    }
    s.apiary_front = axis_of(Vec2{path_x_at(path, s.apiary.z), s.apiary.z} - s.apiary);
    taken.push_back({s.apiary, rules::kApiaryRadius});
  }
  // The dolmen, in the plains to the east.
  if (!find(240.0f, 530.0f, -80.0f, 220.0f, 14.0f, rules::kDolmenRadius, {400.0f, 80.0f}, s.dolmen)) {
    find(180.0f, 540.0f, -150.0f, 300.0f, 14.0f, rules::kDolmenRadius, {400.0f, 80.0f}, s.dolmen);
  }
  taken.push_back({s.dolmen, rules::kDolmenRadius});
  // The wreck, on a beach of the south-west.
  {
    float best = 1e30f;
    const Vec2 axes[4] = {{-1, 0}, {0, 1}, {0, -1}, {1, 0}};
    for (float z = 120.0f; z <= 560.0f; z += 6.0f) {
      for (float x = -575.0f; x <= -80.0f; x += 6.0f) {
        const Vec2 p{x, z};
        if (!rules::in_world(x, z, 20.0f) || !free_of_others(p, rules::kWreckRadius)) continue;
        const float h = natural_height(seed, x, z);
        if (h < rules::kSeaLevel + 0.15f || h > rules::kSeaLevel + 1.6f || edge_side(x, z) < 20.0f) continue;
        if (distance_to_stream(west.stream, x, z, 40.0f) < 35.0f || in_west_place(west, x, z, 20.0f)) continue;
        for (const Vec2 d : axes) {
          const Vec2 u{-d.z, d.x};
          bool ok = sea_at(seed, x + d.x * 13.0f, z + d.z * 13.0f) && land_at(seed, x - d.x * 9.0f, z - d.z * 9.0f);
          for (const float o : {-6.0f, 6.0f}) {
            const Vec2 q = p + u * o;
            const float hq = natural_height(seed, q.x, q.z);
            ok = ok && hq > rules::kSeaLevel + 0.15f && hq < rules::kSeaLevel + 1.8f;
          }
          if (!ok) continue;
          const float score = std::fabs(z - 420.0f) + (s.port.x != 0 && distance(p, s.port) < 90.0f ? 1e6f : 0.0f);
          if (score < best) {
            best = score;
            s.wreck = p;
            s.wreck_along = u;
          }
        }
      }
    }
    if (best > 1e29f) {
      s.wreck = {};
    } else {
      taken.push_back({s.wreck, rules::kWreckRadius});
    }
  }
  // The old aqueduct, across the plains of the south-east: a straight line
  // along an axis, over gently rolling ground.
  {
    const float len = rules::kAqueductArch * rules::kAqueductArches;
    float best = 1e30f;
    for (int axis = 0; axis < 2; ++axis) {
      const Vec2 a{axis == 0 ? 1.0f : 0.0f, axis == 0 ? 0.0f : 1.0f};
      for (float z = 200.0f; z <= 560.0f - (axis ? len : 0.0f); z += 20.0f) {
        for (float x = 160.0f; x <= 560.0f - (axis ? 0.0f : len); x += 20.0f) {
          const Vec2 p0{x, z};
          bool ok = true;
          float lo = 1e9f, hi = -1e9f;
          for (float t = 0; t <= len && ok; t += 4.0f) {
            const Vec2 q = p0 + a * t;
            ok = rules::in_world(q.x, q.z, 16.0f) && distance_to_path(path, q.x, q.z, 20.0f) > 16.0f &&
                 distance_to_stream(west.stream, q.x, q.z, 25.0f) > 20.0f && !in_forest(forest, q.x, q.z, 4.0f) &&
                 !in_castle(castle, q.x, q.z, 12.0f) && free_of_others(q, 3.0f) &&
                 std::max(std::fabs(q.x), std::fabs(q.z)) > rules::kValleyHalfSize + 10.0f;
            const float h = ok ? ground(q.x, q.z) : 0.0f;
            ok = ok && h > rules::kSeaLevel + 3.0f;
            lo = std::min(lo, h);
            hi = std::max(hi, h);
          }
          if (!ok || hi - lo > 9.0f) continue;
          const float score = std::fabs(hi - lo - 4.0f) + 0.01f * distance(p0, {360.0f, 380.0f});
          if (score < best) {
            best = score;
            s.aqueduct_a = p0;
            s.aqueduct_b = p0 + a * len;
          }
        }
      }
    }
  }
  return s;
}

// ---- The relief ----------------------------------------------------------------------

namespace {

// Brings the ground along a way to a line going from y0 at its first point
// to y1 at its last (by its length), flat over `half` on each side, blending
// back into the ground beyond, over `blend`.
void carve_way(Heightmap& m, const std::vector<Vec2>& pts, float y0, float y1, float half, float blend, uint64_t seed) {
  if (pts.size() < 2) return;
  float total = 0;
  for (size_t i = 0; i + 1 < pts.size(); ++i) total += distance(pts[i], pts[i + 1]);
  float x0 = 1e30f, z0 = 1e30f, x1 = -1e30f, z1 = -1e30f;
  for (const Vec2& p : pts) {
    x0 = std::min(x0, p.x);
    z0 = std::min(z0, p.z);
    x1 = std::max(x1, p.x);
    z1 = std::max(z1, p.z);
  }
  const float reach = half + blend + 2.0f;
  const int i0 = Heightmap::i_of(x0 - reach), i1 = std::min(Heightmap::i_of(x1 + reach) + 1, Heightmap::kPointsX - 1);
  const int j0 = Heightmap::j_of(z0 - reach), j1 = std::min(Heightmap::j_of(z1 + reach) + 1, Heightmap::kPointsZ - 1);
  for (int i = i0; i <= i1; ++i) {
    for (int j = j0; j <= j1; ++j) {
      const Vec2 p{Heightmap::x_of(i), Heightmap::z_of(j)};
      float along = 0;
      const float d = polyline_distance(pts, p, &along);
      if (d > reach) continue;
      const float wobble = 1.5f * (value_noise(seed + 1210, p.x, p.z, 7.0f) - 0.5f);
      const float k = 1 - smooth(half, half + blend, d + std::max(0.0f, wobble));
      const float target = y0 + (y1 - y0) * (total > 0 ? along / total : 0.0f);
      float& h = m.heights[Heightmap::index(i, j)];
      h += (target - h) * k;
    }
  }
}

}  // namespace

void shape_sites(Heightmap& m, SitesLayout& s, uint64_t seed) {
  if (!s.valid() || m.empty()) return;
  const auto each_point = [&](Vec2 c, float reach, auto&& f) {
    const int i0 = Heightmap::i_of(c.x - reach), i1 = std::min(Heightmap::i_of(c.x + reach) + 1, Heightmap::kPointsX - 1);
    const int j0 = Heightmap::j_of(c.z - reach), j1 = std::min(Heightmap::j_of(c.z + reach) + 1, Heightmap::kPointsZ - 1);
    for (int i = i0; i <= i1; ++i) {
      for (int j = j0; j <= j1; ++j) f(Heightmap::x_of(i), Heightmap::z_of(j), m.heights[Heightmap::index(i, j)]);
    }
  };
  const auto mean = [&](Vec2 c, float r) {
    float sum = m.height(c.x, c.z);
    for (int k = 0; k < 8; ++k) {
      const Vec2 p = c + dir16(static_cast<Angle16>(k * 8192)) * r;
      sum += m.height(p.x, p.z);
    }
    return sum / 9.0f;
  };
  // (Its blend wider where the ground is far above or below it: gentle all round.)
  const auto level = [&](Vec2 c, float top, float flat, float outer, uint64_t salt) {
    each_point(c, 2.0f * outer + 4.0f, [&](float x, float z, float& h) {
      const float wobble = 3.0f * (value_noise(seed + salt, x, z, 11.0f) - 0.5f);
      const float reach = std::clamp(flat + 2.8f * std::fabs(h - top), outer, flat + 2.0f * (outer - flat));
      const float k = 1 - smooth(flat, reach, std::hypot(x - c.x, z - c.z) + wobble);
      h += (top - h) * k;
    });
  };

  // The port's quay: flat at its level from the shore inland (filling what
  // is lower), going down quickly into the sea before it.
  if (s.port.x != 0 || s.port.z != 0) {
    const Vec2 d = s.port_sea, u{-d.z, d.x};
    const float L = rules::kPortLevel;
    s.port_level = L;
    each_point(s.port - d * (rules::kPortDepth * 0.5f), rules::kPortHalf + rules::kPortDepth + 50.0f, [&](float x, float z, float& h) {
      const Vec2 p{x, z};
      const float v = dot(p - s.port, d * -1.0f), w = std::fabs(dot(p - s.port, u));
      const float wobble = 4.0f * (value_noise(seed + 1220, x, z, 13.0f) - 0.5f);
      // (Its corners rounded, its edges wandering.)
      const float ox = std::max(w - rules::kPortHalf + 12.0f, 0.0f), oz = std::max(v - rules::kPortDepth + 12.0f, 0.0f);
      const float out = std::max(std::sqrt(ox * ox + oz * oz) - 12.0f, 0.0f) + std::max(0.0f, wobble);
      const float k = 1 - smooth(0.0f, std::max(24.0f, 2.8f * std::fabs(h - L)), out);
      if (k <= 0) return;
      if (v >= -3.0f) {
        h += (L - h) * k;
      } else if (v > -40.0f) {  // its bank going down into the sea, gently (land there: going up from it, gently)
        const float bank = L - 0.55f * (-3.0f - v), rise = L + 0.55f * (-3.0f - v);
        h += (std::clamp(h, bank, rise) - h) * k;
      }
    });
  }

  // The abbey: its ground flat, cut into the mountain (higher behind), built
  // up before it; then its way up from the foot of the slope, cut and built
  // up along it.
  {
    const Vec2 c = s.abbey;
    float sum = 0;
    int n = 0;
    for (int a = -2; a <= 2; ++a) {
      for (int b = -2; b <= 2; ++b) {
        sum += m.height(c.x + a * s.abbey_hx * 0.5f, c.z + b * s.abbey_hz * 0.5f);
        ++n;
      }
    }
    s.abbey_level = sum / n;
    const float L = s.abbey_level;
    each_point(c, std::max(s.abbey_hx, s.abbey_hz) + 50.0f, [&](float x, float z, float& h) {
      const float wobble = 5.0f * (value_noise(seed + 1230, x, z, 15.0f) - 0.5f) + 1.5f * (value_noise(seed + 1231, x, z, 5.0f) - 0.5f);
      const float out = std::max({std::fabs(x - c.x) - (s.abbey_hx + 5.0f), std::fabs(z - c.z) - (s.abbey_hz + 5.0f), 0.0f});
      const float blend = z > c.z ? 30.0f : (h > L ? 14.0f : 24.0f);
      const float k = out > 0 ? 1 - smooth(0.0f, blend, out + std::max(0.0f, wobble)) : 1.0f;  // (flat all inside)
      h += (L - h) * k;
    });
    if (s.abbey_way.size() >= 2) {
      const Vec2 foot = s.abbey_way.front();
      carve_way(m, s.abbey_way, m.height(foot.x, foot.z), L, rules::kAbbeyWayHalf, 9.0f, seed);
    }
  }
  // The camp, the lookout, the chapel, the apiary: flat ground.
  if (placed(s.camp)) level(s.camp, mean(s.camp, 8.0f), rules::kSeaCampRadius, rules::kSeaCampRadius + 14.0f, 1240);
  if (placed(s.lookout)) level(s.lookout, mean(s.lookout, 4.0f), rules::kLookoutRadius, rules::kLookoutRadius + 9.0f, 1241);
  if (placed(s.chapel)) level(s.chapel, mean(s.chapel, 7.0f), rules::kChapelRadius, rules::kChapelRadius + 12.0f, 1242);
  if (placed(s.apiary)) level(s.apiary, mean(s.apiary, 6.0f), rules::kApiaryRadius, rules::kApiaryRadius + 10.0f, 1243);
  // The quarry: a pit dug in steps, a ramp up out of it towards its lane.
  if (placed(s.quarry)) {
    const Vec2 c = s.quarry, f = s.quarry_front, side{-f.z, f.x};
    const float top = mean(c, rules::kQuarryRadius);
    const float F = top - rules::kQuarryDepth;
    s.quarry_level = F;
    const float R = rules::kQuarryRadius, rim = rules::kQuarryRim;
    each_point(c, R + rim + 14.0f, [&](float x, float z, float& h) {
      const Vec2 p{x, z};
      const float wobble = 3.0f * (value_noise(seed + 1250, x, z, 9.0f) - 0.5f) + 1.0f * (value_noise(seed + 1251, x, z, 3.0f) - 0.5f);
      const float d = distance(p, c) + wobble;
      float target = 1e9f;
      if (d < R) {
        target = F;
      } else if (d < R + rim) {  // three benches, their faces steep
        const float t = (d - R) / rim * 3.0f;
        const float k = std::floor(t), frac = t - k;
        target = F + rules::kQuarryDepth * (k + smooth(0.7f, 1.0f, frac)) / 3.0f + 0.3f * k;
      }
      const float v = dot(p - c, f), w = std::fabs(dot(p - c, side));
      const float cut = std::min(h, target);
      if (v > R - 3.0f && w < 5.0f) {  // the ramp
        const float ramp = F + std::clamp((v - (R - 3.0f)) / (rim + 10.0f), 0.0f, 1.0f) * (top - F);
        const float k = 1 - smooth(2.5f, 5.0f, w);
        h = cut + (std::min(h, std::max(cut, ramp)) - cut) * k;
      } else {
        h = cut;
      }
    });
  }
  // The dolmen: its ground flat, a low mound beside it.
  if (s.dolmen.x != 0 || s.dolmen.z != 0) {
    level(s.dolmen, m.height(s.dolmen.x, s.dolmen.z), 3.5f, 9.0f, 1260);
    const Vec2 mound = s.dolmen + Vec2{7.5f, 3.0f};
    each_point(mound, 9.0f, [&](float x, float z, float& h) {
      const float d = std::hypot(x - mound.x, z - mound.z);
      h += 1.7f * (1 - smooth(0.0f, 7.5f, d));
    });
  }
  // The aqueduct: its top's level, above the highest ground along it.
  if (s.aqueduct_a.x != 0 || s.aqueduct_a.z != 0) {
    float hi = -1e9f;
    const float len = distance(s.aqueduct_a, s.aqueduct_b);
    for (float t = 0; t <= len; t += 2.0f) {
      const Vec2 q = s.aqueduct_a + (s.aqueduct_b - s.aqueduct_a) * (t / len);
      hi = std::max(hi, m.height(q.x, q.z));
    }
    s.aqueduct_level = hi + 5.5f;
  }
}

// ---- Where things are ----------------------------------------------------------------

bool in_port(const SitesLayout& s, float x, float z, float margin) {
  if (s.port.x == 0 && s.port.z == 0) return false;
  const Vec2 d = s.port_sea, u{-d.z, d.x}, p{x, z};
  const float v = dot(p - s.port, d * -1.0f), w = std::fabs(dot(p - s.port, u));
  return w < rules::kPortHalf + margin && v > -4.0f - margin && v < rules::kPortDepth + margin;
}

bool bare_site(const SitesLayout& s, float x, float z) {
  if (!s.valid()) return false;
  const float dq = std::hypot(x - s.quarry.x, z - s.quarry.z);
  const float wobble = 3.0f * (value_noise(0x51554152ULL, x, z, 6.0f) - 0.5f);
  return (placed(s.quarry) && dq + wobble < rules::kQuarryRadius + rules::kQuarryRim - 1.0f) ||
         (placed(s.camp) && std::hypot(x - s.camp.x, z - s.camp.z) + wobble < 5.5f);
}

bool in_abbey(const SitesLayout& s, float x, float z, float margin) {
  return s.valid() && std::fabs(x - s.abbey.x) < s.abbey_hx + 1.5f + margin && std::fabs(z - s.abbey.z) < s.abbey_hz + 1.5f + margin;
}

bool in_site(const SitesLayout& s, float x, float z, float margin) {
  if (!s.valid()) return false;
  const Vec2 p{x, z};
  if (in_port(s, x, z, margin) || in_abbey(s, x, z, margin + 3.0f)) return true;
  if (distance(p, s.abbey_teleporter) < 4.0f + margin) return true;
  if (!s.abbey_way.empty() && std::fabs(x - s.abbey.x) < 140.0f && std::fabs(z - s.abbey.z) < 160.0f &&
      polyline_distance(s.abbey_way, p) < rules::kAbbeyWayHalf + 1.5f + margin) {
    return true;
  }
  if (placed(s.camp) && (distance(p, s.camp) < rules::kSeaCampRadius + margin || distance(p, s.camp_teleporter) < 4.0f + margin)) return true;
  if (placed(s.lookout) && distance(p, s.lookout) < rules::kLookoutRadius + margin) return true;
  if (placed(s.quarry) && distance(p, s.quarry) < rules::kQuarryRadius + rules::kQuarryRim + 2.0f + margin) return true;
  if (placed(s.chapel) && distance(p, s.chapel) < rules::kChapelRadius + margin) return true;
  if (placed(s.apiary) && distance(p, s.apiary) < rules::kApiaryRadius + margin) return true;
  if ((s.dolmen.x != 0 || s.dolmen.z != 0) && distance(p, s.dolmen) < rules::kDolmenRadius + margin) return true;
  if ((s.wreck.x != 0 || s.wreck.z != 0) && distance(p, s.wreck) < rules::kWreckRadius + margin) return true;
  if (s.aqueduct_a.x != 0 || s.aqueduct_a.z != 0) {
    if (polyline_distance({s.aqueduct_a, s.aqueduct_b}, p) < 3.0f + margin) return true;
  }
  return false;
}

// ---- The objects ---------------------------------------------------------------------

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

Vec2 local_to_world(const Object& o, float lx, float lz) {
  const SinCos sc = sincos16(o.yaw);
  return {o.pos.x + lx * sc.cos + lz * sc.sin, o.pos.z - lx * sc.sin + lz * sc.cos};
}

}  // namespace

void build_sites(World& w) {
  SitesLayout& s = w.sites;
  if (!s.valid() || w.ground.empty()) return;
  const size_t first = w.objects.size();
  Rng rng(w.seed ^ 0x5349544553504345ULL, 59);  // "SITESPCE": its own stream
  const auto v8 = [&]() { return static_cast<uint8_t>(rng.below(256)); };
  const auto r01 = [&]() { return static_cast<float>(rng.below(1000)) / 1000.0f; };
  const auto add = [&](const Object& o) { w.objects.push_back(o); };
  // Only on ground flat enough, where nothing of the sites' stands yet, off the ways.
  const auto add_if_free = [&](const Object& o) {
    Box b = object_box(o);
    {
      const float center = w.ground.height(o.pos.x, o.pos.z);
      float lowest = center;
      for (float cx : {b.min.x, b.max.x}) {
        for (float cz : {b.min.z, b.max.z}) lowest = std::min(lowest, w.ground.height(cx, cz));
      }
      if (center - lowest > 0.6f * (b.max.y - b.min.y) + 0.1f) return false;
    }
    if (under_water(w, o.pos.x, o.pos.z, 0.5f)) return false;
    const float reach = 0.5f * std::max(b.max.x - b.min.x, b.max.z - b.min.z);
    if (distance_to_path(w.path, o.pos.x, o.pos.z, reach + 2.0f) < reach + rules::kPathHalfWidth + 0.3f) return false;
    if (distance_to_lanes(w.roads, o.pos.x, o.pos.z, reach + 2.0f) < reach + rules::kLaneHalfWidth + 0.3f) return false;
    b.min = b.min - Vec3{0.3f, 0, 0.3f};
    b.max = b.max + Vec3{0.3f, 0, 0.3f};
    b.min.y = -1e9f;
    b.max.y = 1e9f;
    for (size_t k = first; k < w.objects.size(); ++k) {
      if (std::fabs(w.objects[k].pos.x - o.pos.x) > 20.0f || std::fabs(w.objects[k].pos.z - o.pos.z) > 20.0f) continue;
      Box other = object_box(w.objects[k]);
      other.min.y = -1e9f;
      other.max.y = 1e9f;
      if (overlaps(b, other)) return false;
    }
    w.objects.push_back(o);
    return true;
  };
  const auto ground_low = [&](Vec2 c, float hx, float hz) {
    float lowest = w.ground.height(c.x, c.z);
    for (float dx : {-hx, hx}) {
      for (float dz : {-hz, hz}) lowest = std::min(lowest, w.ground.height(c.x + dx, c.z + dz));
    }
    return lowest;
  };
  // An invisible wall from the ground up to `top` (a rail along a pier, an arch's pier...).
  const auto wall_to = [&](Vec2 c, float hx, float hz, float top) {
    Object col = make(ObjectKind::Collider, c, 0, 0);
    const float low = ground_low(c, hx, hz);
    col.size = {hx, std::max(0.2f, (top - low) * 0.5f), hz};
    add(col);
  };

  // ---- The port.
  if (s.port.x != 0 || s.port.z != 0) {
    const Vec2 d = s.port_sea, u{-d.z, d.x};
    const float deck = s.port_level + rules::kPierDeck;
    for (size_t k = 0; k < s.piers.size(); ++k) {
      const Vec2 root = s.piers[k];
      const int n = static_cast<int>(s.pier_lengths[k] / 4.0f);
      for (int i = 0; i < n; ++i) {
        const Vec2 c = root + d * (2.0f + 4.0f * static_cast<float>(i));
        Object pier = make(ObjectKind::Pier, c, yaw_along(d), v8(), {2.0f, 0.15f, rules::kPierHalf});
        pier.pos.y = deck - 0.3f;
        add(pier);
        // Its rails: solid from the sea's floor (one may not fall in).
        for (const float side : {-1.0f, 1.0f}) {
          const Vec2 r = c + u * (side * (rules::kPierHalf + 0.07f));
          wall_to(r, std::fabs(d.x) * 2.0f + std::fabs(u.x) * 0.06f, std::fabs(d.z) * 2.0f + std::fabs(u.z) * 0.06f, deck + 0.95f);
        }
      }
      const Vec2 end = root + d * (4.0f * static_cast<float>(n) + 0.07f);
      wall_to(end, std::fabs(u.x) * (rules::kPierHalf + 0.13f) + std::fabs(d.x) * 0.06f,
              std::fabs(u.z) * (rules::kPierHalf + 0.13f) + std::fabs(d.z) * 0.06f, deck + 0.95f);
      // Boats moored along it.
      for (int b = 0; b < 2; ++b) {
        const float t = 6.0f + (s.pier_lengths[k] - 9.0f) * (b == 0 ? 0.35f : 0.9f);
        const float side = (k + b) % 2 == 0 ? 1.0f : -1.0f;
        const Vec2 at = root + d * t + u * (side * (rules::kPierHalf + 1.55f));
        if (!sea_at(w.seed, at.x, at.z, 0.8f) || rng.below(100) < 20) continue;
        Object boat = make(ObjectKind::Boat, at, yaw_along(d), v8());
        add(boat);
      }
      add_if_free(make(ObjectKind::Lamp, root - d * 1.6f + u * 2.3f, 0));
      add_if_free(make(ObjectKind::Barrel, root - d * 2.0f - u * 2.4f, 0, v8()));
    }
    const ObjectKind kinds[4] = {ObjectKind::HouseCottage, ObjectKind::HouseHut, ObjectKind::HouseChimney, ObjectKind::HouseCottage};
    for (size_t k = 0; k < s.port_houses.size(); ++k) {
      const Object house = make(kinds[k % 4], s.port_houses[k], yaw_facing(s.port_house_fronts[k]), v8());
      if (!add_if_free(house)) continue;
      if (k % 2 == 0) add_if_free(make(ObjectKind::Barrel, local_to_world(house, 3.2f, -1.0f), 0, v8()));
      else add_if_free(make(ObjectKind::Woodpile, local_to_world(house, 0, 3.4f), house.yaw, v8()));
    }
    for (const float o : {-1.6f, 1.6f}) add_if_free(make(ObjectKind::Stall, s.port_market + u * o, yaw_facing(d), v8()));
    add_if_free(make(ObjectKind::Crate, s.port_market + u * 3.7f, 0, v8(), {0.35f, 0.35f, 0.35f}));
    add_if_free(make(ObjectKind::Barrel, s.port_market - u * 3.6f, 0, v8()));
    for (const float o : {-25.0f, -19.0f, 25.0f}) add_if_free(make(ObjectKind::FishRack, s.port - d * 6.5f + u * o, yaw_along(u), v8() & 0xFE));
    for (const float o : {11.0f, 19.0f}) add_if_free(make(ObjectKind::FishRack, s.port - d * 7.0f + u * o, yaw_along(u), v8() | 1));
    for (int k = 0; k < 4; ++k) {
      add_if_free(make(ObjectKind::Crate, s.port - d * (3.0f + r01() * 2.0f) + u * (-8.0f + 4.0f * k + r01()), 0, v8(),
                       {0.4f, 0.4f, 0.4f}));
    }
    for (const float o : {-12.0f, 12.0f}) add_if_free(make(ObjectKind::Lamp, s.port - d * 14.0f + u * o, 0));
    add_if_free(make(ObjectKind::Sign, s.port - d * (rules::kPortDepth - 1.0f) + u * 3.0f, yaw_facing(d * -1.0f)));
    add(make(ObjectKind::Teleporter, s.port_teleporter, 0, static_cast<uint8_t>(TeleporterId::Port)));
  }

  // ---- The abbey.
  {
    const Vec2 c = s.abbey;
    const float hx = s.abbey_hx, hz = s.abbey_hz;
    // Its walls, a gap for the gate in the front one.
    for (int k = 0; k < static_cast<int>(2 * hx / 4.0f); ++k) {
      const float x = -hx + 2.0f + 4.0f * static_cast<float>(k);
      add(make(ObjectKind::AbbeyWall, c + Vec2{x, -hz}, 0, v8()));
      if (std::fabs(x) > 4.5f) add(make(ObjectKind::AbbeyWall, c + Vec2{x, hz}, Angle16{32768}, v8()));
    }
    for (int k = 0; k < static_cast<int>(2 * hz / 4.0f); ++k) {
      const float z = -hz + 2.0f + 4.0f * static_cast<float>(k);
      add(make(ObjectKind::AbbeyWall, c + Vec2{-(hx + 0.51f), z}, Angle16{16384}, v8()));
      add(make(ObjectKind::AbbeyWall, c + Vec2{hx + 0.51f, z}, Angle16{49152}, v8()));
    }
    // (The front wall's stretches next to the gate: the gatehouse is 8 m wide.)
    const Object gate = make(ObjectKind::AbbeyGate, c + Vec2{0, hz}, Angle16{32768}, v8());
    add(gate);
    for (const float side : {-1.0f, 1.0f}) {
      Object col = make(ObjectKind::Collider, local_to_world(gate, side * 2.85f, 0), 0, 0);
      col.size = {1.15f, 3.2f, 1.1f};
      add(col);
    }
    add(make(ObjectKind::Temple, s.temple, Angle16{32768}, v8()));
    add(make(ObjectKind::Belfry, s.belfry, 0, v8()));
    add(make(ObjectKind::Dorter, s.dorter, yaw_facing({1, 0}), v8()));
    add(make(ObjectKind::Dorter, s.refectory, yaw_facing({-1, 0}), static_cast<uint8_t>(v8() | 1)));
    // The cloister's garden: a well, fruit trees, benches, a shrine.
    add_if_free(make(ObjectKind::Well, s.cloister, 0, v8()));
    for (const float sx : {-1.0f, 1.0f}) {
      for (const float sz : {-1.0f, 1.0f}) {
        const float sc = 0.9f + 0.25f * r01();
        add_if_free(make(ObjectKind::FruitTree, s.cloister + Vec2{sx * 10.0f, sz * 6.5f}, static_cast<Angle16>(rng.below(65536)), v8(), {sc, sc, sc}));
      }
      add_if_free(make(ObjectKind::Bench, s.cloister + Vec2{sx * 4.5f, 0}, Angle16{16384}, v8()));
    }
    add_if_free(make(ObjectKind::Shrine, s.cloister + Vec2{0, -7.0f}, 0, v8()));
    // The walk from the gate to the church: lanterns.
    for (float z = hz - 5.0f; z > -hz + 2.5f + 2 * rules::kTempleHalfZ + 3.0f; z -= 9.0f) {
      for (const float sx : {-3.2f, 3.2f}) add_if_free(make(ObjectKind::Lamp, c + Vec2{sx, z}, 0));
    }
    // The kitchen gardens by the front wall, the hives, the graves behind the bell tower.
    for (const float sx : {-1.0f, 1.0f}) {
      for (int k = 0; k < 2; ++k) {
        add_if_free(make(ObjectKind::Crops, c + Vec2{sx * (17.0f + 9.0f * k), hz - 8.0f}, 0, static_cast<uint8_t>(v8() | (k & 1)),
                         {3.6f, 1, 3.0f}));
      }
    }
    for (int k = 0; k < 5; ++k) add_if_free(make(ObjectKind::Beehive, c + Vec2{-hx + 4.0f + 2.2f * k, hz - 3.0f}, 0, v8()));
    for (int row = 0; row < 2; ++row) {
      for (int k = 0; k < 4; ++k) {
        add_if_free(make(ObjectKind::Gravestone, c + Vec2{hx - 15.0f + 2.4f * k, -hz + 4.0f + 2.6f * row}, Angle16{32768}, v8()));
      }
    }
    add_if_free(make(ObjectKind::Woodpile, s.refectory + Vec2{-0.5f, -14.5f}, Angle16{16384}, v8()));
    add_if_free(make(ObjectKind::Haystack, c + Vec2{-hx + 5.0f, -hz + 5.0f}, 0, v8()));
    // Outside: its teleporter, lanterns by the gate, a shrine at the way's turn, a signpost at its foot.
    add(make(ObjectKind::Teleporter, s.abbey_teleporter, 0, static_cast<uint8_t>(TeleporterId::Abbey)));
    for (const float side : {-1.0f, 1.0f}) add_if_free(make(ObjectKind::Lamp, c + Vec2{side * 5.6f, hz + 2.4f}, 0));
    if (s.abbey_way.size() > 40) {
      const Vec2 foot = s.abbey_way.front(), next = s.abbey_way[6];
      const Vec2 dd = (next - foot) * (1.0f / std::max(distance(next, foot), 1e-3f));
      add_if_free(make(ObjectKind::Sign, foot + dd * 3.0f + Vec2{-dd.z, dd.x} * 2.2f, yaw_facing(axis_of(dd * -1.0f))));
      for (size_t k = 30; k + 30 < s.abbey_way.size(); k += 40) {
        const Vec2 a = s.abbey_way[k], b = s.abbey_way[k + 2];
        const Vec2 t = (b - a) * (1.0f / std::max(distance(a, b), 1e-3f));
        add_if_free(make(ObjectKind::Cairn, a + Vec2{-t.z, t.x} * (rules::kAbbeyWayHalf + 1.6f), static_cast<Angle16>(rng.below(65536)), v8(),
                         {0.9f, 0.9f, 0.9f}));
      }
    }
  }

  // ---- The camp: tents round the fire (on the hut's side, away from its
  // opening), the hut, a palisade opening towards the lane, racks of hides,
  // its teleporter outside. (Laid out in its own frame: local +Z its opening.)
  if (placed(s.camp)) {
    const Vec2 c = s.camp;
    const float r = rules::kSeaCampRadius - 2.5f;
    const Object frame = make(ObjectKind::Collider, c, yaw_facing(s.camp_front * -1.0f));
    const auto at = [&](float lx, float lz) { return local_to_world(frame, lx, lz); };
    const auto turned = [&](Angle16 yaw) { return static_cast<Angle16>((yaw + frame.yaw) & 0xFFFF); };
    add(make(ObjectKind::Campfire, c, 0, v8()));
    for (int k = 0; k < 4; ++k) {
      const Vec2 dir = dir16(static_cast<Angle16>(36000 + k * 7000));  // (away from the opening)
      const Vec2 where = at(dir.x * 6.5f, dir.z * 6.5f);
      add_if_free(make(ObjectKind::Tent, where, yaw_facing(axis_of(c - where)), v8()));
    }
    add_if_free(make(ObjectKind::Cabin, at(-9.0f, -9.0f), turned(yaw_facing({1, 0})), v8()));
    add_if_free(make(ObjectKind::Woodpile, at(2.6f, -1.8f), turned(0), v8()));
    add_if_free(make(ObjectKind::FishRack, at(8.5f, -8.0f), turned(Angle16{16384}), v8() & 0xFE));
    add_if_free(make(ObjectKind::Barrel, at(-4.0f, -10.5f), 0, v8()));
    add_if_free(make(ObjectKind::Crate, at(-2.8f, -10.8f), 0, v8(), {0.4f, 0.4f, 0.4f}));
    add_if_free(make(ObjectKind::Lamp, at(3.0f, 3.0f), 0));
    // The palisade: a square of fences, open towards the lane (and the teleporter).
    for (int side = 0; side < 4; ++side) {
      const bool along_x = side < 2;
      const float sign = side % 2 == 0 ? -1.0f : 1.0f;
      for (int k = 0; k < static_cast<int>(r); ++k) {
        const float t = -r + 1.0f + 2.0f * static_cast<float>(k);
        if (side == 1 && std::fabs(t) < 2.5f) continue;
        const Vec2 where = along_x ? at(t, sign * r) : at(sign * (r + 0.08f), t);
        add_if_free(make(ObjectKind::Fence, where, turned(along_x ? Angle16{0} : Angle16{16384}), v8()));
      }
    }
    add(make(ObjectKind::Teleporter, s.camp_teleporter, 0, static_cast<uint8_t>(TeleporterId::Camp)));
  }

  // ---- The old watchtower on the cliffs: broken walls around it, a bench looking out to sea.
  if (placed(s.lookout)) {
    const Object tower = make(ObjectKind::Tower, s.lookout, yaw_facing(s.lookout_front), v8());
    add(tower);
    for (int k = 0; k < 4; ++k) {
      const Vec2 dir = dir16(static_cast<Angle16>(k * 16384 + 8192));
      const Vec2 at = s.lookout + dir * 6.0f;
      add_if_free(make(ObjectKind::RuinWall, at, static_cast<Angle16>((k & 1) << 14), static_cast<uint8_t>(v8() & 0xFE)));
    }
    add_if_free(make(ObjectKind::Rubble, local_to_world(tower, 3.6f, 2.5f), 0, v8()));
    add_if_free(make(ObjectKind::Bench, local_to_world(tower, 0, 6.2f), static_cast<Angle16>((tower.yaw + 32768) & 0xFFFF), v8()));
    add_if_free(make(ObjectKind::Lamp, local_to_world(tower, 2.2f, -4.4f), 0));
  }

  // ---- The quarry: blocks on its floor, carts, a crane by the rim, the hut above the ramp.
  if (placed(s.quarry)) {
    const Vec2 c = s.quarry, f = s.quarry_front, side{-f.z, f.x};
    for (int k = 0; k < 9; ++k) {
      const Vec2 at = c + dir16(static_cast<Angle16>(rng.below(65536))) * (3.0f + 9.0f * r01());
      const float a = 0.5f + 0.4f * r01(), b = 0.4f + 0.3f * r01(), e = 0.5f + 0.4f * r01();
      add_if_free(make(ObjectKind::Block, at, static_cast<Angle16>(rng.below(4) << 14), v8(), {a, b, e}));
    }
    add_if_free(make(ObjectKind::Minecart, c + f * 6.0f + side * 2.5f, yaw_along(f), static_cast<uint8_t>(v8() | 1)));
    add_if_free(make(ObjectKind::Minecart, c - side * 6.0f, yaw_along(side), v8()));
    add_if_free(make(ObjectKind::Crane, c - f * (rules::kQuarryRadius - 2.0f) + side * 4.0f, yaw_facing(f), v8()));
    add_if_free(make(ObjectKind::Cabin, c + f * (rules::kQuarryRadius + rules::kQuarryRim + 6.0f) + side * 9.0f, yaw_facing(side * -1.0f), v8()));
    add_if_free(make(ObjectKind::Lamp, c + f * (rules::kQuarryRadius - 1.0f) + side * 3.5f, 0));
    for (int k = 0; k < 6; ++k) {
      const Vec2 at = c + dir16(static_cast<Angle16>(rng.below(65536))) * (rules::kQuarryRadius + 1.0f + 6.0f * r01());
      const float sc = 0.4f + 0.3f * r01();
      add_if_free(make(ObjectKind::Rubble, at, 0, v8(), {sc, sc, sc}));
    }
  }

  // ---- The chapel: its graves behind it within a low wall, yews, a bench, a lantern.
  if (placed(s.chapel)) {
    const Object chapel = make(ObjectKind::Chapel, s.chapel, yaw_facing(s.chapel_front), v8());
    add(chapel);
    for (int row = 0; row < 3; ++row) {
      for (int k = 0; k < 4; ++k) {
        add_if_free(make(ObjectKind::Gravestone, local_to_world(chapel, -6.5f + 2.2f * k + (row & 1) * 0.6f, 6.5f + 2.4f * row), chapel.yaw, v8()));
      }
    }
    // The low wall round the graves.
    for (float t = -9.0f; t <= 9.0f; t += 0.74f) {
      for (const float lz : {4.4f, 13.8f}) {
        const Vec2 at = local_to_world(chapel, t, lz);
        if (lz < 5 && std::fabs(t) < 4.5f) continue;  // (the chapel's back, the way through)
        add_if_free(make(ObjectKind::StoneWall, at, 0, v8()));
      }
    }
    for (float t = 5.2f; t <= 13.2f; t += 0.74f) {
      for (const float lx : {-9.4f, 9.4f}) add_if_free(make(ObjectKind::StoneWall, local_to_world(chapel, lx, t), 0, v8()));
    }
    for (const float lx : {-11.5f, 11.5f}) add_if_free(make(ObjectKind::Pine, local_to_world(chapel, lx, 2.0f), 0, v8(), {1.1f, 1.1f, 1.1f}));
    add_if_free(make(ObjectKind::Bench, local_to_world(chapel, -4.5f, -5.0f), chapel.yaw, v8()));
    add_if_free(make(ObjectKind::Lamp, local_to_world(chapel, 2.6f, -4.6f), 0));
  }

  // ---- The apiary: the hut, rows of hives behind it, fruit trees around.
  if (placed(s.apiary)) {
    const Object hut = make(ObjectKind::HouseHut, s.apiary, yaw_facing(s.apiary_front), v8());
    add(hut);
    for (int row = 0; row < 3; ++row) {
      for (int k = 0; k < 4; ++k) add_if_free(make(ObjectKind::Beehive, local_to_world(hut, -4.5f + 3.0f * k, 5.5f + 2.6f * row), 0, v8()));
    }
    for (int k = 0; k < 5; ++k) {
      const Vec2 at = s.apiary + dir16(static_cast<Angle16>(rng.below(65536))) * (8.5f + 3.0f * r01());
      const float sc = 0.85f + 0.3f * r01();
      add_if_free(make(ObjectKind::FruitTree, at, static_cast<Angle16>(rng.below(65536)), v8(), {sc, sc, sc}));
    }
    add_if_free(make(ObjectKind::Barrel, local_to_world(hut, 3.0f, -1.2f), 0, v8()));
    add_if_free(make(ObjectKind::Bench, local_to_world(hut, -3.6f, -3.2f), hut.yaw, v8()));
    for (int k = 0; k < 5; ++k) add_if_free(make(ObjectKind::Fence, local_to_world(hut, -5.0f + 2.0f * k, 13.6f), hut.yaw, v8()));
  }

  // ---- The dolmen and the standing stones round it.
  if (s.dolmen.x != 0 || s.dolmen.z != 0) {
    add(make(ObjectKind::Dolmen, s.dolmen, static_cast<Angle16>(rng.below(2) << 14), v8()));
    for (int k = 0; k < 5; ++k) {
      const Vec2 at = s.dolmen + dir16(static_cast<Angle16>(k * 13107 + rng.below(3000))) * (5.5f + r01());
      const float hgt = 1.2f + 0.8f * r01();
      add_if_free(make(ObjectKind::Menhir, at, 0, v8(), {1, hgt, 1}));
    }
  }

  // ---- The wreck, crates and barrels washed up around it, driftwood.
  if (s.wreck.x != 0 || s.wreck.z != 0) {
    add(make(ObjectKind::Wreck, s.wreck, yaw_along(s.wreck_along), v8()));
    for (int k = 0; k < 6; ++k) {
      const Vec2 at = s.wreck + dir16(static_cast<Angle16>(rng.below(65536))) * (5.0f + 4.0f * r01());
      if (k % 3 == 0) add_if_free(make(ObjectKind::Barrel, at, 0, v8()));
      else if (k % 3 == 1) add_if_free(make(ObjectKind::Crate, at, 0, v8(), {0.4f, 0.4f, 0.4f}));
      else add_if_free(make(ObjectKind::Log, at, static_cast<Angle16>(rng.below(2) << 14), v8(), {1.3f, 0.2f, 1}));
    }
  }

  // ---- The aqueduct's arches, a few fallen; each arch's piers solid.
  if (s.aqueduct_a.x != 0 || s.aqueduct_a.z != 0) {
    const Vec2 a = (s.aqueduct_b - s.aqueduct_a) * (1.0f / std::max(distance(s.aqueduct_a, s.aqueduct_b), 1e-3f));
    const Vec2 across{-a.z, a.x};
    for (int k = 0; k < rules::kAqueductArches; ++k) {
      const Vec2 c = s.aqueduct_a + a * (rules::kAqueductArch * (static_cast<float>(k) + 0.5f));
      const bool fallen = k > 1 && k < rules::kAqueductArches - 2 && rng.below(100) < 18;
      const float low = ground_low(c, 4.0f * std::fabs(a.x) + 1.0f * std::fabs(across.x), 4.0f * std::fabs(a.z) + 1.0f * std::fabs(across.z));
      const float height = s.aqueduct_level - low;
      if (fallen) {
        add_if_free(make(ObjectKind::Rubble, c + across * 1.5f, 0, v8(), {1.2f, 1.2f, 1.2f}));
        add_if_free(make(ObjectKind::Block, c - across * 1.6f + a * 1.5f, static_cast<Angle16>(rng.below(4) << 14), v8(), {0.8f, 0.5f, 0.6f}));
      } else {
        add(make(ObjectKind::Aqueduct, c, yaw_along(a), v8(), {1, height, 1}));
      }
      // Its piers (the one between two arches is shared): even under a fallen arch, they stand.
      for (const float e : {-1.0f, 1.0f}) {
        if (e > 0 && k + 1 < rules::kAqueductArches) continue;
        const Vec2 p = c + a * (e * rules::kAqueductArch * 0.5f);
        wall_to(p, std::fabs(a.x) * 0.75f + std::fabs(across.x) * 1.05f, std::fabs(a.z) * 0.75f + std::fabs(across.z) * 1.05f,
                fallen && e < 0 ? low + height * 0.6f : s.aqueduct_level);
      }
    }
  }
}

// ---- The villagers -------------------------------------------------------------------

void add_site_npcs(World& w) {
  const SitesLayout& s = w.sites;
  if (!s.valid() || w.ground.empty() || w.place.kind != PlaceKind::Outside) return;
  struct Who {
    uint8_t npc;
    Vec2 at;
  };
  const Vec2 d = s.port_sea, u{-d.z, d.x};
  const Vec2 pier = s.piers.empty() ? s.port : s.piers[s.piers.size() / 2];
  const Vec2 camp_side{s.camp_front.z, -s.camp_front.x};  // (the camp's local +X)
  const Who people[] = {
      {npc::kLoic, pier - d * 2.5f + u * 2.6f},
      {npc::kNolwenn, s.port_market - d * 1.8f},
      {npc::kGauthier, s.abbey + Vec2{3.5f, s.abbey_hz - 4.0f}},
      {npc::kYsolde, s.camp + camp_side * 2.4f + s.camp_front * 2.0f},
      {npc::kTristan, s.camp - camp_side * 6.0f + s.camp_front * 5.5f},
      {npc::kArmand, s.lookout + s.lookout_front * 5.0f + Vec2{s.lookout_front.z, s.lookout_front.x} * 2.0f},
      {npc::kRaoul, s.quarry + s.quarry_front * 8.0f - Vec2{-s.quarry_front.z, s.quarry_front.x} * 2.0f},
      {npc::kSylvain, s.chapel + s.chapel_front * 5.5f + Vec2{s.chapel_front.z, s.chapel_front.x} * 1.8f},
      {npc::kHeloise, s.apiary + s.apiary_front * 4.5f + Vec2{s.apiary_front.z, s.apiary_front.x} * 2.2f},
  };
  const BodyShape& shape = species_info(Species::Villager).shape;
  for (const Who& who : people) {
    if (npc_index(w, who.npc) >= 0) continue;
    const Vec2 site = who.npc == npc::kLoic || who.npc == npc::kNolwenn ? s.port
                      : who.npc == npc::kYsolde || who.npc == npc::kTristan ? s.camp
                      : who.npc == npc::kArmand ? s.lookout : who.npc == npc::kRaoul ? s.quarry
                      : who.npc == npc::kSylvain ? s.chapel : who.npc == npc::kHeloise ? s.apiary : s.abbey;
    if (!placed(site)) continue;
    Creature c;
    c.species = Species::Villager;
    c.npc = who.npc;
    c.variant = static_cast<uint8_t>(mix(w.seed ^ (0x4E50430000ULL + who.npc)) & 0xFF);
    for (int k = 0; k < 40; ++k) {
      const Vec2 dd = dir16(static_cast<Angle16>(k * 10923));
      const float rad = k == 0 ? 0.0f : 0.8f + 0.35f * static_cast<float>(k / 6);
      const Vec2 p = who.at + dd * rad;
      c.body.pos = {p.x, w.ground.height(p.x, p.z), p.z};
      if (!inside_any_object(w, c.body, shape)) break;
    }
    c.home = {c.body.pos.x, c.body.pos.z};
    add_creature(w, c);
  }
}

// ---- The temple's monks ----------------------------------------------------------------

Vec2 monk_spot(uint8_t n) {
  switch (n) {
    case npc::kHugues: return {-5.0f, -6.5f};
    case npc::kOdile: return {5.0f, -11.5f};
    case npc::kBenoit: return {-5.0f, -16.5f};
    case npc::kAmbroise: return {0.0f, -24.0f};
    default: return {0, -4.0f};
  }
}

void spawn_monks(World& w) {
  if (w.interior.kind != PlaceKind::Temple) return;
  for (uint8_t n = npc::kHugues; n <= npc::kAmbroise; ++n) {
    if (npc_index(w, n) >= 0) continue;
    Creature c;
    c.species = Species::Villager;
    c.npc = n;
    c.variant = static_cast<uint8_t>(mix(w.seed ^ (0x4D4F494E45ULL + n)) & 0xFF);
    const Vec2 at = monk_spot(n);
    c.body.pos = {at.x, 0, at.z};
    c.body.facing_x = 0;
    c.body.facing_z = 1;
    c.home = at;
    add_creature(w, c);
    keep_trainer_monster(w, n);
  }
}

}  // namespace monstres
