#include "gles3_renderer.hpp"

#include <GLES3/gl3.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <unordered_map>
#include <vector>

namespace monstres {

namespace {

// ---- Meshes -------------------------------------------------------------------

constexpr const char* kMeshVertexShader = R"(#version 300 es
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec4 iPosWind;    // per instance: position, wind
layout(location = 4) in vec4 iRotation;   // quaternion
layout(location = 5) in vec4 iScale;
layout(location = 6) in vec4 iTint;
uniform mat4 uViewProjection;
uniform float uTime;
out vec3 vNormal;
out vec3 vWorld;
out vec4 vColor;
out float vWater;
vec3 rotate(vec4 q, vec3 v) {
  return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}
void main() {
  vec3 local = aPos * iScale.xyz;
  // Normals of a non-uniformly scaled mesh: divide by the scale.
  vNormal = rotate(iRotation, aNormal / iScale.xyz);
  vec3 world = rotate(iRotation, local) + iPosWind.xyz;
  // Wind: the higher the vertex, the more it sways.
  if (iPosWind.w > 0.0) {
    float phase = uTime * 1.7 + iPosWind.x * 0.35 + iPosWind.z * 0.25;
    float sway = sin(phase) * 0.7 + sin(phase * 2.3 + 1.3) * 0.3;
    world.xz += vec2(1.0, 0.45) * sway * iPosWind.w * 0.12 * max(local.y, 0.0);
  }
  vWorld = world;
  vColor = aColor * iTint;
  vWater = iScale.w;
  gl_Position = uViewProjection * vec4(world, 1.0);
}
)";

constexpr const char* kMeshFragmentShader = R"(#version 300 es
precision mediump float;
in vec3 vNormal;
in vec3 vWorld;
in vec4 vColor;
in float vWater;
uniform vec3 uCamera;
uniform vec3 uLight;
uniform float uTimeFs;
uniform vec3 uFog;
uniform vec2 uFogRange;
out vec4 outColor;
void main() {
  vec3 n = normalize(vNormal);
  if (!gl_FrontFacing) n = -n;  // double-sided meshes
  float diffuse = max(dot(n, uLight), 0.0);
  // Ambient light: bluish from the sky, greenish bounced from the ground.
  vec3 ambient = mix(vec3(0.36, 0.40, 0.32), vec3(0.52, 0.58, 0.70), n.y * 0.5 + 0.5);
  vec3 color = vColor.rgb * (ambient + vec3(1.0, 0.95, 0.85) * 0.62 * diffuse);
  float alpha = vColor.a;
  if (vWater > 0.5) {
    // Small moving waves: sky reflection at grazing angles and sun glints.
    vec3 wave = normalize(vec3(sin(vWorld.x * 1.7 + uTimeFs * 1.3) * 0.06 + sin(vWorld.z * 2.3 - uTimeFs) * 0.04,
                               1.0,
                               cos(vWorld.z * 1.5 + uTimeFs * 1.1) * 0.06 + cos(vWorld.x * 2.1 + uTimeFs * 0.7) * 0.04));
    vec3 view = normalize(uCamera - vWorld);
    float fresnel = pow(1.0 - max(dot(view, wave), 0.0), 3.0);
    color = mix(color, uFog * 1.05, fresnel * 0.7);
    float glint = pow(max(dot(reflect(-uLight, wave), view), 0.0), 80.0);
    color += vec3(1.0, 0.95, 0.85) * glint * 0.8;
    alpha = max(alpha, fresnel * 0.9 * step(0.01, vColor.a));
  }
  float fog = smoothstep(uFogRange.x, uFogRange.y, distance(vWorld, uCamera));
  outColor = vec4(mix(color, uFog, fog), alpha);
}
)";

// ---- Sky: one full-screen triangle, gradient + sun -----------------------------

constexpr const char* kSkyVertexShader = R"(#version 300 es
out vec2 vNdc;
void main() {
  vec2 p = vec2(gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? 3.0 : -1.0);
  vNdc = p;
  gl_Position = vec4(p, 1.0, 1.0);
}
)";

constexpr const char* kSkyFragmentShader = R"(#version 300 es
precision highp float;
in vec2 vNdc;
uniform mat4 uInverseViewProjection;
uniform vec3 uCamera;
uniform vec3 uSun;
uniform vec3 uZenith;
uniform vec3 uHorizon;
out vec4 outColor;
void main() {
  vec4 far = uInverseViewProjection * vec4(vNdc, 1.0, 1.0);
  vec3 dir = normalize(far.xyz / far.w - uCamera);
  float up = max(dir.y, 0.0);
  vec3 sky = mix(uHorizon, uZenith, pow(up, 0.55));
  float sun = max(dot(dir, uSun), 0.0);
  sky += vec3(1.0, 0.85, 0.6) * (pow(sun, 8.0) * 0.25 + pow(sun, 64.0) * 0.35);  // glow
  sky = mix(sky, vec3(1.0, 0.98, 0.9), smoothstep(0.9993, 0.9996, sun));      // disc
  outColor = vec4(sky, 1.0);
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
  bool transparent = false;
  bool double_sided = false;
};

class Gles3Renderer final : public Renderer {
public:
  bool init(std::span<const MeshData> meshes) {
    mesh_program_ = link(kMeshVertexShader, kMeshFragmentShader);
    sky_program_ = link(kSkyVertexShader, kSkyFragmentShader);
    if (!mesh_program_ || !sky_program_) return false;
    u_view_projection_ = glGetUniformLocation(mesh_program_, "uViewProjection");
    u_time_ = glGetUniformLocation(mesh_program_, "uTime");
    u_time_fs_ = glGetUniformLocation(mesh_program_, "uTimeFs");
    u_camera_ = glGetUniformLocation(mesh_program_, "uCamera");
    u_light_ = glGetUniformLocation(mesh_program_, "uLight");
    u_fog_ = glGetUniformLocation(mesh_program_, "uFog");
    u_fog_range_ = glGetUniformLocation(mesh_program_, "uFogRange");
    u_sky_inverse_ = glGetUniformLocation(sky_program_, "uInverseViewProjection");
    u_sky_camera_ = glGetUniformLocation(sky_program_, "uCamera");
    u_sky_sun_ = glGetUniformLocation(sky_program_, "uSun");
    u_sky_zenith_ = glGetUniformLocation(sky_program_, "uZenith");
    u_sky_horizon_ = glGetUniformLocation(sky_program_, "uHorizon");
    glGenVertexArrays(1, &sky_vao_);

    glGenBuffers(1, &instance_vbo_);
    meshes_.resize(meshes.size());
    for (size_t i = 0; i < meshes.size(); ++i) set_mesh(i, meshes[i]);

    glFrontFace(GL_CCW);
    glCullFace(GL_BACK);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    return true;
  }

  ~Gles3Renderer() override {
    for (GpuMesh& m : meshes_) release(m);
    for (auto& [list, c] : cache_) glDeleteBuffers(static_cast<GLsizei>(c.buffers.size()), c.buffers.data());
    glDeleteBuffers(1, &instance_vbo_);
    glDeleteVertexArrays(1, &sky_vao_);
    glDeleteProgram(mesh_program_);
    glDeleteProgram(sky_program_);
  }

  void set_mesh(size_t index, const MeshData& data) override {
    if (index >= meshes_.size()) meshes_.resize(index + 1);
    GpuMesh& m = meshes_[index];
    release(m);
    m.vertex_count = static_cast<GLsizei>(data.vertex_count());
    m.transparent = data.transparent;
    m.double_sided = data.double_sided;
    if (m.vertex_count == 0) return;

    glGenVertexArrays(1, &m.vao);
    glBindVertexArray(m.vao);
    glGenBuffers(1, &m.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(data.vertices.size() * sizeof(float)),
                 data.vertices.data(), GL_STATIC_DRAW);
    constexpr GLsizei vertex_stride = MeshData::kFloatsPerVertex * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, vertex_stride, offset(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, vertex_stride, offset(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, vertex_stride, offset(6 * sizeof(float)));
    // Per-instance attributes: pointed at an instance buffer just before each draw.
    for (GLuint i = 0; i < 4; ++i) {
      glEnableVertexAttribArray(3 + i);
      glVertexAttribDivisor(3 + i, 1);
    }
    glBindVertexArray(0);
  }

  void begin_frame(const Frame& frame) override {
    glViewport(0, 0, frame.pixel_width, frame.pixel_height);
    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);
  }

  void draw(const View& view, std::span<const DrawList* const> lists) override {
    if (view.sky) draw_sky(view);

    glUseProgram(mesh_program_);
    glUniformMatrix4fv(u_view_projection_, 1, GL_FALSE, view.view_projection.m);
    glUniform1f(u_time_, view.time);
    glUniform1f(u_time_fs_, view.time);
    glUniform3f(u_camera_, view.camera_x, view.camera_y, view.camera_z);
    glUniform3f(u_light_, view.light_x, view.light_y, view.light_z);
    glUniform3f(u_fog_, view.fog.r, view.fog.g, view.fog.b);
    glUniform2f(u_fog_range_, view.fog_start, view.fog_end);
    glEnable(GL_DEPTH_TEST);
    for (const DrawList* list : lists) {
      if (list->cached) update_cache(*list);
    }

    // Opaque first (depth writes on), then transparent (blended, no depth writes).
    for (bool transparent : {false, true}) {
      glDepthMask(transparent ? GL_FALSE : GL_TRUE);
      if (transparent) glEnable(GL_BLEND); else glDisable(GL_BLEND);
      for (size_t i = 0; i < meshes_.size(); ++i) {
        const GpuMesh& m = meshes_[i];
        if (m.vertex_count == 0 || m.transparent != transparent) continue;
        if (m.double_sided) glDisable(GL_CULL_FACE); else glEnable(GL_CULL_FACE);
        glBindVertexArray(m.vao);
        for (const DrawList* list : lists) {
          if (i >= list->batches.size() || list->batches[i].empty()) continue;
          const auto& instances = list->batches[i];
          if (list->cached) {
            glBindBuffer(GL_ARRAY_BUFFER, cache_[list].buffers[i]);
          } else {
            glBindBuffer(GL_ARRAY_BUFFER, instance_vbo_);
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(instances.size() * sizeof(Instance)),
                         instances.data(), GL_STREAM_DRAW);
          }
          point_instance_attributes();
          glDrawArraysInstanced(GL_TRIANGLES, 0, m.vertex_count, static_cast<GLsizei>(instances.size()));
        }
      }
    }
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
  }

  void end_frame() override {}

  std::vector<uint8_t> draw_offscreen(const View& view, std::span<const DrawList* const> lists, int width,
                                      int height) override {
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4, 0);
    if (width <= 0 || height <= 0) return pixels;
    // Multisampled color + depth, resolved into a plain color buffer to read back.
    GLuint fbo[2], rb[3];
    glGenFramebuffers(2, fbo);
    glGenRenderbuffers(3, rb);
    GLint samples = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &samples);
    samples = std::min(samples, 4);
    glBindRenderbuffer(GL_RENDERBUFFER, rb[0]);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, width, height);
    glBindRenderbuffer(GL_RENDERBUFFER, rb[1]);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, width, height);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo[0]);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb[0]);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rb[1]);
    glBindRenderbuffer(GL_RENDERBUFFER, rb[2]);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo[1]);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb[2]);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
      glBindFramebuffer(GL_FRAMEBUFFER, fbo[0]);
      glViewport(0, 0, width, height);
      glClearColor(0, 0, 0, 0);
      glDepthMask(GL_TRUE);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      draw(view, lists);
      glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo[0]);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo[1]);
      glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
      glBindFramebuffer(GL_FRAMEBUFFER, fbo[1]);
      std::vector<uint8_t> bottom_up(pixels.size());
      glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, bottom_up.data());
      const size_t row = static_cast<size_t>(width) * 4;
      for (int y = 0; y < height; ++y) {
        std::copy_n(bottom_up.begin() + static_cast<std::ptrdiff_t>(static_cast<size_t>(height - 1 - y) * row), row,
                    pixels.begin() + static_cast<std::ptrdiff_t>(static_cast<size_t>(y) * row));
      }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(2, fbo);
    glDeleteRenderbuffers(3, rb);
    return pixels;  // the next frame sets the screen viewport again
  }

private:
  void draw_sky(const View& view) {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glUseProgram(sky_program_);
    glUniformMatrix4fv(u_sky_inverse_, 1, GL_FALSE, view.inverse_view_projection.m);
    glUniform3f(u_sky_camera_, view.camera_x, view.camera_y, view.camera_z);
    glUniform3f(u_sky_sun_, view.sun_x, view.sun_y, view.sun_z);
    glUniform3f(u_sky_zenith_, view.sky_zenith.r, view.sky_zenith.g, view.sky_zenith.b);
    glUniform3f(u_sky_horizon_, view.sky_horizon.r, view.sky_horizon.g, view.sky_horizon.b);
    glBindVertexArray(sky_vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
  }

  // Instance attributes read from the buffer bound to GL_ARRAY_BUFFER.
  static void point_instance_attributes() {
    constexpr GLsizei stride = sizeof(Instance);
    const size_t offsets[] = {offsetof(Instance, x), offsetof(Instance, rotation), offsetof(Instance, sx),
                              offsetof(Instance, tint)};
    for (GLuint i = 0; i < 4; ++i) {
      glVertexAttribPointer(3 + i, 4, GL_FLOAT, GL_FALSE, stride, offset(offsets[i]));
    }
  }

  // Uploads a cached list once per version, one buffer per batch.
  void update_cache(const DrawList& list) {
    Cache& c = cache_[&list];
    if (c.valid && c.version == list.version) return;
    if (c.buffers.size() < list.batches.size()) {
      const size_t old = c.buffers.size();
      c.buffers.resize(list.batches.size());
      glGenBuffers(static_cast<GLsizei>(c.buffers.size() - old), c.buffers.data() + old);
    }
    for (size_t i = 0; i < list.batches.size(); ++i) {
      glBindBuffer(GL_ARRAY_BUFFER, c.buffers[i]);
      glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(list.batches[i].size() * sizeof(Instance)),
                   list.batches[i].data(), GL_STATIC_DRAW);
    }
    c.version = list.version;
    c.valid = true;
  }

  static void release(GpuMesh& m) {
    if (m.vbo) glDeleteBuffers(1, &m.vbo);
    if (m.vao) glDeleteVertexArrays(1, &m.vao);
    m.vbo = m.vao = 0;
  }

  GLuint mesh_program_ = 0, sky_program_ = 0;
  GLint u_time_fs_ = -1;
  GLint u_view_projection_ = -1, u_time_ = -1, u_camera_ = -1, u_light_ = -1, u_fog_ = -1, u_fog_range_ = -1;
  GLint u_sky_inverse_ = -1, u_sky_camera_ = -1, u_sky_sun_ = -1, u_sky_zenith_ = -1, u_sky_horizon_ = -1;
  GLuint sky_vao_ = 0;
  GLuint instance_vbo_ = 0;
  std::vector<GpuMesh> meshes_;
  struct Cache {
    bool valid = false;
    uint32_t version = 0;
    std::vector<GLuint> buffers;
  };
  std::unordered_map<const DrawList*, Cache> cache_;
};

}  // namespace

std::unique_ptr<Renderer> make_gles3_renderer(std::span<const MeshData> meshes) {
  auto renderer = std::make_unique<Gles3Renderer>();
  if (!renderer->init(meshes)) return nullptr;
  return renderer;
}

}  // namespace monstres
