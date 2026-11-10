// Browser entry point: main loop, saving to IndexedDB, and the small C API
// called from JavaScript (post.js, web/controls.js).
//
// Input flow: web/controls.js reads the joystick, the jump button, screen
// drags and the keyboard, and calls monstres_set_move / monstres_look /
// monstres_jump. Each fixed tick turns that into a monstres::Input.

#include "gles3_renderer.hpp"
#include "monstres/core/debug.hpp"
#include "monstres/core/save.hpp"
#include "monstres/core/world.hpp"
#include "monstres/render/scene.hpp"

#include <emscripten.h>
#include <emscripten/html5.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace monstres;

namespace {

constexpr const char* kSavePath = "/save/game.sav";
constexpr const char* kSaveTmpPath = "/save/game.sav.tmp";
constexpr double kTickMs = 1000.0 / rules::kTicksPerSecond;
constexpr uint64_t kAutosaveEveryTicks = 5 * rules::kTicksPerSecond;
constexpr int kJumpBufferTicks = 6;  // a jump pressed just before landing still counts
constexpr int kMaxTicksPerFrame = 10;

struct App {
  World world;
  std::unique_ptr<Renderer> renderer;
  std::vector<CubeInstance> cubes;
  Camera camera;
  float move_x = 0, move_y = 0;  // -1..1, set by JavaScript
  int jump_buffer = 0;           // ticks left during which a jump press is pending
  bool frozen = false;           // ?frozen: no automatic ticks (tests, screenshots)
  double last_time = 0;
  double accumulator = 0;
  uint64_t last_saved_tick = 0;
  std::vector<uint8_t> export_buffer;
  std::string json;
};

App* g_app = nullptr;

EM_JS(void, js_sync_save, (), { Module.monstresSync(); });
EM_JS(void, js_fatal, (const char* msg), { Module.monstresFatal(UTF8ToString(msg)); });
EM_JS(void, js_ready, (), { Module.monstresReady(); });

uint64_t random_seed() {
  return static_cast<uint64_t>(emscripten_get_now() * 1000.0) ^
         (static_cast<uint64_t>(emscripten_random() * 4294967296.0) << 20);
}

int8_t to_axis(float v) {
  return static_cast<int8_t>(std::lround(std::clamp(v, -1.0f, 1.0f) * 127.0f));
}

Input current_input(App& app) {
  Input in;
  in.move_x = to_axis(app.move_x);
  in.move_y = to_axis(app.move_y);
  in.yaw = angle16_from_radians(app.camera.yaw);
  in.jump = app.jump_buffer > 0;
  return in;
}

void tick(App& app, const Input& in) {
  const bool was_on_ground = app.world.player.on_ground;
  step(app.world, in);
  if (in.jump && was_on_ground) app.jump_buffer = 0;  // the jump happened
  if (app.jump_buffer > 0) --app.jump_buffer;
}

void write_save(App& app) {
  const std::vector<uint8_t> bytes = serialize(app.world);
  FILE* f = std::fopen(kSaveTmpPath, "wb");
  if (!f) return;
  const bool ok = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
  std::fclose(f);
  if (ok && std::rename(kSaveTmpPath, kSavePath) == 0) {
    app.last_saved_tick = app.world.tick;
    js_sync_save();
  }
}

bool read_save(World& out) {
  FILE* f = std::fopen(kSavePath, "rb");
  if (!f) return false;
  std::vector<uint8_t> bytes;
  uint8_t buf[4096];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) bytes.insert(bytes.end(), buf, buf + n);
  std::fclose(f);
  LoadError error = LoadError::None;
  auto world = deserialize(bytes, &error);
  if (!world) {
    std::printf("Sauvegarde ignorée (%s), nouvelle partie.\n", load_error_name(error));
    return false;
  }
  out = std::move(*world);
  return true;
}

void frame(void* user) {
  App& app = *static_cast<App*>(user);

  const double now = emscripten_get_now();
  double dt = now - app.last_time;
  app.last_time = now;

  if (!app.frozen) {
    app.accumulator = std::min(app.accumulator + dt, kTickMs * kMaxTicksPerFrame);
    while (app.accumulator >= kTickMs) {
      app.accumulator -= kTickMs;
      tick(app, current_input(app));
    }
    if (app.world.tick - app.last_saved_tick >= kAutosaveEveryTicks) write_save(app);
  }

  double css_w = 0, css_h = 0;
  emscripten_get_element_css_size("#canvas", &css_w, &css_h);
  const double dpr = emscripten_get_device_pixel_ratio();
  const int pw = std::max(1, static_cast<int>(css_w * dpr));
  const int ph = std::max(1, static_cast<int>(css_h * dpr));
  emscripten_set_canvas_element_size("#canvas", pw, ph);

  build_scene(app.world, app.cubes);
  const View view = build_view(app.world, app.camera, static_cast<float>(pw) / static_cast<float>(ph));
  app.renderer->begin_frame({pw, ph, palette::kSky});
  app.renderer->draw_cubes(view, app.cubes);
  app.renderer->end_frame();
}

}  // namespace

// ---- C API used by JavaScript ----------------------------------------------

extern "C" {

// Joystick or keyboard direction: x right, y forward, each in -1..1.
EMSCRIPTEN_KEEPALIVE void monstres_set_move(float x, float y) {
  g_app->move_x = x;
  g_app->move_y = y;
}

// Rotates the camera by an angle in radians (screen drag or tests).
EMSCRIPTEN_KEEPALIVE void monstres_look(float delta_yaw, float delta_pitch) {
  Camera& c = g_app->camera;
  c.yaw = std::remainder(c.yaw + delta_yaw, static_cast<float>(2 * kPi));
  c.pitch = std::clamp(c.pitch + delta_pitch, kMinPitch, kMaxPitch);
}

EMSCRIPTEN_KEEPALIVE void monstres_set_camera(float yaw, float pitch) {
  g_app->camera.yaw = yaw;
  g_app->camera.pitch = std::clamp(pitch, kMinPitch, kMaxPitch);
}

EMSCRIPTEN_KEEPALIVE void monstres_jump() { g_app->jump_buffer = kJumpBufferTicks; }

EMSCRIPTEN_KEEPALIVE const char* monstres_state_json() {
  const Camera& c = g_app->camera;
  std::string json = to_json(g_app->world);
  json.pop_back();  // add the camera, which lives here and not in the world
  char camera[96];
  std::snprintf(camera, sizeof camera, ",\"camera\":{\"yaw\":%.4f,\"pitch\":%.4f}}",
                static_cast<double>(c.yaw), static_cast<double>(c.pitch));
  g_app->json = json + camera;
  return g_app->json.c_str();
}

// Runs `ticks` ticks with the current joystick/camera input (tests, ?frozen).
EMSCRIPTEN_KEEPALIVE double monstres_debug_step(int ticks) {
  for (int i = 0; i < ticks; ++i) tick(*g_app, current_input(*g_app));
  return static_cast<double>(g_app->world.tick);
}

EMSCRIPTEN_KEEPALIVE void monstres_save_now() { write_save(*g_app); }

EMSCRIPTEN_KEEPALIVE int monstres_export_prepare() {
  g_app->export_buffer = serialize(g_app->world);
  return static_cast<int>(g_app->export_buffer.size());
}

EMSCRIPTEN_KEEPALIVE const uint8_t* monstres_export_data() { return g_app->export_buffer.data(); }

EMSCRIPTEN_KEEPALIVE int monstres_import_save(const uint8_t* data, int size) {
  if (!data || size <= 0) return 0;
  auto world = deserialize({data, static_cast<size_t>(size)});
  if (!world) return 0;
  g_app->world = std::move(*world);
  write_save(*g_app);
  return 1;
}

}  // extern "C"

// ---- Entry point -----------------------------------------------------------

int main(int argc, char** argv) {
  // Arguments come from the page URL (see pre.js): ?seed=42&new&frozen
  uint64_t seed = 0;
  bool has_seed = false, force_new = false, frozen = false;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--seed") && i + 1 < argc) {
      seed = std::strtoull(argv[++i], nullptr, 10);
      has_seed = true;
    } else if (!std::strcmp(argv[i], "--new")) {
      force_new = true;
    } else if (!std::strcmp(argv[i], "--frozen")) {
      frozen = true;
    }
  }

  g_app = new App();  // lives for the whole page
  App& app = *g_app;
  app.frozen = frozen;

  EmscriptenWebGLContextAttributes attrs;
  emscripten_webgl_init_context_attributes(&attrs);
  attrs.majorVersion = 2;
  attrs.minorVersion = 0;
  attrs.alpha = false;
  attrs.depth = true;
  attrs.antialias = true;
  const auto ctx = emscripten_webgl_create_context("#canvas", &attrs);
  if (ctx <= 0) {
    js_fatal("WebGL2 n'est pas disponible sur ce navigateur.");
    return 1;
  }
  emscripten_webgl_make_context_current(ctx);
  app.renderer = make_gles3_renderer();
  if (!app.renderer) {
    js_fatal("Impossible d'initialiser le rendu (shaders).");
    return 1;
  }

  // The save folder was loaded from IndexedDB in pre.js before main() ran.
  if (force_new || !read_save(app.world)) {
    app.world = new_game(has_seed ? seed : random_seed());
    write_save(app);
  }
  app.last_saved_tick = app.world.tick;

  app.last_time = emscripten_get_now();
  emscripten_set_main_loop_arg(frame, &app, 0, false);
  js_ready();
  return 0;
}
