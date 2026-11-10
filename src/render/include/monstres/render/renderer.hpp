#pragma once

#include <span>

namespace monstres {

struct Color {
  float r = 0, g = 0, b = 0, a = 1;
};

// Column-major 4x4 matrix, as OpenGL expects it.
struct Mat4 {
  float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
};

// One box to draw. All boxes are drawn with a single instanced draw call.
struct CubeInstance {
  float x = 0, y = 0, z = 0;         // center
  float yaw = 0;                     // rotation around Y, radians
  float hx = 0.5f, hy = 0.5f, hz = 0.5f;  // half extents
  float pattern = 0;                 // 0 = plain, 1 = checkerboard (ground)
  Color color;
};
static_assert(sizeof(CubeInstance) == 12 * sizeof(float), "uploaded as-is to the GPU");

struct View {
  Mat4 view_projection;
  float camera_x = 0, camera_y = 0, camera_z = 0;
  float light_x = 0, light_y = 1, light_z = 0;  // direction towards the light, normalized
  Color fog;
  float fog_start = 30, fog_end = 70;
};

struct Frame {
  int pixel_width = 0;
  int pixel_height = 0;
  Color clear;
};

class Renderer {
public:
  virtual ~Renderer() = default;
  virtual void begin_frame(const Frame& frame) = 0;
  virtual void draw_cubes(const View& view, std::span<const CubeInstance> cubes) = 0;
  virtual void end_frame() = 0;
};

}  // namespace monstres
