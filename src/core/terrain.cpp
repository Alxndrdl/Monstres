#include "monstres/core/terrain.hpp"

#include "monstres/core/rng.hpp"

#include <algorithm>
#include <cmath>

namespace monstres {

namespace {

constexpr int kControlPoints = 19;          // every 6 m from z = -54 to z = +54
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
  // Random walk outwards from the middle, at most ±3.5 m per step, kept within ±14 m.
  for (int dir : {1, -1}) {
    for (int i = middle + dir; i >= 0 && i < kControlPoints; i += dir) {
      const float step = static_cast<float>(static_cast<int>(rng.below(701)) - 350) / 100.0f;
      xs[i] = std::clamp(xs[i - dir] + step, -14.0f, 14.0f);
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

float distance_to_path(const Path& path, float x, float z) {
  float best2 = 1e30f;
  for (size_t i = 0; i + 1 < path.points.size(); ++i) {
    const Vec2 a = path.points[i], b = path.points[i + 1];
    // Cheap rejection: the segment is farther along z than the best so far.
    if ((a.z - z) * (b.z - z) > 0) {  // both ends on the same side of z
      const float gap_z = std::min(std::fabs(a.z - z), std::fabs(b.z - z));
      if (gap_z * gap_z >= best2) continue;
    }
    const float abx = b.x - a.x, abz = b.z - a.z;
    const float len2 = abx * abx + abz * abz;
    float t = len2 > 0 ? ((x - a.x) * abx + (z - a.z) * abz) / len2 : 0;
    t = std::clamp(t, 0.0f, 1.0f);
    const float dx = a.x + abx * t - x, dz = a.z + abz * t - z;
    best2 = std::min(best2, dx * dx + dz * dz);
  }
  return std::sqrt(best2);
}

}  // namespace monstres
