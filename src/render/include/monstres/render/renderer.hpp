#pragma once

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

// A mesh: flat-shaded triangles, 9 floats per vertex
// (position xyz, normal xyz, color rgb). Built once at startup.
struct MeshData {
  std::vector<float> vertices;
  static constexpr int kFloatsPerVertex = 9;
  size_t vertex_count() const { return vertices.size() / kFloatsPerVertex; }
};

// One copy of a mesh in the world. All copies of a mesh are drawn with a
// single instanced draw call.
struct Instance {
  float x = 0, y = 0, z = 0;              // position of the mesh origin
  float yaw = 0;                          // rotation around Y, radians
  float sx = 1, sy = 1, sz = 1;           // scale
  float pattern = 0;                      // 0 = plain, 1 = checkerboard (ground)
  Color tint{1, 1, 1, 1};                 // multiplies the mesh colors
};
static_assert(sizeof(Instance) == 12 * sizeof(float), "uploaded as-is to the GPU");

// What to draw this frame: batches[i] holds the instances of mesh i.
struct DrawList {
  std::vector<std::vector<Instance>> batches;
};

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
  virtual void draw(const View& view, const DrawList& list) = 0;
  virtual void end_frame() = 0;
};

}  // namespace monstres
