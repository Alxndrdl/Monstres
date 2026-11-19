#include "monstres/render/draw_plan.hpp"

#include <algorithm>
#include <cmath>

namespace monstres {

RenderOptions render_options_from_bits(int bits) {
  RenderOptions o;
  o.deferred_shading = (bits & 1) != 0;
  return o;
}

float distance2_to_box(const Aabb& b, float x, float y, float z) {
  const float dx = std::max({b.min_x - x, 0.0f, x - b.max_x});
  const float dy = std::max({b.min_y - y, 0.0f, y - b.max_y});
  const float dz = std::max({b.min_z - z, 0.0f, z - b.max_z});
  return dx * dx + dy * dy + dz * dz;
}

bool near_fade_line(const View& view, const Aabb& box) {
  const float r = view.fade_radius;
  if (r <= 0) return false;
  // The segment against the box grown by the radius (slabs): contains every
  // point within the radius of the box, so never misses one.
  const float from[3] = {view.fade_from_x, view.fade_from_y, view.fade_from_z};
  const float to[3] = {view.fade_to_x, view.fade_to_y, view.fade_to_z};
  const float lo[3] = {box.min_x - r, box.min_y - r, box.min_z - r};
  const float hi[3] = {box.max_x + r, box.max_y + r, box.max_z + r};
  float t0 = 0, t1 = 1;
  for (int a = 0; a < 3; ++a) {
    const float d = to[a] - from[a];
    if (std::fabs(d) < 1e-6f) {
      if (from[a] < lo[a] || from[a] > hi[a]) return false;
      continue;
    }
    float ta = (lo[a] - from[a]) / d, tb = (hi[a] - from[a]) / d;
    if (ta > tb) std::swap(ta, tb);
    t0 = std::max(t0, ta);
    t1 = std::min(t1, tb);
    if (t0 > t1) return false;
  }
  return true;
}

namespace {

// The box of the parts overlapping a range (parts sorted by their first,
// as they are built; else all of them are looked at).
bool parts_bounds(const std::vector<Part>& parts, DrawRange r, Aabb& out) {
  const uint32_t end = r.first + r.count;
  bool any = false;
  const auto take = [&](const Part& p) {
    if (p.hidden || p.range.count == 0 || p.bounds.empty()) return;
    if (p.range.first >= end || p.range.first + p.range.count <= r.first) return;
    out.add(p.bounds);
    any = true;
  };
  const bool sorted = std::is_sorted(parts.begin(), parts.end(),
                                     [](const Part& a, const Part& b) { return a.range.first < b.range.first; });
  if (!sorted) {
    for (const Part& p : parts) take(p);
    return any;
  }
  // The first part that may reach the range: the last one starting at or before it.
  auto it = std::upper_bound(parts.begin(), parts.end(), r.first,
                             [](uint32_t v, const Part& p) { return v < p.range.first; });
  if (it != parts.begin()) --it;
  for (; it != parts.end() && it->range.first < end; ++it) take(*it);
  return any;
}

// The box of instances (each: the mesh's box moved, turned and scaled; as a
// sphere around it, plus how far the wind may sway it).
bool instances_bounds(std::span<const Instance> instances, const Aabb& mesh, Aabb& out) {
  if (mesh.empty() || instances.size() > kMaxBoundedInstances) return false;
  const float cx = (mesh.min_x + mesh.max_x) / 2, cy = (mesh.min_y + mesh.max_y) / 2, cz = (mesh.min_z + mesh.max_z) / 2;
  const float hx = (mesh.max_x - mesh.min_x) / 2, hy = (mesh.max_y - mesh.min_y) / 2, hz = (mesh.max_z - mesh.min_z) / 2;
  for (const Instance& in : instances) {
    float x = cx * in.sx, y = cy * in.sy, z = cz * in.sz;
    rotate(in.rotation, x, y, z);
    const float ex = hx * std::fabs(in.sx), ey = hy * std::fabs(in.sy), ez = hz * std::fabs(in.sz);
    float radius = std::sqrt(ex * ex + ey * ey + ez * ez);
    // The vertex shader: up to 1.1 * 0.12 * 3.2 * wind * height.
    if (in.wind > 0) radius += 0.45f * in.wind * std::max(std::fabs(mesh.max_y * in.sy), std::fabs(mesh.min_y * in.sy));
    out.add(in.x + x - radius, in.y + y - radius, in.z + z - radius);
    out.add(in.x + x + radius, in.y + y + radius, in.z + z + radius);
  }
  return !instances.empty();
}

}  // namespace

void plan_draws(std::span<const DrawList* const> lists, std::span<const PlanMesh> meshes, bool far, bool transparent,
                const View& view, std::vector<DrawItem>& out) {
  out.clear();
  const size_t light_count = std::min(view.light_count, kMaxPointLights);
  const uint16_t all_lights = static_cast<uint16_t>((1u << light_count) - 1);

  for (size_t i = 0; i < meshes.size(); ++i) {
    const PlanMesh& m = meshes[i];
    if (m.vertex_count == 0 || m.transparent != transparent) continue;
    for (const DrawList* list : lists) {
      if (list->far != far || i >= list->batches.size() || list->batches[i].empty()) continue;
      const auto& instances = list->batches[i];
      const auto total = static_cast<uint32_t>(instances.size());
      const std::vector<Part>* parts = i < list->parts.size() && !list->parts[i].empty() ? &list->parts[i] : nullptr;
      const std::vector<Part>* mesh_parts =
          i < list->mesh_parts.size() && !list->mesh_parts[i].empty() ? &list->mesh_parts[i] : nullptr;

      const auto add = [&](uint32_t first, uint32_t count, int32_t first_vertex, int32_t vertex_count) {
        if (count == 0 || vertex_count <= 0) return;
        DrawItem item;
        item.mesh = static_cast<uint32_t>(i);
        item.list = list;
        item.first_instance = first;
        item.instance_count = count;
        item.first_vertex = first_vertex;
        item.vertex_count = vertex_count;
        {
          Aabb box = empty_aabb();
          bool known;
          if (mesh_parts && vertex_count < m.vertex_count) {
            // Parts of a mesh drawn once, unturned (the ground): its own boxes.
            known = parts_bounds(*mesh_parts, {static_cast<uint32_t>(first_vertex), static_cast<uint32_t>(vertex_count)}, box);
          } else if (mesh_parts && !parts && total == 1) {
            known = parts_bounds(*mesh_parts, {0, static_cast<uint32_t>(m.vertex_count)}, box);
          } else if (parts) {
            known = parts_bounds(*parts, {first, count}, box);
          } else {
            known = instances_bounds(std::span(instances).subspan(first, count), m.bounds, box);
          }
          item.bounded = known && !box.empty();
          if (item.bounded) item.bounds = box;
        }
        item.fade = !transparent && (!item.bounded || near_fade_line(view, item.bounds));
        item.lights = all_lights;
        if (item.bounded) {
          item.lights = 0;
          for (size_t l = 0; l < light_count; ++l) {
            const PointLight& p = view.lights[l];
            if (distance2_to_box(item.bounds, p.x, p.y, p.z) <= p.radius * p.radius) item.lights |= static_cast<uint16_t>(1u << l);
          }
        }
        out.push_back(item);
      };

      const DrawList::Visible* v = i < list->visible.size() ? &list->visible[i] : nullptr;
      if (!v || v->all) {
        add(0, total, 0, m.vertex_count);
        continue;
      }
      // Culled (cull.hpp): the instances and parts of the mesh in view only.
      for (const DrawRange& r : v->instances) {
        const uint32_t first = std::min(r.first, total);
        const uint32_t count = std::min(r.count, total - first);
        if (v->vertices.empty()) {
          add(first, count, 0, m.vertex_count);
          continue;
        }
        for (const DrawRange& part : v->vertices) {
          const int32_t end = std::min(static_cast<int32_t>(part.first + part.count), m.vertex_count);
          add(first, count, static_cast<int32_t>(part.first), end - static_cast<int32_t>(part.first));
        }
      }
    }
  }
}

bool light_rect(const Mat4& vp, const PointLight& l, int width, int height, PixelRect& out) {
  out = {0, 0, width, height};
  float lo_x = 1e30f, lo_y = 1e30f, hi_x = -1e30f, hi_y = -1e30f;
  int behind = 0;
  for (int k = 0; k < 8; ++k) {
    const float x = l.x + ((k & 1) ? l.radius : -l.radius);
    const float y = l.y + ((k & 2) ? l.radius : -l.radius);
    const float z = l.z + ((k & 4) ? l.radius : -l.radius);
    const float* m = vp.m;
    const float cx = m[0] * x + m[4] * y + m[8] * z + m[12];
    const float cy = m[1] * x + m[5] * y + m[9] * z + m[13];
    const float cw = m[3] * x + m[7] * y + m[11] * z + m[15];
    if (cw < 0.05f) {
      ++behind;
      continue;
    }
    lo_x = std::min(lo_x, cx / cw);
    hi_x = std::max(hi_x, cx / cw);
    lo_y = std::min(lo_y, cy / cw);
    hi_y = std::max(hi_y, cy / cw);
  }
  if (behind == 8) return false;  // all of it behind the camera
  if (behind > 0) return true;    // around the camera: the whole screen
  if (hi_x < -1 || lo_x > 1 || hi_y < -1 || lo_y > 1) return false;
  const auto px = [](float ndc, int size) { return (std::clamp(ndc, -1.0f, 1.0f) * 0.5f + 0.5f) * static_cast<float>(size); };
  const int x0 = static_cast<int>(std::floor(px(lo_x, width))), x1 = static_cast<int>(std::ceil(px(hi_x, width)));
  const int y0 = static_cast<int>(std::floor(px(lo_y, height))), y1 = static_cast<int>(std::ceil(px(hi_y, height)));
  out = {x0, y0, x1 - x0, y1 - y0};
  return out.w > 0 && out.h > 0;
}

}  // namespace monstres
