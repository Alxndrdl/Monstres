// Browser entry point: main loop, keyboard, saving to IndexedDB, and the
// small C API called from post.js (window.monstres).

#include "gles3_renderer.hpp"
#include "monstres/core/debug.hpp"
#include "monstres/core/save.hpp"
#include "monstres/core/world.hpp"
#include "monstres/render/scene.hpp"

#include <emscripten.h>
#include <emscripten/html5.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <vector>

using namespace monstres;

namespace {

constexpr const char* kSavePath = "/save/game.sav";
constexpr const char* kSaveTmpPath = "/save/game.sav.tmp";
constexpr double kTickMs = 1000.0 / rules::kTicksPerSecond;
constexpr uint64_t kAutosaveEveryTicks = 50;
constexpr size_t kMaxQueuedActions = 2;

struct App {
  World world;
  std::unique_ptr<Renderer> renderer;
  std::vector<QuadInstance> quads;
  std::deque<Action> pending;
  bool frozen = false;           // ?frozen: no automatic ticks (tests, screenshots)
  double last_time = 0;
  double accumulator = 0;
  uint64_t last_saved_tick = 0;
  int hud_hp = -1, hud_score = -1, hud_over = -1;
  std::vector<uint8_t> export_buffer;
  std::string json;
};

App* g_app = nullptr;

EM_JS(void, js_sync_save, (), { Module.monstresSync(); });
EM_JS(void, js_fatal, (const char* msg), { Module.monstresFatal(UTF8ToString(msg)); });
EM_JS(void, js_ready, (), { Module.monstresReady(); });
EM_JS(void, js_update_hud, (int hp, int max_hp, int score, int game_over), {
  Module.monstresHud(hp, max_hp, score, !!game_over);
});

uint64_t random_seed() {
  return static_cast<uint64_t>(emscripten_get_now() * 1000.0) ^
         (static_cast<uint64_t>(emscripten_random() * 4294967296.0) << 20);
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
    std::fprintf(stderr, "save ignored (error %d)\n", static_cast<int>(error));
    return false;
  }
  out = std::move(*world);
  return true;
}

Action action_for_key(const char* code) {
  // KeyboardEvent.code is the physical key: WASD on QWERTY = ZQSD on AZERTY.
  if (!std::strcmp(code, "ArrowUp") || !std::strcmp(code, "KeyW")) return Action::Up;
  if (!std::strcmp(code, "ArrowDown") || !std::strcmp(code, "KeyS")) return Action::Down;
  if (!std::strcmp(code, "ArrowLeft") || !std::strcmp(code, "KeyA")) return Action::Left;
  if (!std::strcmp(code, "ArrowRight") || !std::strcmp(code, "KeyD")) return Action::Right;
  if (!std::strcmp(code, "Space")) return Action::Attack;
  return Action::None;
}

bool on_key_down(int, const EmscriptenKeyboardEvent* e, void* user) {
  App& app = *static_cast<App*>(user);
  if (app.world.game_over() && (!std::strcmp(e->code, "Enter") || !std::strcmp(e->code, "KeyR"))) {
    app.world = new_game(random_seed());
    app.pending.clear();
    write_save(app);
    return true;
  }
  const Action a = action_for_key(e->code);
  if (a == Action::None) return false;
  if (app.pending.size() < kMaxQueuedActions) app.pending.push_back(a);
  return true;  // prevents arrows and space from scrolling the page
}

void update_hud(App& app) {
  const Player& p = app.world.player;
  const int over = app.world.game_over() ? 1 : 0;
  if (p.hp == app.hud_hp && static_cast<int>(p.score) == app.hud_score && over == app.hud_over) return;
  app.hud_hp = p.hp;
  app.hud_score = static_cast<int>(p.score);
  app.hud_over = over;
  js_update_hud(p.hp, rules::kPlayerMaxHp, app.hud_score, over);
}

void frame(void* user) {
  App& app = *static_cast<App*>(user);

  const double now = emscripten_get_now();
  double dt = now - app.last_time;
  app.last_time = now;
  if (dt > 250) dt = 250;  // tab was in the background: don't fast-forward

  if (!app.frozen) {
    app.accumulator += dt;
    while (app.accumulator >= kTickMs) {
      app.accumulator -= kTickMs;
      Action a = Action::None;
      if (!app.pending.empty()) {
        a = app.pending.front();
        app.pending.pop_front();
      }
      step(app.world, a);
    }
    if (app.world.tick - app.last_saved_tick >= kAutosaveEveryTicks) write_save(app);
  }

  double css_w = 0, css_h = 0;
  emscripten_get_element_css_size("#canvas", &css_w, &css_h);
  const double dpr = emscripten_get_device_pixel_ratio();
  const int pw = static_cast<int>(css_w * dpr);
  const int ph = static_cast<int>(css_h * dpr);
  emscripten_set_canvas_element_size("#canvas", pw, ph);

  build_scene(app.world, app.quads);
  app.renderer->begin_frame({pw, ph, static_cast<float>(rules::kWidth),
                             static_cast<float>(rules::kHeight), palette::kBackground});
  app.renderer->draw_quads(app.quads);
  app.renderer->end_frame();

  update_hud(app);
}

}  // namespace

// ---- C API used by post.js -------------------------------------------------

extern "C" {

EMSCRIPTEN_KEEPALIVE const char* monstres_state_json() {
  g_app->json = to_json(g_app->world);
  return g_app->json.c_str();
}

EMSCRIPTEN_KEEPALIVE double monstres_debug_step(int action) {
  if (action < 0 || action > static_cast<int>(Action::Attack)) return -1;
  step(g_app->world, static_cast<Action>(action));
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
  g_app->pending.clear();
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
  attrs.antialias = false;
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

  emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, &app, true, on_key_down);
  app.last_time = emscripten_get_now();
  update_hud(app);
  emscripten_set_main_loop_arg(frame, &app, 0, false);
  js_ready();
  return 0;
}
