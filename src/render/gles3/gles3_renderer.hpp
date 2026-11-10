#pragma once

#include "monstres/render/renderer.hpp"

#include <memory>

namespace monstres {

// Needs a current OpenGL ES 3.0 / WebGL2 context. Returns nullptr on failure.
std::unique_ptr<Renderer> make_gles3_renderer();

}  // namespace monstres
