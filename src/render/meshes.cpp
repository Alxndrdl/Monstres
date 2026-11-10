#include "monstres/render/meshes.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <utility>

namespace monstres {

namespace {

struct V3 {
  float x = 0, y = 0, z = 0;
};
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
V3 mul(V3 a, V3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float length(V3 a) { return std::sqrt(dot(a, a)); }

constexpr float kTau = 6.28318530718f;

// Deterministic pseudo-random numbers for shapes (splitmix64).
float hash01(uint64_t a, uint64_t b = 0) {
  uint64_t z = a * 0x9E3779B97F4A7C15ULL + b + 0x632BE59BD9B4E019ULL;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  z ^= z >> 31;
  return static_cast<float>(z >> 40) / static_cast<float>(1ULL << 24);
}

Color shade(Color c, float k) { return {c.r * k, c.g * k, c.b * k, 1}; }

namespace colors {
constexpr Color kBark{0.42f, 0.28f, 0.17f, 1};
constexpr Color kWood{0.80f, 0.66f, 0.45f, 1};
constexpr Color kLeaves{0.34f, 0.62f, 0.26f, 1};
constexpr Color kNeedles{0.18f, 0.42f, 0.27f, 1};
constexpr Color kBushLeaves{0.40f, 0.66f, 0.30f, 1};
constexpr Color kRock{0.56f, 0.56f, 0.58f, 1};
constexpr Color kMoss{0.38f, 0.52f, 0.28f, 1};
constexpr Color kCratePlank{0.74f, 0.55f, 0.32f, 1};
constexpr Color kCrateFrame{0.50f, 0.34f, 0.18f, 1};
constexpr Color kWhite{1, 1, 1, 1};
}  // namespace colors

// Appends flat-shaded triangles to a mesh.
class Builder {
public:
  void tri(V3 a, V3 b, V3 c, Color color) {
    V3 n = cross(b - a, c - a);
    const float len = length(n);
    if (len < 1e-9f) return;  // degenerate (e.g. the tip of a cone)
    n = n * (1.0f / len);
    for (V3 p : {a, b, c}) {
      mesh_.vertices.insert(mesh_.vertices.end(), {p.x, p.y, p.z, n.x, n.y, n.z, color.r, color.g, color.b});
    }
  }
  // Counter-clockwise when seen from the front.
  void quad(V3 a, V3 b, V3 c, V3 d, Color color) {
    tri(a, b, c, color);
    tri(a, c, d, color);
  }
  MeshData take() { return std::move(mesh_); }

private:
  MeshData mesh_;
};

using Transform = std::function<V3(V3)>;
const Transform kIdentity = [](V3 p) { return p; };

// Truncated cone along +Y from y = 0 to y = height (a cone when r1 = 0).
void frustum(Builder& b, const Transform& t, float r0, float r1, float height, int segments,
             Color side, const Color* bottom_cap, const Color* top_cap, float phase = 0) {
  for (int i = 0; i < segments; ++i) {
    const float a0 = phase + kTau * static_cast<float>(i) / static_cast<float>(segments);
    const float a1 = phase + kTau * static_cast<float>(i + 1) / static_cast<float>(segments);
    const V3 b0{r0 * std::cos(a0), 0, r0 * std::sin(a0)}, b1{r0 * std::cos(a1), 0, r0 * std::sin(a1)};
    const V3 t0{r1 * std::cos(a0), height, r1 * std::sin(a0)}, t1{r1 * std::cos(a1), height, r1 * std::sin(a1)};
    b.quad(t(b0), t(t0), t(t1), t(b1), side);
    if (top_cap && r1 > 0) b.tri(t({0, height, 0}), t(t1), t(t0), *top_cap);
    if (bottom_cap) b.tri(t({0, 0, 0}), t(b0), t(b1), *bottom_cap);
  }
}

// Unit icosphere (one subdivision: 80 faces), as shared vertices + faces.
struct IndexedMesh {
  std::vector<V3> vertices;
  std::vector<std::array<int, 3>> faces;
};

IndexedMesh icosphere() {
  const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;
  IndexedMesh m;
  m.vertices = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t},
                {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
  const std::vector<std::array<int, 3>> base = {
      {0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4},
      {11, 10, 2}, {10, 7, 6}, {7, 1, 8}, {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8},
      {3, 8, 9}, {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}};
  std::map<std::pair<int, int>, int> midpoints;
  auto midpoint = [&](int a, int b) {
    const auto key = std::minmax(a, b);
    if (auto it = midpoints.find(key); it != midpoints.end()) return it->second;
    m.vertices.push_back((m.vertices[static_cast<size_t>(a)] + m.vertices[static_cast<size_t>(b)]) * 0.5f);
    const int index = static_cast<int>(m.vertices.size() - 1);
    midpoints[key] = index;
    return index;
  };
  for (const auto& f : base) {
    const int ab = midpoint(f[0], f[1]), bc = midpoint(f[1], f[2]), ca = midpoint(f[2], f[0]);
    m.faces.push_back({f[0], ab, ca});
    m.faces.push_back({f[1], bc, ab});
    m.faces.push_back({f[2], ca, bc});
    m.faces.push_back({ab, bc, ca});
  }
  for (V3& v : m.vertices) v = v * (1.0f / length(v));
  return m;
}

// A lumpy ball: icosphere with each vertex pushed in or out by up to `noise`.
// `face_color` picks the color of each face from its outward normal.
void blob(Builder& b, V3 center, V3 radii, float noise, uint64_t seed,
          const std::function<Color(V3 normal, size_t face)>& face_color,
          const std::function<V3(V3)>& reshape = nullptr) {
  IndexedMesh m = icosphere();
  for (size_t i = 0; i < m.vertices.size(); ++i) {
    const float k = 1.0f + noise * (2.0f * hash01(seed, i) - 1.0f);
    V3 p = mul(m.vertices[i] * k, radii);
    if (reshape) p = reshape(p);
    m.vertices[i] = center + p;
  }
  for (size_t f = 0; f < m.faces.size(); ++f) {
    V3 a = m.vertices[static_cast<size_t>(m.faces[f][0])];
    V3 c1 = m.vertices[static_cast<size_t>(m.faces[f][1])];
    V3 c2 = m.vertices[static_cast<size_t>(m.faces[f][2])];
    V3 n = cross(c1 - a, c2 - a);
    // Keep every face pointing outwards, whatever the deformation did.
    if (dot(n, (a + c1 + c2) * (1.0f / 3.0f) - center) < 0) {
      std::swap(c1, c2);
      n = n * -1.0f;
    }
    b.tri(a, c1, c2, face_color(n * (1.0f / std::max(length(n), 1e-9f)), f));
  }
}

// Rescales a mesh so its bounds become exactly [lo, hi], and recomputes the
// normals (the scale may be non-uniform).
void fit_bounds(MeshData& mesh, V3 lo, V3 hi) {
  constexpr int k = MeshData::kFloatsPerVertex;
  V3 mn{1e9f, 1e9f, 1e9f}, mx{-1e9f, -1e9f, -1e9f};
  for (size_t i = 0; i < mesh.vertices.size(); i += k) {
    mn = {std::min(mn.x, mesh.vertices[i]), std::min(mn.y, mesh.vertices[i + 1]), std::min(mn.z, mesh.vertices[i + 2])};
    mx = {std::max(mx.x, mesh.vertices[i]), std::max(mx.y, mesh.vertices[i + 1]), std::max(mx.z, mesh.vertices[i + 2])};
  }
  for (size_t i = 0; i < mesh.vertices.size(); i += k) {
    mesh.vertices[i] = lo.x + (mesh.vertices[i] - mn.x) / (mx.x - mn.x) * (hi.x - lo.x);
    mesh.vertices[i + 1] = lo.y + (mesh.vertices[i + 1] - mn.y) / (mx.y - mn.y) * (hi.y - lo.y);
    mesh.vertices[i + 2] = lo.z + (mesh.vertices[i + 2] - mn.z) / (mx.z - mn.z) * (hi.z - lo.z);
  }
  for (size_t i = 0; i < mesh.vertices.size(); i += 3 * k) {
    const V3 a{mesh.vertices[i], mesh.vertices[i + 1], mesh.vertices[i + 2]};
    const V3 b{mesh.vertices[i + k], mesh.vertices[i + k + 1], mesh.vertices[i + k + 2]};
    const V3 c{mesh.vertices[i + 2 * k], mesh.vertices[i + 2 * k + 1], mesh.vertices[i + 2 * k + 2]};
    V3 n = cross(b - a, c - a);
    n = n * (1.0f / std::max(length(n), 1e-9f));
    for (int v = 0; v < 3; ++v) {
      mesh.vertices[i + static_cast<size_t>(v * k) + 3] = n.x;
      mesh.vertices[i + static_cast<size_t>(v * k) + 4] = n.y;
      mesh.vertices[i + static_cast<size_t>(v * k) + 5] = n.z;
    }
  }
}

// Each face slightly lighter or darker: gives the low-poly look.
std::function<Color(V3, size_t)> jitter(Color base, uint64_t seed, float amount = 0.08f) {
  return [=](V3, size_t face) { return shade(base, 1.0f + amount * (2.0f * hash01(seed, face) - 1.0f)); };
}

// ---- The meshes ----

MeshData cube() {
  Builder b;
  const V3 corners[8] = {{-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
                         {-1, -1, 1},  {1, -1, 1},  {1, 1, 1},  {-1, 1, 1}};
  const int faces[6][4] = {{1, 2, 6, 5}, {4, 7, 3, 0}, {3, 7, 6, 2}, {4, 0, 1, 5}, {5, 6, 7, 4}, {0, 3, 2, 1}};
  for (const auto& f : faces) b.quad(corners[f[0]], corners[f[1]], corners[f[2]], corners[f[3]], colors::kWhite);
  return b.take();
}

MeshData crate() {
  Builder b;
  constexpr float border = 0.18f;
  // Each face: a darker frame around a lighter plank panel.
  const V3 normals[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
  for (V3 n : normals) {
    // u, v span the face so that u x v = n (counter-clockwise from outside).
    V3 u = std::fabs(n.y) > 0.5f ? V3{1, 0, 0} : V3{0, 1, 0};
    V3 v = cross(n, u);
    u = cross(v, n);
    auto p = [&](float s, float t) { return n + u * s + v * t; };
    const float o = 1.0f, i = 1.0f - border;
    b.quad(p(-o, -o), p(o, -o), p(o, -i), p(-o, -i), colors::kCrateFrame);
    b.quad(p(-o, i), p(o, i), p(o, o), p(-o, o), colors::kCrateFrame);
    b.quad(p(-o, -i), p(-i, -i), p(-i, i), p(-o, i), colors::kCrateFrame);
    b.quad(p(i, -i), p(o, -i), p(o, i), p(i, i), colors::kCrateFrame);
    b.quad(p(-i, -i), p(i, -i), p(i, i), p(-i, i), colors::kCratePlank);
  }
  return b.take();
}

MeshData tree() {
  Builder b;
  frustum(b, kIdentity, 0.2f, 0.13f, 2.3f, 6, colors::kBark, nullptr, nullptr);
  const struct { V3 center; V3 radii; } crowns[] = {
      {{0, 2.75f, 0}, {1.3f, 1.05f, 1.3f}},
      {{0.6f, 2.45f, 0.35f}, {0.85f, 0.75f, 0.85f}},
      {{-0.55f, 2.5f, -0.3f}, {0.9f, 0.8f, 0.9f}},
      {{0.1f, 3.55f, -0.1f}, {0.8f, 0.7f, 0.8f}},
  };
  uint64_t seed = 11;
  for (const auto& c : crowns) {
    blob(b, c.center, c.radii, 0.12f, seed, jitter(colors::kLeaves, seed));
    ++seed;
  }
  return b.take();
}

MeshData pine() {
  Builder b;
  frustum(b, kIdentity, 0.18f, 0.12f, 1.3f, 6, colors::kBark, nullptr, nullptr);
  const Color under = shade(colors::kNeedles, 0.7f);
  const struct { float y, radius, height; Color color; } tiers[] = {
      {0.8f, 1.35f, 1.9f, colors::kNeedles},
      {1.8f, 1.05f, 1.7f, shade(colors::kNeedles, 1.07f)},
      {2.8f, 0.7f, 1.6f, shade(colors::kNeedles, 1.14f)},
  };
  float phase = 0;
  for (const auto& tier : tiers) {
    const float y = tier.y;
    frustum(b, [y](V3 p) { return V3{p.x, p.y + y, p.z}; }, tier.radius, 0, tier.height, 8, tier.color,
            &under, nullptr, phase);
    phase += 0.4f;
  }
  return b.take();
}

MeshData rock(uint64_t variant) {
  Builder b;
  const Color base = shade(colors::kRock, 0.9f + 0.2f * hash01(variant, 99));
  const bool mossy = variant % 2 == 1;
  blob(b, {0, 0, 0}, {1, 1, 1}, 0.32f, 1000 + variant,
       [&](V3 n, size_t face) {
         if (mossy && n.y > 0.75f) return shade(colors::kMoss, 0.95f + 0.1f * hash01(variant, face));
         return shade(base, 0.9f + 0.18f * hash01(variant + 7, face));
       },
       [](V3 p) { return V3{p.x, std::max(p.y, -0.3f), p.z}; });  // flat underside
  MeshData m = b.take();
  fit_bounds(m, {-1, 0, -1}, {1, 1, 1});
  return m;
}

MeshData log_mesh() {
  Builder b;
  // Built along Y (0..2, radius 1), then laid down along X with its bottom on the ground.
  const Transform lay_down = [](V3 p) { return V3{p.y - 1, 1 - p.x, p.z}; };
  frustum(b, lay_down, 1, 1, 2, 8, colors::kBark, &colors::kWood, &colors::kWood);
  return b.take();
}

MeshData stump() {
  Builder b;
  frustum(b, kIdentity, 1, 0.88f, 1, 8, colors::kBark, nullptr, &colors::kWood);
  return b.take();
}

MeshData bush() {
  Builder b;
  const struct { V3 center; V3 radii; } parts[] = {
      {{0, 0.45f, 0}, {0.75f, 0.5f, 0.75f}},
      {{0.4f, 0.38f, 0.25f}, {0.55f, 0.42f, 0.55f}},
      {{-0.4f, 0.4f, -0.2f}, {0.55f, 0.45f, 0.55f}},
      {{0.1f, 0.6f, -0.35f}, {0.45f, 0.4f, 0.45f}},
  };
  uint64_t seed = 50;
  for (const auto& p : parts) {
    blob(b, p.center, p.radii, 0.15f, seed, jitter(colors::kBushLeaves, seed, 0.1f),
         [](V3 q) { return V3{q.x, std::max(q.y, -0.35f), q.z}; });
    ++seed;
  }
  MeshData m = b.take();
  fit_bounds(m, {-1, 0, -1}, {1, 1, 1});
  return m;
}

}  // namespace

std::vector<MeshData> build_meshes() {
  std::vector<MeshData> meshes(kMeshCount);
  meshes[static_cast<size_t>(MeshId::Cube)] = cube();
  meshes[static_cast<size_t>(MeshId::Tree)] = tree();
  meshes[static_cast<size_t>(MeshId::Pine)] = pine();
  for (size_t i = 0; i < kRockVariants; ++i) {
    meshes[static_cast<size_t>(MeshId::Rock0) + i] = rock(i);
  }
  meshes[static_cast<size_t>(MeshId::Log)] = log_mesh();
  meshes[static_cast<size_t>(MeshId::Stump)] = stump();
  meshes[static_cast<size_t>(MeshId::Bush)] = bush();
  meshes[static_cast<size_t>(MeshId::Crate)] = crate();
  return meshes;
}

}  // namespace monstres
