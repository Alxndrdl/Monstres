#include "monstres/core/weather.hpp"
#include "monstres/core/world.hpp"
#include "monstres/render/scene.hpp"
#include "monstres/render/weather_view.hpp"
#include "test_helpers.hpp"

#include <cmath>
#include <set>

using namespace monstres;

namespace {

constexpr uint64_t kMinute = 60ULL * rules::kTicksPerSecond;

WeatherField field_at(uint64_t seed, uint64_t tick, Season season = Season::Summer) {
  WeatherField f;
  f.seed = seed;
  f.tick = tick;
  f.season = season;
  return f;
}

// Everywhere the weather chosen (debug menu), at once.
WeatherField forced(WeatherKind kind, uint64_t tick = 30 * kMinute) {
  WeatherField f = field_at(42, tick);
  f.forced = weather_of(kind);
  f.forced_amount = 1;
  return f;
}

size_t count(const DrawList& list, MeshId id) {
  const auto i = static_cast<size_t>(id);
  return i < list.batches.size() ? list.batches[i].size() : 0;
}

size_t clouds(const DrawList& list) {
  size_t n = 0;
  for (size_t v = 0; v < kCloudVariants; ++v) {
    n += count(list, mesh_offset(MeshId::Cloud0, v)) + count(list, mesh_offset(MeshId::CloudLow0, v));
  }
  return n;
}

// A view at noon from the start, looking along the horizon, with the weather of `field`.
struct Scene {
  View view;
  float flash = 0;
  DrawList far_list, near_list;
};
Scene look_at_sky(const WeatherField& field, float yaw = 0) {
  World w = cached_game(42);
  w.clock = 12 * rules::kTicksPerHour;
  Camera camera;
  camera.yaw = yaw;
  camera.pitch = 0.05f;
  Scene s;
  s.view = build_view(w, camera, 16.0f / 9.0f, 0);
  s.flash = apply_weather(s.view, field);
  build_weather(field, s.view, s.flash, 3.0f, s.far_list, s.near_list);
  return s;
}

}  // namespace

TEST_CASE("each kind of weather is itself, and they blend") {
  for (int k = 0; k < kWeatherKinds; ++k) {
    const auto kind = static_cast<WeatherKind>(k);
    CAPTURE(weather_name(kind));
    CHECK(kind_of(weather_of(kind)) == kind);
  }
  CHECK(weather_of(WeatherKind::Overcast).cover == 1.0f);  // the sky is not seen
  CHECK(weather_of(WeatherKind::Storm).storm == 1.0f);
  CHECK(weather_of(WeatherKind::Snow).rain == 0.0f);
  const Weather half = mix(weather_of(WeatherKind::Clear), weather_of(WeatherKind::Rain), 0.5f);
  CHECK(half.rain == doctest::Approx(0.4f));
}

TEST_CASE("the weather is the same for the same seed and time, and a new world starts in the sun") {
  const WeatherField a = field_at(7, 50 * kMinute), b = field_at(7, 50 * kMinute);
  CHECK(a.at(120, -340) == b.at(120, -340));
  // The first while of a world: clear or a few clouds everywhere.
  for (float x = -3000; x <= 3000; x += 250) {
    const WeatherKind k = kind_of(field_at(7, 0).at(x, x * 0.7f));
    CHECK((k == WeatherKind::Clear || k == WeatherKind::FewClouds));
  }
}

TEST_CASE("the weather differs from place to place, changes slowly, and every kind comes") {
  std::set<WeatherKind> seen;
  std::set<WeatherKind> here_and_now;
  const WeatherField f = field_at(3, 90 * kMinute);
  for (float x = -6000; x <= 6000; x += 400) {
    for (float z = -6000; z <= 6000; z += 400) here_and_now.insert(kind_of(f.at(x, z)));
  }
  CHECK(here_and_now.size() >= 4);  // rain over there, sun here...
  // Every kind in its season: snow in winter, rain the rest of the year.
  for (const Season season : {Season::Summer, Season::Winter}) {
    std::set<WeatherKind> in_season;
    for (uint64_t minute = 6; minute < 600; minute += 3) {
      const WeatherKind k = kind_of(field_at(3, minute * kMinute, season).at(0, 0));
      seen.insert(k);
      in_season.insert(k);
    }
    CHECK(in_season.count(WeatherKind::Snow) == (season == Season::Winter ? 1u : 0u));
    CHECK(in_season.count(WeatherKind::Rain) == (season == Season::Winter ? 0u : 1u));
  }
  CHECK(seen.size() == static_cast<size_t>(kWeatherKinds));
  // Small steps, small changes: no jump from a place or a second to the next.
  for (float x = -2000; x <= 2000; x += 37) {
    const Weather w0 = f.at(x, 300), w1 = f.at(x + 5, 300), w2 = field_at(3, 90 * kMinute + 60).at(x, 300);
    CHECK(std::fabs(w0.cover - w1.cover) < 0.05f);
    CHECK(std::fabs(w0.rain - w1.rain) < 0.05f);
    CHECK(std::fabs(w0.rain - w2.rain) < 0.05f);
  }
}

TEST_CASE("the weather drifts with the wind") {
  const Vec2 wind = wind_velocity(5);
  CHECK(std::hypot(wind.x, wind.z) == doctest::Approx(weather::kWindSpeed));
  // A minute later (within the same while), the weather seen at a place is
  // the one that was upwind of it.
  const uint64_t t0 = 100 * kMinute + 10 * rules::kTicksPerSecond;
  const WeatherField before = field_at(5, t0), after = field_at(5, t0 + 30 * rules::kTicksPerSecond);
  for (float x = -1500; x <= 1500; x += 300) {
    const Weather w0 = before.at(x, 200), w1 = after.at(x + wind.x * 30, 200 + wind.z * 30);
    CHECK(w0.cover == doctest::Approx(w1.cover).epsilon(0.02));
    CHECK(w0.rain == doctest::Approx(w1.rain).epsilon(0.02));
  }
}

TEST_CASE("the weather chosen in the debug menu is around the player, the world's own farther") {
  WeatherField f = field_at(3, 90 * kMinute);
  f.forced = weather_of(WeatherKind::Snow);
  f.forced_amount = 1;
  f.center = {100, -50};
  CHECK(f.at(100, -50) == weather_of(WeatherKind::Snow));
  CHECK(f.at(100 + weather::kForcedRadius - 10, -50) == weather_of(WeatherKind::Snow));
  const float far = weather::kForcedRadius + weather::kForcedFade + 10;
  CHECK(f.at(100 + far, -50) == f.natural(100 + far, -50));
  // A storm afar: fine weather here, a storm there.
  WeatherField g = forced(WeatherKind::Clear);
  g.afar = true;
  g.afar_at = {0, -weather::kAfarDistance};
  CHECK(kind_of(g.at(0, 0)) == WeatherKind::Clear);
  CHECK(kind_of(g.at(0, -weather::kAfarDistance)) == WeatherKind::Storm);
}

TEST_CASE("lightning strikes in storms only, now and then, briefly") {
  int bolts = 0, near = 0;
  const WeatherField storm = forced(WeatherKind::Storm);
  for (uint64_t t = 0; t < 120ULL * rules::kTicksPerSecond; ++t) {
    WeatherField f = storm;
    f.tick += t;
    const Lightning b = lightning_at(f, 0, 0);
    if (!b.on) continue;
    ++bolts;
    CHECK(b.brightness > 0);
    CHECK(f.at(b.x, b.z).storm >= 0.5f);
    if (std::hypot(b.x, b.z) < 1500) ++near;
  }
  // About one every three seconds, each lit for a quarter of a second.
  const int seconds_lit = bolts / static_cast<int>(weather::kBoltTicks);
  CHECK(seconds_lit > 15);
  CHECK(seconds_lit < 80);
  CHECK(near > bolts / 3);
  for (uint64_t t = 0; t < 60ULL * rules::kTicksPerSecond; ++t) {
    WeatherField f = forced(WeatherKind::Rain);
    f.tick += t;
    CHECK_FALSE(lightning_at(f, 0, 0).on);
  }
}

TEST_CASE("a clear sky has few clouds, an overcast one is full of grey ones") {
  const Scene clear = look_at_sky(forced(WeatherKind::Clear));
  const Scene few = look_at_sky(forced(WeatherKind::FewClouds));
  const Scene overcast = look_at_sky(forced(WeatherKind::Overcast));
  CHECK(clouds(clear.far_list) < clouds(few.far_list));
  CHECK(clouds(few.far_list) < clouds(overcast.far_list));
  CHECK(clouds(few.far_list) > 10);
  // In the sky everywhere around, not only above the village.
  float lo_x = 1e9f, hi_x = -1e9f;
  for (size_t v = 0; v < kCloudVariants; ++v) {
    for (const Instance& c : overcast.far_list.batches[static_cast<size_t>(MeshId::CloudLow0) + v]) {
      lo_x = std::min(lo_x, c.x);
      hi_x = std::max(hi_x, c.x);
      CHECK(c.y >= weather_view::kCloudBase - 40);
    }
  }
  CHECK(hi_x - lo_x > 2000);
  // The sky: blue when clear, grey when overcast, and its own clouds overhead.
  CHECK(clear.view.sky_zenith.b > clear.view.sky_zenith.r * 1.5f);
  CHECK(overcast.view.sky_zenith.b < overcast.view.sky_zenith.r * 1.2f);
  CHECK(overcast.view.cloud_cover > 0.9f);  // (a little haze)
  CHECK(overcast.view.light_color.r < clear.view.light_color.r * 0.4f);  // the sun is hidden
  CHECK(count(clear.near_list, MeshId::Rain) == 0);
}

TEST_CASE("rain falls around the camera, and is seen afar under its clouds") {
  const Scene rain = look_at_sky(forced(WeatherKind::Rain));
  CHECK(count(rain.near_list, MeshId::Rain) > 10);
  CHECK(count(rain.near_list, MeshId::Snow) == 0);
  for (const Instance& d : rain.near_list.batches[static_cast<size_t>(MeshId::Rain)]) {
    CHECK(std::hypot(d.x - rain.view.camera_x, d.z - rain.view.camera_z) < 40);
  }
  CHECK(rain.view.fog_half < look_at_sky(forced(WeatherKind::Clear)).view.fog_half * 0.6f);  // hazy
  // Fine weather here, a storm ahead: its curtains of rain, its dark clouds.
  WeatherField afar = forced(WeatherKind::Clear);
  afar.afar = true;
  afar.afar_at = {0, -weather::kAfarDistance};  // ahead: the camera looks towards -Z
  const Scene s = look_at_sky(afar);
  CHECK(count(s.near_list, MeshId::Rain) == 0);
  int afar_shafts = 0;
  for (const Instance& shaft : s.far_list.batches[static_cast<size_t>(MeshId::RainShaft)]) {
    // Near the storm, or beyond the weather chosen (the world's own there).
    const bool storm = std::hypot(shaft.x, shaft.z + weather::kAfarDistance) < weather::kAfarRadius + 200;
    CHECK((storm || std::hypot(shaft.x, shaft.z) > weather::kForcedRadius));
    afar_shafts += storm ? 1 : 0;
    CHECK(shaft.tint.a > 0);
  }
  CHECK(afar_shafts > 2);
  bool dark = false;
  for (size_t v = 0; v < kCloudVariants; ++v) {
    for (const Instance& c : s.far_list.batches[static_cast<size_t>(MeshId::Cloud0) + v]) dark = dark || c.tint.r < 0.8f;
    for (const Instance& c : s.far_list.batches[static_cast<size_t>(MeshId::CloudLow0) + v]) dark = dark || c.tint.r < 0.8f;
  }
  CHECK(dark);
}

TEST_CASE("snow falls slowly, fog closes in, and a bolt nearby lights everything") {
  const Scene snow = look_at_sky(forced(WeatherKind::Snow));
  CHECK(count(snow.near_list, MeshId::Snow) > 10);
  CHECK(count(snow.near_list, MeshId::Rain) == 0);
  const Scene fog = look_at_sky(forced(WeatherKind::Fog));
  CHECK(fog.view.fog_end < 600);
  CHECK(fog.view.fog_half < 100);
  CHECK(clouds(fog.far_list) < clouds(look_at_sky(forced(WeatherKind::FewClouds)).far_list));  // lost in it
  // A storm: wait for a bolt near the camera.
  WeatherField storm = forced(WeatherKind::Storm);
  Lightning bolt;
  for (int k = 0; k < 6000 && !(bolt.on && bolt.brightness > 0.8f && std::hypot(bolt.x, bolt.z) < 600); ++k) {
    ++storm.tick;
    bolt = lightning_at(storm, 0, 0);
  }
  REQUIRE(bolt.on);
  const Scene lit = look_at_sky(storm);
  CHECK(lit.flash > 0.5f);
  CHECK(count(lit.far_list, MeshId::Bolt) == 1);
  storm.tick += weather::kBoltTicks + 1;
  WeatherField dark = storm;
  if (!lightning_at(dark, 0, 0).on) CHECK(look_at_sky(dark).view.light_color.r < lit.view.light_color.r);
}
