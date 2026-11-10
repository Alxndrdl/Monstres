#include "gles3_renderer.hpp"

#include <GLES3/gl3.h>

#include <cstddef>
#include <cstdio>
#include <vector>

namespace monstres {

namespace {

constexpr const char* kVertexShader = R"(#version 300 es
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;
layout(location = 3) in vec4 iPosYaw;        // per instance
layout(location = 4) in vec4 iScalePattern;
layout(location = 5) in vec4 iTint;
uniform mat4 uViewProjection;
out vec3 vNormal;
out vec3 vWorld;
out vec3 vColor;
out float vPattern;
void main() {
  float c = cos(iPosYaw.w), s = sin(iPosYaw.w);
  vec3 p = aPos * iScalePattern.xyz;
  p = vec3(c * p.x + s * p.z, p.y, -s * p.x + c * p.z);
  // Normals of a non-uniformly scaled mesh: divide by the scale.
  vec3 n = aNormal / iScalePattern.xyz;
  vNormal = vec3(c * n.x + s * n.z, n.y, -s * n.x + c * n.z);
  vWorld = p + iPosYaw.xyz;
  vColor = aColor * iTint.rgb;
  vPattern = iScalePattern.w;
  gl_Position = uViewProjection * vec4(vWorld, 1.0);
}
)";

constexpr const char* kFragmentShader = R"(#version 300 es
precision mediump float;
in vec3 vNormal;
in vec3 vWorld;
in vec3 vColor;
in float vPattern;
uniform vec3 uCamera;
uniform vec3 uLight;
uniform vec3 uFog;
uniform vec2 uFogRange;
out vec4 outColor;
void main() {
  vec3 color = vColor;
  if (vPattern > 0.5) {  // ground: 1 m checkerboard
    float checker = mod(floor(vWorld.x) + floor(vWorld.z), 2.0);
    color *= mix(0.94, 1.04, checker);
  }
  vec3 n = normalize(vNormal);
  float diffuse = max(dot(n, uLight), 0.0);
  // Ambient light: bluish from the sky, greenish bounced from the ground.
  vec3 ambient = mix(vec3(0.38, 0.40, 0.34), vec3(0.50, 0.56, 0.66), n.y * 0.5 + 0.5);
  color *= ambient + vec3(1.0, 0.96, 0.88) * 0.62 * diffuse;
  float fog = smoothstep(uFogRange.x, uFogRange.y, distance(vWorld, uCamera));
  outColor = vec4(mix(color, uFog, fog), 1.0);
}
)";

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

struct GpuMesh {
  GLuint vao = 0;
  GLuint vbo = 0;
  GLsizei vertex_count = 0;
};

class Gles3Renderer final : public Renderer {
public:
  bool init(std::span<const MeshData> meshes) {
    program_ = link(kVertexShader, kFragmentShader);
    if (!program_) return false;
    u_view_projection_ = glGetUniformLocation(program_, "uViewProjection");
    u_camera_ = glGetUniformLocation(program_, "uCamera");
    u_light_ = glGetUniformLocation(program_, "uLight");
    u_fog_ = glGetUniformLocation(program_, "uFog");
    u_fog_range_ = glGetUniformLocation(program_, "uFogRange");

    glGenBuffers(1, &instance_vbo_);
    for (const MeshData& data : meshes) meshes_.push_back(upload(data));

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    return true;
  }

  ~Gles3Renderer() override {
    for (GpuMesh& m : meshes_) {
      glDeleteBuffers(1, &m.vbo);
      glDeleteVertexArrays(1, &m.vao);
    }
    glDeleteBuffers(1, &instance_vbo_);
    glDeleteProgram(program_);
  }

  void begin_frame(const Frame& frame) override {
    glViewport(0, 0, frame.pixel_width, frame.pixel_height);
    glClearColor(frame.clear.r, frame.clear.g, frame.clear.b, frame.clear.a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  }

  void draw(const View& view, const DrawList& list) override {
    glUseProgram(program_);
    glUniformMatrix4fv(u_view_projection_, 1, GL_FALSE, view.view_projection.m);
    glUniform3f(u_camera_, view.camera_x, view.camera_y, view.camera_z);
    glUniform3f(u_light_, view.light_x, view.light_y, view.light_z);
    glUniform3f(u_fog_, view.fog.r, view.fog.g, view.fog.b);
    glUniform2f(u_fog_range_, view.fog_start, view.fog_end);

    glBindBuffer(GL_ARRAY_BUFFER, instance_vbo_);
    for (size_t i = 0; i < list.batches.size() && i < meshes_.size(); ++i) {
      const auto& instances = list.batches[i];
      if (instances.empty() || meshes_[i].vertex_count == 0) continue;
      glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(instances.size() * sizeof(Instance)),
                   instances.data(), GL_STREAM_DRAW);
      glBindVertexArray(meshes_[i].vao);
      glDrawArraysInstanced(GL_TRIANGLES, 0, meshes_[i].vertex_count, static_cast<GLsizei>(instances.size()));
    }
    glBindVertexArray(0);
  }

  void end_frame() override {}

private:
  GpuMesh upload(const MeshData& data) {
    GpuMesh m;
    m.vertex_count = static_cast<GLsizei>(data.vertex_count());
    glGenVertexArrays(1, &m.vao);
    glBindVertexArray(m.vao);

    glGenBuffers(1, &m.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(data.vertices.size() * sizeof(float)),
                 data.vertices.data(), GL_STATIC_DRAW);
    constexpr GLsizei vertex_stride = MeshData::kFloatsPerVertex * sizeof(float);
    for (GLuint a = 0; a < 3; ++a) {
      glEnableVertexAttribArray(a);
      glVertexAttribPointer(a, 3, GL_FLOAT, GL_FALSE, vertex_stride, offset(a * 3 * sizeof(float)));
    }

    // Per-instance attributes, read from the shared instance buffer.
    glBindBuffer(GL_ARRAY_BUFFER, instance_vbo_);
    constexpr GLsizei stride = sizeof(Instance);
    const size_t attribute_offsets[] = {offsetof(Instance, x), offsetof(Instance, sx), offsetof(Instance, tint)};
    for (GLuint i = 0; i < 3; ++i) {
      glEnableVertexAttribArray(3 + i);
      glVertexAttribPointer(3 + i, 4, GL_FLOAT, GL_FALSE, stride, offset(attribute_offsets[i]));
      glVertexAttribDivisor(3 + i, 1);
    }
    glBindVertexArray(0);
    return m;
  }

  GLuint program_ = 0;
  GLint u_view_projection_ = -1, u_camera_ = -1, u_light_ = -1, u_fog_ = -1, u_fog_range_ = -1;
  GLuint instance_vbo_ = 0;
  std::vector<GpuMesh> meshes_;
};

}  // namespace

std::unique_ptr<Renderer> make_gles3_renderer(std::span<const MeshData> meshes) {
  auto renderer = std::make_unique<Gles3Renderer>();
  if (!renderer->init(meshes)) return nullptr;
  return renderer;
}

}  // namespace monstres
