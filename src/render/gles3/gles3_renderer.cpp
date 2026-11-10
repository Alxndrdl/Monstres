#include "gles3_renderer.hpp"

#include <GLES3/gl3.h>

#include <cstddef>
#include <cstdio>

namespace monstres {

namespace {

constexpr const char* kVertexShader = R"(#version 300 es
layout(location = 0) in vec2 aCorner;   // unit quad corner, 0..1
layout(location = 1) in vec4 aRect;     // x, y, w, h in world units
layout(location = 2) in vec4 aColor;
uniform vec2 uWorldSize;
out vec4 vColor;
void main() {
  vec2 p = aRect.xy + aCorner * aRect.zw;
  vec2 ndc = p / uWorldSize * 2.0 - 1.0;
  gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
  vColor = aColor;
}
)";

constexpr const char* kFragmentShader = R"(#version 300 es
precision mediump float;
in vec4 vColor;
out vec4 outColor;
void main() { outColor = vColor; }
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

class Gles3Renderer final : public Renderer {
public:
  bool init() {
    program_ = link(kVertexShader, kFragmentShader);
    if (!program_) return false;
    u_world_size_ = glGetUniformLocation(program_, "uWorldSize");

    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);

    static constexpr float kCorners[] = {0, 0, 1, 0, 0, 1, 1, 1};
    glGenBuffers(1, &corner_vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, corner_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof kCorners, kCorners, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

    glGenBuffers(1, &instance_vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, instance_vbo_);
    constexpr GLsizei stride = sizeof(QuadInstance);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(offsetof(QuadInstance, x)));
    glVertexAttribDivisor(1, 1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(offsetof(QuadInstance, color)));
    glVertexAttribDivisor(2, 1);

    glBindVertexArray(0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    return true;
  }

  ~Gles3Renderer() override {
    glDeleteBuffers(1, &instance_vbo_);
    glDeleteBuffers(1, &corner_vbo_);
    glDeleteVertexArrays(1, &vao_);
    glDeleteProgram(program_);
  }

  void begin_frame(const Frame& frame) override {
    glViewport(0, 0, frame.pixel_width, frame.pixel_height);
    glClearColor(frame.clear.r, frame.clear.g, frame.clear.b, frame.clear.a);
    glClear(GL_COLOR_BUFFER_BIT);

    // Letterbox: largest viewport with the world's aspect ratio, centered.
    const float sx = static_cast<float>(frame.pixel_width) / frame.world_width;
    const float sy = static_cast<float>(frame.pixel_height) / frame.world_height;
    const float scale = sx < sy ? sx : sy;
    const auto w = static_cast<GLsizei>(frame.world_width * scale);
    const auto h = static_cast<GLsizei>(frame.world_height * scale);
    glViewport((frame.pixel_width - w) / 2, (frame.pixel_height - h) / 2, w, h);

    glUseProgram(program_);
    glUniform2f(u_world_size_, frame.world_width, frame.world_height);
  }

  void draw_quads(std::span<const QuadInstance> quads) override {
    if (quads.empty()) return;
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, instance_vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(quads.size_bytes()), quads.data(),
                 GL_STREAM_DRAW);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, static_cast<GLsizei>(quads.size()));
    glBindVertexArray(0);
  }

  void end_frame() override {}

private:
  GLuint program_ = 0;
  GLint u_world_size_ = -1;
  GLuint vao_ = 0;
  GLuint corner_vbo_ = 0;
  GLuint instance_vbo_ = 0;
};

}  // namespace

std::unique_ptr<Renderer> make_gles3_renderer() {
  auto renderer = std::make_unique<Gles3Renderer>();
  if (!renderer->init()) return nullptr;
  return renderer;
}

}  // namespace monstres
