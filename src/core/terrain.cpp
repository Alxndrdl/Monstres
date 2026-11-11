#include "monstres/core/terrain.hpp"

#include "monstres/core/rng.hpp"

#include <algorithm>
#include <cmath>

namespace monstres {

namespace {

// Every 6 m, from beyond one edge of the world to beyond the other.
constexpr int kControlPoints = 2 * (static_cast<int>(rules::kPathExtent / 6.0f) + 1) + 1;
constexpr float kControlStep = 6.0f;
constexpr int kSamplesPerSegment = 8;

// Catmull-Rom spline through p1..p2 (p0 and p3 shape the tangents).
float catmull_rom(float p0, float p1, float p2, float p3, float t) {
  const float t2 = t * t, t3 = t2 * t;
  return 0.5f * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 +
                 (-p0 + 3 * p1 - 3 * p2 + p3) * t3);
}

}  // namespace

Path make_path(uint64_t seed) {
  Rng rng(seed ^ 0x5041544850415448ULL, 7);  // own stream: does not disturb the object layout
  float xs[kControlPoints];
  const int middle = kControlPoints / 2;
  xs[middle] = static_cast<float>(static_cast<int>(rng.below(201)) - 100) / 100.0f;  // ±1 m from spawn
  // Random walk outwards from the middle, at most ±3.5 m per step, kept within ±kPathMaxX.
  for (int dir : {1, -1}) {
    for (int i = middle + dir; i >= 0 && i < kControlPoints; i += dir) {
      const float step = static_cast<float>(static_cast<int>(rng.below(701)) - 350) / 100.0f;
      xs[i] = std::clamp(xs[i - dir] + step, -rules::kPathMaxX, rules::kPathMaxX);
    }
  }

  Path path;
  const float z0 = -kControlStep * static_cast<float>(middle);
  for (int seg = 0; seg + 1 < kControlPoints; ++seg) {
    const float p0 = xs[std::max(seg - 1, 0)];
    const float p1 = xs[seg];
    const float p2 = xs[seg + 1];
    const float p3 = xs[std::min(seg + 2, kControlPoints - 1)];
    for (int s = 0; s < kSamplesPerSegment; ++s) {
      const float t = static_cast<float>(s) / kSamplesPerSegment;
      path.points.push_back({catmull_rom(p0, p1, p2, p3, t),
                             z0 + kControlStep * (static_cast<float>(seg) + t)});
    }
  }
  path.points.push_back({xs[kControlPoints - 1], z0 + kControlStep * (kControlPoints - 1)});
  return path;
}

float distance_to_path(const Path& path, float x, float z, float limit) {
  const auto& pts = path.points;
  float best2 = limit * limit;
  if (pts.size() < 2) return std::sqrt(best2);
  auto visit = [&](size_t i) {
    const Vec2 a = pts[i], b = pts[i + 1];
    const float abx = b.x - a.x, abz = b.z - a.z;
    const float len2 = abx * abx + abz * abz;
    float t = len2 > 0 ? ((x - a.x) * abx + (z - a.z) * abz) / len2 : 0;
    t = std::clamp(t, 0.0f, 1.0f);
    const float dx = a.x + abx * t - x, dz = a.z + abz * t - z;
    best2 = std::min(best2, dx * dx + dz * dz);
  };
  // The points go by increasing z: start from the segment at z and walk
  // outwards, stopping on each side at the first segment that is farther
  // along z alone than the best distance so far (all the next ones are too).
  const auto k = static_cast<size_t>(std::lower_bound(pts.begin(), pts.end(), z,
                                                      [](const Vec2& p, float v) { return p.z < v; }) -
                                     pts.begin());
  const size_t first = std::min(k > 0 ? k - 1 : 0, pts.size() - 2);
  for (size_t i = first; i + 1 < pts.size(); ++i) {
    const float gap = pts[i].z - z;
    if (gap > 0 && gap * gap >= best2) break;
    visit(i);
  }
  for (size_t i = first; i-- > 0;) {
    const float gap = z - pts[i + 1].z;
    if (gap > 0 && gap * gap >= best2) break;
    visit(i);
  }
  return std::sqrt(best2);
}

}  // namespace monstres
