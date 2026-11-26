// The continent's picture for the map (outer.hpp: continent_map_rows,
// map_detail): the ground as the player walks on it, in the colors of the
// playable area's own picture (web/menu.js, buildBase), so that both meet
// unseen. One painter for all of it: the whole continent a few meters a
// pixel, a tile of it a meter a pixel (zoomed in: its things drawn over it).

#include "monstres/core/continent.hpp"
#include "monstres/core/heightmap.hpp"
#include "monstres/core/outer.hpp"
#include "monstres/core/water.hpp"

#include <algorithm>
#include <cmath>

namespace monstres {

namespace {

using continent_map::kCell;
using continent_map::kHeight;
using continent_map::kWidth;

float smooth(float e0, float e1, float x) {
  const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
  return t * t * (3 - 2 * t);
}

struct Rgb {
  float r, g, b;
};

Rgb blend(Rgb c, Rgb to, float k) { return {c.r + (to.r - c.r) * k, c.g + (to.g - c.g) * k, c.b + (to.b - c.b) * k}; }

float px_x(int i) { return Continent::kMinX + kCell * (static_cast<float>(i) + 0.5f); }
float px_z(int j) { return Continent::kMinZ + kCell * (static_cast<float>(j) + 0.5f); }

// The ground under a pixel: the points of the ground's 2 m lattice the
// pixels' middles fall on (every other one: kCell is 4 m), as the tiles
// have them (the relief's grid within it): the very ground walked on.
float ground_at(const World& w, int i, int j) {
  constexpr int kStep = static_cast<int>(kCell / Heightmap::kCellSize);
  constexpr int kI0 = static_cast<int>((Continent::kMinX + kCell / 2 - Heightmap::kMinX) / Heightmap::kCellSize);
  constexpr int kJ0 = static_cast<int>((Continent::kMinZ + kCell / 2 - Heightmap::kMinZ) / Heightmap::kCellSize);
  static_assert(Continent::kMinX + kCell / 2 - Heightmap::kMinX == kI0 * Heightmap::kCellSize, "pixels on the lattice");
  static_assert(Continent::kMinZ + kCell / 2 - Heightmap::kMinZ == kJ0 * Heightmap::kCellSize, "pixels on the lattice");
  return w.ground.lattice_point(kI0 + kStep * i, kJ0 + kStep * j);
}

// A pixel at (x, z) on ground at h; its neighbours' heights `apart` meters
// away on each side (west, east, north, south: the slope, the shading).
// RGBA; alpha: 1 the sea, 2 a pond or a river, else 3 + 252 times how green
// it is (the seasons' tints, the map's).
uint32_t pixel_hash(uint64_t seed, float x, float z) {
  const auto i = static_cast<int64_t>(std::floor(x)), j = static_cast<int64_t>(std::floor(z));
  uint64_t h = seed * 0x9E3779B97F4A7C15ULL + static_cast<uint64_t>(i) * 0xBF58476D1CE4E5B9ULL + static_cast<uint64_t>(j) * 0x94D049BB133111EBULL;
  h ^= h >> 31;
  h *= 0xD6E8FEB86659FD93ULL;
  h ^= h >> 32;
  return static_cast<uint32_t>(h);
}

// `dots`: its trees and rocks seen from afar, as many as grow there (a pixel
// too big for one: here and there a dot, the woods' texture; zoomed in, the
// things themselves are drawn over the ground).
void paint(const World& w, float x, float z, float h, float hw, float he, float hn, float hs, float apart, bool dots, uint8_t* o) {
  constexpr float kSea = rules::kSeaLevel;
  const float land = w.ground.land_height(x, z);  // where the sea is (Heightmap::sea)
  // The waters, all alike (water.hpp): the sea; the lakes and rivers below.
  WaterSpot water;
  const bool wet = water_at_ground(w, x, z, h, water);
  Rgb c;
  uint8_t kind = 3;
  float green = 1;
  if (wet && water.kind == WaterKind::Sea) {  // the sea, lighter in the shallows
    const float d = std::min(1.0f, (kSea - h) / 12.0f);
    c = {70 - 40 * d, 150 - 60 * d, 190 - 40 * d};
    kind = 1;
  } else {
    // As the playable area's picture: greener low, yellower up the slopes.
    const float t = std::clamp((h + 4) / 12, 0.0f, 1.0f);
    c = {78 + 70 * t, 128 + 46 * t, 62 + 30 * t};
    const float slope = std::hypot(he - hw, hs - hn) / (2 * apart);
    const float rock = std::clamp((slope - 0.7f) / 0.6f, 0.0f, 1.0f);
    c = blend(c, {140, 136, 132}, rock);
    // The regions: the desert's sand, the marsh, the capital, the volcano's ash.
    const float desert = desert_share(x, z);
    c = blend(c, {220, 186, 128}, desert);
    c = blend(c, {104, 112, 74}, smooth(0.0f, 0.3f, place_shape(Region::Marsh, x, z)) * 0.7f);
    c = blend(c, {178, 168, 146}, smooth(0.0f, 0.3f, place_shape(Region::Capital, x, z)));
    // Bare rock up high, snow (all year long: as in the game, going on from the north's).
    const float high = smooth(110.0f, 200.0f, h) * 0.8f;
    c = blend(c, {140, 136, 132}, high);
    const float snow = w.ground.snow(x, z, h) * (1 - smooth(1.1f, 1.6f, slope));
    c = blend(c, {236, 240, 248}, snow);
    const float ash = volcano_share(x, z) * 0.4f;
    c = {c.r * (1 - ash), c.g * (1 - ash * 1.1f), c.b * (1 - ash * 1.1f)};
    // Beaches.
    const bool beach = h < kSea + 2.5f && land < kSea + 1.0f;
    if (beach) c = {214, 196, 140};
    green = (1 - rock) * (1 - high) * (1 - snow) * (1 - 0.8f * desert) * (beach ? 0.35f : 1.0f);
    // Its woods darker (their trees and rocks: drawn over it, zoomed in).
    // (Zoomed in, the things themselves instead: the ground as the playable area's picture has it.)
    if (dots && !beach && snow < 0.5f) {
      const float n = outer_nature(w, x, z);
      const float wood = smooth(0.12f, 0.5f, n);
      c = {c.r * (1 - 0.24f * wood), c.g * (1 - 0.14f * wood), c.b * (1 - 0.24f * wood)};
      const uint32_t roll = pixel_hash(w.seed, x, z);
      const float chance = std::min(0.9f, 0.45f * n * apart * apart / 25.0f);
      if (static_cast<float>(roll & 0xFFFF) / 65536.0f < chance) {
        const bool stone = h > 120.0f || desert > 0.5f || ((roll >> 16) & 7) == 0;
        const bool pine = h > 40.0f || mountains_share(x, z) > 0.1f;
        const Rgb crown = stone ? Rgb{132, 128, 124} : pine ? Rgb{24, 70, 48} : Rgb{38, 92, 44};
        c = blend(c, crown, stone ? 0.6f : 0.85f);
      }
    }
  }
  // The lakes, the marsh's water, the stream and the rivers.
  if (wet && water.kind != WaterKind::Sea) {
    const float d = std::min(1.0f, (water.level - h) / 1.2f);
    c = {80 - 30 * d, 150 - 40 * d, 196 - 20 * d};
    kind = 2;
  }
  // The lava in the crater.
  const Crater crater = volcano_crater();
  if (std::hypot(x - crater.center.x, z - crater.center.z) < crater.radius * 0.8f) {
    c = {240, 110, 40};
    kind = 1;  // (no season's tint)
  }
  // Light from the north-west, as on the playable area's picture (as much
  // for a slope whatever the pixels' size).
  const float shade = std::clamp(1 + 0.21f / apart * ((hw - he) + (hn - hs)), 0.78f, 1.18f);
  o[0] = static_cast<uint8_t>(std::clamp(c.r * shade, 0.0f, 255.0f));
  o[1] = static_cast<uint8_t>(std::clamp(c.g * shade, 0.0f, 255.0f));
  o[2] = static_cast<uint8_t>(std::clamp(c.b * shade, 0.0f, 255.0f));
  o[3] = kind == 3 ? static_cast<uint8_t>(3 + std::lround(252.0f * std::clamp(green, 0.0f, 1.0f))) : kind;
}

}  // namespace

void continent_map_rows(const World& w, const Continent& k, int j0, int j1, std::vector<uint8_t>& out) {
  j0 = std::clamp(j0, 0, kHeight);
  j1 = std::clamp(j1, j0, kHeight);
  out.assign(static_cast<size_t>(j1 - j0) * kWidth * 4, 0);
  if (w.ground.empty() || k.empty() || j1 == j0) return;
  // The heights first, a row more above and below (the slopes, the shading).
  const int rows = j1 - j0 + 2;
  std::vector<float> heights(static_cast<size_t>(rows) * kWidth);
  for (int r = 0; r < rows; ++r) {
    const float z = px_z(j0 - 1 + r);
    for (int i = 0; i < kWidth; ++i) {
      // (Well within the relief: its own picture there.)
      heights[static_cast<size_t>(r * kWidth + i)] = rules::relief_inset(px_x(i), z) > 4 * kCell ? 0.0f : ground_at(w, i, j0 - 1 + r);
    }
  }
  const auto H = [&](int i, int r) { return heights[static_cast<size_t>(r * kWidth + std::clamp(i, 0, kWidth - 1))]; };
  for (int j = j0; j < j1; ++j) {
    const int r = j - j0 + 1;
    const float z = px_z(j);
    for (int i = 0; i < kWidth; ++i) {
      const float x = px_x(i);
      // (Within the relief: its own picture over it; drawn all the same, a
      // pixel or two in, so that no gap shows where the pictures blend.)
      if (rules::relief_inset(x, z) > 3 * kCell) continue;
      paint(w, x, z, H(i, r), H(i - 1, r), H(i + 1, r), H(i, r - 1), H(i, r + 1), kCell, true, &out[static_cast<size_t>(((j - j0) * kWidth + i) * 4)]);
    }
  }
}

void map_detail(const World& w, int ti, int tj, std::vector<uint8_t>& out) {
  constexpr int n = continent_map::kDetail;
  out.assign(static_cast<size_t>(n * n * 4), 0);
  if (w.ground.empty()) return;
  const float x0 = Heightmap::kMinX + continent_map::kDetailSize * static_cast<float>(ti);
  const float z0 = Heightmap::kMinZ + continent_map::kDetailSize * static_cast<float>(tj);
  constexpr float kPx = continent_map::kDetailSize / n;
  constexpr int kApart = 3;  // (pixels: the shading over 6 m, as the playable area's)
  constexpr int m = n + 2 * kApart;
  std::vector<float> heights(static_cast<size_t>(m * m));
  for (int a = 0; a < m; ++a) {
    for (int b = 0; b < m; ++b) {
      heights[static_cast<size_t>(a * m + b)] =
          w.ground.height(x0 + kPx * (static_cast<float>(a - kApart) + 0.5f), z0 + kPx * (static_cast<float>(b - kApart) + 0.5f));
    }
  }
  const auto H = [&](int a, int b) { return heights[static_cast<size_t>((a + kApart) * m + b + kApart)]; };
  for (int b = 0; b < n; ++b) {
    for (int a = 0; a < n; ++a) {
      const float x = x0 + kPx * (static_cast<float>(a) + 0.5f), z = z0 + kPx * (static_cast<float>(b) + 0.5f);
      paint(w, x, z, H(a, b), H(a - kApart, b), H(a + kApart, b), H(a, b - kApart), H(a, b + kApart), kApart * kPx, false,
            &out[static_cast<size_t>((b * n + a) * 4)]);
    }
  }
}

}  // namespace monstres
