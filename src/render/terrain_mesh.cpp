#include "monstres/render/terrain_mesh.hpp"

#include "monstres/core/continent.hpp"

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
constexpr Color kSlope{0.50f, 0.44f, 0.34f, 1};   // bare earth and stones on steep slopes
constexpr Color kPaving{0.60f, 0.58f, 0.54f, 1};  // old flagstones of the ruins' courtyard
constexpr Color kForestFloor{0.27f, 0.40f, 0.20f, 1};  // shaded grass and moss under the trees
constexpr Color kCastlePaving{0.58f, 0.56f, 0.53f, 1};
constexpr Color kCaveRock{0.46f, 0.44f, 0.43f, 1};

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

void vertex(MeshData& m, float x, float y, float z, Color c, Vec3 n = {0, 1, 0}) {
  m.vertices.insert(m.vertices.end(), {x, y, z, n.x, n.y, n.z, c.r, c.g, c.b, c.a});
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
  // Hilltops a little drier, steep slopes bare.
  const float h = w.ground.height(x, z);
  c = lerp(c, kGrassDry, smooth(2.5f, 8.0f, h) * 0.45f);
  const float steep = 1 - w.ground.normal(x, z).y;  // 0 flat, 0.29 at 45°
  c = lerp(c, kSlope, smooth(0.10f, 0.25f, steep) * 0.8f);
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
  if (w.forest.radius_x > 0) {
    // Under the trees: darker grass and moss, fading out at the edge of the forest.
    const float fx = (x - w.forest.center.x) / w.forest.radius_x, fz = (z - w.forest.center.z) / w.forest.radius_z;
    const float inside = 1 - smooth(0.75f, 1.05f, std::sqrt(fx * fx + fz * fz));
    c = lerp(c, kForestFloor, inside * (0.55f + 0.3f * value_noise(w.seed + 33, x, z, 5.0f)));
  }
  if (w.castle.radius > 0) {
    // The castle's courtyard: flagstones.
    const float dx = x - w.castle.center.x, dz = z - w.castle.center.z;
    const float inside = 1 - smooth(rules::kCastleHalf - 1.0f, rules::kCastleHalf + 1.5f, std::max(std::fabs(dx), std::fabs(dz)));
    const Color stone = lerp(kCastlePaving, kEarth, 0.2f * value_noise(w.seed + 34, x, z, 1.5f));
    c = lerp(c, stone, inside * 0.85f);
  }
  if (w.cave.radius > 0) {
    // The cave's hill: grey rock, a little grass on top.
    const float dx = x - w.cave.center.x, dz = z - w.cave.center.z;
    const float rock = 1 - smooth(w.cave.radius - 1.0f, w.cave.radius + 2.0f, std::sqrt(dx * dx + dz * dz));
    c = lerp(c, lerp(kCaveRock, kGrassDark, 0.35f * smooth(0.55f, 0.8f, value_noise(w.seed + 35, x, z, 4.0f))), rock * 0.9f);
  }
  if (w.ruins.radius > 0) {
    // The courtyard of the ruins: worn flagstones, grass growing through.
    const float dx = x - w.ruins.center.x, dz = z - w.ruins.center.z;
    const float inside = 1 - smooth(7.0f, 9.5f, std::max(std::fabs(dx), std::fabs(dz)));
    const float worn = value_noise(w.seed + 31, x, z, 2.5f);
    const Color stone = lerp(kPaving, kEarth, 0.25f * value_noise(w.seed + 32, x, z, 1.0f));
    c = lerp(c, stone, inside * (0.35f + 0.6f * smooth(0.3f, 0.6f, worn)));
  }
  // The wilds: the continent's forest floor, the beach and the sea floor.
  if (region_at(x, z) == Region::Forest) c = lerp(c, kForestFloor, 0.6f);
  // Near the shore only: the continent's ground there is close to the sea (around the valley it is 2 m above).
  const float coast = smooth(rules::kSeaLevel + 1.5f, rules::kSeaLevel + 0.2f, w.ground.land_height(x, z));
  c = lerp(c, kSand, smooth(rules::kSeaLevel + 3.0f, rules::kSeaLevel + 0.8f, h) * coast);
  c = lerp(c, kLakeBed, smooth(rules::kSeaLevel, rules::kSeaLevel - 2.5f, h) * coast);
  return c;
}

}  // namespace

float ground_height(const World& w, float x, float z) { return w.ground.height(x, z); }

Vec2 ground_center(float x, float z) {
  const auto snap = [](float v) { return std::round(v / terrain::kRecenter) * terrain::kRecenter; };
  const float limit = terrain::kDetailHalfSize - terrain::kRecenter;
  return {std::clamp(snap(x), -limit, limit), std::clamp(snap(z), -limit, limit)};
}

namespace {

uint32_t pack(Color c) {
  const auto byte = [](float v) { return static_cast<uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
  return byte(c.r) | (byte(c.g) << 8) | (byte(c.b) << 16) | (255u << 24);
}
Color unpack(uint32_t p) {
  return {static_cast<float>(p & 255u) / 255.0f, static_cast<float>((p >> 8) & 255u) / 255.0f,
          static_cast<float>((p >> 16) & 255u) / 255.0f, 1};
}

}  // namespace

MeshData build_ground_mesh(const World& world, GroundColors& colors, Vec2 center) {
  MeshData m;
  const float d = terrain::kDetailHalfSize;
  constexpr float kGrid = Heightmap::kCellSize;
  constexpr int kPoints = Heightmap::kPoints;
  if (colors.packed.size() != static_cast<size_t>(kPoints) * kPoints) colors.packed.assign(static_cast<size_t>(kPoints) * kPoints, 0);
  const auto index = [&](float v) { return std::clamp(static_cast<int>(std::lround((v + d) / kGrid)), 0, kPoints - 1); };
  const auto height = [&](float x, float z) { return world.ground.empty() ? 0.0f : world.ground.at(index(x), index(z)); };
  const auto on = [](float v, float cell) {  // a multiple of `cell` from the relief's edge
    const float q = (v + terrain::kDetailHalfSize) / cell;
    return std::fabs(q - std::round(q)) < 1e-3f;
  };
  struct Point {
    float y;
    Color c;
    Vec3 n;
  };
  const auto point = [&](int ring, float x, float z) {
    float y = height(x, z);
    // On a ring's outer edge, between two points of the coarser ring: on the
    // straight line between them (no gap between the rings).
    if (ring + 1 < terrain::kRings) {
      const float outer = terrain::kRingHalf[ring], fine = terrain::kRingCells[ring], coarse = terrain::kRingCells[ring + 1];
      if (std::fabs(std::fabs(x - center.x) - outer) < 1e-3f && std::fabs(x) < d && !on(z, coarse)) {
        y = 0.5f * (height(x, z - fine) + height(x, z + fine));
      } else if (std::fabs(std::fabs(z - center.z) - outer) < 1e-3f && std::fabs(z) < d && !on(x, coarse)) {
        y = 0.5f * (height(x - fine, z) + height(x + fine, z));
      }
    }
    uint32_t& c = colors.packed[static_cast<size_t>(index(x) * kPoints + index(z))];
    if ((c >> 24) == 0) c = pack(ground_color(world, x, z));
    return Point{y, unpack(c), world.ground.normal(x, z)};
  };
  m.vertices.reserve(48000 * 6 * MeshData::kFloatsPerVertex);
  for (int ring = 0; ring < terrain::kRings; ++ring) {
    const float cell = terrain::kRingCells[ring];
    const float inner = ring == 0 ? 0.0f : terrain::kRingHalf[ring - 1];
    const float outer = terrain::kRingHalf[ring];
    const float x_lo = std::max(-d, center.x - outer), x_hi = std::min(d, center.x + outer);
    const float z_lo = std::max(-d, center.z - outer), z_hi = std::min(d, center.z + outer);
    const int nx = static_cast<int>(std::lround((x_hi - x_lo) / cell)), nz = static_cast<int>(std::lround((z_hi - z_lo) / cell));
    // Each point once (shared by up to four cells), but those inside the finer ring.
    std::vector<Point> points(static_cast<size_t>((nx + 1) * (nz + 1)));
    for (int i = 0; i <= nx; ++i) {
      for (int j = 0; j <= nz; ++j) {
        const float x = x_lo + cell * static_cast<float>(i), z = z_lo + cell * static_cast<float>(j);
        if (std::fabs(x - center.x) < inner - 1e-3f && std::fabs(z - center.z) < inner - 1e-3f) continue;
        points[static_cast<size_t>(i * (nz + 1) + j)] = point(ring, x, z);
      }
    }
    for (int i = 0; i < nx; ++i) {
      for (int j = 0; j < nz; ++j) {
        const float x0 = x_lo + cell * static_cast<float>(i), x1 = x0 + cell;
        const float z0 = z_lo + cell * static_cast<float>(j), z1 = z0 + cell;
        // Inside the finer ring: drawn there.
        if (std::fabs(0.5f * (x0 + x1) - center.x) < inner && std::fabs(0.5f * (z0 + z1) - center.z) < inner) continue;
        const auto at = [&](int a, int b) { return points[static_cast<size_t>((i + a) * (nz + 1) + j + b)]; };
        const Point p00 = at(0, 0), p10 = at(1, 0), p01 = at(0, 1), p11 = at(1, 1);
        // Counter-clockwise seen from above; the same triangles as Heightmap::height (in 2 m cells).
        vertex(m, x0, p00.y, z0, p00.c, p00.n); vertex(m, x0, p01.y, z1, p01.c, p01.n); vertex(m, x1, p11.y, z1, p11.c, p11.n);
        vertex(m, x0, p00.y, z0, p00.c, p00.n); vertex(m, x1, p11.y, z1, p11.c, p11.n); vertex(m, x1, p10.y, z0, p10.c, p10.n);
      }
    }
  }
  return m;
}

MeshData build_ground_mesh(const World& world) {
  GroundColors colors;
  return build_ground_mesh(world, colors, ground_center(world.player.pos.x, world.player.pos.z));
}

MeshData build_water_mesh(const World& world) {
  MeshData m;
  m.transparent = true;
  // The sea, where the relief goes below it (clear in the shallows, deep blue
  // farther): squares of 8 m at its level, as the continent's water beyond.
  if (!world.ground.empty()) {
    constexpr float kCell = 8.0f;
    const float d = terrain::kDetailHalfSize;
    const int n = static_cast<int>(2 * d / kCell);
    const auto water = [&](float x, float z) {
      const float depth = rules::kSeaLevel - world.ground.height(x, z);
      return Color{0.18f - 0.08f * smooth(0.0f, 20.0f, depth), 0.44f - 0.18f * smooth(0.0f, 20.0f, depth),
                   0.56f - 0.16f * smooth(0.0f, 20.0f, depth), 0.55f + 0.45f * smooth(0.0f, 14.0f, depth)};
    };
    const float y = rules::kSeaLevel;
    for (int i = 0; i < n; ++i) {
      for (int j = 0; j < n; ++j) {
        const float x0 = -d + kCell * static_cast<float>(i), z0 = -d + kCell * static_cast<float>(j);
        const float x1 = x0 + kCell, z1 = z0 + kCell;
        const float lowest = std::min({world.ground.height(x0, z0), world.ground.height(x1, z0), world.ground.height(x0, z1),
                                       world.ground.height(x1, z1), world.ground.height(x0 + 4, z0 + 4)});
        if (lowest > rules::kSeaLevel + 0.2f || !world.ground.sea(x0 + 4, z0 + 4)) continue;  // not a hollow of the hills
        vertex(m, x0, y, z0, water(x0, z0)); vertex(m, x0, y, z1, water(x0, z1)); vertex(m, x1, y, z1, water(x1, z1));
        vertex(m, x0, y, z0, water(x0, z0)); vertex(m, x1, y, z1, water(x1, z1)); vertex(m, x1, y, z0, water(x1, z0));
      }
    }
  }
  const Lake& lake = world.village.lake;
  if (lake.radius_x <= 0) {
    face_up(m);
    return m;
  }
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
      // Following the ground, just above it.
      auto y = [&](float x, float z) { return ground_height(world, x, z) + terrain::kPathHeight; };
      vertex(m, ax, y(ax, az), az, ca); vertex(m, bx, y(bx, bz), bz, cb); vertex(m, cx, y(cx, cz), cz, cc);
      vertex(m, ax, y(ax, az), az, ca); vertex(m, cx, y(cx, cz), cz, cc); vertex(m, dx, y(dx, dz), dz, cd);
    }
  }
  face_up(m);
  return m;
}

}  // namespace monstres
