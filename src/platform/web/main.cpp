// Browser entry point: main loop, saving to IndexedDB, and the small C API
// called from JavaScript (post.js, web/controls.js).
//
// Input flow: web/controls.js reads the joystick, the jump button, screen
// drags and the keyboard, and calls monstres_set_move / monstres_look /
// monstres_jump. Each fixed tick turns that into a monstres::Input.

#include "gles3_renderer.hpp"
#include "monstres/core/battle.hpp"
#include "monstres/core/debug.hpp"
#include "monstres/core/save.hpp"
#include "monstres/core/variants.hpp"
#include "monstres/core/world.hpp"
#include "monstres/render/models.hpp"
#include "monstres/render/terrain_mesh.hpp"
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

constexpr const char* kModelsDir = "/models";  // preloaded from assets/models
constexpr const char* kSavePath = "/save/game.sav";
constexpr const char* kSaveTmpPath = "/save/game.sav.tmp";
constexpr double kTickMs = 1000.0 / rules::kTicksPerSecond;
constexpr uint64_t kAutosaveEveryTicks = 5 * rules::kTicksPerSecond;
constexpr int kJumpBufferTicks = 6;  // a jump pressed just before landing still counts
constexpr int kMaxTicksPerFrame = 10;

struct App {
  World world;
  std::unique_ptr<Renderer> renderer;
  DrawList landscape;   // what never moves, rebuilt when the world changes
  DrawList dynamic;     // player, clouds: rebuilt every frame
  Camera camera;
  float move_x = 0, move_y = 0;  // -1..1, set by JavaScript
  int jump_buffer = 0;           // ticks left during which a jump press is pending
  int attack_buffer = 0;         // same for an attack (battle)
  uint8_t attack_slot = 0;       // 1..3
  int parry_buffer = 0;
  bool hold_clock = false;       // debug: the time of day stands still
  uint32_t held_clock = 0;
  Command command = Command::None;  // sent with the next tick
  uint16_t command_target = 0;
  bool frozen = false;           // ?frozen: no automatic ticks (tests, screenshots)
  bool redraw = true;            // frozen mode only redraws when something changed
  int selected = -1;             // creature shown in the info panel (-1: none)
  bool paused = false;           // a game menu is open: time stands still
  std::vector<uint8_t> portrait; // last portrait drawn for the page (RGBA)
  int canvas_w = 0, canvas_h = 0;
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
  in.attack = app.attack_buffer > 0 ? app.attack_slot : 0;
  in.parry = app.parry_buffer > 0;
  in.command = app.command;
  in.target = app.command_target;
  return in;
}

void tick(App& app, const Input& in) {
  World& w = app.world;
  const bool was_on_ground = w.player.on_ground;
  const bool fighting = w.battle.phase == BattlePhase::Fight;
  const size_t creatures = w.creatures.size();
  step(w, in);
  if (app.hold_clock) w.clock = app.held_clock;
  // A press is kept a few ticks, until it happens (landing, end of a cooldown).
  const Fighter& ally = w.battle.ally;
  if (in.jump && (fighting ? ally.state == FighterState::Dodge : was_on_ground)) app.jump_buffer = 0;
  if (app.jump_buffer > 0) --app.jump_buffer;
  if (in.attack && ally.attack_tick == w.tick) app.attack_buffer = 0;
  if (app.attack_buffer > 0) --app.attack_buffer;
  if (in.parry && ally.state == FighterState::Parry) app.parry_buffer = 0;
  if (app.parry_buffer > 0) --app.parry_buffer;
  app.command = Command::None;
  if (w.creatures.size() != creatures) app.selected = -1;  // a captured monster left the world
  if (w.battle.phase == BattlePhase::Fight) app.selected = -1;  // the enemy has its own ring
}

// In a fight the camera stays behind the companion, facing its enemy.
void follow_battle(App& app, double dt_ms) {
  float fx, fz;
  if (app.world.battle.phase != BattlePhase::Fight || !battle_forward(app.world, fx, fz)) return;
  const float target = std::atan2(-fx, -fz);  // forward = (-sin yaw, -cos yaw)
  const float k = std::min(1.0f, static_cast<float>(dt_ms) * 0.012f);
  Camera& c = app.camera;
  c.yaw = std::remainder(c.yaw + std::remainder(target - c.yaw, static_cast<float>(2 * kPi)) * k,
                         static_cast<float>(2 * kPi));
  app.redraw = true;
}

// Call whenever app.world is replaced (new game, loaded or imported save).
void world_changed(App& app) {
  app.redraw = true;
  app.selected = -1;
  build_landscape(app.world, app.landscape);
  app.renderer->set_mesh(static_cast<size_t>(MeshId::Ground), build_ground_mesh(app.world));
  app.renderer->set_mesh(static_cast<size_t>(MeshId::Path), build_path_mesh(app.world));
  app.renderer->set_mesh(static_cast<size_t>(MeshId::Water), build_water_mesh(app.world));
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

View current_view(const App& app, float time) {
  const float aspect = app.canvas_h > 0 ? static_cast<float>(app.canvas_w) / static_cast<float>(app.canvas_h) : 1.0f;
  return build_view(app.world, app.camera, aspect, time);
}

void frame(void* user) {
  App& app = *static_cast<App*>(user);

  const double now = emscripten_get_now();
  double dt = now - app.last_time;
  app.last_time = now;

  if (app.paused) app.accumulator = 0;
  if (!app.frozen && !app.paused) {
    app.accumulator = std::min(app.accumulator + dt, kTickMs * kMaxTicksPerFrame);
    while (app.accumulator >= kTickMs) {
      app.accumulator -= kTickMs;
      tick(app, current_input(app));
    }
    if (app.world.tick - app.last_saved_tick >= kAutosaveEveryTicks) write_save(app);
  }
  if (!app.paused) follow_battle(app, dt);

  double css_w = 0, css_h = 0;
  emscripten_get_element_css_size("#canvas", &css_w, &css_h);
  const double dpr = emscripten_get_device_pixel_ratio();
  const int pw = std::max(1, static_cast<int>(css_w * dpr));
  const int ph = std::max(1, static_cast<int>(css_h * dpr));
  if (pw != app.canvas_w || ph != app.canvas_h) {
    // Setting the canvas size clears it, even to the same value: only on change.
    emscripten_set_canvas_element_size("#canvas", pw, ph);
    app.canvas_w = pw;
    app.canvas_h = ph;
    app.redraw = true;
  }
  if (app.frozen && !app.redraw) return;  // nothing moved: keep the last image
  app.redraw = false;

  // Time follows the simulation, so ?frozen screenshots are reproducible.
  const float time = static_cast<float>(app.world.tick) / rules::kTicksPerSecond;
  build_dynamic(app.world, time, app.dynamic, app.selected);
  const View view = current_view(app, time);
  const DrawList* lists[] = {&app.landscape, &app.dynamic};
  app.renderer->begin_frame({pw, ph});
  app.renderer->draw(view, lists);
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
  g_app->redraw = true;
  if (g_app->world.battle.phase == BattlePhase::Fight) delta_yaw = 0;  // locked on the enemy
  c.yaw = std::remainder(c.yaw + delta_yaw, static_cast<float>(2 * kPi));
  c.pitch = std::clamp(c.pitch + delta_pitch, kMinPitch, kMaxPitch);
}

EMSCRIPTEN_KEEPALIVE void monstres_set_camera(float yaw, float pitch) {
  g_app->redraw = true;
  g_app->camera.yaw = yaw;
  g_app->camera.pitch = std::clamp(pitch, kMinPitch, kMaxPitch);
}

EMSCRIPTEN_KEEPALIVE void monstres_jump() { g_app->jump_buffer = kJumpBufferTicks; }

// Battle buttons: attack 1 (quick), 2 and 3 (special); parry. The dodge is
// the jump.
EMSCRIPTEN_KEEPALIVE void monstres_attack(int slot) {
  if (slot < 1 || slot > 3) return;
  g_app->attack_slot = static_cast<uint8_t>(slot);
  g_app->attack_buffer = kJumpBufferTicks;
}

EMSCRIPTEN_KEEPALIVE void monstres_parry() { g_app->parry_buffer = kJumpBufferTicks; }

// Context menu: 1 engage creatures[target], 2 flee, 3 capture (Command).
// Applied with the next tick (right away when frozen, for tests).
EMSCRIPTEN_KEEPALIVE void monstres_command(int command, int target) {
  if (command < 1 || command > 3 || target < 0 || target > 65535) return;
  g_app->command = static_cast<Command>(command);
  g_app->command_target = static_cast<uint16_t>(target);
  if (g_app->frozen) {
    tick(*g_app, current_input(*g_app));
    g_app->redraw = true;
  }
}

// Debug options (settings menu): the time of day, in hours (0..24), and
// whether it stands still.
EMSCRIPTEN_KEEPALIVE void monstres_set_hour(float hour) {
  const float h = std::fmod(std::fmod(hour, 24.0f) + 24.0f, 24.0f);
  g_app->world.clock = std::min(static_cast<uint32_t>(h * rules::kTicksPerHour), rules::kTicksPerDay - 1);
  g_app->held_clock = g_app->world.clock;
  g_app->redraw = true;
}

EMSCRIPTEN_KEEPALIVE void monstres_hold_hour(int hold) {
  g_app->hold_clock = hold != 0;
  g_app->held_clock = g_app->world.clock;
}

EMSCRIPTEN_KEEPALIVE const char* monstres_team_json() {
  g_app->json = team_json(g_app->world);
  return g_app->json.c_str();
}

EMSCRIPTEN_KEEPALIVE const char* monstres_battle_json() {
  g_app->json = battle_json(g_app->world);
  return g_app->json.c_str();
}

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
  g_app->redraw = true;
  return static_cast<double>(g_app->world.tick);
}

EMSCRIPTEN_KEEPALIVE void monstres_save_now() { write_save(*g_app); }

// Selects the creature under a point of the canvas (normalized device
// coordinates, -1..1, y up), or nothing. Returns its index or -1.
EMSCRIPTEN_KEEPALIVE int monstres_pick(float ndc_x, float ndc_y) {
  g_app->selected = pick_creature(g_app->world, current_view(*g_app, 0), ndc_x, ndc_y);
  g_app->redraw = true;
  return g_app->selected;
}

// Game menus: time stands still while one is open (saved when it opens).
EMSCRIPTEN_KEEPALIVE void monstres_set_paused(int paused) {
  g_app->paused = paused != 0;
  if (g_app->paused) write_save(*g_app);
}

// Renames the magician (UTF-8, cut to kMaxNameBytes on a character boundary).
EMSCRIPTEN_KEEPALIVE void monstres_set_name(const char* name) {
  std::string n = name ? name : "";
  if (n.size() > kMaxNameBytes) {
    size_t cut = kMaxNameBytes;
    while (cut > 0 && (static_cast<unsigned char>(n[cut]) & 0xC0) == 0x80) --cut;  // not inside a character
    n.resize(cut);
  }
  g_app->world.wizard.name = n;
  write_save(*g_app);
}

EMSCRIPTEN_KEEPALIVE const char* monstres_wizard_json() {
  g_app->json = wizard_json(g_app->world);
  return g_app->json.c_str();
}

EMSCRIPTEN_KEEPALIVE const char* monstres_bestiary_json() {
  g_app->json = bestiary_json(g_app->world);
  return g_app->json.c_str();
}

EMSCRIPTEN_KEEPALIVE const char* monstres_map_json() {
  g_app->json = map_json(g_app->world);
  return g_app->json.c_str();
}

// Heights of the ground, Heightmap::kPoints squared floats (see map_json).
EMSCRIPTEN_KEEPALIVE const float* monstres_heights() { return g_app->world.ground.heights.data(); }

// Draws a portrait of `size` x `size` pixels: species index, or -1 for the
// magician; `look` is the genes of a monster (genes_code, as in the JSON).
// Returns the RGBA pixels (rows from the top), valid until the next call.
EMSCRIPTEN_KEEPALIVE const uint8_t* monstres_portrait(int species, int look, int size) {
  PortraitSubject subject;
  subject.wizard = species < 0;
  subject.species = static_cast<Species>(std::clamp(species, 0, static_cast<int>(Species::Count) - 1));
  const Genes genes = genes_from_code(static_cast<uint32_t>(look));
  if (valid_genes(subject.species, genes)) subject.genes = genes;
  Portrait p = build_portrait(subject, 1.0f);
  const DrawList* lists[] = {&p.list};
  g_app->portrait = g_app->renderer->draw_offscreen(p.view, lists, size, size);
  g_app->redraw = true;  // the screen's viewport has to be set again
  return g_app->portrait.data();
}

EMSCRIPTEN_KEEPALIVE void monstres_select(int index) {
  const bool valid = index >= 0 && static_cast<size_t>(index) < g_app->world.creatures.size();
  g_app->selected = valid ? index : -1;
  g_app->redraw = true;
}

// The selected creature as JSON (see creature_json), or "null".
EMSCRIPTEN_KEEPALIVE const char* monstres_selected_json() {
  g_app->json = creature_json(g_app->world, g_app->selected);
  return g_app->json.c_str();
}

// Where creatures[index] is drawn, as JSON {"x", "y"} in normalized device
// coordinates, or "null" (tests click on it).
EMSCRIPTEN_KEEPALIVE const char* monstres_creature_screen_json(int index) {
  float x = 0, y = 0;
  if (!creature_on_screen(g_app->world, current_view(*g_app, 0), index, x, y)) {
    g_app->json = "null";
  } else {
    char buf[64];
    std::snprintf(buf, sizeof buf, "{\"x\":%.4f,\"y\":%.4f}", static_cast<double>(x), static_cast<double>(y));
    g_app->json = buf;
  }
  return g_app->json.c_str();
}

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
  world_changed(*g_app);
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
  std::vector<MeshData> meshes;
  std::string error;
  if (!load_models(kModelsDir, meshes, &error)) {
    js_fatal(("Impossible de charger les modèles : " + error).c_str());
    return 1;
  }
  app.renderer = make_gles3_renderer(meshes);
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
  world_changed(app);

  app.last_time = emscripten_get_now();
  emscripten_set_main_loop_arg(frame, &app, 0, false);
  js_ready();
  return 0;
}
