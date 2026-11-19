#pragma once

#include "monstres/render/renderer.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace monstres {

// The order and the details of a frame's draw calls, worked out on the CPU
// before the renderer issues them (no OpenGL: testable natively), following
// RenderOptions (renderer.hpp).

// From the bits the page sends: 1 deferred_shading.
RenderOptions render_options_from_bits(int bits);

// What the plan needs to know of a mesh.
struct PlanMesh {
  int32_t vertex_count = 0;  // drawn: indices, or vertices when there are none
  bool transparent = false;
  Aabb bounds = empty_aabb();  // of its vertices (empty: unknown)
};

// One draw call: instances of one batch of one list, with vertices of the mesh.
struct DrawItem {
  uint32_t mesh = 0;
  const DrawList* list = nullptr;
  uint32_t first_instance = 0, instance_count = 0;
  int32_t first_vertex = 0, vertex_count = 0;
  bool bounded = false;  // whether `bounds` is known
  Aabb bounds = empty_aabb();
  // Needs the program that may discard (what is in the camera's way shows
  // through): only what is near that line; the rest keeps the GPU's early
  // depth test with a program without `discard`.
  bool fade = true;
  uint16_t lights = 0;   // bit i: View::lights[i] reaches its box (all of them when not bounded)
};

// The draws of one pass (far or near lists; opaque or transparent meshes):
// meshes in index order, each with the lists in order, then their visible
// ranges (DrawList::visible).
void plan_draws(std::span<const DrawList* const> lists, std::span<const PlanMesh> meshes, bool far, bool transparent,
                const View& view, std::vector<DrawItem>& out);

// Instances above this, in a batch without parts, are not bounded one by one
// (the draw is treated as reaching everything).
inline constexpr uint32_t kMaxBoundedInstances = 4096;

// Whether the box comes near the line where things show through
// (View::fade_*): conservative, never false when a pixel of it may fade.
bool near_fade_line(const View& view, const Aabb& box);

// Squared distance from a point to a box (0 inside).
float distance2_to_box(const Aabb& box, float x, float y, float z);

// The pixels a point light may reach on a screen of width x height (deferred
// shading draws it there only): x, y from the bottom left, w, h.
// False when it is all out of view. The whole screen when the camera is
// within its reach (or too close to tell).
struct PixelRect {
  int x = 0, y = 0, w = 0, h = 0;
};
bool light_rect(const Mat4& view_projection, const PointLight& light, int width, int height, PixelRect& out);


}  // namespace monstres
