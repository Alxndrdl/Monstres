// Browser entry point: main loop, saving to IndexedDB, and the small C API
// called from JavaScript (post.js, web/controls.js).
//
// Input flow: web/controls.js reads the joystick, the jump button, screen
// drags and the keyboard, and calls monstres_set_move / monstres_look /
// monstres_jump. Each fixed tick turns that into a monstres::Input.

#include "gles3_renderer.hpp"
#include "monstres/core/battle.hpp"
#include "monstres/core/debug.hpp"
#include "monstres/core/interior.hpp"
#include "monstres/core/save.hpp"
#include "monstres/core/variants.hpp"
#include "monstres/core/weather.hpp"
#include "monstres/core/world.hpp"
#include "monstres/render/cull.hpp"
#include "monstres/render/draw_plan.hpp"
#include "monstres/render/models.hpp"
#include "monstres/render/terrain_mesh.hpp"
#include "monstres/render/far_terrain.hpp"
#include "monstres/render/scene.hpp"
#include "monstres/render/weather_view.hpp"
#include "monstres/render/intro.hpp"
#include "monstres/render/interior_mesh.hpp"

#include <emscripten.h>
#include <emscripten/html5.h>
#include <sys/stat.h>

#include <algorithm>
#include <optional>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

using namespace monstres;

namespace {

constexpr const char* kModelsDir = "/models";  // preloaded from assets/models
// The saves: kSaveSlots games, each in its file (the first one's name is
// from when there was one only); kCurrentPath tells which one is played.
constexpr int kSaveSlots = 3;
constexpr const char* kSavePaths[kSaveSlots] = {"/save/game.sav", "/save/game2.sav", "/save/game3.sav"};
constexpr const char* kSaveTmpPath = "/save/game.sav.tmp";
constexpr const char* kCurrentPath = "/save/current";
constexpr double kTickMs = 1000.0 / rules::kTicksPerSecond;
constexpr uint64_t kAutosaveEveryTicks = 5 * rules::kTicksPerSecond;
constexpr int kJumpBufferTicks = 6;  // a jump pressed just before landing still counts
constexpr int kMaxTicksPerFrame = 10;
constexpr int kMaxPortraitSize = 1024;        // pixels a side (the page asks for 96 to 256)
constexpr int kMaxSaveBytes = 8 * 1024 * 1024;  // an imported file bigger than this is not a save (post.js)

// Where the time of a frame goes (settings menu, "détail du temps de calcul"):
// sums and worst values over half a second.
enum ProfilePart { kSim, kSave, kLandscape, kScene, kView, kDraw, kWasm, kInterval, kProfileParts };
struct Profile {
  bool on = false;
  double since = 0;  // start of the current half second
  int frames = 0;
  int ticks = 0;
  int landscape_builds = 0;
  int chunks_prepared = 0;
  int tile_swaps = 0;
  double sum[kProfileParts] = {};
  double worst[kProfileParts] = {};
  std::string json = "null";  // the last half second
};

struct App {
  World world;
  std::unique_ptr<Renderer> renderer;
  DrawList terrain;     // the ground (coarse, and detailed tiles around the player), the path, the water
  DrawList landscape;   // objects, grass: rebuilt around the player as it walks
  DrawList dynamic;     // player, clouds: rebuilt every frame
  DrawList interior;    // inside (interior.hpp): its walls and objects, built when going in
  uint64_t door_tick = ~0ULL;  // World::door_tick when the place was last set up
  Continent continent;  // the land beyond the playable area (built once per seed)
  GroundColors ground_colors;    // the ground's colors, kept as the tiles are built
  GroundTiles ground_tiles;      // the detailed tiles drawn, built ahead of time
  std::vector<Part> path_parts, water_parts;  // for culling the terrain list
  LandscapeCache landscape_cache;  // the landscape by chunks, computed ahead of time
  // Resolution: device pixels per CSS pixel, at most kPixelBudget pixels,
  // lowered (down to kMinScale) when frames come too slowly.
  double render_scale = 1;       // share of the best resolution, adapted
  bool adaptive = true;          // settings menu: lower it when frames are slow
  double slow_since = 0, fast_since = 0;  // when frames started being too slow / fast enough
  double interval_avg = 16.7;    // smoothed time between frames (ms)
  double scale_ceiling = 2, scale_ceiling_time = 0;  // the last resolution found too slow, and when
  DrawList far_landscape;  // its ground and waters (far pass)
  DrawList far_dynamic;    // the volcano's smoke (far pass)
  double continent_ms = 0; // how long building it took (profile)
  Camera camera;
  float move_x = 0, move_y = 0;  // -1..1, set by JavaScript
  int jump_buffer = 0;           // ticks left during which a jump press is pending
  int attack_buffer = 0;         // same for an attack (battle)
  uint8_t attack_slot = 0;       // 1..3
  int parry_buffer = 0;
  bool jump_held = false;        // the jump button or key is down (a flying companion climbs)
  bool hold_clock = false;       // debug: the time of day stands still
  uint32_t held_clock = 0;
  Command command = Command::None;  // sent with the next tick
  uint16_t command_target = 0;
  std::vector<std::pair<Command, uint16_t>> queued;  // more in the same frame (two runes touched at once): one per tick
  bool frozen = false;           // ?frozen: no automatic ticks (tests, screenshots)
  bool redraw = true;            // frozen mode only redraws when something changed
  int selected = -1;             // creature shown in the info panel (-1: none)
  uint32_t selected_id = 0;      // its Creature::id: creatures come and go, indices move
  Vec2 landscape_center{1e9f, 1e9f};  // where the detailed landscape was built around
  uint64_t camera_tick = 0;      // the camera moved on until this tick
  // The weather (debug menu): -1 natural, a WeatherKind chosen around the
  // player, or kWeatherAfar (fine here, a storm ahead). What is chosen is
  // reached in a moment (forced_now), at once when frozen.
  int weather_choice = -1;
  Weather forced_now;
  float forced_amount = 0;
  Vec2 afar_at;
  Profile profile;
  bool paused = false;           // a game menu is open: time stands still
  std::vector<uint8_t> portrait; // last portrait drawn for the page (RGBA)
  int canvas_w = 0, canvas_h = 0;
  double last_time = 0;
  double accumulator = 0;
  uint64_t last_saved_tick = 0;
  int slot = 0;                  // the save played (kSavePaths)
  // No save yet (the very first visit): a world is shown behind the
  // character creation, but not saved; the first new game replaces it.
  bool placeholder = false;
  // The arrival (intro.hpp), after a new game: how far into it (ms).
  bool intro = false;
  double intro_ms = 0;
  bool covered = false;  // the page covers the view (the character creation): not drawn
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

// What JavaScript sends is not trusted to be a number (NaN would spread to
// the camera and the player's place).
float finite_or_zero(float v) { return std::isfinite(v) ? v : 0.0f; }

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
  in.jump_held = app.jump_held;
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
  if (in.attack && (ally.attack_tick == w.tick || ally.state == FighterState::Windup)) app.attack_buffer = 0;
  if (app.attack_buffer > 0) --app.attack_buffer;
  if (in.parry && ally.state == FighterState::Parry) app.parry_buffer = 0;
  if (app.parry_buffer > 0) --app.parry_buffer;
  app.command = Command::None;
  if (!app.queued.empty()) {
    app.command = app.queued.front().first;
    app.command_target = app.queued.front().second;
    app.queued.erase(app.queued.begin());
  }
  // Creatures come and go: follow the selected one by its id.
  if (w.creatures.size() != creatures && app.selected >= 0) app.selected = find_creature(w, app.selected_id);
  if (w.battle.phase == BattlePhase::Fight || w.battle.phase == BattlePhase::Prepare) {
    app.selected = -1;  // the foes have their own rings
  }
}

void select(App& app, int index) {
  const bool valid = index >= 0 && static_cast<size_t>(index) < app.world.creatures.size();
  app.selected = valid ? index : -1;
  app.selected_id = valid ? app.world.creatures[static_cast<size_t>(index)].id : 0;
  app.redraw = true;
}

// In a fight the camera stays behind the companion, facing its enemy (or the magician, when training).
void follow_battle(App& app, double dt_ms) {
  float fx, fz;
  const BattlePhase phase = app.world.battle.phase;
  if ((phase != BattlePhase::Fight && phase != BattlePhase::Training && phase != BattlePhase::Prepare) ||
      !battle_forward(app.world, fx, fz)) {
    return;  // moving freely: the player turns the camera
  }
  const float target = std::atan2(-fx, -fz);  // forward = (-sin yaw, -cos yaw)
  const float k = std::min(1.0f, static_cast<float>(dt_ms) * 0.012f);
  Camera& c = app.camera;
  c.yaw = std::remainder(c.yaw + std::remainder(target - c.yaw, static_cast<float>(2 * kPi)) * k,
                         static_cast<float>(2 * kPi));
  app.redraw = true;
}

constexpr int kWeatherAfar = kWeatherKinds;  // debug: the sun here, a storm ahead

// The weather now: the world's, with what the debug menu chose.
WeatherField weather_field(const App& app) {
  WeatherField f;
  f.seed = app.world.seed;
  f.tick = app.world.tick;
  f.center = {app.world.player.pos.x, app.world.player.pos.z};
  f.forced = app.forced_now;
  f.forced_amount = app.forced_amount;
  f.afar = app.weather_choice == kWeatherAfar;
  f.afar_at = app.afar_at;
  return f;
}

// The weather chosen in the debug menu comes in a second and a half (at once when frozen).
void follow_weather_choice(App& app, double dt_ms) {
  const int choice = app.weather_choice;
  const float target = choice >= 0 ? 1.0f : 0.0f;
  const Weather goal = choice == kWeatherAfar ? weather_of(WeatherKind::Clear)
                       : choice >= 0         ? weather_of(static_cast<WeatherKind>(choice))
                                             : app.forced_now;
  const float k = app.frozen ? 1.0f : static_cast<float>(std::min(1.0, dt_ms / 1500.0 * 3));
  if (app.forced_amount == 0 && target > 0) app.forced_now = goal;  // from the natural weather: no blend of kinds
  app.forced_now = mix(app.forced_now, goal, k);
  app.forced_amount += (target - app.forced_amount) * k;
  if (app.forced_amount < 0.002f && target == 0) app.forced_amount = 0;
}

constexpr double kBackgroundMs = 2.0;  // each frame, at most this long building ahead (tiles, chunks)

// The detailed ground and landscape follow the player: built ahead of time,
// a little each frame, and swapped in once ready. `all_now`: build what is
// needed right away (a new world). True when the landscape was rebuilt.
bool update_landscape(App& app, bool all_now = false) {
  const World& w = app.world;
  const double start = emscripten_get_now();
  const double budget = all_now ? 1e9 : kBackgroundMs;
  int prepared = 0;
  const bool rebuilt = follow_player(w, app.landscape, app.landscape_cache, app.landscape_center, budget, all_now, &prepared);
  if (rebuilt) app.redraw = true;
  if (app.profile.on) app.profile.chunks_prepared += prepared;
  const double left = std::max(0.2, budget - (emscripten_get_now() - start));
  if (app.ground_tiles.update(w, app.ground_colors, {w.player.pos.x, w.player.pos.z}, left)) {
    // The drawn tiles in one mesh, the coarse ground's other tiles in order (GroundTiles).
    app.renderer->set_mesh(terrain::kDetailMesh, app.ground_tiles.detail());
    app.renderer->set_mesh_indices(static_cast<size_t>(MeshId::Ground), app.ground_tiles.coarse_indices());
    app.ground_tiles.fill_list(app.terrain, app.path_parts, app.water_parts);
    app.redraw = true;
    if (app.profile.on) ++app.profile.tile_swaps;
  }
  return rebuilt;
}

// Through a door (or a world loaded inside): the interior's walls and
// objects, or the landscape outside again; the camera turned the way the
// player goes.
void place_changed(App& app, bool through_door = true) {
  const World& w = app.world;
  app.door_tick = w.door_tick;
  app.selected = -1;
  app.redraw = true;
  app.camera.up = {0, 1, 0};
  if (through_door) app.camera.yaw = std::atan2(-w.player.facing_x, -w.player.facing_z);
  if (inside(w)) {
    app.renderer->set_mesh(static_cast<size_t>(MeshId::InteriorShell), build_interior_mesh(w));
    build_interior_list(w, app.interior);
  } else {
    app.renderer->set_mesh(static_cast<size_t>(MeshId::InteriorShell), MeshData{});
    app.interior.batches.clear();
    update_landscape(app, true);
  }
}

// Call whenever app.world is replaced (new game, loaded or imported save).
void world_changed(App& app) {
  app.redraw = true;
  app.selected = -1;
  app.camera.smooth = false;  // straight to its subject
  app.camera_tick = app.world.tick;
  app.landscape_center = {1e9f, 1e9f};
  app.ground_colors = {};  // another world: other colors
  app.landscape_cache.clear();
  // The ground outside (loaded inside: the world outside, as left).
  std::optional<World> outside;
  if (inside(app.world)) outside = outside_world(app.world);
  const World& out = outside ? *outside : app.world;
  const MeshData coarse = build_coarse_ground(out, app.ground_colors);
  app.ground_tiles.reset(coarse);
  app.renderer->set_mesh(static_cast<size_t>(MeshId::Ground), coarse);
  MeshData path = build_path_mesh(out);
  app.path_parts = path.parts;
  app.renderer->set_mesh(static_cast<size_t>(MeshId::Path), path);
  MeshData water = build_water_mesh(out);
  app.water_parts = water.parts;
  app.renderer->set_mesh(static_cast<size_t>(MeshId::Water), water);
  if (!inside(app.world)) update_landscape(app, true);
  place_changed(app, false);
  // The continent depends on the seed only: built again for another world.
  if (app.continent.empty() || app.continent.seed != app.world.seed) {
    const double start = emscripten_get_now();
    app.continent = make_continent(app.world.seed);
    const MeshData far_ground = build_far_ground_mesh(app.continent);
    const MeshData far_water = build_far_water_mesh(app.continent);
    app.renderer->set_mesh(static_cast<size_t>(MeshId::FarGround), far_ground);
    app.renderer->set_mesh(static_cast<size_t>(MeshId::FarWater), far_water);
    app.renderer->set_mesh(static_cast<size_t>(MeshId::Lava), build_lava_mesh(app.continent));
    build_far_landscape(app.far_landscape);
    app.far_landscape.mesh_parts.assign(kMeshCount, {});
    app.far_landscape.mesh_parts[static_cast<size_t>(MeshId::FarGround)] = far_ground.parts;
    app.far_landscape.mesh_parts[static_cast<size_t>(MeshId::FarWater)] = far_water.parts;
    app.continent_ms = emscripten_get_now() - start;
  }
}

bool valid_slot(int slot) { return slot >= 0 && slot < kSaveSlots; }

// Written beside, then renamed: a write cut short never damages the file.
bool write_file(const char* path, std::span<const uint8_t> bytes) {
  FILE* f = std::fopen(kSaveTmpPath, "wb");
  if (!f) return false;
  const bool ok = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
  std::fclose(f);
  if (ok && std::rename(kSaveTmpPath, path) == 0) return true;
  std::remove(kSaveTmpPath);
  return false;
}

bool read_file(const char* path, std::vector<uint8_t>& bytes) {
  bytes.clear();
  FILE* f = std::fopen(path, "rb");
  if (!f) return false;
  uint8_t buf[4096];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof buf, f)) > 0 && bytes.size() <= static_cast<size_t>(kMaxSaveBytes)) {
    bytes.insert(bytes.end(), buf, buf + n);
  }
  std::fclose(f);
  return true;
}

void write_save(App& app) {
  // Even when it fails (storage full or refused): the next try is the next
  // autosave, not every frame.
  app.last_saved_tick = app.world.tick;
  if (app.placeholder) return;  // not a game of the player's yet
  if (write_file(kSavePaths[app.slot], serialize(app.world))) js_sync_save();
}

// Which save is played, kept for the next visit.
void write_current_slot(const App& app) {
  const uint8_t digit = static_cast<uint8_t>('1' + app.slot);
  if (write_file(kCurrentPath, {&digit, 1})) js_sync_save();
}

int read_current_slot() {
  std::vector<uint8_t> bytes;
  if (!read_file(kCurrentPath, bytes) || bytes.size() != 1) return 0;
  const int slot = bytes[0] - '1';
  return valid_slot(slot) ? slot : 0;
}

bool read_save(World& out, int slot) {
  std::vector<uint8_t> bytes;
  if (!read_file(kSavePaths[slot], bytes)) return false;
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

// Half a second of measures, as JSON for the page.
void profile_report(App& app, double now, int pw, int ph) {
  Profile& p = app.profile;
  const double span = now - p.since;
  if (span < 500 || p.frames == 0) return;
  static const char* kNames[] = {"sim", "save", "landscape", "scene", "view", "draw", "wasm", "interval"};
  std::string s = "{\"frames\":" + std::to_string(p.frames) + ",\"seconds\":" + std::to_string(span / 1000);
  s += ",\"ticks\":" + std::to_string(p.ticks) + ",\"landscapeBuilds\":" + std::to_string(p.landscape_builds);
  s += ",\"continentMs\":" + std::to_string(app.continent_ms);
  s += ",\"chunksPrepared\":" + std::to_string(p.chunks_prepared) + ",\"tileSwaps\":" + std::to_string(p.tile_swaps);
  s += ",\"tilesBuilt\":" + std::to_string(app.ground_tiles.built());
  s += ",\"grassChunks\":" + std::to_string(app.landscape_cache.chunks_with_grass());
  s += ",\"renderScale\":" + std::to_string(app.render_scale);
  s += ",\"avg\":{";
  for (int i = 0; i < kProfileParts; ++i) {
    s += std::string(i ? "," : "") + "\"" + kNames[i] + "\":" + std::to_string(p.sum[i] / p.frames);
  }
  s += "},\"worst\":{";
  for (int i = 0; i < kProfileParts; ++i) s += std::string(i ? "," : "") + "\"" + kNames[i] + "\":" + std::to_string(p.worst[i]);
  const RenderStats& r = app.renderer->stats();
  s += "},\"drawCalls\":" + std::to_string(r.draw_calls) + ",\"instances\":" + std::to_string(r.instances);
  s += ",\"triangles\":" + std::to_string(r.triangles) + ",\"trianglesBy\":{";
  static const char* kGroups[] = {"terrain", "vegetation", "scenery", "characters", "effects", "sky"};
  for (size_t g = 0; g < kMeshGroups; ++g) {
    s += std::string(g ? "," : "") + "\"" + kGroups[g] + "\":" + std::to_string(r.triangles_by_group[g]);
  }
  s += "},\"uploadedKiB\":" + std::to_string(r.uploaded_bytes / 1024);
  s += ",\"gpuMs\":" + (r.gpu_ms >= 0 ? std::to_string(r.gpu_ms) : std::string("null"));
  s += ",\"pixels\":[" + std::to_string(pw) + "," + std::to_string(ph) + "]";
  s += ",\"dpr\":" + std::to_string(emscripten_get_device_pixel_ratio());
  size_t lights = 0;
  {
    const View v = current_view(app, 0);
    lights = v.light_count;
  }
  s += ",\"lights\":" + std::to_string(lights);
  s += ",\"creatures\":" + std::to_string(app.world.creatures.size());
  s += ",\"objects\":" + std::to_string(app.world.objects.size()) + "}";
  p.json = s;
  const bool on = p.on;
  p = {};
  p.on = on;
  p.since = now;
  p.json = s;
}

constexpr double kPixelBudget = 1.6e6;  // at most this many pixels drawn (phones have very dense screens)
constexpr double kMinScale = 0.6;       // the resolution goes down to this share of the best one when frames are slow
constexpr double kScaleStep = 0.85;

// Device pixels per CSS pixel: the screen's density, within the budget,
// times the adapted share.
double pixel_scale(const App& app, double css_w, double css_h) {
  const double dpr = emscripten_get_device_pixel_ratio();
  const double area = css_w * css_h;
  const double best = area > 0 ? std::min(dpr, std::sqrt(kPixelBudget / area)) : dpr;
  return best * app.render_scale;
}

// Lowers the resolution when frames come too slowly for a while (under 48
// per second), raises it again when they come fast enough; not above a
// resolution found too slow less than half a minute ago. Not when frozen
// (tests, pictures) nor paused.
void adapt_resolution(App& app, double dt, double now) {
  if (!app.adaptive) {
    app.render_scale = 1;
    return;
  }
  if (app.frozen || app.paused || dt <= 0 || dt > 250) return;  // a hidden tab, a pause
  app.interval_avg += (dt - app.interval_avg) * 0.05;
  if (app.interval_avg > 21.0) {
    app.fast_since = 0;
    if (app.slow_since == 0) app.slow_since = now;
  } else if (app.interval_avg < 17.8) {
    app.slow_since = 0;
    if (app.fast_since == 0) app.fast_since = now;
  } else {
    app.slow_since = app.fast_since = 0;
  }
  if (app.slow_since > 0 && now - app.slow_since > 1000 && app.render_scale > kMinScale) {
    app.scale_ceiling = app.render_scale;
    app.scale_ceiling_time = now;
    app.render_scale = std::max(kMinScale, app.render_scale * kScaleStep);
    app.slow_since = now;
    app.interval_avg = 19;  // measure again at the new resolution
  } else if (app.fast_since > 0 && now - app.fast_since > 4000 && app.render_scale < 1) {
    const double up = std::min(1.0, app.render_scale / kScaleStep);
    if (up < app.scale_ceiling - 1e-6 || now - app.scale_ceiling_time > 30000) {
      app.render_scale = up;
      app.interval_avg = 17;
    }
    app.fast_since = now;
  }
}

void frame(void* user) {
  App& app = *static_cast<App*>(user);

  const double now = emscripten_get_now();
  double dt = now - app.last_time;
  app.last_time = now;
  Profile& prof = app.profile;
  const bool profiling = prof.on;
  double mark = now;
  // Time since `mark`, added to a part of the profile.
  const auto measure = [&](ProfilePart part) {
    if (!profiling) return;
    const double t = emscripten_get_now();
    prof.sum[part] += t - mark;
    prof.worst[part] = std::max(prof.worst[part], t - mark);
    mark = t;
  };

  if (app.paused) app.accumulator = 0;
  if (!app.frozen && !app.paused) {
    app.accumulator = std::min(app.accumulator + dt, kTickMs * kMaxTicksPerFrame);
    while (app.accumulator >= kTickMs) {
      app.accumulator -= kTickMs;
      tick(app, current_input(app));
      if (profiling) ++prof.ticks;
    }
    measure(kSim);
    if (app.world.tick - app.last_saved_tick >= kAutosaveEveryTicks) write_save(app);
    measure(kSave);
  }
  if (!app.paused) {
    follow_battle(app, dt);
    // The camera moves on with the game's time (so that ?frozen pictures are stable).
    const uint64_t ticks = app.world.tick - std::min(app.camera_tick, app.world.tick);
    app.camera_tick = app.world.tick;
    if (ticks > 0 || !app.camera.smooth) {
      update_camera(app.camera, app.world, static_cast<float>(ticks) / rules::kTicksPerSecond);
      app.redraw = true;
    }
  }
  mark = emscripten_get_now();
  if (app.world.door_tick != app.door_tick) place_changed(app);  // through a door
  const bool in = inside(app.world);
  if (!in && update_landscape(app, app.frozen) && profiling) ++prof.landscape_builds;  // frozen: pictures right away
  measure(kLandscape);

  adapt_resolution(app, dt, now);
  double css_w = 0, css_h = 0;
  emscripten_get_element_css_size("#canvas", &css_w, &css_h);
  const double scale = pixel_scale(app, css_w, css_h);
  const int pw = std::max(1, static_cast<int>(css_w * scale));
  const int ph = std::max(1, static_cast<int>(css_h * scale));
  if (pw != app.canvas_w || ph != app.canvas_h) {
    // Setting the canvas size clears it, even to the same value: only on change.
    emscripten_set_canvas_element_size("#canvas", pw, ph);
    app.canvas_w = pw;
    app.canvas_h = ph;
    app.redraw = true;
  }
  if (app.frozen && !app.redraw) return;  // nothing moved: keep the last image
  if (app.covered) return;                // hidden behind the page: not drawn at all
  app.redraw = false;

  // Time follows the simulation, so ?frozen screenshots are reproducible.
  const float time = static_cast<float>(app.world.tick) / rules::kTicksPerSecond;
  mark = emscripten_get_now();
  View view = current_view(app, time);
  if (app.intro) {
    // The arrival's flight, ending where the game's camera is.
    if (!app.paused) app.intro_ms += app.frozen ? 0 : dt;
    const float seconds = static_cast<float>(app.intro_ms / 1000.0);
    const Shot last{{view.camera_x, view.camera_y, view.camera_z},
                    {app.world.player.pos.x, app.world.player.pos.y + 1.0f, app.world.player.pos.z}};
    const Shot shot = intro_shot(app.world, seconds, last);
    const float aspect = app.canvas_h > 0 ? static_cast<float>(app.canvas_w) / static_cast<float>(app.canvas_h) : 1.0f;
    view = build_view_from(app.world, shot.eye, shot.target, aspect, time);
    if (seconds >= kIntroSeconds) app.intro = false;
    app.redraw = true;
  }
  follow_weather_choice(app, dt);
  const WeatherField weather = weather_field(app);
  // Inside: no weather, no landscape, nothing far away; the interior.
  const float flash = in ? 0.0f : apply_weather(view, weather);
  // Only what the camera can see (cull.hpp).
  const Frustum in_front = frustum_of(view.view_projection);
  if (!in) {
    cull_list(app.terrain, in_front);
    cull_list(app.landscape, in_front);
    cull_list(app.far_landscape, frustum_of(view.far_view_projection));
  }
  measure(kView);
  build_dynamic(app.world, time, app.dynamic, app.selected, &in_front);
  if (!in) {
    build_far_dynamic(app.world, time, app.far_dynamic);
    build_weather(weather, view, flash, time, app.far_dynamic, app.dynamic);
  }
  measure(kScene);
  const DrawList* outdoors[] = {&app.far_landscape, &app.far_dynamic, &app.terrain, &app.landscape, &app.dynamic};
  const DrawList* indoors[] = {&app.interior, &app.dynamic};
  app.renderer->begin_frame({pw, ph});
  if (in) {
    app.renderer->draw(view, indoors);
  } else {
    app.renderer->draw(view, outdoors);
  }
  app.renderer->end_frame();
  measure(kDraw);
  if (profiling) {
    const double wasm = emscripten_get_now() - now;
    prof.sum[kWasm] += wasm;
    prof.worst[kWasm] = std::max(prof.worst[kWasm], wasm);
    prof.sum[kInterval] += dt;
    prof.worst[kInterval] = std::max(prof.worst[kInterval], dt);
    ++prof.frames;
    profile_report(app, emscripten_get_now(), pw, ph);
  }
}

// Another world is played, in the save `slot`: nothing pending from the one before.
void enter_world(App& app, World world, int slot) {
  app.world = std::move(world);
  app.slot = slot;
  app.placeholder = false;
  app.intro = false;
  app.command = Command::None;
  app.queued.clear();
  app.jump_buffer = app.attack_buffer = app.parry_buffer = 0;
  app.accumulator = 0;
  app.last_saved_tick = app.world.tick;
  write_current_slot(app);
  world_changed(app);
}

// A name as the game keeps it: at most kMaxNameBytes of UTF-8, cut on a
// character's boundary, without control characters.
std::string clean_name(std::string n) {
  std::erase_if(n, [](char c) { return static_cast<unsigned char>(c) < 0x20 || c == 0x7f; });
  if (n.size() > kMaxNameBytes) {
    size_t cut = kMaxNameBytes;
    while (cut > 0 && (static_cast<unsigned char>(n[cut]) & 0xC0) == 0x80) --cut;  // not inside a character
    n.resize(cut);
  }
  return n;
}

// The seed typed for a new world: a number as it is, any other text by its
// letters (FNV-1a), nothing: at random.
uint64_t seed_from_text(const std::string& text) {
  if (text.empty()) return random_seed();
  const bool digits = text.size() <= 19 && std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; });
  if (digits) return std::strtoull(text.c_str(), nullptr, 10);
  uint64_t h = 0xcbf29ce484222325ULL;
  for (const char c : text) h = (h ^ static_cast<unsigned char>(c)) * 0x100000001b3ULL;
  return h;
}

}  // namespace

// ---- C API used by JavaScript ----------------------------------------------

// A function returning JSON for the page: the text is valid until the next one.
#define JSON_EXPORT(name, expression)              \
  EMSCRIPTEN_KEEPALIVE const char* name() {        \
    g_app->json = (expression);                    \
    return g_app->json.c_str();                    \
  }

extern "C" {

// Joystick or keyboard direction: x right, y forward, each in -1..1.
EMSCRIPTEN_KEEPALIVE void monstres_set_move(float x, float y) {
  g_app->move_x = finite_or_zero(x);
  g_app->move_y = finite_or_zero(y);
}

// Rotates the camera by an angle in radians (screen drag or tests).
EMSCRIPTEN_KEEPALIVE void monstres_look(float delta_yaw, float delta_pitch) {
  Camera& c = g_app->camera;
  g_app->redraw = true;
  delta_yaw = finite_or_zero(delta_yaw);
  delta_pitch = finite_or_zero(delta_pitch);
  const BattlePhase phase = g_app->world.battle.phase;
  float fx, fz;
  if (phase == BattlePhase::Prepare || battle_forward(g_app->world, fx, fz)) delta_yaw = 0;  // locked on the target
  c.yaw = std::remainder(c.yaw + delta_yaw, static_cast<float>(2 * kPi));
  c.pitch = std::clamp(c.pitch + delta_pitch, kMinPitch, kMaxPitch);
}

EMSCRIPTEN_KEEPALIVE void monstres_set_camera(float yaw, float pitch) {
  if (!std::isfinite(yaw) || !std::isfinite(pitch)) return;
  g_app->redraw = true;
  g_app->camera.yaw = std::remainder(yaw, static_cast<float>(2 * kPi));
  g_app->camera.pitch = std::clamp(pitch, kMinPitch, kMaxPitch);
}

EMSCRIPTEN_KEEPALIVE void monstres_jump() { g_app->jump_buffer = kJumpBufferTicks; }
EMSCRIPTEN_KEEPALIVE void monstres_jump_held(int held) { g_app->jump_held = held != 0; }

// Battle buttons: attack 1 (quick), 2 and 3 (special); parry. The dodge is
// the jump.
EMSCRIPTEN_KEEPALIVE void monstres_attack(int slot) {
  if (slot < 1 || slot > 3) return;
  g_app->attack_slot = static_cast<uint8_t>(slot);
  g_app->attack_buffer = kJumpBufferTicks;
}

EMSCRIPTEN_KEEPALIVE void monstres_parry() { g_app->parry_buffer = kJumpBufferTicks; }

// A command (Command, by its number: the page reads the names once, see
// monstres_command_names_json and post.js) and its target (what it means is
// with each Command). Applied with the next tick (right away when frozen or paused).
JSON_EXPORT(monstres_command_names_json, command_names_json())

EMSCRIPTEN_KEEPALIVE void monstres_command(int command, int target) {
  if (command < 1 || command >= static_cast<int>(kCommandCount) || target < 0 || target > 65535) return;
  if (g_app->command != Command::None && !g_app->frozen && !g_app->paused) {
    if (g_app->queued.size() < 8) g_app->queued.emplace_back(static_cast<Command>(command), static_cast<uint16_t>(target));
    return;
  }
  g_app->command = static_cast<Command>(command);
  g_app->command_target = static_cast<uint16_t>(target);
  // Frozen (tests), or a menu open (time stands still: choosing spells,
  // letting a monster go): one tick, so that it happens now.
  if (g_app->frozen || g_app->paused) {
    tick(*g_app, current_input(*g_app));
    g_app->redraw = true;
  }
}

// Debug options (settings menu): the time of day, in hours (0..24), and
// whether it stands still.
EMSCRIPTEN_KEEPALIVE void monstres_set_hour(float hour) {
  if (!std::isfinite(hour)) return;
  const float h = std::fmod(std::fmod(hour, 24.0f) + 24.0f, 24.0f);
  g_app->world.clock = std::min(static_cast<uint32_t>(h * rules::kTicksPerHour), rules::kTicksPerDay - 1);
  g_app->held_clock = g_app->world.clock;
  g_app->redraw = true;
}

// Debug option (settings menu): the weather around the player. -1: the
// world's own; 0..6: a WeatherKind; 7: fine weather here and a storm about
// a kilometer ahead (where the camera looks now), to see one from afar.
EMSCRIPTEN_KEEPALIVE void monstres_set_weather(int choice) {
  App& app = *g_app;
  if (choice < -1 || choice > kWeatherAfar) return;
  app.weather_choice = choice;
  if (choice == kWeatherAfar) {
    const float fx = -std::sin(app.camera.yaw), fz = -std::cos(app.camera.yaw);
    app.afar_at = {app.world.player.pos.x + fx * weather::kAfarDistance, app.world.player.pos.z + fz * weather::kAfarDistance};
  }
  if (app.frozen) follow_weather_choice(app, 0);  // at once (tests, pictures)
  app.redraw = true;
}

EMSCRIPTEN_KEEPALIVE void monstres_hold_hour(int hold) {
  g_app->hold_clock = hold != 0;
  g_app->held_clock = g_app->world.clock;
}

JSON_EXPORT(monstres_team_json, team_json(g_app->world))

JSON_EXPORT(monstres_battle_json, battle_json(g_app->world))

EMSCRIPTEN_KEEPALIVE const char* monstres_state_json() {
  const Camera& c = g_app->camera;
  std::string json = to_json(g_app->world);
  json.pop_back();  // add the camera, which lives here and not in the world
  char camera[128];
  std::snprintf(camera, sizeof camera, ",\"camera\":{\"yaw\":%.4f,\"pitch\":%.4f},\"active\":%d,",
                static_cast<double>(c.yaw), static_cast<double>(c.pitch), g_app->world.active);
  // The arrival's flight, the first visit (no game of the player's yet).
  const std::string arrival = std::string("\"intro\":") + (g_app->intro ? "true" : "false") + ",\"placeholder\":" +
                              (g_app->placeholder ? "true" : "false") + ",";
  // The weather where the player is (and the debug menu's choice).
  const WeatherField field = weather_field(*g_app);
  const Weather w = field.at(g_app->world.player.pos.x, g_app->world.player.pos.z);
  const Lightning bolt = lightning_at(field, g_app->world.player.pos.x, g_app->world.player.pos.z);
  char weather[384];
  std::snprintf(weather, sizeof weather,
                "\"weather\":{\"kind\":\"%s\",\"choice\":%d,\"cover\":%.3f,\"dark\":%.3f,\"rain\":%.3f,"
                "\"snow\":%.3f,\"fog\":%.3f,\"storm\":%.3f,\"wind\":%.3f,\"bolt\":%s}}",
                weather_name(kind_of(w)), g_app->weather_choice, static_cast<double>(w.cover),
                static_cast<double>(w.dark), static_cast<double>(w.rain), static_cast<double>(w.snow),
                static_cast<double>(w.fog), static_cast<double>(w.storm), static_cast<double>(w.wind),
                bolt.on ? "true" : "false");
  g_app->json = json + camera + arrival + weather;
  return g_app->json.c_str();
}

// Runs `ticks` ticks with the current joystick/camera input (tests, ?frozen).
EMSCRIPTEN_KEEPALIVE double monstres_debug_step(int ticks) {
  for (int i = 0; i < ticks; ++i) tick(*g_app, current_input(*g_app));
  g_app->redraw = true;
  return static_cast<double>(g_app->world.tick);
}

EMSCRIPTEN_KEEPALIVE void monstres_save_now() { write_save(*g_app); }

// Debug (tests, screenshots): puts the player at (x, z) on the ground, its
// companion beside it; the squares around fill with monsters.
EMSCRIPTEN_KEEPALIVE void monstres_debug_teleport(float x, float z) {
  if (!std::isfinite(x) || !std::isfinite(z)) return;
  World& w = g_app->world;
  const float limit = rules::kWorldHalfSize - 1;
  x = std::clamp(x, -limit, limit);
  z = std::clamp(z, -limit, limit);
  w.player.pos = {x, w.ground.height(x, z), z};
  if (inside(w)) {  // on top of what stands there (the tower's stair, its landing)
    for_objects_near(w, {{x - 0.1f, -1, z - 0.1f}, {x + 0.1f, 50, z + 0.1f}}, [&](uint32_t, const Box& b) {
      if (b.min.x < x && b.max.x > x && b.min.z < z && b.max.z > z) w.player.pos.y = std::max(w.player.pos.y, b.max.y);
    });
  }
  w.player.velocity_y = 0;
  if (!w.creatures.empty()) {
    Body& c = w.creatures[0].body;
    c.pos = {x + 0.8f, w.ground.height(x + 0.8f, z + 1.2f), z + 1.2f};
  }
  update_population(w);
  g_app->camera.smooth = false;
  g_app->redraw = true;
}

// Selects the creature under a point of the canvas (normalized device
// coordinates, -1..1, y up), or nothing. Returns its index or -1.
EMSCRIPTEN_KEEPALIVE int monstres_pick(float ndc_x, float ndc_y) {
  select(*g_app, pick_creature(g_app->world, current_view(*g_app, 0), ndc_x, ndc_y));
  return g_app->selected;
}

// The signpost or teleporter under a point of the canvas (objects index), or -1.
EMSCRIPTEN_KEEPALIVE int monstres_pick_object(float ndc_x, float ndc_y) {
  return pick_object(g_app->world, current_view(*g_app, 0), ndc_x, ndc_y);
}

EMSCRIPTEN_KEEPALIVE const char* monstres_object_json(int index) {
  g_app->json = object_json(g_app->world, index);
  return g_app->json.c_str();
}

JSON_EXPORT(monstres_teleporters_json, teleporters_json(g_app->world))

JSON_EXPORT(monstres_spells_json, spells_json(g_app->world))

JSON_EXPORT(monstres_quests_json, quests_json(g_app->world))
JSON_EXPORT(monstres_bag_json, bag_json(g_app->world))
JSON_EXPORT(monstres_farm_json, farm_json(g_app->world))
JSON_EXPORT(monstres_shop_json, shop_json(g_app->world))

// Frame profiler (settings menu): on or off, and the last half second as JSON ("null" before).
EMSCRIPTEN_KEEPALIVE void monstres_set_profiling(int on) {
  Profile& p = g_app->profile;
  p = {};
  p.on = on != 0;
  p.since = emscripten_get_now();
  g_app->renderer->set_profiling(p.on);
}

// Settings menu: whether the resolution goes down when frames are slow.
EMSCRIPTEN_KEEPALIVE void monstres_set_adaptive_resolution(int on) {
  g_app->adaptive = on != 0;
  g_app->slow_since = g_app->fast_since = 0;
  g_app->redraw = true;
}

// Settings menu, debug: forward or deferred shading, to compare (bit 1: deferred shading).
EMSCRIPTEN_KEEPALIVE void monstres_set_render_options(int bits) {
  g_app->renderer->set_options(render_options_from_bits(bits));
  g_app->redraw = true;
}

EMSCRIPTEN_KEEPALIVE const char* monstres_profile_json() { return g_app->profile.json.c_str(); }

// Game menus: time stands still while one is open (saved when it opens).
EMSCRIPTEN_KEEPALIVE void monstres_set_paused(int paused) {
  g_app->paused = paused != 0;
  if (g_app->paused) write_save(*g_app);
}

// ---- The saves (settings menu): kSaveSlots games, one of them played ----

// [{"slot", "empty"} or {"slot", "current", "valid", "name", "level", "bytes",
// "time" (when it was written, milliseconds since 1970), "seed" (a text: 64 bits)}].
EMSCRIPTEN_KEEPALIVE const char* monstres_saves_json() {
  std::string s = "[";
  std::vector<uint8_t> bytes;
  for (int slot = 0; slot < kSaveSlots; ++slot) {
    s += std::string(slot ? "," : "") + "{\"slot\":" + std::to_string(slot);
    struct stat st {};
    if (!read_file(kSavePaths[slot], bytes) || stat(kSavePaths[slot], &st) != 0) {
      s += ",\"empty\":true}";
      continue;
    }
    const auto summary = peek_save(bytes);
    s += std::string(",\"current\":") + (slot == g_app->slot ? "true" : "false");
    s += std::string(",\"valid\":") + (summary ? "true" : "false");
    s += ",\"bytes\":" + std::to_string(bytes.size());
    s += ",\"time\":" + std::to_string(static_cast<long long>(st.st_mtime) * 1000);
    if (summary) {
      s += ",\"name\":" + json_string(summary->name) + ",\"level\":" + std::to_string(summary->level);
      s += ",\"seed\":\"" + std::to_string(summary->seed) + "\"";
    }
    s += "}";
  }
  g_app->json = s + "]";
  return g_app->json.c_str();
}

// Plays the save of another slot (the one played is saved first). 1 when done.
EMSCRIPTEN_KEEPALIVE int monstres_play_slot(int slot) {
  App& app = *g_app;
  if (!valid_slot(slot)) return 0;
  if (slot == app.slot) return 1;
  World world;
  if (!read_save(world, slot)) return 0;
  write_save(app);
  enter_world(app, std::move(world), slot);
  return 1;
}

// Removes a save: not the one played (another one is played first).
EMSCRIPTEN_KEEPALIVE int monstres_delete_slot(int slot) {
  if (!valid_slot(slot) || slot == g_app->slot || std::remove(kSavePaths[slot]) != 0) return 0;
  js_sync_save();
  return 1;
}

// A new game in an empty slot (the character creation), played right away,
// after the arrival's flight: the magician's name (UTF-8, cut to
// kMaxNameBytes), the world's seed (a number; any other text gives a number
// too; empty: drawn at random), its look (look_code) and its first monster
// (a Species among kStarters). None of them changes afterwards.
EMSCRIPTEN_KEEPALIVE int monstres_new_game(int slot, const char* name, const char* seed, int look, int starter) {
  App& app = *g_app;
  std::vector<uint8_t> bytes;
  const bool free = !read_file(kSavePaths[slot < 0 ? 0 : slot % kSaveSlots], bytes);
  if (!valid_slot(slot) || !free || (slot == app.slot && !app.placeholder)) return 0;
  const WizardLook chosen = look_from_code(static_cast<uint32_t>(look));
  if (!valid_look(chosen) || starter < 0 || starter >= static_cast<int>(Species::Count) ||
      !valid_starter(static_cast<Species>(starter))) {
    return 0;
  }
  write_save(app);
  NewGame choices;
  choices.name = clean_name(name ? name : "");
  choices.look = chosen;
  choices.starter = static_cast<Species>(starter);
  World world = new_game(seed_from_text(seed ? seed : ""), choices);
  enter_world(app, std::move(world), slot);
  write_save(app);
  app.intro = true;
  app.intro_ms = 0;
  return 1;
}

// The page covers the whole view (the character creation): the world is not drawn.
EMSCRIPTEN_KEEPALIVE void monstres_set_covered(int covered) {
  g_app->covered = covered != 0;
  g_app->redraw = true;
}

// The very first visit: no save, the character creation must come first.
EMSCRIPTEN_KEEPALIVE int monstres_needs_creation() { return g_app->placeholder ? 1 : 0; }

// The arrival's flight: skipped (a button), or whether it is on.
EMSCRIPTEN_KEEPALIVE void monstres_skip_intro() {
  g_app->intro = false;
  g_app->redraw = true;
}
EMSCRIPTEN_KEEPALIVE int monstres_intro_on() { return g_app->intro ? 1 : 0; }

// The character creation's picture of the magician (look_code; flags: 1
// without its hat or hood), seen from where the player turns, zooms and
// moves the camera; `time` (seconds) makes it breathe. RGBA pixels, rows
// from the top, valid until the next portrait.
EMSCRIPTEN_KEEPALIVE const uint8_t* monstres_wizard_preview(int look, int flags, float yaw, float pitch, float zoom,
                                                           float pan_x, float pan_y, float time, int width,
                                                           int height) {
  width = std::clamp(width, 1, kMaxPortraitSize);
  height = std::clamp(height, 1, kMaxPortraitSize);
  PortraitSubject subject;
  subject.wizard = true;
  const WizardLook l = look_from_code(static_cast<uint32_t>(look));
  if (valid_look(l)) subject.look = l;
  subject.bare_head = (flags & 1) != 0;
  PreviewCamera cam;
  cam.yaw = finite_or_zero(yaw);
  cam.pitch = finite_or_zero(pitch);
  cam.zoom = std::isfinite(zoom) ? zoom : 1;
  cam.pan_x = std::clamp(finite_or_zero(pan_x), -1.0f, 1.0f);
  cam.pan_y = std::clamp(finite_or_zero(pan_y), -1.0f, 1.0f);
  Portrait p = build_preview(subject, static_cast<float>(width) / static_cast<float>(height), cam, finite_or_zero(time));
  const DrawList* lists[] = {&p.list};
  g_app->portrait = g_app->renderer->draw_offscreen(p.view, lists, width, height);
  g_app->redraw = true;
  return g_app->portrait.data();
}

JSON_EXPORT(monstres_wizard_json, wizard_json(g_app->world))

JSON_EXPORT(monstres_marks_json, marks_json(g_app->world))

JSON_EXPORT(monstres_bestiary_json, bestiary_json(g_app->world))

// The map: of the world outside (inside: as it was left).
JSON_EXPORT(monstres_map_json, map_json(inside(g_app->world) ? outside_world(g_app->world) : g_app->world))

JSON_EXPORT(monstres_continent_json, continent_json(g_app->continent))

// Heights of the continent, Continent::kPointsX * kPointsZ floats (see continent_json).
EMSCRIPTEN_KEEPALIVE const float* monstres_continent_heights() { return g_app->continent.heights.data(); }

// Heights of the ground, Heightmap::kPoints squared floats (see map_json).
EMSCRIPTEN_KEEPALIVE const float* monstres_heights() {
  const World& w = *&g_app->world;
  return (inside(w) ? w.outdoors.ground : w.ground).heights.data();
}

// Draws a portrait of `size` x `size` pixels: species index, or -1 for the
// magician; `look` is the genes of a monster (genes_code, as in the JSON).
// Returns the RGBA pixels (rows from the top), valid until the next call.
EMSCRIPTEN_KEEPALIVE const uint8_t* monstres_portrait(int species, int look, int size) {
  size = std::clamp(size, 1, kMaxPortraitSize);  // post.js reads size * size pixels
  PortraitSubject subject;
  subject.wizard = species < 0;
  subject.look = g_app->world.wizard.look;  // the magician's portrait: as the player made it
  subject.species = static_cast<Species>(std::clamp(species, 0, static_cast<int>(Species::Count) - 1));
  const Genes genes = genes_from_code(static_cast<uint32_t>(look));
  if (valid_genes(subject.species, genes)) subject.genes = genes;
  Portrait p = build_portrait(subject, 1.0f);
  const DrawList* lists[] = {&p.list};
  g_app->portrait = g_app->renderer->draw_offscreen(p.view, lists, size, size);
  g_app->redraw = true;  // the screen's viewport has to be set again
  return g_app->portrait.data();
}

EMSCRIPTEN_KEEPALIVE void monstres_select(int index) { select(*g_app, index); }

// The selected creature as JSON (see creature_json), or "null".
JSON_EXPORT(monstres_selected_json, creature_json(g_app->world, g_app->selected))

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

// The save of a slot, ready to be copied out (monstres_export_data): its size, 0 if there is none.
EMSCRIPTEN_KEEPALIVE int monstres_export_prepare(int slot) {
  App& app = *g_app;
  if (!valid_slot(slot)) return 0;
  if (slot == app.slot) {
    app.export_buffer = serialize(app.world);
  } else if (!read_file(kSavePaths[slot], app.export_buffer)) {
    app.export_buffer.clear();
  }
  return static_cast<int>(app.export_buffer.size());
}

EMSCRIPTEN_KEEPALIVE const uint8_t* monstres_export_data() { return g_app->export_buffer.data(); }

// A save file brought in, into an empty slot (it is not played yet). The
// file is checked whole; kept as it is, whatever its version.
EMSCRIPTEN_KEEPALIVE int monstres_import_save(const uint8_t* data, int size, int slot) {
  if (!data || size <= 0 || size > kMaxSaveBytes || !valid_slot(slot) || slot == g_app->slot) return 0;
  std::vector<uint8_t> there;
  if (read_file(kSavePaths[slot], there)) return 0;  // never over a save
  const std::span<const uint8_t> bytes{data, static_cast<size_t>(size)};
  if (!peek_save(bytes) || !write_file(kSavePaths[slot], bytes)) return 0;
  js_sync_save();
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
  set_mesh_bounds(meshes);  // for culling
  app.renderer = make_gles3_renderer(meshes);
  if (!app.renderer) {
    js_fatal("Impossible d'initialiser le rendu (shaders).");
    return 1;
  }

  // The save folder was loaded from IndexedDB in pre.js before main() ran.
  app.slot = read_current_slot();
  if (force_new || !read_save(app.world, app.slot)) {
    app.world = new_game(has_seed ? seed : random_seed());
    // No save at all and nothing asked by the address: the first visit, the
    // character creation comes first (this world is only seen behind it).
    std::vector<uint8_t> any;
    bool saved = false;
    for (int s = 0; s < kSaveSlots && !saved; ++s) saved = read_file(kSavePaths[s], any);
    app.placeholder = !force_new && !has_seed && !saved;
    write_save(app);
  }
  app.last_saved_tick = app.world.tick;
  world_changed(app);

  app.last_time = emscripten_get_now();
  emscripten_set_main_loop_arg(frame, &app, 0, false);
  js_ready();
  return 0;
}
