#pragma once

#include <span>

namespace monstres {

struct Color {
  float r = 0, g = 0, b = 0, a = 1;
};

// One rectangle, in world units (1 unit = 1 grid cell). The backend draws
// all of them in a single instanced draw call.
struct QuadInstance {
  float x = 0, y = 0, w = 1, h = 1;
  Color color;
};
static_assert(sizeof(QuadInstance) == 8 * sizeof(float), "uploaded as-is to the GPU");

struct Frame {
  int pixel_width = 0;
  int pixel_height = 0;
  float world_width = 1;   // the world is letterboxed to keep its aspect ratio
  float world_height = 1;
  Color clear;
};

class Renderer {
public:
  virtual ~Renderer() = default;
  virtual void begin_frame(const Frame& frame) = 0;
  virtual void draw_quads(std::span<const QuadInstance> quads) = 0;
  virtual void end_frame() = 0;
};

}  // namespace monstres
