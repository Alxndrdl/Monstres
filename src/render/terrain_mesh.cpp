#include "monstres/render/terrain_mesh.hpp"

#include <algorithm>
#include <cmath>

namespace monstres {

namespace {

constexpr Color kGrassDark{0.33f, 0.52f, 0.24f, 1};
constexpr Color kGrassLight{0.47f, 0.66f, 0.31f, 1};
constexpr Color kGrassDry{0.58f, 0.66f, 0.34f, 1};
constexpr Color kEarth{0.48f, 0.38f, 0.26f, 1};
constexpr Color kPath{0.60f, 0.47f, 0.32f, 1};
constexpr Color kSand{0.80f, 0.72f, 0.52f, 1};
constexpr Color kLakeBed{0.36f, 0.42f, 0.36f, 1};
constexpr Color kSquare{0.58f, 0.50f, 0.38f, 1};  // trodden earth of the village square

uint64_t mix64(uint64_t z) {
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}

float lattice(uint64_t seed, int64_t x, int64_t z) {
  const uint64_t h = mix64(seed * 0x9E3779B97F4A7C15ULL + static_cast<uint64_t>(x) * 0xC2B2AE3D27D4EB4FULL +
                           static_cast<uint64_t>(z) * 0x165667B19E3779F9ULL);
  return static_cast<float>(h >> 40) / static_cast<float>(1ULL << 24);
}

Color lerp(Color a, Color b, float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

float smooth(float e0, float e1, float x) {
  const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
  return t * t * (3 - 2 * t);
}

// Makes every triangle of a flat mesh face up (counter-clockwise seen from above).
void face_up(MeshData& m) {
  constexpr size_t kv = MeshData::kFloatsPerVertex;
  for (size_t t = 0; t < m.vertices.size(); t += 3 * kv) {
    const float* a = &m.vertices[t];
    const float* b = &m.vertices[t + kv];
    const float* c = &m.vertices[t + 2 * kv];
    const float ny = (c[0] - a[0]) * (b[2] - a[2]) - (b[0] - a[0]) * (c[2] - a[2]);
    if (ny < 0) {
      std::swap_ranges(m.vertices.begin() + static_cast<std::ptrdiff_t>(t + kv),
                       m.vertices.begin() + static_cast<std::ptrdiff_t>(t + 2 * kv),
                       m.vertices.begin() + static_cast<std::ptrdiff_t>(t + 2 * kv));
    }
  }
}

void vertex(MeshData& m, float x, float y, float z, Color c) {
  m.vertices.insert(m.vertices.end(), {x, y, z, 0, 1, 0, c.r, c.g, c.b, c.a});
}

// 0 at the shore, 1 in the middle of the lake, negative outside.
float lake_depth_factor(const Lake& lake, float x, float z) {
  if (lake.radius_x <= 0) return -1;
  const float dx = (x - lake.center.x) / lake.radius_x, dz = (z - lake.center.z) / lake.radius_z;
  return 1 - std::sqrt(dx * dx + dz * dz);
}

Color base_ground_color(uint64_t seed, float x, float z) {
  const float large = value_noise(seed + 1, x, z, 11.0f);
  const float small = value_noise(seed + 2, x, z, 3.0f);
  Color c = lerp(kGrassDark, kGrassLight, 0.65f * large + 0.35f * small);
  c = lerp(c, kGrassDry, smooth(0.62f, 0.85f, value_noise(seed + 3, x, z, 17.0f)) * 0.7f);
  // Patches of bare earth.
  const float earth = value_noise(seed + 4, x, z, 6.0f) * 0.75f + small * 0.25f;
  return lerp(c, kEarth, smooth(0.70f, 0.80f, earth) * 0.85f);
}

Color ground_color(const World& w, float x, float z) {
  Color c = base_ground_color(w.seed, x, z);
  if (w.village.lake.radius_x > 0) {
    // The village square: trodden earth fading into the grass.
    const float dx = x - w.village.center.x, dz = z - w.village.center.z;
    const float square = 1 - smooth(4.5f, 8.0f, std::sqrt(dx * dx + dz * dz) + value_noise(w.seed + 30, x, z, 2.0f) * 1.5f);
    c = lerp(c, kSquare, square * 0.9f);
    // Sandy shore, then the lake bed.
    const float depth = lake_depth_factor(w.village.lake, x, z);
    c = lerp(c, kSand, smooth(-0.18f, -0.02f, depth));
    c = lerp(c, kLakeBed, smooth(0.08f, 0.35f, depth));
  }
  return c;
}

}  // namespace

float ground_height(const World& w, float x, float z) {
  const float depth = lake_depth_factor(w.village.lake, x, z);
  return depth > 0 ? -1.4f * std::min(1.0f, depth * 2.5f) : 0.0f;
}

float value_noise(uint64_t seed, float x, float z, float cell) {
  const float fx = x / cell, fz = z / cell;
  const float x0 = std::floor(fx), z0 = std::floor(fz);
  const float tx = fx - x0, tz = fz - z0;
  const float sx = tx * tx * (3 - 2 * tx), sz = tz * tz * (3 - 2 * tz);
  const auto ix = static_cast<int64_t>(x0), iz = static_cast<int64_t>(z0);
  const float a = lattice(seed, ix, iz), b = lattice(seed, ix + 1, iz);
  const float c = lattice(seed, ix, iz + 1), d = lattice(seed, ix + 1, iz + 1);
  return (a + (b - a) * sx) + ((c + (d - c) * sx) - (a + (b - a) * sx)) * sz;
}

MeshData build_ground_mesh(const World& world) {
  MeshData m;
  const float d = terrain::kDetailHalfSize;
  const int cells = static_cast<int>(2 * d);
  // Fine grid: 1 m cells, colors at the corners.
  for (int i = 0; i < cells; ++i) {
    for (int j = 0; j < cells; ++j) {
      const float x0 = -d + static_cast<float>(i), x1 = x0 + 1;
      const float z0 = -d + static_cast<float>(j), z1 = z0 + 1;
      const Color c00 = ground_color(world, x0, z0), c10 = ground_color(world, x1, z0);
      const Color c01 = ground_color(world, x0, z1), c11 = ground_color(world, x1, z1);
      const float h00 = ground_height(world, x0, z0), h10 = ground_height(world, x1, z0);
      const float h01 = ground_height(world, x0, z1), h11 = ground_height(world, x1, z1);
      // Counter-clockwise seen from above.
      vertex(m, x0, h00, z0, c00); vertex(m, x0, h01, z1, c01); vertex(m, x1, h11, z1, c11);
      vertex(m, x0, h00, z0, c00); vertex(m, x1, h11, z1, c11); vertex(m, x1, h10, z0, c10);
    }
  }
  // Coarse skirt from the fine grid out to the horizon (4 trapezoids).
  const float h = terrain::kHorizonHalfSize;
  const Color far = lerp(kGrassDark, kGrassLight, 0.5f);
  const float corners_in[4][2] = {{-d, -d}, {d, -d}, {d, d}, {-d, d}};
  const float corners_out[4][2] = {{-h, -h}, {h, -h}, {h, h}, {-h, h}};
  for (int k = 0; k < 4; ++k) {
    const int n = (k + 1) % 4;
    const Color ci = ground_color(world, corners_in[k][0], corners_in[k][1]);
    const Color cn = ground_color(world, corners_in[n][0], corners_in[n][1]);
    vertex(m, corners_in[k][0], 0, corners_in[k][1], ci);
    vertex(m, corners_in[n][0], 0, corners_in[n][1], cn);
    vertex(m, corners_out[n][0], 0, corners_out[n][1], far);
    vertex(m, corners_in[k][0], 0, corners_in[k][1], ci);
    vertex(m, corners_out[n][0], 0, corners_out[n][1], far);
    vertex(m, corners_out[k][0], 0, corners_out[k][1], far);
  }
  return m;
}

MeshData build_water_mesh(const World& world) {
  MeshData m;
  m.transparent = true;
  const Lake& lake = world.village.lake;
  if (lake.radius_x <= 0) return m;
  constexpr int kSegments = 48;
  constexpr float kTau = 6.28318530718f;
  const float y = rules::kWaterLevel;
  const float rings[] = {0.0f, 0.55f, 0.85f, 1.03f};  // fraction of the radius
  const Color colors[] = {{0.16f, 0.38f, 0.52f, 0.9f}, {0.20f, 0.45f, 0.58f, 0.86f},
                          {0.28f, 0.56f, 0.64f, 0.72f}, {0.42f, 0.68f, 0.70f, 0.0f}};
  auto point = [&](int ring, int seg, float& x, float& z) {
    const float a = kTau * static_cast<float>(seg) / kSegments;
    x = lake.center.x + std::cos(a) * lake.radius_x * rings[ring];
    z = lake.center.z + std::sin(a) * lake.radius_z * rings[ring];
  };
  for (int ring = 0; ring < 3; ++ring) {
    for (int seg = 0; seg < kSegments; ++seg) {
      float ax, az, bx, bz, cx, cz, dx, dz;
      point(ring, seg, ax, az);
      point(ring, seg + 1, bx, bz);
      point(ring + 1, seg + 1, cx, cz);
      point(ring + 1, seg, dx, dz);
      if (ring > 0) {
        vertex(m, ax, y, az, colors[ring]); vertex(m, cx, y, cz, colors[ring + 1]); vertex(m, bx, y, bz, colors[ring]);
      }
      vertex(m, ax, y, az, colors[ring]); vertex(m, dx, y, dz, colors[ring + 1]); vertex(m, cx, y, cz, colors[ring + 1]);
    }
  }
  face_up(m);
  return m;
}

MeshData build_path_mesh(const World& world) {
  MeshData m;
  m.transparent = true;
  const auto& pts = world.path.points;
  if (pts.size() < 2) return m;
  const size_t n = pts.size();
  const float y = terrain::kPathHeight;

  // Across the path: soft edge, dirt, center, dirt, soft edge.
  constexpr int kColumns = 5;
  const float offsets[kColumns] = {-1.2f, -0.75f, 0.0f, 0.75f, 1.2f};
  const float alphas[kColumns] = {0.0f, 1.0f, 1.0f, 1.0f, 0.0f};

  auto point = [&](size_t i, int col, float& x, float& z, Color& c) {
    const Vec2 prev = pts[i > 0 ? i - 1 : i], next = pts[i + 1 < n ? i + 1 : i];
    float tx = next.x - prev.x, tz = next.z - prev.z;
    const float len = std::sqrt(tx * tx + tz * tz);
    tx /= len;
    tz /= len;
    const float wobble = 0.9f + 0.2f * value_noise(world.seed + 9, static_cast<float>(i), static_cast<float>(col), 3.0f);
    const float w = rules::kPathHalfWidth * offsets[col] * wobble;
    x = pts[i].x - tz * w;
    z = pts[i].z + tx * w;
    const float shade = 0.92f + 0.12f * value_noise(world.seed + 10, x, z, 1.5f);
    // Fade out over the last few meters at both ends.
    const float end_fade = std::min(smooth(0.0f, 8.0f, static_cast<float>(i)),
                                    smooth(0.0f, 8.0f, static_cast<float>(n - 1 - i)));
    c = {kPath.r * shade, kPath.g * shade, kPath.b * shade, alphas[col] * end_fade};
  };

  for (size_t i = 0; i + 1 < n; ++i) {
    for (int col = 0; col + 1 < kColumns; ++col) {
      float ax, az, bx, bz, cx, cz, dx, dz;
      Color ca, cb, cc, cd;
      point(i, col, ax, az, ca);
      point(i, col + 1, bx, bz, cb);
      point(i + 1, col + 1, cx, cz, cc);
      point(i + 1, col, dx, dz, cd);
      vertex(m, ax, y, az, ca); vertex(m, bx, y, bz, cb); vertex(m, cx, y, cz, cc);
      vertex(m, ax, y, az, ca); vertex(m, cx, y, cz, cc); vertex(m, dx, y, dz, cd);
    }
  }
  face_up(m);
  return m;
}

}  // namespace monstres
