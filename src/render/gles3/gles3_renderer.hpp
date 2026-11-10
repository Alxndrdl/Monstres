#pragma once

#include "monstres/render/renderer.hpp"

#include <memory>
#include <span>

namespace monstres {

// Needs a current OpenGL ES 3.0 / WebGL2 context. Uploads `meshes` once;
// DrawList batch i is drawn with meshes[i]. Returns nullptr on failure.
std::unique_ptr<Renderer> make_gles3_renderer(std::span<const MeshData> meshes);

}  // namespace monstres
