#include "gles3_renderer.hpp"

#include <GLES3/gl3.h>

#include <cstddef>
#include <cstdio>

namespace monstres {

namespace {

constexpr const char* kVertexShader = R"(#version 300 es
layout(location = 0) in vec3 aPos;          // unit cube, -1..1
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 iCenterYaw;    // per instance
layout(location = 3) in vec4 iHalfPattern;
layout(location = 4) in vec4 iColor;
uniform mat4 uViewProjection;
out vec3 vNormal;
out vec3 vWorld;
out vec4 vColor;
out float vPattern;
void main() {
  float c = cos(iCenterYaw.w), s = sin(iCenterYaw.w);
  vec3 p = aPos * iHalfPattern.xyz;
  p = vec3(c * p.x + s * p.z, p.y, -s * p.x + c * p.z);
  vNormal = vec3(c * aNormal.x + s * aNormal.z, aNormal.y, -s * aNormal.x + c * aNormal.z);
  vWorld = p + iCenterYaw.xyz;
  vColor = iColor;
  vPattern = iHalfPattern.w;
  gl_Position = uViewProjection * vec4(vWorld, 1.0);
}
)";

constexpr const char* kFragmentShader = R"(#version 300 es
precision mediump float;
in vec3 vNormal;
in vec3 vWorld;
in vec4 vColor;
in float vPattern;
uniform vec3 uCamera;
uniform vec3 uLight;
uniform vec3 uFog;
uniform vec2 uFogRange;
out vec4 outColor;
void main() {
  vec3 color = vColor.rgb;
  if (vPattern > 0.5) {  // ground: 1 m checkerboard
    float checker = mod(floor(vWorld.x) + floor(vWorld.z), 2.0);
    color *= mix(0.92, 1.05, checker);
  }
  float diffuse = max(dot(normalize(vNormal), uLight), 0.0);
  color *= 0.5 + 0.6 * diffuse;
  float fog = smoothstep(uFogRange.x, uFogRange.y, distance(vWorld, uCamera));
  outColor = vec4(mix(color, uFog, fog), vColor.a);
}
)";

// Unit cube: 6 faces x 4 vertices (position + normal), 36 indices.
constexpr float kCubeVertices[] = {
    // +X
    1, -1, -1, 1, 0, 0,  1, 1, -1, 1, 0, 0,  1, 1, 1, 1, 0, 0,  1, -1, 1, 1, 0, 0,
    // -X
    -1, -1, 1, -1, 0, 0,  -1, 1, 1, -1, 0, 0,  -1, 1, -1, -1, 0, 0,  -1, -1, -1, -1, 0, 0,
    // +Y
    -1, 1, -1, 0, 1, 0,  -1, 1, 1, 0, 1, 0,  1, 1, 1, 0, 1, 0,  1, 1, -1, 0, 1, 0,
    // -Y
    -1, -1, 1, 0, -1, 0,  -1, -1, -1, 0, -1, 0,  1, -1, -1, 0, -1, 0,  1, -1, 1, 0, -1, 0,
    // +Z
    -1, -1, 1, 0, 0, 1,  1, -1, 1, 0, 0, 1,  1, 1, 1, 0, 0, 1,  -1, 1, 1, 0, 0, 1,
    // -Z
    1, -1, -1, 0, 0, -1,  -1, -1, -1, 0, 0, -1,  -1, 1, -1, 0, 0, -1,  1, 1, -1, 0, 0, -1,
};
constexpr unsigned short kCubeIndices[] = {
    0, 1, 2, 0, 2, 3,  4, 5, 6, 4, 6, 7,  8, 9, 10, 8, 10, 11,
    12, 13, 14, 12, 14, 15,  16, 17, 18, 16, 18, 19,  20, 21, 22, 20, 22, 23,
};

GLuint compile(GLenum type, const char* source) {
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  GLint ok = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[1024];
    glGetShaderInfoLog(shader, sizeof log, nullptr, log);
    std::fprintf(stderr, "shader compile error: %s\n", log);
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

GLuint link(const char* vs_source, const char* fs_source) {
  GLuint vs = compile(GL_VERTEX_SHADER, vs_source);
  GLuint fs = compile(GL_FRAGMENT_SHADER, fs_source);
  if (!vs || !fs) return 0;
  GLuint program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glLinkProgram(program);
  glDeleteShader(vs);
  glDeleteShader(fs);
  GLint ok = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[1024];
    glGetProgramInfoLog(program, sizeof log, nullptr, log);
    std::fprintf(stderr, "program link error: %s\n", log);
    glDeleteProgram(program);
    return 0;
  }
  return program;
}

void* offset(size_t bytes) { return reinterpret_cast<void*>(bytes); }

class Gles3Renderer final : public Renderer {
public:
  bool init() {
    program_ = link(kVertexShader, kFragmentShader);
    if (!program_) return false;
    u_view_projection_ = glGetUniformLocation(program_, "uViewProjection");
    u_camera_ = glGetUniformLocation(program_, "uCamera");
    u_light_ = glGetUniformLocation(program_, "uLight");
    u_fog_ = glGetUniformLocation(program_, "uFog");
    u_fog_range_ = glGetUniformLocation(program_, "uFogRange");

    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);

    glGenBuffers(1, &mesh_vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, mesh_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof kCubeVertices, kCubeVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), offset(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), offset(3 * sizeof(float)));

    glGenBuffers(1, &index_buffer_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof kCubeIndices, kCubeIndices, GL_STATIC_DRAW);

    glGenBuffers(1, &instance_vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, instance_vbo_);
    constexpr GLsizei stride = sizeof(CubeInstance);
    const size_t attribute_offsets[] = {offsetof(CubeInstance, x), offsetof(CubeInstance, hx),
                                        offsetof(CubeInstance, color)};
    for (GLuint i = 0; i < 3; ++i) {
      glEnableVertexAttribArray(2 + i);
      glVertexAttribPointer(2 + i, 4, GL_FLOAT, GL_FALSE, stride, offset(attribute_offsets[i]));
      glVertexAttribDivisor(2 + i, 1);
    }

    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    return true;
  }

  ~Gles3Renderer() override {
    glDeleteBuffers(1, &instance_vbo_);
    glDeleteBuffers(1, &index_buffer_);
    glDeleteBuffers(1, &mesh_vbo_);
    glDeleteVertexArrays(1, &vao_);
    glDeleteProgram(program_);
  }

  void begin_frame(const Frame& frame) override {
    glViewport(0, 0, frame.pixel_width, frame.pixel_height);
    glClearColor(frame.clear.r, frame.clear.g, frame.clear.b, frame.clear.a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  }

  void draw_cubes(const View& view, std::span<const CubeInstance> cubes) override {
    if (cubes.empty()) return;
    glUseProgram(program_);
    glUniformMatrix4fv(u_view_projection_, 1, GL_FALSE, view.view_projection.m);
    glUniform3f(u_camera_, view.camera_x, view.camera_y, view.camera_z);
    glUniform3f(u_light_, view.light_x, view.light_y, view.light_z);
    glUniform3f(u_fog_, view.fog.r, view.fog.g, view.fog.b);
    glUniform2f(u_fog_range_, view.fog_start, view.fog_end);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, instance_vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(cubes.size_bytes()), cubes.data(),
                 GL_STREAM_DRAW);
    glDrawElementsInstanced(GL_TRIANGLES, 36, GL_UNSIGNED_SHORT, nullptr,
                            static_cast<GLsizei>(cubes.size()));
    glBindVertexArray(0);
  }

  void end_frame() override {}

private:
  GLuint program_ = 0;
  GLint u_view_projection_ = -1, u_camera_ = -1, u_light_ = -1, u_fog_ = -1, u_fog_range_ = -1;
  GLuint vao_ = 0;
  GLuint mesh_vbo_ = 0;
  GLuint index_buffer_ = 0;
  GLuint instance_vbo_ = 0;
};

}  // namespace

std::unique_ptr<Renderer> make_gles3_renderer() {
  auto renderer = std::make_unique<Gles3Renderer>();
  if (!renderer->init()) return nullptr;
  return renderer;
}

}  // namespace monstres
