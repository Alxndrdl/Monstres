#include "monstres/core/terrain.hpp"

#include "monstres/core/continent.hpp"
#include "monstres/core/heightmap.hpp"

#include "monstres/core/rng.hpp"

#include <algorithm>
#include <cmath>

namespace monstres {

namespace {

// Every 6 m: the valley's part first (as it always was), then on to beyond
// the edges of the grown world.
constexpr int kValleyControlPoints = 2 * (static_cast<int>(rules::kPathExtent / 6.0f) + 1) + 1;
constexpr int kExtraControlPoints = static_cast<int>((rules::kPathFullExtent - rules::kPathExtent) / 6.0f) + 1;  // per end
constexpr int kControlPoints = kValleyControlPoints + 2 * kExtraControlPoints;
// Northwards, more of them before those (the world grew north).
constexpr int kNorthControlPoints = static_cast<int>((rules::kPathNorthExtent - rules::kPathFullExtent) / 6.0f) + 1;
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
  // Random walk outwards from the middle, at most ±3.5 m per step, kept within ±kPathMaxX
  // (the valley's part: the same draws as before the world grew).
  const int valley_half = kValleyControlPoints / 2;
  for (int dir : {1, -1}) {
    for (int i = middle + dir; i >= middle - valley_half && i <= middle + valley_half; i += dir) {
      const float step = static_cast<float>(static_cast<int>(rng.below(701)) - 350) / 100.0f;
      xs[i] = std::clamp(xs[i - dir] + step, -rules::kPathMaxX, rules::kPathMaxX);
    }
  }
  // Beyond the valley: its own stream, winding farther.
  Rng far(seed ^ 0x5041544846415252ULL, 8);
  for (int dir : {1, -1}) {
    for (int i = middle + dir * (valley_half + 1); i >= 0 && i < kControlPoints; i += dir) {
      const float step = static_cast<float>(static_cast<int>(far.below(701)) - 350) / 100.0f;
      xs[i] = std::clamp(xs[i - dir] + step, -rules::kPathFarMaxX, rules::kPathFarMaxX);
    }
  }

  // Further north (its own stream again: the rest stays as it was), winding
  // back towards the middle as it goes up to the mountains' col.
  constexpr int kAll = kNorthControlPoints + kControlPoints;
  float all[kAll];
  std::copy(xs, xs + kControlPoints, all + kNorthControlPoints);
  Rng north(seed ^ 0x504154484E4F5254ULL, 9);  // "PATHNORT"
  for (int i = kNorthControlPoints - 1; i >= 0; --i) {
    const float step = static_cast<float>(static_cast<int>(north.below(701)) - 350) / 100.0f;
    const float pull = -0.02f * all[i + 1];
    all[i] = std::clamp(all[i + 1] + step + pull, -rules::kPathFarMaxX, rules::kPathFarMaxX);
  }

  const float z0 = -kControlStep * static_cast<float>(middle + kNorthControlPoints);
  // Beyond the valley, wide bends of changing size (seen from afar, not a
  // line down the world): nothing in the valley, growing past it.
  {
    const uint64_t s = seed ^ 0x42454E4453ULL;  // "BENDS"
    for (int dir : {1, -1}) {
      float phase = 6.2831853f * value_noise(s, dir * 7.0f, 3.0f, 1.0f);
      const int from = kNorthControlPoints + middle;
      for (int i = from; i >= 0 && i < kAll; i += dir) {
        const float z = z0 + kControlStep * static_cast<float>(i);
        // (Northwards, past the river's way across the terrace: it crosses the path once.)
        const float out = std::fabs(z) - (z < 0 ? rules::kPathBendNorthFrom : rules::kPathExtent + 25.0f);
        if (out <= 0) continue;
        const float wave = 240.0f + 220.0f * value_noise(s + 1, z / 380.0f, dir * 5.0f, 1.0f);
        phase += 6.2831853f * kControlStep / wave;
        const float grow = std::min(1.0f, out / 140.0f);
        // (Never too steep across: the path still goes on north or south.)
        const float amp = std::min(40.0f + 70.0f * value_noise(s + 2, z / 330.0f, dir * 9.0f, 1.0f), 0.27f * wave) *
                          grow * grow * (3 - 2 * grow);
        all[i] = std::clamp(all[i] + amp * wave_sin(phase), -rules::kPathBendMaxX, rules::kPathBendMaxX);
      }
    }
  }

  Path path;
  for (int seg = 0; seg + 1 < kAll; ++seg) {
    const float p0 = all[std::max(seg - 1, 0)];
    const float p1 = all[seg];
    const float p2 = all[seg + 1];
    const float p3 = all[std::min(seg + 2, kAll - 1)];
    // (More samples where it bends across: about a point every 0.9 m or closer.)
    const float across = p2 - p1;
    const int samples = std::max(kSamplesPerSegment, static_cast<int>(std::ceil(std::sqrt(across * across + kControlStep * kControlStep) / 0.9f)));
    for (int s = 0; s < samples; ++s) {
      const float t = static_cast<float>(s) / static_cast<float>(samples);
      path.points.push_back({catmull_rom(p0, p1, p2, p3, t),
                             z0 + kControlStep * (static_cast<float>(seg) + t)});
    }
  }
  path.points.push_back({all[kAll - 1], z0 + kControlStep * (kAll - 1)});
  // Southwards it ends on the beach where it comes to the sea (not on into
  // it). (Northwards it goes up onto the terrace, whatever the continent's
  // ground below it.)
  {
    auto& pts = path.points;
    for (size_t i = pts.size() / 2; i < pts.size(); ++i) {
      if (pts[i].z > rules::kPathExtent && continent_height(seed, pts[i].x, pts[i].z) < rules::kSeaLevel + 0.6f) {
        pts.resize(std::min(pts.size(), i + 4));
        break;
      }
    }
  }
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
