#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace monstres {

struct Color {
  float r = 0, g = 0, b = 0, a = 1;
};

// Column-major 4x4 matrix, as OpenGL expects it.
struct Mat4 {
  float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
};

// A mesh: triangles, 10 floats per vertex
// (position xyz, normal xyz, color rgba).
struct MeshData {
  std::vector<float> vertices;
  bool transparent = false;   // alpha-blended, drawn after the opaque meshes
  bool double_sided = false;  // no back-face culling (grass blades, flowers)
  static constexpr size_t kFloatsPerVertex = 10;
  size_t vertex_count() const { return vertices.size() / kFloatsPerVertex; }
};

// Rotation as a unit quaternion.
struct Quat {
  float x = 0, y = 0, z = 0, w = 1;
};

// Rotation around one axis (radians). Around X, a positive angle swings a
// hanging limb forward (towards -Z); around Z, it tilts +Y towards -X.
inline Quat quat_axis(float ax, float ay, float az, float angle) {
  const float s = std::sin(angle / 2);
  return {ax * s, ay * s, az * s, std::cos(angle / 2)};
}
inline Quat quat_yaw(float angle) { return quat_axis(0, 1, 0, angle); }
inline Quat quat_x(float angle) { return quat_axis(1, 0, 0, angle); }
inline Quat quat_z(float angle) { return quat_axis(0, 0, 1, angle); }

// a * b: rotate by b, then by a.
inline Quat operator*(const Quat& a, const Quat& b) {
  return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
          a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

// Rotates the vector (x, y, z) in place.
inline void rotate(const Quat& q, float& x, float& y, float& z) {
  // v + 2 * cross(q.xyz, cross(q.xyz, v) + w * v)
  const float cx = q.y * z - q.z * y + q.w * x;
  const float cy = q.z * x - q.x * z + q.w * y;
  const float cz = q.x * y - q.y * x + q.w * z;
  const float rx = x + 2 * (q.y * cz - q.z * cy);
  const float ry = y + 2 * (q.z * cx - q.x * cz);
  const float rz = z + 2 * (q.x * cy - q.y * cx);
  x = rx;
  y = ry;
  z = rz;
}

// One copy of a mesh in the world: scaled, then rotated, then moved. All
// copies of a mesh are drawn with a single instanced draw call.
struct Instance {
  float x = 0, y = 0, z = 0;     // position of the mesh origin
  float wind = 0;                // how much the wind sways it (0 = rigid)
  Quat rotation;
  float sx = 1, sy = 1, sz = 1;  // scale
  float material = 0;            // kLit, kWater or kGlowing
  Color tint{1, 1, 1, 1};        // multiplies the mesh colors (alpha too)
};
static_assert(sizeof(Instance) == 16 * sizeof(float), "uploaded as-is to the GPU");

// Instance::material.
inline constexpr float kLit = 0;      // shaded by the sun, the sky and the lights around
inline constexpr float kWater = 1;    // waves and reflections
inline constexpr float kGlowing = 2;  // gives its own light: as bright by night as by day
inline constexpr float kFadeNear = 3; // lit, shrinks away between View::fade_near_* (grass, flowers)
inline constexpr float kFadeFar = 4;  // lit, shrinks away between View::fade_far_* (small things)

// Vertex colors with this alpha (opaque meshes: windows, lanterns) light up
// at night (View::night).
inline constexpr float kLitAtNightAlpha = 0.985f;

// A small light (lantern, the magician's staff, a glowing monster).
struct PointLight {
  float x = 0, y = 0, z = 0;
  float radius = 1;  // no light beyond
  Color color;       // at the center
};
inline constexpr size_t kMaxPointLights = 12;

// The usual case: turned around the vertical axis only.
inline Instance make_instance(float x, float y, float z, float yaw, float sx, float sy, float sz,
                              Color tint = {1, 1, 1, 1}, float wind = 0) {
  Instance in;
  in.x = x;
  in.y = y;
  in.z = z;
  in.rotation = quat_yaw(yaw);
  in.sx = sx;
  in.sy = sy;
  in.sz = sz;
  in.tint = tint;
  in.wind = wind;
  return in;
}

// What to draw: batches[i] holds the instances of mesh i. Opaque meshes are
// drawn first, then transparent ones, in index order.
struct DrawList {
  std::vector<std::vector<Instance>> batches;
  // A list marked `cached` is uploaded to the GPU once and reused until its
  // version changes (for the landscape, which never moves).
  bool cached = false;
  uint32_t version = 0;
};

struct View {
  Mat4 view_projection;
  Mat4 inverse_view_projection;  // for the sky
  float camera_x = 0, camera_y = 0, camera_z = 0;
  float sun_x = 0, sun_y = 1, sun_z = 0;  // direction towards the sun disc, normalized
  float light_x = 0, light_y = 1, light_z = 0;  // direction of the light used for shading
  float moon_x = 0, moon_y = -1, moon_z = 0;  // direction towards the moon, normalized
  Color sky_zenith;
  Color sky_horizon;
  Color fog;
  // Lighting (the defaults: a sunny day, as portraits are drawn).
  Color light_color{0.62f, 0.589f, 0.527f, 1};  // sun (or moon) on surfaces facing it
  Color ambient_sky{0.52f, 0.58f, 0.70f, 1};    // on surfaces facing up
  Color ambient_ground{0.36f, 0.40f, 0.32f, 1}; // on surfaces facing down
  float night = 0;                              // 0 day .. 1 night: stars, lit windows
  PointLight lights[kMaxPointLights];
  size_t light_count = 0;
  float fog_start = 45, fog_end = 320;
  // Where kFadeNear and kFadeFar instances shrink away: from (fade_x, fade_z), between start and end meters.
  float fade_x = 0, fade_z = 0;
  float fade_near_start = 1e6f, fade_near_end = 2e6f;
  float fade_far_start = 1e6f, fade_far_end = 2e6f;
  float time = 0;  // seconds, drives the wind
  bool sky = true;  // false: no sky, the background stays transparent (portraits)
};

struct Frame {
  int pixel_width = 0;
  int pixel_height = 0;
};

// What the last frame cost the renderer (see Renderer::set_profiling).
// Triangles are counted by group of meshes (models.hpp, mesh_group).
inline constexpr size_t kMeshGroups = 6;
struct RenderStats {
  uint32_t draw_calls = 0;
  uint64_t instances = 0;
  uint64_t triangles = 0;
  uint64_t triangles_by_group[kMeshGroups] = {};
  uint64_t uploaded_bytes = 0;  // instance data sent to the GPU
  double gpu_ms = -1;           // GPU time of a recent frame, -1: unknown (no timer on this device)
};

class Renderer {
public:
  virtual ~Renderer() = default;
  // Replaces mesh `index` (used for meshes built at runtime, like the ground).
  virtual void set_mesh(size_t index, const MeshData& mesh) = 0;
  virtual void begin_frame(const Frame& frame) = 0;
  // Draws the sky, then every list (all opaque batches, then all transparent ones).
  virtual void draw(const View& view, std::span<const DrawList* const> lists) = 0;
  virtual void end_frame() = 0;
  // Counting and timing (a little slower): stats() describes the last frame.
  virtual void set_profiling(bool on) { (void)on; }
  virtual const RenderStats& stats() const {
    static const RenderStats none;
    return none;
  }
  // Draws the lists into an image of width x height pixels instead of the
  // screen (portraits for the menus), antialiased. Returns RGBA bytes, rows
  // from the top; transparent where nothing was drawn when view.sky is false.
  virtual std::vector<uint8_t> draw_offscreen(const View& view, std::span<const DrawList* const> lists, int width,
                                              int height) = 0;
};

}  // namespace monstres
