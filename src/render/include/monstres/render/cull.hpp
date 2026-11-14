#pragma once

#include "monstres/render/renderer.hpp"

#include <span>

namespace monstres {

// Culling: drawing only what the camera can see. Lists and meshes are cut
// into parts by where they are (DrawList::parts, MeshData::parts); each
// frame, the parts whose box is out of the view's frustum are left out.
// Done here, on the CPU, before the renderer (no OpenGL: testable natively).

// The six planes of a view's frustum: a x + b y + c z + d >= 0 inside.
struct Frustum {
  float planes[6][4] = {};
};
Frustum frustum_of(const Mat4& view_projection);

// Whether some of the box may be in view (it may also be just outside, near
// a corner of the frustum: never the other way round).
bool in_view(const Frustum& frustum, const Aabb& box);

// Fills list.visible for a view: of each batch with parts, the parts in
// view. Neighbors are merged: a small part out of view between two in view
// is drawn as well (fewer draw calls), but never a hidden one. Batches
// without parts are drawn whole.
void cull_list(DrawList& list, const Frustum& frustum);

// The ranges of the parts in view (merged as above).
void visible_ranges(const std::vector<Part>& parts, const Frustum& frustum, uint32_t gap, std::vector<DrawRange>& out);

// Sorts the triangles of a mesh drawn once (the continent) by where they
// are around (cx, cz): by sectors, then by bands of distance (the bands'
// inner edges, in meters, increasing), each group a part (MeshData::parts,
// replaced). Triangles wider than `big` meters make one last part.
void split_into_parts(MeshData& mesh, float cx, float cz, int sectors, std::span<const float> bands, float big);

inline constexpr uint32_t kMergeInstances = 24;  // instances drawn for nothing rather than one more draw call
inline constexpr uint32_t kMergeVertices = 1600;  // (or indices) about 530 triangles

}  // namespace monstres
