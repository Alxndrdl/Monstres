#include "monstres/render/cull.hpp"

#include <algorithm>
#include <cmath>

namespace monstres {

Frustum frustum_of(const Mat4& vp) {
  // Rows of the (column-major) matrix: clip = row · (x, y, z, 1). Inside:
  // -w <= x, y, z <= w, so w ± x >= 0, and so on.
  const auto row = [&](int r, int k) { return vp.m[4 * k + r]; };
  Frustum f;
  for (int p = 0; p < 6; ++p) {
    const int axis = p / 2;
    const float sign = p % 2 == 0 ? 1.0f : -1.0f;
    float len = 0;
    for (int k = 0; k < 4; ++k) {
      f.planes[p][k] = row(3, k) + sign * row(axis, k);
      if (k < 3) len += f.planes[p][k] * f.planes[p][k];
    }
    len = std::sqrt(len);
    if (len > 0) {
      for (float& v : f.planes[p]) v /= len;
    }
  }
  return f;
}

bool in_view(const Frustum& f, const Aabb& b) {
  for (const auto& p : f.planes) {
    // The corner of the box farthest along the plane's normal: if even that
    // one is behind the plane, all of the box is.
    const float x = p[0] >= 0 ? b.max_x : b.min_x;
    const float y = p[1] >= 0 ? b.max_y : b.min_y;
    const float z = p[2] >= 0 ? b.max_z : b.min_z;
    if (p[0] * x + p[1] * y + p[2] * z + p[3] < 0) return false;
  }
  return true;
}

void visible_ranges(const std::vector<Part>& parts, const Frustum& f, uint32_t gap, std::vector<DrawRange>& out) {
  out.clear();
  bool hidden_since = false;  // a hidden part since the last range: no merging over it
  for (const Part& part : parts) {
    if (part.hidden) {
      hidden_since = true;
      continue;
    }
    if (part.range.count == 0 || !in_view(f, part.bounds)) continue;
    if (!out.empty()) {
      DrawRange& last = out.back();
      const uint32_t end = last.first + last.count;
      if (part.range.first >= end && part.range.first - end <= gap && !hidden_since) {
        last.count = part.range.first + part.range.count - last.first;
        continue;
      }
    }
    out.push_back(part.range);
    hidden_since = false;
  }
}

void cull_list(DrawList& list, const Frustum& f) {
  const size_t n = list.batches.size();
  bool any = false;
  for (size_t i = 0; i < n && !any; ++i) {
    any = (i < list.parts.size() && !list.parts[i].empty()) || (i < list.mesh_parts.size() && !list.mesh_parts[i].empty());
  }
  if (!any) {
    list.visible.clear();
    return;
  }
  list.visible.resize(n);
  for (size_t i = 0; i < n; ++i) {
    DrawList::Visible& v = list.visible[i];
    const bool parts = i < list.parts.size() && !list.parts[i].empty();
    const bool mesh_parts = i < list.mesh_parts.size() && !list.mesh_parts[i].empty();
    v.all = !parts && !mesh_parts;
    v.instances.clear();
    v.vertices.clear();
    if (v.all) continue;
    if (parts) {
      visible_ranges(list.parts[i], f, kMergeInstances, v.instances);
    } else if (!list.batches[i].empty()) {
      v.instances.push_back({0, static_cast<uint32_t>(list.batches[i].size())});
    }
    if (mesh_parts) {
      visible_ranges(list.mesh_parts[i], f, kMergeVertices, v.vertices);
      if (v.vertices.empty()) v.instances.clear();  // no part of the mesh in view: nothing
    }
  }
}

void split_into_parts(MeshData& m, float cx, float cz, int sectors, std::span<const float> bands, float big) {
  constexpr size_t kv = MeshData::kFloatsPerVertex;
  const bool indexed = !m.indices.empty();
  const size_t triangles = m.triangle_count();
  // Vertex k of triangle t.
  const auto vertex = [&](size_t t, size_t k) -> const float* {
    const size_t i = indexed ? m.indices[t * 3 + k] : t * 3 + k;
    return &m.vertices[i * kv];
  };
  const size_t groups = static_cast<size_t>(sectors) * (bands.size() + 1) + 1;  // the last one: the big triangles
  std::vector<uint32_t> group_of(triangles);
  std::vector<size_t> count(groups, 0);
  for (size_t t = 0; t < triangles; ++t) {
    float lo_x = 1e30f, hi_x = -1e30f, lo_z = 1e30f, hi_z = -1e30f;
    float mx = 0, mz = 0;
    for (size_t k = 0; k < 3; ++k) {
      const float* v = vertex(t, k);
      lo_x = std::min(lo_x, v[0]);
      hi_x = std::max(hi_x, v[0]);
      lo_z = std::min(lo_z, v[2]);
      hi_z = std::max(hi_z, v[2]);
      mx += v[0] / 3;
      mz += v[2] / 3;
    }
    size_t g = groups - 1;
    if (hi_x - lo_x <= big && hi_z - lo_z <= big) {
      const float dx = mx - cx, dz = mz - cz;
      int sector = static_cast<int>((std::atan2(dz, dx) + 3.14159265f) / 6.2831853f * static_cast<float>(sectors));
      sector = std::clamp(sector, 0, sectors - 1);
      const float d = std::sqrt(dx * dx + dz * dz);
      size_t band = 0;
      while (band < bands.size() && d >= bands[band]) ++band;
      g = static_cast<size_t>(sector) * (bands.size() + 1) + band;
    }
    group_of[t] = static_cast<uint32_t>(g);
    ++count[g];
  }
  // Counting sort of the triangles by group (their indices, or their vertices).
  std::vector<size_t> start(groups + 1, 0);
  for (size_t g = 0; g < groups; ++g) start[g + 1] = start[g] + count[g];
  std::vector<size_t> fill(start.begin(), start.end() - 1);
  if (indexed) {
    std::vector<uint32_t> sorted(m.indices.size());
    for (size_t t = 0; t < triangles; ++t) {
      const size_t to = fill[group_of[t]]++;
      std::copy_n(m.indices.begin() + static_cast<std::ptrdiff_t>(t * 3), 3, sorted.begin() + static_cast<std::ptrdiff_t>(to * 3));
    }
    m.indices = std::move(sorted);
  } else {
    std::vector<float> sorted(m.vertices.size());
    for (size_t t = 0; t < triangles; ++t) {
      const size_t to = fill[group_of[t]]++;
      std::copy_n(m.vertices.begin() + static_cast<std::ptrdiff_t>(t * 3 * kv), 3 * kv,
                  sorted.begin() + static_cast<std::ptrdiff_t>(to * 3 * kv));
    }
    m.vertices = std::move(sorted);
  }
  m.parts.clear();
  for (size_t g = 0; g < groups; ++g) {
    if (count[g] == 0) continue;
    Part part;
    part.range = {static_cast<uint32_t>(start[g] * 3), static_cast<uint32_t>(count[g] * 3)};
    part.bounds = empty_aabb();
    for (size_t t = start[g]; t < start[g] + count[g]; ++t) {
      for (size_t k = 0; k < 3; ++k) {
        const float* v = vertex(t, k);
        part.bounds.add(v[0], v[1], v[2]);
      }
    }
    m.parts.push_back(part);
  }
}

}  // namespace monstres
