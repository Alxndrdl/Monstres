#include "gles3_renderer.hpp"

#include "monstres/render/draw_plan.hpp"
#include "monstres/render/models.hpp"

#include <GLES3/gl3.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
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
uniform float uWind;     // 1: a breeze, more in a storm
uniform vec3 uEye;       // the camera
uniform vec2 uFadeCenter;
uniform vec2 uFadeNear;  // grass, flowers: shrink away between these distances
uniform vec2 uFadeFar;   // small things
uniform vec2 uFadeTree;  // trees and rocks far away
out vec3 vNormal;
out vec3 vWorld;
out vec3 vToCamera;  // computed here: in the fragment shader, mediump may be 16 bits (phones)
out vec4 vColor;
out float vMaterial;
// The same position in every program (the depth pre-pass, then the colors).
invariant gl_Position;
vec3 rotate(vec4 q, vec3 v) {
  return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}
void main() {
  float material = iScale.w;
  float fade = 1.0;
  if (material > 2.5) {
    vec2 range = material > 4.5 ? uFadeTree : material > 3.5 ? uFadeFar : uFadeNear;
    fade = 1.0 - smoothstep(range.x, range.y, distance(iPosWind.xz, uFadeCenter));
    // Then lit as usual; grass and flowers never show through (kSolid).
    material = material < 3.5 ? -1.0 : 0.0;
  }
  vec3 local = aPos * iScale.xyz * fade;
  // Normals of a non-uniformly scaled mesh: divide by the scale.
  vNormal = rotate(iRotation, aNormal / iScale.xyz);
  vec3 world = rotate(iRotation, local) + iPosWind.xyz;
  // Wind: the higher the vertex, the more it sways.
  if (iPosWind.w > 0.0) {
    float phase = uTime * 1.7 + iPosWind.x * 0.35 + iPosWind.z * 0.25;
    float sway = sin(phase) * 0.7 + sin(phase * 2.3 + 1.3) * 0.3;
    world.xz += vec2(1.0, 0.45) * sway * iPosWind.w * 0.12 * uWind * max(local.y, 0.0);
  }
  vWorld = world;
  vToCamera = uEye - world;
  vColor = aColor * iTint;
  vMaterial = material;
  gl_Position = uViewProjection * vec4(world, 1.0);
}
)";

// Built with FADE (1: what stands in the camera's way may be discarded) and
// MODE defined (mesh_fragment_shader): 0 the colors, the point lights in the
// shader; 1 the depth pre-pass, no color; 2 the normals (light pre-pass);
// 3 the colors, the point lights read from the light pre-pass's buffer;
// 4 the G-buffer of deferred shading (color and kind, normal).
constexpr const char* kMeshFragmentShader = R"(
precision mediump float;
#define MAX_LIGHTS 12
in vec3 vNormal;
in vec3 vWorld;
in vec3 vToCamera;
in vec4 vColor;
in float vMaterial;
uniform vec3 uLight;
uniform vec3 uLightColor;
uniform vec3 uAmbientSky;
uniform vec3 uAmbientGround;
uniform float uNight;
uniform bool uOpaquePass;
uniform float uTimeFs;
uniform vec4 uFadeFrom;  // xyz the camera, w the radius (0: nothing fades)
uniform vec3 uFadeTo;    // what it looks at
uniform vec3 uFog;
uniform vec3 uFogRange;  // start, half (distance for half of it), end
uniform int uLightCount;
uniform vec4 uLightPos[MAX_LIGHTS];    // xyz, radius
uniform vec3 uLightColors[MAX_LIGHTS];
uniform sampler2D uLightBuffer;  // MODE 3: the point lights' sum, times uLightBufferScale
uniform float uLightBufferScale;
layout(location = 0) out vec4 outColor;
#if MODE == 4
layout(location = 1) out vec4 outNormal;
#endif
void main() {
  // The distance to the camera and the way to it, without squaring big
  // numbers (in 16 bits, anything beyond 256 m would overflow).
  vec3 a = abs(vToCamera);
  float big = max(max(a.x, a.y), max(a.z, 1e-3));
  vec3 toCamera = vToCamera / big;
  float d = big * length(toCamera);
  vec3 view = toCamera / length(toCamera);
  // What stands between the camera and what it looks at shows through: a
  // fine dither leaves most of its pixels out (not water, not glowing things).
#if FADE
  if (uFadeFrom.w > 0.0 && uOpaquePass && vMaterial > -0.5 && vMaterial < 0.5) {
    vec3 ab = uFadeTo - uFadeFrom.xyz;
    float t = clamp(dot(vWorld - uFadeFrom.xyz, ab) / max(dot(ab, ab), 1e-4), 0.0, 1.0);
    float off = length(vWorld - (uFadeFrom.xyz + ab * t));
    float radius = uFadeFrom.w * (0.55 + 0.45 * t);
    if (t < 0.9 && off < radius) {
      ivec2 c = ivec2(gl_FragCoord.xy) & 3;
      int k = (c.x ^ c.y) | ((c.y & 1) << 1);  // a 4x4 pattern: 0..3 twice over
      // Right in front of the camera (big on screen): gone; farther: a third left; fading in at the edges.
      float keep = 0.3 * smoothstep(0.08, 0.3, t) + 0.7 * smoothstep(0.7, 0.9, t) + 0.5 * smoothstep(radius * 0.7, radius, off);
      if (float(((c.x * 3 + c.y * 2) & 3) * 4 + k) / 16.0 >= keep) discard;
    }
  }
#endif
#if MODE == 1
  outColor = vec4(0.0);
#else
  vec3 n = normalize(vNormal);
  if (!gl_FrontFacing) n = -n;  // double-sided meshes
#if MODE == 2
  outColor = vec4(n * 0.5 + 0.5, 1.0);
#elif MODE == 4
  // The kind (alpha): 0.25 lit, 0.5 glowing, 0.75 a window lit at night (0: nothing there).
  float kind = vMaterial > 1.5 ? 0.5 : uOpaquePass && abs(vColor.a - 0.985) < 0.002 ? 0.75 : 0.25;
  outColor = vec4(vColor.rgb, kind);
  outNormal = vec4(n * 0.5 + 0.5, 1.0);
#else
  float diffuse = max(dot(n, uLight), 0.0);
  // Ambient light: from the sky above, bounced from the ground below.
  vec3 light = mix(uAmbientGround, uAmbientSky, n.y * 0.5 + 0.5) + uLightColor * diffuse;
  // Lanterns, the staff, glowing monsters: soft lights fading with distance.
#if MODE == 3
  light += texelFetch(uLightBuffer, ivec2(gl_FragCoord.xy), 0).rgb * uLightBufferScale;
#else
  for (int i = 0; i < MAX_LIGHTS; ++i) {
    if (i >= uLightCount) break;
    vec3 d = uLightPos[i].xyz - vWorld;
    float dist = length(d);
    float fall = clamp(1.0 - dist / uLightPos[i].w, 0.0, 1.0);
    light += uLightColors[i] * fall * fall * (0.35 + 0.65 * max(dot(n, d / max(dist, 1e-3)), 0.0));
  }
#endif
  vec3 color = vColor.rgb * light;
  float alpha = vColor.a;
  if (vMaterial > 1.5) {
    color = vColor.rgb;  // glowing: its own light
  } else if (uOpaquePass && abs(vColor.a - 0.985) < 0.002) {
    // Windows and lanterns: lit from inside at night.
    color = mix(color, vec3(1.0, 0.78, 0.42) * 1.15, uNight);
    alpha = 1.0;
  } else if (vMaterial > 0.5) {  // water
    // Small moving waves: sky reflection at grazing angles and sun glints.
    vec3 wave = normalize(vec3(sin(vWorld.x * 1.7 + uTimeFs * 1.3) * 0.06 + sin(vWorld.z * 2.3 - uTimeFs) * 0.04,
                               1.0,
                               cos(vWorld.z * 1.5 + uTimeFs * 1.1) * 0.06 + cos(vWorld.x * 2.1 + uTimeFs * 0.7) * 0.04));
    float fresnel = pow(1.0 - max(dot(view, wave), 0.0), 3.0);
    color = mix(color, uFog * 1.05, fresnel * 0.7);
    float glint = pow(max(dot(reflect(-uLight, wave), view), 0.0), 80.0);
    color += uLightColor * (1.0 / 0.62) * glint * 0.8;  // the sun at noon: (1, 0.95, 0.85)
    alpha = max(alpha, fresnel * 0.9 * step(0.01, vColor.a));
  }
  float fog = max(1.0 - exp2(-max(d - uFogRange.x, 0.0) / uFogRange.y), smoothstep(uFogRange.z * 0.75, uFogRange.z, d));
  outColor = vec4(mix(color, uFog, fog), alpha);
#endif
#endif
}
)";

// What a mesh program draws (MODE in the shader).
enum Mode { kColor = 0, kDepth = 1, kNormals = 2, kColorLightBuffer = 3, kGBuffer = 4, kModes = 5 };

std::string mesh_fragment_shader(bool fade, int mode) {
  return std::string("#version 300 es\n#define FADE ") + (fade ? "1" : "0") + "\n#define MODE " +
         std::to_string(mode) + "\n" + kMeshFragmentShader;
}

// ---- Deferred shading: the lighting of the G-buffer, over the whole screen -------
// The same light as the forward shader (ambient, sun or moon, point lights,
// windows at night, glowing things, fog), once per pixel. Where nothing near
// and opaque was drawn (kind 0): left as it is (the sky, the far landscape).

constexpr const char* kComposeFragmentShader = R"(#version 300 es
precision highp float;
#define MAX_LIGHTS 12
uniform highp sampler2D uDepth;
uniform sampler2D uAlbedo;   // color, kind
uniform sampler2D uNormals;
uniform mat4 uInverseViewProjection;
uniform vec2 uScreen;
uniform vec3 uCamera;
uniform vec3 uLight;
uniform vec3 uLightColor;
uniform vec3 uAmbientSky;
uniform vec3 uAmbientGround;
uniform float uNight;
uniform vec3 uFog;
uniform vec3 uFogRange;
uniform int uLightCount;
uniform vec4 uLightPos[MAX_LIGHTS];
uniform vec3 uLightColors[MAX_LIGHTS];
out vec4 outColor;
void main() {
  ivec2 p = ivec2(gl_FragCoord.xy);
  vec4 albedo = texelFetch(uAlbedo, p, 0);
  if (albedo.a < 0.1) discard;
  float depth = texelFetch(uDepth, p, 0).r;
  vec4 world = uInverseViewProjection * vec4(gl_FragCoord.xy / uScreen * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
  vec3 pos = world.xyz / world.w;
  vec3 n = normalize(texelFetch(uNormals, p, 0).xyz * 2.0 - 1.0);
  vec3 color;
  if (albedo.a > 0.4 && albedo.a < 0.6) {
    color = albedo.rgb;  // glowing: its own light
  } else {
    vec3 light = mix(uAmbientGround, uAmbientSky, n.y * 0.5 + 0.5) + uLightColor * max(dot(n, uLight), 0.0);
    for (int i = 0; i < MAX_LIGHTS; ++i) {
      if (i >= uLightCount) break;
      vec3 d = uLightPos[i].xyz - pos;
      float dist = length(d);
      float fall = clamp(1.0 - dist / uLightPos[i].w, 0.0, 1.0);
      light += uLightColors[i] * fall * fall * (0.35 + 0.65 * max(dot(n, d / max(dist, 1e-3)), 0.0));
    }
    color = albedo.rgb * light;
    if (albedo.a > 0.6) color = mix(color, vec3(1.0, 0.78, 0.42) * 1.15, uNight);  // a window lit from inside
  }
  float d = length(pos - uCamera);
  float fog = max(1.0 - exp2(-max(d - uFogRange.x, 0.0) / uFogRange.y), smoothstep(uFogRange.z * 0.75, uFogRange.z, d));
  outColor = vec4(mix(color, uFog, fog), 1.0);
}
)";

// The finished image, copied to the screen.
constexpr const char* kCopyFragmentShader = R"(#version 300 es
precision mediump float;
uniform sampler2D uImage;
out vec4 outColor;
void main() { outColor = texelFetch(uImage, ivec2(gl_FragCoord.xy), 0); }
)";

// ---- Light pre-pass: each point light over the pixels it may reach --------------
// One full-screen triangle (scissored to the light's rectangle); the
// position of each pixel from the depth, its normal from the normals pass.
// Added up (blending) into the light buffer, as the forward shader would.

constexpr const char* kLightFragmentShader = R"(#version 300 es
precision highp float;
uniform highp sampler2D uDepth;
uniform sampler2D uNormals;
uniform mat4 uInverseViewProjection;
uniform vec2 uScreen;       // pixels
uniform vec4 uLightPos;     // xyz, radius
uniform vec3 uLightColor;   // times the buffer's scale
out vec4 outColor;
void main() {
  ivec2 p = ivec2(gl_FragCoord.xy);
  float depth = texelFetch(uDepth, p, 0).r;
  if (depth >= 1.0) discard;  // the sky
  vec4 world = uInverseViewProjection * vec4(gl_FragCoord.xy / uScreen * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
  vec3 pos = world.xyz / world.w;
  vec3 d = uLightPos.xyz - pos;
  float dist = length(d);
  float fall = clamp(1.0 - dist / uLightPos.w, 0.0, 1.0);
  if (fall <= 0.0) discard;
  vec3 n = normalize(texelFetch(uNormals, p, 0).xyz * 2.0 - 1.0);
  outColor = vec4(uLightColor * fall * fall * (0.35 + 0.65 * max(dot(n, d / max(dist, 1e-3)), 0.0)), 1.0);
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
uniform vec3 uMoon;
uniform vec3 uZenith;
uniform vec3 uHorizon;
uniform float uNight;
uniform float uCover;       // clouds overhead: 0..1 of the sky
uniform vec3 uCloudLight;   // their lit color...
uniform vec3 uCloudShade;   // ...and where they are thick
uniform vec2 uCloudShift;   // meters drifted with the wind
out vec4 outColor;
float hash(vec3 p) { return fract(sin(dot(p, vec3(12.9898, 78.233, 37.719))) * 43758.5453); }
float hash2(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p) {
  vec2 i = floor(p), f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(hash2(i), hash2(i + vec2(1.0, 0.0)), f.x), mix(hash2(i + vec2(0.0, 1.0)), hash2(i + vec2(1.0)), f.x), f.y);
}
float clouds(vec2 p) {
  float s = 0.0, a = 0.5;
  for (int k = 0; k < 4; ++k) {
    s += a * noise(p);
    p = p * 2.03 + vec2(17.1, 9.3);
    a *= 0.5;
  }
  return s;
}
void main() {
  vec4 far = uInverseViewProjection * vec4(vNdc, 1.0, 1.0);
  vec3 dir = normalize(far.xyz / far.w - uCamera);
  float up = max(dir.y, 0.0);
  vec3 sky = mix(uHorizon, uZenith, pow(up, 0.55));
  // Clouds overhead: a layer high above (1.5 km), cells of 400 m, as much
  // of it as the weather says; towards the horizon they give way to the
  // clouds drawn as models (and the sky's own grey when it is overcast).
  float veil = 0.0, thick = 0.0;
  if (uCover > 0.01 && dir.y > 0.0) {
    vec2 p = dir.xz / (dir.y + 0.06) * 3.75 + uCloudShift / 400.0;
    float n = clouds(p);
    float edge = mix(0.78, 0.12, uCover);
    veil = smoothstep(edge - 0.04, edge + 0.12, n) * smoothstep(0.0, 0.14, dir.y);
    thick = smoothstep(edge, edge + 0.35, n);
  }
  float clear = (1.0 - veil) * (1.0 - smoothstep(0.55, 1.0, uCover));  // how much of the sky is seen
  // Stars, at night, fading towards the horizon.
  vec3 cell = floor(dir * 180.0);
  float star = step(0.9965, hash(cell)) * hash(cell + 7.0);
  sky += vec3(0.9, 0.92, 1.0) * star * uNight * smoothstep(0.02, 0.25, dir.y) * clear;
  float sun = max(dot(dir, uSun), 0.0);
  float day = 1.0 - uNight;
  float glow = 1.0 - 0.8 * smoothstep(0.3, 1.0, uCover);
  sky += vec3(1.0, 0.85, 0.6) * (pow(sun, 8.0) * 0.25 + pow(sun, 64.0) * 0.35) * day * glow;  // glow
  sky = mix(sky, vec3(1.0, 0.98, 0.9), smoothstep(0.9993, 0.9996, sun) * step(-0.02, uSun.y) * clear);  // disc
  // The moon: a pale disc with a soft halo.
  float moon = max(dot(dir, uMoon), 0.0);
  sky += vec3(0.55, 0.62, 0.8) * pow(moon, 200.0) * 0.5 * uNight * clear;
  vec3 moonColor = mix(vec3(0.85, 0.87, 0.95), vec3(0.62, 0.64, 0.72), hash(floor(dir * 900.0)) * 0.6);
  sky = mix(sky, moonColor, smoothstep(0.99955, 0.99975, moon) * step(-0.02, uMoon.y) * max(uNight, 0.25) * clear);
  // The clouds over all that, lit by the sun where it shines through.
  vec3 cloud = mix(uCloudLight, uCloudShade, thick * 0.75) + vec3(1.0, 0.9, 0.75) * pow(sun, 12.0) * 0.25 * day * (1.0 - thick);
  sky = mix(sky, cloud, veil);
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
  GLuint ebo = 0;            // indices, if any
  GLsizei vertex_count = 0;  // what is drawn: indices, or vertices when there are none
  bool transparent = false;
  bool double_sided = false;
};

// A variant of the mesh program (mesh_fragment_shader) and where its uniforms are.
struct MeshProgram {
  GLuint id = 0;
  GLint view_projection = -1, time = -1, wind = -1, eye = -1, time_fs = -1, light = -1, light_color = -1;
  GLint ambient_sky = -1, ambient_ground = -1, night = -1, fade_from = -1, fade_to = -1, opaque_pass = -1;
  GLint light_count = -1, light_pos = -1, light_colors = -1, fade_center = -1, fade_near = -1, fade_far = -1;
  GLint fade_tree = -1, fog = -1, fog_range = -1, light_buffer = -1, light_buffer_scale = -1;
  bool lights_known = false;  // which of the frame's lights it has (upload_lights)
  uint16_t lights = 0;

  bool link(const std::string& fragment) {
    id = monstres::link(kMeshVertexShader, fragment.c_str());
    if (!id) return false;
    const auto at = [&](const char* name) { return glGetUniformLocation(id, name); };
    view_projection = at("uViewProjection");
    time = at("uTime");
    wind = at("uWind");
    eye = at("uEye");
    time_fs = at("uTimeFs");
    light = at("uLight");
    light_color = at("uLightColor");
    ambient_sky = at("uAmbientSky");
    ambient_ground = at("uAmbientGround");
    night = at("uNight");
    fade_from = at("uFadeFrom");
    fade_to = at("uFadeTo");
    opaque_pass = at("uOpaquePass");
    light_count = at("uLightCount");
    light_pos = at("uLightPos");
    light_colors = at("uLightColors");
    fade_center = at("uFadeCenter");
    fade_near = at("uFadeNear");
    fade_far = at("uFadeFar");
    fade_tree = at("uFadeTree");
    fog = at("uFog");
    fog_range = at("uFogRange");
    light_buffer = at("uLightBuffer");
    light_buffer_scale = at("uLightBufferScale");
    glUseProgram(id);
    glUniform1i(light_buffer, 2);  // texture unit 2 (light_pass)
    return true;
  }

  // The uniforms of the frame (the near view's matrix).
  void set_view(const View& v) {
    glUseProgram(id);
    glUniformMatrix4fv(view_projection, 1, GL_FALSE, v.view_projection.m);
    glUniform1f(time, v.time);
    glUniform1f(wind, 1.0f + 2.2f * v.wind);
    glUniform3f(eye, v.camera_x, v.camera_y, v.camera_z);
    glUniform1f(time_fs, v.time);
    glUniform3f(light, v.light_x, v.light_y, v.light_z);
    glUniform3f(light_color, v.light_color.r, v.light_color.g, v.light_color.b);
    glUniform3f(ambient_sky, v.ambient_sky.r, v.ambient_sky.g, v.ambient_sky.b);
    glUniform3f(ambient_ground, v.ambient_ground.r, v.ambient_ground.g, v.ambient_ground.b);
    glUniform1f(night, v.night);
    glUniform4f(fade_from, v.fade_from_x, v.fade_from_y, v.fade_from_z, v.fade_radius);
    glUniform3f(fade_to, v.fade_to_x, v.fade_to_y, v.fade_to_z);
    glUniform3f(fog, v.fog.r, v.fog.g, v.fog.b);
    glUniform3f(fog_range, v.fog_start, v.fog_half, v.fog_end);
    glUniform2f(fade_center, v.fade_x, v.fade_z);
    glUniform2f(fade_near, v.fade_near_start, v.fade_near_end);
    glUniform2f(fade_far, v.fade_far_start, v.fade_far_end);
    glUniform2f(fade_tree, v.fade_tree_start, v.fade_tree_end);
    lights_known = false;
  }
  void set_view_projection(const Mat4& m) {
    glUseProgram(id);
    glUniformMatrix4fv(view_projection, 1, GL_FALSE, m.m);
  }
  void set_opaque_pass(bool opaque) {
    glUseProgram(id);
    glUniform1i(opaque_pass, opaque ? 1 : 0);
  }
};

class Gles3Renderer final : public Renderer {
public:
  bool init(std::span<const MeshData> meshes) {
    for (int fade = 0; fade < 2; ++fade) {
      for (int mode = 0; mode < kModes; ++mode) {
        if (!programs_[fade][mode].link(mesh_fragment_shader(fade != 0, mode))) return false;
      }
    }
    sky_program_ = link(kSkyVertexShader, kSkyFragmentShader);
    light_program_ = link(kSkyVertexShader, kLightFragmentShader);
    compose_program_ = link(kSkyVertexShader, kComposeFragmentShader);
    copy_program_ = link(kSkyVertexShader, kCopyFragmentShader);
    if (!sky_program_ || !light_program_ || !compose_program_ || !copy_program_) return false;
    // Deferred shading: its textures on units 3, 4, 5 (unit 2: the light buffer).
    glUseProgram(compose_program_);
    glUniform1i(glGetUniformLocation(compose_program_, "uDepth"), 3);
    glUniform1i(glGetUniformLocation(compose_program_, "uAlbedo"), 4);
    glUniform1i(glGetUniformLocation(compose_program_, "uNormals"), 5);
    const auto cu = [&](const char* name) { return glGetUniformLocation(compose_program_, name); };
    u_cp_inverse_ = cu("uInverseViewProjection");
    u_cp_screen_ = cu("uScreen");
    u_cp_camera_ = cu("uCamera");
    u_cp_light_ = cu("uLight");
    u_cp_light_color_ = cu("uLightColor");
    u_cp_ambient_sky_ = cu("uAmbientSky");
    u_cp_ambient_ground_ = cu("uAmbientGround");
    u_cp_night_ = cu("uNight");
    u_cp_fog_ = cu("uFog");
    u_cp_fog_range_ = cu("uFogRange");
    u_cp_light_count_ = cu("uLightCount");
    u_cp_light_pos_ = cu("uLightPos");
    u_cp_light_colors_ = cu("uLightColors");
    glUseProgram(copy_program_);
    glUniform1i(glGetUniformLocation(copy_program_, "uImage"), 3);
    glUseProgram(light_program_);
    glUniform1i(glGetUniformLocation(light_program_, "uDepth"), 0);
    glUniform1i(glGetUniformLocation(light_program_, "uNormals"), 1);
    u_lp_inverse_ = glGetUniformLocation(light_program_, "uInverseViewProjection");
    u_lp_screen_ = glGetUniformLocation(light_program_, "uScreen");
    u_lp_pos_ = glGetUniformLocation(light_program_, "uLightPos");
    u_lp_color_ = glGetUniformLocation(light_program_, "uLightColor");
    // Light sums above 1 need a float buffer; else 8 bits, a quarter of the light.
    const char* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    float_lights_ = extensions && std::strstr(extensions, "color_buffer_float") != nullptr;
    light_buffer_scale_ = float_lights_ ? 1.0f : 4.0f;
    u_sky_moon_ = glGetUniformLocation(sky_program_, "uMoon");
    u_sky_night_ = glGetUniformLocation(sky_program_, "uNight");
    u_sky_inverse_ = glGetUniformLocation(sky_program_, "uInverseViewProjection");
    u_sky_camera_ = glGetUniformLocation(sky_program_, "uCamera");
    u_sky_sun_ = glGetUniformLocation(sky_program_, "uSun");
    u_sky_zenith_ = glGetUniformLocation(sky_program_, "uZenith");
    u_sky_horizon_ = glGetUniformLocation(sky_program_, "uHorizon");
    u_sky_cover_ = glGetUniformLocation(sky_program_, "uCover");
    u_sky_cloud_light_ = glGetUniformLocation(sky_program_, "uCloudLight");
    u_sky_cloud_shade_ = glGetUniformLocation(sky_program_, "uCloudShade");
    u_sky_cloud_shift_ = glGetUniformLocation(sky_program_, "uCloudShift");
    glGenVertexArrays(1, &sky_vao_);

    glGenBuffers(1, &instance_vbo_);
    glGenBuffers(1, &frame_vbo_);
    meshes_.resize(meshes.size());
    plan_meshes_.resize(meshes.size());
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
    glDeleteBuffers(1, &frame_vbo_);
    glDeleteVertexArrays(1, &sky_vao_);
    for (auto& row : programs_) {
      for (MeshProgram& program : row) glDeleteProgram(program.id);
    }
    glDeleteProgram(light_program_);
    glDeleteProgram(compose_program_);
    glDeleteProgram(copy_program_);
    release_light_targets();
    release_shading_targets();
    glDeleteProgram(sky_program_);
  }

  void set_mesh(size_t index, const MeshData& data) override {
    if (index >= meshes_.size()) {
      meshes_.resize(index + 1);
      plan_meshes_.resize(index + 1);
    }
    GpuMesh& m = meshes_[index];
    release(m);
    m.vertex_count = data.vertices.empty() ? 0 : static_cast<GLsizei>(data.draw_count());
    m.transparent = data.transparent;
    m.double_sided = data.double_sided;
    PlanMesh& plan = plan_meshes_[index];
    plan.vertex_count = m.vertex_count;
    plan.transparent = m.transparent;
    plan.bounds = empty_aabb();
    for (size_t v = 0; v + 2 < data.vertices.size(); v += MeshData::kFloatsPerVertex) {
      plan.bounds.add(data.vertices[v], data.vertices[v + 1], data.vertices[v + 2]);
    }
    if (m.vertex_count == 0) return;
    stats_.uploaded_bytes += data.vertices.size() * sizeof(float) + data.indices.size() * sizeof(uint32_t);

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
    if (!data.indices.empty()) {  // kept in the vertex array
      glGenBuffers(1, &m.ebo);
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ebo);
      glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(data.indices.size() * sizeof(uint32_t)),
                   data.indices.data(), GL_STATIC_DRAW);
    }
    // Per-instance attributes: pointed at an instance buffer just before each draw.
    for (GLuint i = 0; i < 4; ++i) {
      glEnableVertexAttribArray(3 + i);
      glVertexAttribDivisor(3 + i, 1);
    }
    glBindVertexArray(0);
  }

  void set_mesh_indices(size_t index, const std::vector<uint32_t>& indices) override {
    if (index >= meshes_.size()) return;
    GpuMesh& m = meshes_[index];
    if (!m.vao || !m.ebo) return;
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)), indices.data(),
                 GL_STATIC_DRAW);
    glBindVertexArray(0);
    m.vertex_count = static_cast<GLsizei>(indices.size());
    plan_meshes_[index].vertex_count = m.vertex_count;
    stats_.uploaded_bytes += indices.size() * sizeof(uint32_t);
  }

  void begin_frame(const Frame& frame) override {
    glViewport(0, 0, frame.pixel_width, frame.pixel_height);
    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);
    if (profiling_) {
      const double gpu = stats_.gpu_ms;
      stats_ = {};
      stats_.gpu_ms = gpu;
      begin_gpu_timer();
    }
  }

  void set_options(const RenderOptions& options) override { options_ = options; }

  void draw(const View& view, std::span<const DrawList* const> lists) override {
    // Deferred shading: the whole image drawn off screen (into scene_texture_),
    // then copied to the screen. Not for portraits (a transparent background).
    GLint previous = 0, viewport[4] = {};
    bool shading = false;
    if (options_.deferred_shading && (view.sky || view.fill)) {
      glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previous);
      glGetIntegerv(GL_VIEWPORT, viewport);
      shading = make_shading_targets(viewport[2], viewport[3]);
      if (shading) {
        glBindFramebuffer(GL_FRAMEBUFFER, scene_fbo_);
        glViewport(0, 0, viewport[2], viewport[3]);
        glDepthMask(GL_TRUE);
        glClear(GL_DEPTH_BUFFER_BIT);
      }
    }
    if (view.sky) {
      draw_sky(view);
    } else if (view.fill) {
      glClearColor(view.background.r, view.background.g, view.background.b, 1);
      glClear(GL_COLOR_BUFFER_BIT);
    }

    // The lights of the frame (each program gets those of each draw: upload_lights).
    light_count_ = std::min(view.light_count, kMaxPointLights);
    for (size_t i = 0; i < light_count_; ++i) {
      const PointLight& l = view.lights[i];
      const float p[] = {l.x, l.y, l.z, l.radius}, c[] = {l.color.r, l.color.g, l.color.b};
      std::copy_n(p, 4, light_pos_ + 4 * i);
      std::copy_n(c, 3, light_colors_ + 3 * i);
    }
    for (auto& row : programs_) {
      for (MeshProgram& program : row) program.set_view(view);
    }
    glEnable(GL_DEPTH_TEST);
    for (const DrawList* list : lists) {
      if (list->cached) update_cache(*list);
    }
    upload_frame_instances(lists);
    view_ = &view;
    // The light pre-pass, when there are lights to add (the near lists only:
    // the far ones are beyond their reach).
    deferred_ = !shading && options_.deferred_lights && light_count_ > 0 && light_pass(view, lists);
    // The far lists first, with their own depth range; then the near ones over them.
    const bool any_far = std::any_of(lists.begin(), lists.end(), [](const DrawList* l) { return l->far; });
    if (any_far) {
      for (auto& row : programs_) {
        for (MeshProgram& program : row) program.set_view_projection(view.far_view_projection);
      }
      draw_lists(lists, true);
      glDepthMask(GL_TRUE);
      glClear(GL_DEPTH_BUFFER_BIT);
      for (auto& row : programs_) {
        for (MeshProgram& program : row) program.set_view_projection(view.view_projection);
      }
    }
    if (shading) {
      draw_shaded(view, lists, viewport[2], viewport[3]);
      // The image to the screen.
      glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous));
      glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
      glDisable(GL_DEPTH_TEST);
      glDisable(GL_CULL_FACE);
      glDisable(GL_BLEND);
      glUseProgram(copy_program_);
      glActiveTexture(GL_TEXTURE3);
      glBindTexture(GL_TEXTURE_2D, scene_texture_);
      glBindVertexArray(sky_vao_);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      glBindTexture(GL_TEXTURE_2D, 0);
      glActiveTexture(GL_TEXTURE0);
      glEnable(GL_DEPTH_TEST);
      if (profiling_) stats_.draw_calls += 2;  // the lighting, the copy
    } else {
      draw_lists(lists, false);
    }
    view_ = nullptr;
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
  }

  // ---- Deferred shading ------------------------------------------------------------
  // 1. The near opaque things once, into the G-buffer: color and kind, normal, depth.
  // 2. Their light, over the whole screen (kComposeFragmentShader), into the image.
  // 3. The transparent ones over it, as usual (lit in their shader), against that depth.
  void draw_shaded(const View& view, std::span<const DrawList* const> lists, int w, int h) {
    // 1.
    glBindFramebuffer(GL_FRAMEBUFFER, gbuffer_fbo_);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);  // the depth: that of the image (cleared after the far lists)
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    for (auto& row : programs_) {
      for (MeshProgram& program : row) program.set_opaque_pass(true);
    }
    plan_draws(lists, plan_meshes_, false, false, view, items_);
    issue(items_, kGBuffer);
    glBindVertexArray(0);

    // 2.
    glBindFramebuffer(GL_FRAMEBUFFER, compose_fbo_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glUseProgram(compose_program_);
    glUniformMatrix4fv(u_cp_inverse_, 1, GL_FALSE, view.inverse_view_projection.m);
    glUniform2f(u_cp_screen_, static_cast<float>(w), static_cast<float>(h));
    glUniform3f(u_cp_camera_, view.camera_x, view.camera_y, view.camera_z);
    glUniform3f(u_cp_light_, view.light_x, view.light_y, view.light_z);
    glUniform3f(u_cp_light_color_, view.light_color.r, view.light_color.g, view.light_color.b);
    glUniform3f(u_cp_ambient_sky_, view.ambient_sky.r, view.ambient_sky.g, view.ambient_sky.b);
    glUniform3f(u_cp_ambient_ground_, view.ambient_ground.r, view.ambient_ground.g, view.ambient_ground.b);
    glUniform1f(u_cp_night_, view.night);
    glUniform3f(u_cp_fog_, view.fog.r, view.fog.g, view.fog.b);
    glUniform3f(u_cp_fog_range_, view.fog_start, view.fog_half, view.fog_end);
    glUniform1i(u_cp_light_count_, static_cast<GLint>(light_count_));
    if (light_count_ > 0) {
      glUniform4fv(u_cp_light_pos_, static_cast<GLsizei>(light_count_), light_pos_);
      glUniform3fv(u_cp_light_colors_, static_cast<GLsizei>(light_count_), light_colors_);
    }
    const GLuint inputs[] = {depth_texture_s_, albedo_texture_, gnormals_texture_};
    for (int k = 0; k < 3; ++k) {
      glActiveTexture(static_cast<GLenum>(GL_TEXTURE3 + k));
      glBindTexture(GL_TEXTURE_2D, inputs[k]);
    }
    glBindVertexArray(sky_vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    for (int k = 0; k < 3; ++k) {  // the depth is drawn into next: no texture left bound to it
      glActiveTexture(static_cast<GLenum>(GL_TEXTURE3 + k));
      glBindTexture(GL_TEXTURE_2D, 0);
    }
    glActiveTexture(GL_TEXTURE0);

    // 3.
    glBindFramebuffer(GL_FRAMEBUFFER, scene_fbo_);
    glEnable(GL_DEPTH_TEST);
    for (auto& row : programs_) {
      for (MeshProgram& program : row) program.set_opaque_pass(false);
    }
    plan_draws(lists, plan_meshes_, false, true, view, items_);
    glEnable(GL_BLEND);
    glDepthMask(GL_FALSE);
    issue(items_, kColor);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
  }

  // Deferred shading's textures, at the size of the screen (made again when
  // it changes). False when the device cannot draw into them.
  bool make_shading_targets(int w, int h) {
    if (shading_targets_failed_ || w <= 0 || h <= 0) return false;
    if (scene_fbo_ && w == shading_w_ && h == shading_h_) return true;
    release_shading_targets();
    const auto texture = [&](GLuint& t, GLenum internal, GLenum format, GLenum type) {
      glGenTextures(1, &t);
      glBindTexture(GL_TEXTURE_2D, t);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(internal), w, h, 0, format, type, nullptr);
    };
    texture(scene_texture_, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
    texture(albedo_texture_, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
    texture(gnormals_texture_, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
    texture(depth_texture_s_, GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT);
    glBindTexture(GL_TEXTURE_2D, 0);
    bool ok = true;
    const auto check = [&] { ok = ok && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE; };
    // The image and its depth (the sky, the far lists, the transparent ones).
    glGenFramebuffers(1, &scene_fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, scene_fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, scene_texture_, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth_texture_s_, 0);
    check();
    // The G-buffer, with the same depth.
    glGenFramebuffers(1, &gbuffer_fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, gbuffer_fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, albedo_texture_, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, gnormals_texture_, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth_texture_s_, 0);
    const GLenum both[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    glDrawBuffers(2, both);
    check();
    // The image without its depth (the lighting reads the depth).
    glGenFramebuffers(1, &compose_fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, compose_fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, scene_texture_, 0);
    check();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!ok) {
      release_shading_targets();
      shading_targets_failed_ = true;
      return false;
    }
    shading_w_ = w;
    shading_h_ = h;
    return true;
  }

  void release_shading_targets() {
    const GLuint textures[] = {scene_texture_, albedo_texture_, gnormals_texture_, depth_texture_s_};
    for (GLuint t : textures) {
      if (t) glDeleteTextures(1, &t);
    }
    const GLuint fbos[] = {scene_fbo_, gbuffer_fbo_, compose_fbo_};
    for (GLuint f : fbos) {
      if (f) glDeleteFramebuffers(1, &f);
    }
    scene_texture_ = albedo_texture_ = gnormals_texture_ = depth_texture_s_ = 0;
    scene_fbo_ = gbuffer_fbo_ = compose_fbo_ = 0;
    shading_w_ = shading_h_ = 0;
  }

  // The lists of one pass (far or near): opaque meshes first (depth writes
  // on), then transparent ones (blended, no depth writes). With the depth
  // pre-pass, the opaque ones twice: depth only, then the colors where it is equal.
  void draw_lists(std::span<const DrawList* const> lists, bool far) {
    for (bool transparent : {false, true}) {
      plan_draws(lists, plan_meshes_, far, transparent, *view_, items_);
      for (auto& row : programs_) {
        for (MeshProgram& program : row) program.set_opaque_pass(!transparent);  // fading edges are not lit windows
      }
      if (transparent) glEnable(GL_BLEND); else glDisable(GL_BLEND);
      // Opaque things near, after the light pre-pass: their point lights from its buffer.
      const Mode color = !transparent && !far && deferred_ ? kColorLightBuffer : kColor;
      if (!transparent && options_.depth_prepass) {
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glDepthMask(GL_TRUE);
        issue(items_, kDepth);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_FALSE);
        glDepthFunc(GL_LEQUAL);
        issue(items_, color);
        glDepthFunc(GL_LESS);
      } else {
        glDepthMask(transparent ? GL_FALSE : GL_TRUE);
        issue(items_, color);
      }
    }
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
  }

  // Issues the draws, setting the state when it changes only (each call
  // costs, in a browser).
  // `lit_only`: only the draws some point light reaches. In kColorLightBuffer,
  // the draws no light reaches are drawn in kColor (not in the light buffer:
  // what is there is another surface's light).
  void issue(const std::vector<DrawItem>& items, Mode mode, bool lit_only = false) {
    const MeshProgram* program = nullptr;
    int culling = -1;  // not known yet
    GLuint vao = 0, buffer = 0;
    const DrawList* uploaded_list = nullptr;  // what instance_vbo_ holds (without the frame's buffer)
    uint32_t uploaded_mesh = 0;
    for (const DrawItem& item : items) {
      if (lit_only && item.lights == 0) continue;
      const Mode used = mode == kColorLightBuffer && item.lights == 0 ? kColor : mode;
      MeshProgram& p = programs_[item.fade ? 1 : 0][used];
      if (&p != program) {
        program = &p;
        glUseProgram(p.id);
      }
      if (used == kColor) upload_lights(p, item.lights);
      const GpuMesh& m = meshes_[item.mesh];
      const int cull = m.double_sided ? 0 : 1;
      if (cull != culling) {
        culling = cull;
        if (cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
      }
      if (m.vao != vao) {
        vao = m.vao;
        glBindVertexArray(m.vao);
      }
      const auto& instances = item.list->batches[item.mesh];
      uint32_t base = 0;
      GLuint wanted;
      if (item.list->cached) {
        wanted = cache_[item.list].buffers[item.mesh];
      } else if (const auto it = frame_offsets_.find(item.list); it != frame_offsets_.end()) {
        wanted = frame_vbo_;
        base = it->second[item.mesh];
      } else {
        wanted = instance_vbo_;
        if (uploaded_list != item.list || uploaded_mesh != item.mesh) {
          uploaded_list = item.list;
          uploaded_mesh = item.mesh;
          glBindBuffer(GL_ARRAY_BUFFER, instance_vbo_);
          glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(instances.size() * sizeof(Instance)),
                       instances.data(), GL_STREAM_DRAW);
          stats_.uploaded_bytes += instances.size() * sizeof(Instance);
          buffer = instance_vbo_;
        }
      }
      if (wanted != buffer) {
        buffer = wanted;
        glBindBuffer(GL_ARRAY_BUFFER, wanted);
      }
      point_instance_attributes(base + item.first_instance);
      if (m.ebo) {
        glDrawElementsInstanced(GL_TRIANGLES, item.vertex_count, GL_UNSIGNED_INT,
                                offset(static_cast<size_t>(item.first_vertex) * sizeof(uint32_t)),
                                static_cast<GLsizei>(item.instance_count));
      } else {
        glDrawArraysInstanced(GL_TRIANGLES, item.first_vertex, item.vertex_count,
                              static_cast<GLsizei>(item.instance_count));
      }
      if (profiling_) {
        const uint64_t triangles = static_cast<uint64_t>(item.vertex_count / 3) * item.instance_count;
        ++stats_.draw_calls;
        stats_.instances += item.instance_count;
        stats_.triangles += triangles;
        // Beyond the models: the ground's tiles (terrain_mesh.hpp).
        const size_t i = item.mesh;
        const auto group = static_cast<size_t>(i < kMeshCount ? mesh_group(static_cast<MeshId>(i)) : MeshGroup::Terrain);
        if (group < kMeshGroups) stats_.triangles_by_group[group] += triangles;
      }
    }
  }

  // Drawn twice, the batches of the lists not cached would be sent again:
  // all of them at once instead, in one buffer for the frame.
  void upload_frame_instances(std::span<const DrawList* const> lists) {
    frame_offsets_.clear();
    if (!options_.depth_prepass && !(options_.deferred_lights && light_count_ > 0)) return;
    frame_instances_.clear();
    for (const DrawList* list : lists) {
      if (list->cached) continue;
      std::vector<uint32_t>& offsets = frame_offsets_[list];
      offsets.assign(list->batches.size(), 0);
      for (size_t i = 0; i < list->batches.size(); ++i) {
        offsets[i] = static_cast<uint32_t>(frame_instances_.size());
        frame_instances_.insert(frame_instances_.end(), list->batches[i].begin(), list->batches[i].end());
      }
    }
    glBindBuffer(GL_ARRAY_BUFFER, frame_vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(frame_instances_.size() * sizeof(Instance)),
                 frame_instances_.data(), GL_STREAM_DRAW);
    stats_.uploaded_bytes += frame_instances_.size() * sizeof(Instance);
  }

  // The lights of a draw (bit i: the frame's light i), sent to the program
  // when they are not those it already has.
  void upload_lights(MeshProgram& p, uint16_t mask) {
    if (p.lights_known && p.lights == mask) return;
    p.lights_known = true;
    p.lights = mask;
    float pos[kMaxPointLights * 4], colors[kMaxPointLights * 3];
    GLsizei n = 0;
    for (size_t i = 0; i < light_count_; ++i) {
      if (!((mask >> i) & 1u)) continue;
      std::copy_n(light_pos_ + 4 * i, 4, pos + 4 * n);
      std::copy_n(light_colors_ + 3 * i, 3, colors + 3 * n);
      ++n;
    }
    glUniform1i(p.light_count, n);
    if (n > 0) {
      glUniform4fv(p.light_pos, n, pos);
      glUniform3fv(p.light_colors, n, colors);
    }
  }

  // ---- Light pre-pass ------------------------------------------------------------
  // 1. The normals and depth of the near opaque things some light reaches, into textures.
  // 2. Each point light, over the pixels it may reach (light_rect), adds its
  //    light there into the light buffer.
  // 3. (draw_lists) The colors read it at their pixel instead of the light loop.
  // False when the buffers cannot be made (then the lights stay in the shader).
  bool light_pass(const View& view, std::span<const DrawList* const> lists) {
    GLint previous = 0, viewport[4] = {};
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previous);
    glGetIntegerv(GL_VIEWPORT, viewport);
    const int w = viewport[2], h = viewport[3];
    if (!make_light_targets(w, h)) return false;

    // 1. Normals and depth.
    glBindFramebuffer(GL_FRAMEBUFFER, normals_fbo_);
    glViewport(0, 0, w, h);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glClearColor(0.5f, 1.0f, 0.5f, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_BLEND);
    for (auto& row : programs_) {
      for (MeshProgram& program : row) program.set_opaque_pass(true);
    }
    // Only what a light reaches: the rest is drawn once, without the light buffer.
    plan_draws(lists, plan_meshes_, false, false, view, items_);
    issue(items_, kNormals, true);
    glBindVertexArray(0);

    // 2. The lights, added up.
    glBindFramebuffer(GL_FRAMEBUFFER, light_fbo_);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glUseProgram(light_program_);
    glUniformMatrix4fv(u_lp_inverse_, 1, GL_FALSE, view.inverse_view_projection.m);
    glUniform2f(u_lp_screen_, static_cast<float>(w), static_cast<float>(h));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, depth_texture_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, normals_texture_);
    glBindVertexArray(sky_vao_);
    glEnable(GL_SCISSOR_TEST);
    for (size_t i = 0; i < light_count_; ++i) {
      const PointLight& l = view.lights[i];
      PixelRect r;
      if (!light_rect(view.view_projection, l, w, h, r)) continue;
      glScissor(r.x, r.y, r.w, r.h);
      glUniform4f(u_lp_pos_, l.x, l.y, l.z, l.radius);
      const float k = 1.0f / light_buffer_scale_;
      glUniform3f(u_lp_color_, l.color.r * k, l.color.g * k, l.color.b * k);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      if (profiling_) ++stats_.draw_calls;
    }
    glDisable(GL_SCISSOR_TEST);
    glBindVertexArray(0);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);

    // 3. The light buffer, read by the colors (texture unit 2).
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, light_texture_);
    glActiveTexture(GL_TEXTURE0);
    for (auto& row : programs_) {
      glUseProgram(row[kColorLightBuffer].id);
      glUniform1f(row[kColorLightBuffer].light_buffer_scale, light_buffer_scale_);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous));
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    return true;
  }

  // The light pre-pass's textures, at the size of the screen (made again
  // when it changes). False when the device cannot draw into them.
  bool make_light_targets(int w, int h) {
    if (light_targets_failed_ || w <= 0 || h <= 0) return false;
    if (normals_fbo_ && w == targets_w_ && h == targets_h_) return true;
    release_light_targets();
    const auto texture = [&](GLuint& t, GLenum internal, GLenum format, GLenum type) {
      glGenTextures(1, &t);
      glBindTexture(GL_TEXTURE_2D, t);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(internal), w, h, 0, format, type, nullptr);
    };
    texture(normals_texture_, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
    texture(depth_texture_, GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT);
    glGenFramebuffers(1, &normals_fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, normals_fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, normals_texture_, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth_texture_, 0);
    bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glGenFramebuffers(1, &light_fbo_);
    for (int attempt = 0; ok && attempt < 2; ++attempt) {
      if (light_texture_) glDeleteTextures(1, &light_texture_);
      light_texture_ = 0;
      if (float_lights_) {
        texture(light_texture_, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT);
      } else {
        texture(light_texture_, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
      }
      glBindFramebuffer(GL_FRAMEBUFFER, light_fbo_);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, light_texture_, 0);
      if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) break;
      if (!float_lights_) ok = false;
      float_lights_ = false;  // then in 8 bits
      light_buffer_scale_ = 4.0f;
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!ok) {
      release_light_targets();
      light_targets_failed_ = true;
      return false;
    }
    targets_w_ = w;
    targets_h_ = h;
    return true;
  }

  void release_light_targets() {
    const GLuint textures[] = {normals_texture_, depth_texture_, light_texture_};
    for (GLuint t : textures) {
      if (t) glDeleteTextures(1, &t);
    }
    if (normals_fbo_) glDeleteFramebuffers(1, &normals_fbo_);
    if (light_fbo_) glDeleteFramebuffers(1, &light_fbo_);
    normals_texture_ = depth_texture_ = light_texture_ = normals_fbo_ = light_fbo_ = 0;
    targets_w_ = targets_h_ = 0;
  }

  void end_frame() override {
    if (profiling_) end_gpu_timer();
  }

  void set_profiling(bool on) override {
    profiling_ = on;
    if (on && !timer_checked_) {
      timer_checked_ = true;
      // EXT_disjoint_timer_query(_webgl2): measures the GPU time of a frame, where the browser allows it.
      const char* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
      timer_available_ = extensions && std::strstr(extensions, "disjoint_timer_query") != nullptr;
    }
    if (!on) stats_ = {};
  }

  const RenderStats& stats() const override { return stats_; }

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
    glUniform3f(u_sky_moon_, view.moon_x, view.moon_y, view.moon_z);
    glUniform1f(u_sky_night_, view.night);
    glUniform3f(u_sky_zenith_, view.sky_zenith.r, view.sky_zenith.g, view.sky_zenith.b);
    glUniform3f(u_sky_horizon_, view.sky_horizon.r, view.sky_horizon.g, view.sky_horizon.b);
    glUniform1f(u_sky_cover_, view.cloud_cover);
    glUniform3f(u_sky_cloud_light_, view.cloud_light.r, view.cloud_light.g, view.cloud_light.b);
    glUniform3f(u_sky_cloud_shade_, view.cloud_shade.r, view.cloud_shade.g, view.cloud_shade.b);
    // Only the drift within a long stretch (the noise does not repeat, but
    // big numbers lose their precision on phones): it jumps every 400 km.
    glUniform2f(u_sky_cloud_shift_, std::fmod(view.cloud_shift_x, 400000.0f), std::fmod(view.cloud_shift_z, 400000.0f));
    glBindVertexArray(sky_vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
  }

  // Instance attributes read from the buffer bound to GL_ARRAY_BUFFER.
  // From instance `first` on (WebGL2 has no base instance: the attributes
  // point further into the buffer instead).
  static void point_instance_attributes(uint32_t first = 0) {
    constexpr GLsizei stride = sizeof(Instance);
    const size_t offsets[] = {offsetof(Instance, x), offsetof(Instance, rotation), offsetof(Instance, sx),
                              offsetof(Instance, tint)};
    for (GLuint i = 0; i < 4; ++i) {
      glVertexAttribPointer(3 + i, 4, GL_FLOAT, GL_FALSE, stride, offset(offsets[i] + first * sizeof(Instance)));
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
      stats_.uploaded_bytes += list.batches[i].size() * sizeof(Instance);
    }
    c.version = list.version;
    c.valid = true;
  }

  // GPU time: one query at a time, read back a few frames later.
  static constexpr GLenum kTimeElapsed = 0x88BF;  // GL_TIME_ELAPSED_EXT
  static constexpr GLenum kGpuDisjoint = 0x8FBB;  // GL_GPU_DISJOINT_EXT
  void begin_gpu_timer() {
    if (!timer_available_) return;
    if (timer_query_ && timer_running_) return;
    if (timer_query_ && !timer_running_) {  // the last one: done?
      GLuint ready = 0;
      glGetQueryObjectuiv(timer_query_, GL_QUERY_RESULT_AVAILABLE, &ready);
      if (!ready) return;
      GLuint ns = 0;
      glGetQueryObjectuiv(timer_query_, GL_QUERY_RESULT, &ns);
      GLint disjoint = 0;
      glGetIntegerv(kGpuDisjoint, &disjoint);
      if (!disjoint) stats_.gpu_ms = static_cast<double>(ns) / 1e6;
    }
    if (!timer_query_) glGenQueries(1, &timer_query_);
    glBeginQuery(kTimeElapsed, timer_query_);
    timer_running_ = true;
    timer_this_frame_ = true;
  }
  void end_gpu_timer() {
    if (!timer_this_frame_) return;
    glEndQuery(kTimeElapsed);
    timer_running_ = false;
    timer_this_frame_ = false;
  }

  static void release(GpuMesh& m) {
    if (m.vbo) glDeleteBuffers(1, &m.vbo);
    if (m.ebo) glDeleteBuffers(1, &m.ebo);
    if (m.vao) glDeleteVertexArrays(1, &m.vao);
    m.vbo = m.vao = m.ebo = 0;
  }

  MeshProgram programs_[2][kModes];  // [fade][mode]
  // The light pre-pass (light_pass).
  GLuint light_program_ = 0;
  GLint u_lp_inverse_ = -1, u_lp_screen_ = -1, u_lp_pos_ = -1, u_lp_color_ = -1;
  GLuint normals_fbo_ = 0, light_fbo_ = 0, normals_texture_ = 0, depth_texture_ = 0, light_texture_ = 0;
  int targets_w_ = 0, targets_h_ = 0;
  bool float_lights_ = false, light_targets_failed_ = false, deferred_ = false;
  float light_buffer_scale_ = 4.0f;  // the light buffer holds the light divided by this
  // Deferred shading (draw_shaded).
  GLuint compose_program_ = 0, copy_program_ = 0;
  GLint u_cp_inverse_ = -1, u_cp_screen_ = -1, u_cp_camera_ = -1, u_cp_light_ = -1, u_cp_light_color_ = -1;
  GLint u_cp_ambient_sky_ = -1, u_cp_ambient_ground_ = -1, u_cp_night_ = -1, u_cp_fog_ = -1, u_cp_fog_range_ = -1;
  GLint u_cp_light_count_ = -1, u_cp_light_pos_ = -1, u_cp_light_colors_ = -1;
  GLuint scene_fbo_ = 0, gbuffer_fbo_ = 0, compose_fbo_ = 0;
  GLuint scene_texture_ = 0, albedo_texture_ = 0, gnormals_texture_ = 0, depth_texture_s_ = 0;
  int shading_w_ = 0, shading_h_ = 0;
  bool shading_targets_failed_ = false;
  GLuint sky_program_ = 0;
  GLint u_sky_inverse_ = -1, u_sky_camera_ = -1, u_sky_sun_ = -1, u_sky_zenith_ = -1, u_sky_horizon_ = -1;
  GLint u_sky_cover_ = -1, u_sky_cloud_light_ = -1, u_sky_cloud_shade_ = -1, u_sky_cloud_shift_ = -1;
  GLint u_sky_moon_ = -1, u_sky_night_ = -1;
  RenderOptions options_;
  const View* view_ = nullptr;  // during draw()
  size_t light_count_ = 0;
  float light_pos_[kMaxPointLights * 4] = {}, light_colors_[kMaxPointLights * 3] = {};
  std::vector<PlanMesh> plan_meshes_;
  std::vector<DrawItem> items_;
  GLuint frame_vbo_ = 0;  // the instances of the lists not cached, when drawn twice
  std::vector<Instance> frame_instances_;
  std::unordered_map<const DrawList*, std::vector<uint32_t>> frame_offsets_;
  GLuint sky_vao_ = 0;
  GLuint instance_vbo_ = 0;
  bool profiling_ = false;
  bool timer_checked_ = false, timer_available_ = false;
  bool timer_running_ = false, timer_this_frame_ = false;
  GLuint timer_query_ = 0;
  RenderStats stats_;
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
