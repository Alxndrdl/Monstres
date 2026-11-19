#pragma once

#include "monstres/render/renderer.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace monstres {

// The order and the details of a frame's draw calls, worked out on the CPU
// before the renderer issues them (no OpenGL: testable natively), following
// RenderOptions (renderer.hpp).

// From the bits the page sends: 1 split_fade, 2 sort_opaque, 4 cull_lights, 8 depth_prepass.
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
  float distance2 = 0;   // from the camera to the box (0: inside, or unknown)
  bool fade = true;      // needs the program that discards (always, unless split_fade)
  uint16_t lights = 0;   // bit i: View::lights[i] reaches it (all of them, unless cull_lights)
};

// The draws of one pass (far or near lists; opaque or transparent meshes):
// meshes in index order, each with the lists in order, then their visible
// ranges (DrawList::visible), as the renderer always drew them; sorted by
// distance if asked (opaque only, stable).
void plan_draws(std::span<const DrawList* const> lists, std::span<const PlanMesh> meshes, bool far, bool transparent,
                const View& view, const RenderOptions& options, std::vector<DrawItem>& out);

// Instances above this, in a batch without parts, are not bounded one by one
// (the draw is treated as reaching everything).
inline constexpr uint32_t kMaxBoundedInstances = 4096;

// Whether the box comes near the line where things show through
// (View::fade_*): conservative, never false when a pixel of it may fade.
bool near_fade_line(const View& view, const Aabb& box);

// Squared distance from a point to a box (0 inside).
float distance2_to_box(const Aabb& box, float x, float y, float z);

}  // namespace monstres
