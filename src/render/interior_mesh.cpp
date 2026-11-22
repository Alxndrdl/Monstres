#include "monstres/render/interior_mesh.hpp"

#include "monstres/core/heightmap.hpp"
#include "monstres/core/interior.hpp"
#include "monstres/render/landscape.hpp"
#include "monstres/render/models.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace monstres {

namespace {

constexpr float kTau = 6.28318530718f;

struct Builder {
  MeshData mesh;
  void vertex(Vec3 p, Vec3 n, Color c) {
    mesh.vertices.insert(mesh.vertices.end(), {p.x, p.y, p.z, n.x, n.y, n.z, c.r, c.g, c.b, c.a});
  }
  // A quad seen from where its normal points (corners in order around it).
  void quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec3 n, Color color) {
    const Vec3 e1 = b - a, e2 = c - a;
    const Vec3 cr{e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x};
    if (cr.x * n.x + cr.y * n.y + cr.z * n.z < 0) std::swap(b, d);
    for (const Vec3 p : {a, b, c, a, c, d}) vertex(p, n, color);
  }
};

Color shade(Color c, float k) { return {c.r * k, c.g * k, c.b * k, c.a}; }
Color mixc(Color a, Color b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1}; }

constexpr Color kPlaster{0.90f, 0.85f, 0.75f, 1};
constexpr Color kTimber{0.38f, 0.25f, 0.15f, 1};
constexpr Color kPlanks{0.62f, 0.44f, 0.27f, 1};
constexpr Color kMud{0.70f, 0.57f, 0.40f, 1};
constexpr Color kThatch{0.70f, 0.58f, 0.32f, 1};
constexpr Color kStone{0.58f, 0.55f, 0.52f, 1};
constexpr Color kDoor{0.45f, 0.27f, 0.14f, 1};
constexpr Color kWindow{0.75f, 0.85f, 0.95f, kLitAtNightAlpha};
constexpr Color kRock{0.36f, 0.34f, 0.38f, 1};
constexpr Color kRockFloor{0.42f, 0.39f, 0.37f, 1};
constexpr Color kRockDark{0.20f, 0.18f, 0.24f, 1};
constexpr Color kEarth{0.45f, 0.33f, 0.22f, 1};
constexpr Color kEarthFloor{0.50f, 0.38f, 0.26f, 1};
constexpr Color kEarthDark{0.30f, 0.22f, 0.15f, 1};
constexpr Color kBark{0.40f, 0.28f, 0.18f, 1};
constexpr Color kWhitewash{0.88f, 0.87f, 0.83f, 1};

// ---- The cave: its surface on a grid (surface nets) ----------------------------

MeshData cave_mesh(const Interior& in) {
  constexpr float h = 0.45f;
  const Box b = in.bounds;
  const int nx = static_cast<int>((b.max.x - b.min.x) / h) + 2;
  const int ny = static_cast<int>((b.max.y - b.min.y) / h) + 2;
  const int nz = static_cast<int>((b.max.z - b.min.z) / h) + 2;
  const auto at = [&](int i, int j, int k) { return static_cast<size_t>((i * nz + k) * ny + j); };
  const auto point = [&](int i, int j, int k) {
    return Vec3{b.min.x + h * static_cast<float>(i), b.min.y + h * static_cast<float>(j), b.min.z + h * static_cast<float>(k)};
  };
  // The field, column by column (the walls and the ceiling depend on x, z only).
  std::vector<float> field(static_cast<size_t>(nx) * ny * nz);
  for (int i = 0; i < nx; ++i) {
    for (int k = 0; k < nz; ++k) {
      const Vec3 p = point(i, 0, k);
      const float walls = wall_distance(in, p.x, p.z), ceiling = ceiling_height(in, p.x, p.z);
      const float floor = floor_height(in, p.x, p.z);
      for (int j = 0; j < ny; ++j) {
        const float y = b.min.y + h * static_cast<float>(j);
        const float r = in.rounding;
        const auto smin = [r](float a, float c) {
          const float t = std::max(r - std::fabs(a - c), 0.0f) / r;
          return std::min(a, c) - t * t * r * 0.25f;
        };
        field[at(i, j, k)] = smin(smin(y - floor, walls), ceiling - y);
      }
    }
  }
  // One vertex per cell the surface crosses: the average of where it crosses the cell's edges.
  std::vector<int32_t> index(static_cast<size_t>(nx) * ny * nz, -1);
  Builder out;
  std::vector<Vec3> positions;
  static constexpr int kEdges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3},
                                        {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
  for (int i = 0; i + 1 < nx; ++i) {
    for (int j = 0; j + 1 < ny; ++j) {
      for (int k = 0; k + 1 < nz; ++k) {
        float v[8];
        Vec3 c[8];
        int inside_count = 0;
        for (int n = 0; n < 8; ++n) {
          const int di = n & 1, dj = (n >> 1) & 1, dk = (n >> 2) & 1;
          v[n] = field[at(i + di, j + dj, k + dk)];
          c[n] = point(i + di, j + dj, k + dk);
          inside_count += v[n] > 0;
        }
        if (inside_count == 0 || inside_count == 8) continue;
        Vec3 sum{};
        int crossings = 0;
        for (const auto& e : kEdges) {
          const float a = v[e[0]], d = v[e[1]];
          if ((a > 0) == (d > 0)) continue;
          const float t = a / (a - d);
          sum = sum + c[e[0]] + (c[e[1]] - c[e[0]]) * t;
          ++crossings;
        }
        index[at(i, j, k)] = static_cast<int32_t>(positions.size());
        positions.push_back(sum * (1.0f / static_cast<float>(crossings)));
      }
    }
  }
  // Colors: the floor lighter, the ceiling darker, streaks and specks of noise.
  for (const Vec3& p : positions) {
    const Vec3 n = surface_normal(in, p);
    const float grain = value_noise(in.noise + 7, p.x + p.y * 0.7f, p.z - p.y * 0.4f, 1.3f);
    const float streak = value_noise(in.noise + 8, p.x * 0.3f, p.y * 2.5f + p.z * 0.3f, 1.0f);
    // The burrow: earth, not rock (roots in its ceiling).
    const bool earth = in.kind == PlaceKind::Burrow;
    Color rock = earth ? kEarth : kRock, floor = earth ? kEarthFloor : kRockFloor, dark = earth ? kEarthDark : kRockDark;
    if (in.kind == PlaceKind::Mine) {  // the mine: brownish rock, a dusty floor of earth (the natural cave: grey rock)
      const float nat = natural_at(in, p.x, p.z);
      rock = mixc(mixc(kRock, kEarth, 0.35f), kRock, nat);
      floor = mixc(mixc(kEarthFloor, kRockFloor, 0.4f), kRockFloor, nat);
      dark = mixc(mixc(kRockDark, kEarthDark, 0.5f), kRockDark, nat);
    }
    Color col = n.y > 0.6f ? mixc(rock, floor, std::min(1.0f, (n.y - 0.6f) * 4)) : n.y < -0.5f ? dark : rock;
    col = shade(col, 0.82f + 0.3f * grain + 0.12f * (streak - 0.5f));
    out.vertex(p, n, col);
  }
  // A quad across each grid edge the surface crosses, facing the open.
  const auto cell = [&](int i, int j, int k) { return index[at(i, j, k)]; };
  for (int i = 1; i + 1 < nx; ++i) {
    for (int j = 1; j + 1 < ny; ++j) {
      for (int k = 1; k + 1 < nz; ++k) {
        const float here = field[at(i, j, k)];
        const struct {
          int di, dj, dk;
        } axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        for (int a = 0; a < 3; ++a) {
          const auto [di, dj, dk] = axes[a];
          const float there = field[at(i + di, j + dj, k + dk)];
          if ((here > 0) == (there > 0)) continue;
          // The four cells around the edge.
          int q[4];
          if (a == 0) q[0] = cell(i, j - 1, k - 1), q[1] = cell(i, j, k - 1), q[2] = cell(i, j, k), q[3] = cell(i, j - 1, k);
          else if (a == 1) q[0] = cell(i - 1, j, k - 1), q[1] = cell(i - 1, j, k), q[2] = cell(i, j, k), q[3] = cell(i, j, k - 1);
          else q[0] = cell(i - 1, j - 1, k), q[1] = cell(i, j - 1, k), q[2] = cell(i, j, k), q[3] = cell(i - 1, j, k);
          if (q[0] < 0 || q[1] < 0 || q[2] < 0 || q[3] < 0) continue;
          // Facing the open: towards `there` when it is open.
          const float sign = there > 0 ? 1.0f : -1.0f;
          const Vec3 want{di * sign, dj * sign, dk * sign};
          const Vec3 p0 = positions[static_cast<size_t>(q[0])], p1 = positions[static_cast<size_t>(q[1])],
                     p2 = positions[static_cast<size_t>(q[2])];
          const Vec3 e1 = p1 - p0, e2 = p2 - p0;
          const Vec3 cr{e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x};
          const bool flip = cr.x * want.x + cr.y * want.y + cr.z * want.z < 0;
          const uint32_t a0 = static_cast<uint32_t>(q[0]), a1 = static_cast<uint32_t>(q[flip ? 3 : 1]),
                         a2 = static_cast<uint32_t>(q[2]), a3 = static_cast<uint32_t>(q[flip ? 1 : 3]);
          out.mesh.indices.insert(out.mesh.indices.end(), {a0, a1, a2, a0, a2, a3});
        }
      }
    }
  }
  return out.mesh;
}

// ---- Rooms -------------------------------------------------------------------------

// Floor and ceiling of a room, and a wall along its outline, with windows
// and the door (in the wall at z = 0).
struct Look {
  Color floor, wall, low, beam, ceiling;
  bool round, stone;
};

void floor_and_ceiling(Builder& out, const RoomPiece& r, const Look& look) {
  if (!look.round) {
    const float x0 = r.a.x - r.half.x, x1 = r.a.x + r.half.x, z0 = r.a.z - r.half.z, z1 = r.a.z + r.half.z;
    // Planks across, a little different each.
    const int planks = static_cast<int>((x1 - x0) / 0.3f);
    for (int p = 0; p < planks; ++p) {
      const float a = x0 + (x1 - x0) * p / planks, b = x0 + (x1 - x0) * (p + 1) / planks;
      const Color c = shade(look.floor, 0.9f + 0.18f * value_noise(31, a * 3, 0, 1.0f));
      out.quad({a, 0, z0}, {b, 0, z0}, {b, 0, z1}, {a, 0, z1}, {0, 1, 0}, c);
    }
    out.quad({x0, r.height, z0}, {x1, r.height, z0}, {x1, r.height, z1}, {x0, r.height, z1}, {0, -1, 0}, look.ceiling);
    for (float x = x0 + 0.9f; x < x1 - 0.3f; x += 1.4f) {  // beams under the ceiling
      const float y = r.height - 0.18f;
      // Only their underside: seen from above (the camera behind the ceiling), nothing hides the room.
      out.quad({x - 0.1f, y, z0}, {x + 0.1f, y, z0}, {x + 0.1f, y, z1}, {x - 0.1f, y, z1}, {0, -1, 0}, look.beam);
    }
    return;
  }
  constexpr int kSegments = 40;
  for (int s = 0; s < kSegments; ++s) {
    const float a0 = kTau * s / kSegments, a1 = kTau * (s + 1) / kSegments;
    const Vec3 c{r.a.x, 0, r.a.z};
    const Vec3 p0{r.a.x + r.radius * std::cos(a0), 0, r.a.z + r.radius * std::sin(a0)};
    const Vec3 p1{r.a.x + r.radius * std::cos(a1), 0, r.a.z + r.radius * std::sin(a1)};
    const Color fc = shade(look.floor, 0.92f + 0.12f * ((s * 7) % 3) / 2.0f);
    for (const Vec3 v : {c, p1, p0}) out.vertex(v, {0, 1, 0}, fc);  // facing up
    const Vec3 up{0, r.height, 0};
    for (const Vec3 v : {c + up, p0 + up, p1 + up}) out.vertex(v, {0, -1, 0}, look.ceiling);  // facing down
  }
}

void walls(Builder& out, const RoomPiece& r, const Look& look, float door_width, float door_height) {
  // The outline, as points around (inwards normals).
  struct Edge {
    Vec3 a, b, n;
  };
  std::vector<Edge> edges;
  if (look.round) {
    constexpr int kSegments = 40;
    for (int s = 0; s < kSegments; ++s) {
      const float a0 = kTau * s / kSegments, a1 = kTau * (s + 1) / kSegments, am = (a0 + a1) / 2;
      edges.push_back({{r.a.x + r.radius * std::cos(a0), 0, r.a.z + r.radius * std::sin(a0)},
                       {r.a.x + r.radius * std::cos(a1), 0, r.a.z + r.radius * std::sin(a1)},
                       {-std::cos(am), 0, -std::sin(am)}});
    }
  } else {
    const float x0 = r.a.x - r.half.x, x1 = r.a.x + r.half.x, z0 = r.a.z - r.half.z, z1 = r.a.z + r.half.z;
    // Cut in pieces about a meter long (for the windows and the door).
    const auto side = [&](Vec3 a, Vec3 b, Vec3 n) {
      const float len = std::sqrt((b.x - a.x) * (b.x - a.x) + (b.z - a.z) * (b.z - a.z));
      const int pieces = std::max(1, static_cast<int>(len / 0.9f));
      for (int p = 0; p < pieces; ++p) edges.push_back({a + (b - a) * (static_cast<float>(p) / pieces), a + (b - a) * (static_cast<float>(p + 1) / pieces), n});
    };
    side({x0, 0, z0}, {x1, 0, z0}, {0, 0, 1});
    side({x1, 0, z0}, {x1, 0, z1}, {-1, 0, 0});
    side({x1, 0, z1}, {x0, 0, z1}, {0, 0, -1});
    side({x0, 0, z1}, {x0, 0, z0}, {1, 0, 0});
  }
  const float top = r.height;
  const float band = look.stone ? 0 : 0.35f;  // a darker band at the bottom (timber, mud)
  int count = 0;
  bool door_done = false;
  for (const Edge& e : edges) {
    const Vec3 up{0, 1, 0};
    const float mx = (e.a.x + e.b.x) / 2, mz = (e.a.z + e.b.z) / 2;
    const bool door = !door_done && std::fabs(mz) < 0.05f + (look.round ? 0.25f : 0) && std::fabs(mx) < door_width && e.n.z < -0.5f;
    const Color wall = look.stone ? shade(look.wall, 0.86f + 0.2f * value_noise(41, mx * 2, mz * 2, 1.0f)) : look.wall;
    if (band > 0) out.quad(e.a, e.b, e.b + up * band, e.a + up * band, e.n, look.low);
    out.quad(e.a + up * band, e.b + up * band, e.b + up * top, e.a + up * top, e.n, wall);
    if (look.stone) {  // courses of stones
      for (float y = 0.6f; y < top; y += 0.6f) {
        const Vec3 l = e.a + up * y + e.n * 0.01f, rr = e.b + up * y + e.n * 0.01f;
        out.quad(l, rr, rr + up * 0.035f, l + up * 0.035f, e.n, shade(look.wall, 0.7f));
      }
    }
    // The door: a dark plank door in its frame, a little in front of the wall.
    if (door) {
      door_done = true;
      const Vec3 o{0, 0, mz};
      const float hw = door_width / 2;
      const Vec3 n = e.n;
      const Vec3 f = n * 0.02f;
      out.quad(o + Vec3{-hw, 0, 0} + f, o + Vec3{hw, 0, 0} + f, o + Vec3{hw, door_height, 0} + f,
               o + Vec3{-hw, door_height, 0} + f, n, kDoor);
      for (const float x : {-hw - 0.08f, hw}) {
        out.quad(o + Vec3{x, 0, 0} + f * 2, o + Vec3{x + 0.08f, 0, 0} + f * 2, o + Vec3{x + 0.08f, door_height + 0.08f, 0} + f * 2,
                 o + Vec3{x, door_height + 0.08f, 0} + f * 2, n, look.beam);
      }
      out.quad(o + Vec3{-hw - 0.08f, door_height, 0} + f * 2, o + Vec3{hw + 0.08f, door_height, 0} + f * 2,
               o + Vec3{hw + 0.08f, door_height + 0.1f, 0} + f * 2, o + Vec3{-hw - 0.08f, door_height + 0.1f, 0} + f * 2,
               n, look.beam);
    } else if ((count++ % (look.stone ? 5 : 3)) == 1) {
      // A window: glass lit at night, in a wooden frame.
      const Vec3 m{(e.a.x + e.b.x) / 2, 0, (e.a.z + e.b.z) / 2};
      const Vec3 along = (e.b - e.a) * 0.5f;
      const float l = std::sqrt(along.x * along.x + along.z * along.z);
      const Vec3 t = l > 0 ? along * (1.0f / l) : Vec3{1, 0, 0};
      const float w = look.stone ? 0.16f : 0.32f, y0 = look.stone ? 2.6f + 3.0f * value_noise(43, mx, mz, 1.0f) : 1.15f;
      const float hgt = look.stone ? 0.7f : 0.62f;
      const Vec3 f = e.n * 0.015f;
      out.quad(m - t * (w + 0.06f) + up * (y0 - 0.06f) + f, m + t * (w + 0.06f) + up * (y0 - 0.06f) + f,
               m + t * (w + 0.06f) + up * (y0 + hgt + 0.06f) + f, m - t * (w + 0.06f) + up * (y0 + hgt + 0.06f) + f, e.n,
               look.beam);
      out.quad(m - t * w + up * y0 + f * 2, m + t * w + up * y0 + f * 2, m + t * w + up * (y0 + hgt) + f * 2,
               m - t * w + up * (y0 + hgt) + f * 2, e.n, kWindow);
    }
  }
  // Timber posts in the corners, a beam along the top (not in stone).
  if (!look.stone && !look.round) {
    for (const Edge& e : edges) {
      const Vec3 f = e.n * 0.02f;
      const Vec3 up{0, 1, 0};
      out.quad(e.a + up * (top - 0.2f) + f, e.b + up * (top - 0.2f) + f, e.b + up * top + f, e.a + up * top + f, e.n, look.beam);
    }
  }
}

// A tall hall's stair, whole: each step a slice of the ring from the wall
// (into it a little) to a meter inside the stair's circle, its tread on top
// of its two blocks (interior.cpp), its riser, its inner side, and under it a
// slope going up with the stair (no gaps under or beside it); a post under the
// landing.
void spiral_stair(Builder& out, const World& w, const RoomPiece& r, Color tread, Color side) {
  struct Step {
    Vec3 sum{};
    int blocks = 0;
    float top = 0;
  };
  std::array<Step, 256> steps{};
  int count = 0;
  const Object* landing = nullptr;
  for (const Object& o : w.objects) {
    if (o.kind != ObjectKind::Step) continue;
    if (o.variant == kLandingVariant) {
      landing = &o;
      continue;
    }
    Step& s = steps[o.variant];
    s.sum = s.sum + Vec3{o.pos.x - r.a.x, 0, o.pos.z - r.a.z};
    ++s.blocks;
    s.top = o.pos.y + 2 * o.size.y;
    count = std::max(count, o.variant + 1);
  }
  constexpr float kRise = 0.3f, kUnder = 0.55f;  // each step's height; the slope under the steps, below their fronts
  const Vec3 up{0, 1, 0};
  for (int i = 0; i < count; ++i) {
    const Step& s = steps[static_cast<size_t>(i)];
    if (s.blocks == 0) continue;
    const float ring = std::sqrt(s.sum.x * s.sum.x + s.sum.z * s.sum.z) / static_cast<float>(s.blocks);
    const float mid = std::atan2(s.sum.z, s.sum.x), half = 0.152f;  // a step's half angle (kStairStep)
    const float a0 = mid - half, a1 = mid + half;
    const float r0 = ring - 0.95f, r1 = r.radius + 0.05f;
    const auto at = [&](float rad, float a, float y) { return Vec3{r.a.x + rad * std::cos(a), y, r.a.z + rad * std::sin(a)}; };
    const float low0 = std::max(0.0f, s.top - kRise - kUnder), low1 = std::max(0.0f, s.top - kUnder);
    const Color t = shade(tread, 0.92f + 0.16f * value_noise(47, static_cast<float>(i), 0, 1.0f));
    // The tread, and its riser (from the step below's tread up).
    out.quad(at(r0, a0, s.top), at(r1, a0, s.top), at(r1, a1, s.top), at(r0, a1, s.top), up, t);
    const Vec3 back{std::sin(a0), 0, -std::cos(a0)};  // towards the step below
    out.quad(at(r0, a0, s.top - kRise), at(r1, a0, s.top - kRise), at(r1, a0, s.top), at(r0, a0, s.top), back, side);
    if (i == 0) out.quad(at(r0, a0, 0), at(r1, a0, 0), at(r1, a0, s.top - kRise), at(r0, a0, s.top - kRise), back, side);
    // Its inner side, down to the slope; the slope under it.
    const Vec3 in{-std::cos(mid), 0, -std::sin(mid)};
    out.quad(at(r0, a0, low0), at(r0, a1, low1), at(r0, a1, s.top), at(r0, a0, s.top), in, shade(side, 0.9f));
    if (low1 > 0) out.quad(at(r0, a0, low0), at(r1, a0, low0), at(r1, a1, low1), at(r0, a1, low1), up * -1.0f, shade(side, 0.7f));
    if (i + 1 == count) {  // the last one's back, up to the landing
      const Vec3 ahead{-std::sin(a1), 0, std::cos(a1)};
      out.quad(at(r0, a1, low1), at(r1, a1, low1), at(r1, a1, s.top), at(r0, a1, s.top), ahead, side);
    }
  }
  if (landing) {
    const float y = landing->pos.y, k = 0.22f;
    const Vec3 c{landing->pos.x, 0, landing->pos.z};
    const Vec3 sides[4] = {{1, 0, 0}, {0, 0, 1}, {-1, 0, 0}, {0, 0, -1}};
    for (const Vec3 n : sides) {
      const Vec3 t{-n.z, 0, n.x};
      const Vec3 a = c + n * k - t * k, b = c + n * k + t * k;
      out.quad(a, b, b + up * y, a + up * y, n, shade(side, 0.85f));
    }
  }
}

MeshData room_mesh(const World& w) {
  const Interior& in = w.interior;
  Builder out;
  if (in.pieces.empty()) return out.mesh;
  const RoomPiece& r = in.pieces[0];
  const ObjectKind building = w.place.door < w.outdoors.objects.size() ? w.outdoors.objects[w.place.door].kind : ObjectKind::Count;
  Look look{kPlanks, kPlaster, kTimber, kTimber, shade(kPlanks, 0.8f), r.radius > 0, false};
  float door_width = 0.9f, door_height = 1.9f;
  if (in.kind == PlaceKind::Tower) {
    look = {shade(kStone, 0.95f), kStone, kStone, shade(kStone, 0.6f), shade(kStone, 0.55f), true, true};
    door_width = 1.24f;
    door_height = 2.5f;
  } else if (in.kind == PlaceKind::HollowTree) {  // inside the trunk: wood all around
    look = {shade(kPlanks, 0.8f), kBark, shade(kBark, 0.85f), shade(kBark, 0.6f), shade(kBark, 0.5f), true, false};
    door_width = 1.24f;
    door_height = 2.5f;
  } else if (in.kind == PlaceKind::Lighthouse) {
    look = {shade(kStone, 0.9f), kWhitewash, shade(kStone, 0.85f), shade(kStone, 0.6f), shade(kWhitewash, 0.7f), true, true};
    door_width = 1.24f;
    door_height = 2.5f;
  } else if (in.kind == PlaceKind::Temple) {  // pale stone, a dark wooden ceiling
    look = {shade(kStone, 0.8f), shade(kWhitewash, 0.92f), shade(kStone, 0.9f), shade(kTimber, 0.9f), shade(kStone, 0.6f), false, true};
    door_width = 2.0f;
    door_height = 3.2f;
  } else if (building == ObjectKind::Cabin) {
    look = {shade(kPlanks, 0.9f), shade(kBark, 1.3f), kBark, kTimber, shade(kPlanks, 0.75f), false, false};
  } else if (building == ObjectKind::Farmhouse) {
    look.low = shade(kStone, 0.9f);
  } else if (building == ObjectKind::HouseHut) {
    look = {shade(kMud, 0.9f), kMud, shade(kMud, 0.75f), kTimber, kThatch, true, false};
  } else if (building == ObjectKind::HouseChimney) {
    look.wall = shade(kPlanks, 1.15f);
    look.low = shade(kStone, 0.9f);
  }
  floor_and_ceiling(out, r, look);
  walls(out, r, look, door_width, door_height);
  if (in.kind == PlaceKind::Tower || in.kind == PlaceKind::Lighthouse) spiral_stair(out, w, r, shade(kStone, 1.05f), shade(kStone, 0.75f));
  if (in.kind == PlaceKind::HollowTree) spiral_stair(out, w, r, kPlanks, shade(kBark, 0.9f));
  return out.mesh;
}

}  // namespace

MeshData build_interior_mesh(const World& w) {
  if (!inside(w)) return {};
  return w.interior.rounding > 0 ? cave_mesh(w.interior) : room_mesh(w);  // the cave, the burrow
}

void build_interior_list(const World& w, DrawList& list) {
  list.batches.assign(kMeshCount, {});
  list.parts.clear();
  list.mesh_parts.clear();
  list.visible.clear();
  list.cached = true;
  ++list.version;
  if (!inside(w)) return;
  list.batches[static_cast<size_t>(MeshId::InteriorShell)].push_back(make_instance(0, 0, 0, 0, 1, 1, 1));
  for (const Object& o : w.objects) add_interior_object(w, o, list);
}

}  // namespace monstres
