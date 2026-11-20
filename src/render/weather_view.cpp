#include "monstres/render/weather_view.hpp"

#include "monstres/core/world.hpp"
#include "monstres/render/cull.hpp"
#include "monstres/render/models.hpp"
#include "scene_util.hpp"

#include <algorithm>
#include <cmath>
#include <span>

namespace monstres {

namespace {

using scene_util::add;
using scene_util::kTau;
using scene_util::lerp;
using scene_util::rand01;

float smooth(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return t * t * (3 - 2 * t);
}

float luminance(Color c) { return 0.3f * c.r + 0.59f * c.g + 0.11f * c.b; }
Color scaled(Color c, float k) { return {c.r * k, c.g * k, c.b * k, c.a}; }
Color mixed(Color a, Color b, float t) {
  return {lerp(a.r, b.r, t), lerp(a.g, b.g, t), lerp(a.b, b.b, t), lerp(a.a, b.a, t)};
}
Color plus(Color c, float r, float g, float b) { return {c.r + r, c.g + g, c.b + b, c.a}; }

// The grey of clouds, as bright as `c` times k.
Color grey_like(Color c, float k) {
  constexpr Color kGrey{0.80f, 0.83f, 0.88f, 1};
  const float l = luminance(c) / luminance(kGrey) * k;
  return {kGrey.r * l, kGrey.g * l, kGrey.b * l, 1};
}

// A rotation turning "up" into the unit vector u.
Quat up_to(float ux, float uy, float uz) {
  const float s = std::sqrt(uz * uz + ux * ux);
  if (s < 1e-5f) return {};
  return quat_axis(uz / s, 0, -ux / s, std::acos(std::clamp(uy, -1.0f, 1.0f)));
}

float positive_mod(float a, float m) {
  const float r = std::fmod(a, m);
  return r < 0 ? r + m : r;
}

}  // namespace

float apply_weather(View& v, const WeatherField& field) {
  const Weather w = field.at(v.camera_x, v.camera_z);
  const Color horizon = v.sky_horizon;  // as the hour makes it, before the weather
  // The sky closes over (overcast and darker ones): grey, darker in a storm.
  const float veil = smooth((w.cover - 0.3f) / 0.7f);
  const float dim = 1 - 0.55f * w.dark;
  v.sky_zenith = mixed(v.sky_zenith, grey_like(horizon, 0.9f * dim), veil);
  v.sky_horizon = mixed(v.sky_horizon, grey_like(horizon, dim), veil);
  // Fog: everything goes pale and close.
  const Color mist = grey_like(horizon, 0.97f);
  v.sky_horizon = mixed(v.sky_horizon, mist, w.fog);
  v.sky_zenith = mixed(v.sky_zenith, mist, w.fog);
  v.fog = v.sky_horizon;
  v.fog_start = lerp(v.fog_start, 2.0f, w.fog);
  v.fog_half *= (1 - 0.62f * w.rain) * (1 - 0.6f * w.snow);
  v.fog_half = lerp(v.fog_half, 35.0f, w.fog);
  v.fog_end = lerp(v.fog_end, 5000.0f, std::max(w.rain, w.snow));
  v.fog_end = lerp(v.fog_end, 260.0f, w.fog);
  // Falling snow hides the distance: a white veil, close.
  v.fog = mixed(v.fog, grey_like(horizon, 1.08f), 0.6f * w.snow);
  v.fog_start = lerp(v.fog_start, 4.0f, w.snow);
  v.fog_half = lerp(v.fog_half, 45.0f, 0.85f * w.snow);
  v.fog_end = lerp(v.fog_end, 420.0f, w.snow);
  // Clouds hide the sun: a softer, greyer, darker light.
  const float block = std::min(0.9f, 0.8f * veil + 0.15f * w.dark + 0.45f * w.fog);
  v.light_color = scaled(v.light_color, 1 - block);
  v.ambient_sky = scaled(mixed(v.ambient_sky, grey_like(v.ambient_sky, 1.0f), veil), (1 + 0.2f * veil) * (1 - 0.4f * w.dark));
  v.ambient_ground = scaled(v.ambient_ground, 1 - 0.3f * w.dark);
  v.wind = w.wind;
  // The clouds overhead (drawn by the sky).
  v.cloud_cover = w.cover * (1 - 0.9f * w.fog);  // not seen through the fog
  const float bright = luminance(horizon);
  v.cloud_light = scaled(mixed(horizon, {bright, bright, bright, 1}, 0.6f), 1.18f * (1 - 0.55f * w.dark));
  v.cloud_shade = scaled(plus(v.cloud_light, -0.02f, -0.01f, 0.03f), 0.72f - 0.25f * w.dark);
  const double t = static_cast<double>(field.tick) / rules::kTicksPerSecond;
  const Vec2 wind = wind_velocity(field.seed);
  v.cloud_shift_x = static_cast<float>(wind.x * t);
  v.cloud_shift_z = static_cast<float>(wind.z * t);
  // A lightning bolt: the nearer, the more it lights everything.
  const Lightning bolt = lightning_at(field, v.camera_x, v.camera_z);
  float flash = 0;
  if (bolt.on) {
    const float d = std::hypot(bolt.x - v.camera_x, bolt.z - v.camera_z);
    flash = bolt.brightness * (0.06f + 0.94f * smooth(1 - d / 1800.0f));
  }
  if (flash > 0) {
    v.light_color = plus(v.light_color, 0.6f * flash, 0.63f * flash, 0.75f * flash);
    v.ambient_sky = plus(v.ambient_sky, 0.25f * flash, 0.26f * flash, 0.32f * flash);
    v.ambient_ground = plus(v.ambient_ground, 0.12f * flash, 0.12f * flash, 0.15f * flash);
    v.sky_zenith = plus(v.sky_zenith, 0.3f * flash, 0.31f * flash, 0.38f * flash);
    v.sky_horizon = plus(v.sky_horizon, 0.32f * flash, 0.33f * flash, 0.4f * flash);
    v.fog = plus(v.fog, 0.22f * flash, 0.23f * flash, 0.28f * flash);
    v.cloud_light = plus(v.cloud_light, 0.45f * flash, 0.46f * flash, 0.55f * flash);
    v.cloud_shade = plus(v.cloud_shade, 0.35f * flash, 0.36f * flash, 0.44f * flash);
  }
  return flash;
}

void build_weather(const WeatherField& field, const View& view, float flash, float time, DrawList& far_list,
                   DrawList& near_list) {
  using namespace weather_view;
  far_list.batches.resize(kMeshCount);
  near_list.batches.resize(kMeshCount);
  const float cx = view.camera_x, cy = view.camera_y, cz = view.camera_z;
  const uint64_t seed = field.seed;
  const Vec2 wind = wind_velocity(seed);

  // ---- Clouds, everywhere: one place every kCloudSpacing meters among the
  // clouds drifting with the wind, a cloud there as big as the weather there
  // allows (none in a clear sky, all of them when it is overcast).
  const Frustum far_view = frustum_of(view.far_view_projection);
  const double t = static_cast<double>(field.tick) / rules::kTicksPerSecond;
  const double drift_x = wind.x * t, drift_z = wind.z * t;
  const double cu = cx - drift_x, cv = cz - drift_z;  // the camera among the drifting clouds
  const auto i0 = static_cast<int64_t>(std::floor((cu - kCloudReach) / kCloudSpacing));
  const auto i1 = static_cast<int64_t>(std::floor((cu + kCloudReach) / kCloudSpacing));
  const auto j0 = static_cast<int64_t>(std::floor((cv - kCloudReach) / kCloudSpacing));
  const auto j1 = static_cast<int64_t>(std::floor((cv + kCloudReach) / kCloudSpacing));
  // Lost in the haze: not drawn.
  const auto hidden = [&](float d) {
    return d > view.fog_end || (d > view.fog_start && std::exp2(-(d - view.fog_start) / view.fog_half) < 0.015f);
  };
  // Clouds glow by day (brighter than the lighting alone), turn grey-blue at night.
  const float day = lerp(1.45f, 0.16f, view.night);
  const float night_blue = lerp(0.0f, 0.1f, view.night);
  for (int64_t i = i0; i <= i1; ++i) {
    for (int64_t j = j0; j <= j1; ++j) {
      const auto r = [&](uint64_t salt) {
        return rand01(seed ^ (0xC10D0000ULL + salt), static_cast<uint64_t>(i), static_cast<uint64_t>(j));
      };
      const auto x = static_cast<float>((static_cast<double>(i) + 0.1 + 0.8 * r(1)) * kCloudSpacing + drift_x);
      const auto z = static_cast<float>((static_cast<double>(j) + 0.1 + 0.8 * r(2)) * kCloudSpacing + drift_z);
      const float d = std::hypot(x - cx, z - cz);
      if (d > kCloudReach || hidden(d)) continue;
      const Weather w = field.at(x, z);
      const float amount = smooth((w.cover * 1.15f - r(3)) / 0.15f);
      if (amount < 0.03f) continue;
      const float size = 0.55f + 0.45f * amount;
      Instance c;
      c.x = x;
      c.z = z;
      c.y = lerp(kCloudBase, kCloudTop, r(7)) - 35.0f * w.storm;
      c.sx = lerp(65.0f, 135.0f, r(4)) * size;
      c.sz = lerp(40.0f, 80.0f, r(5)) * size;
      c.sy = lerp(16.0f, 30.0f, r(6)) * size * (1 + 1.6f * w.storm);  // storm clouds tower up
      c.rotation = quat_yaw(r(8) * kTau);
      const float reach = std::max(c.sx, c.sz);
      if (in_view(far_view, {x - reach, c.y, z - reach, x + reach, c.y + c.sy, z + reach})) {
        const float k = day * (1 - 0.88f * w.dark);
        c.tint = {k + 0.8f * flash, k + 0.8f * flash + night_blue * 0.2f, k + 0.9f * flash + night_blue, 1};
        const auto variant = static_cast<size_t>(r(9) * kCloudVariants) % kCloudVariants;
        add(far_list, mesh_offset(d < kCloudLowFrom ? MeshId::Cloud0 : MeshId::CloudLow0, variant), c);
      }
      // Under a rainy (or snowy) cloud afar: a curtain down to the ground.
      const float wet = std::max(w.rain, w.snow);
      if (wet > 0.3f && d > kShaftFrom && amount > 0.5f && r(10) < 0.75f) {
        Instance s;
        s.x = x;
        s.z = z;
        s.y = -40;  // under any ground: the ground hides its foot
        s.sy = c.y + 12 - s.y;
        s.sx = s.sz = std::min(c.sx, c.sz) * 0.75f;
        if (in_view(far_view, {x - s.sx, s.y, z - s.sx, x + s.sx, s.y + s.sy, z + s.sx})) {
          const float snowy = w.snow / std::max(w.rain + w.snow, 1e-3f);
          const Color tone = mixed(scaled(view.fog, 0.7f - 0.5f * w.dark), scaled(view.fog, 1.08f), snowy);
          s.tint = {tone.r, tone.g, tone.b, wet * (0.75f + 0.25f * w.dark) * smooth((d - kShaftFrom) / 300.0f)};
          add(far_list, MeshId::RainShaft, s);
        }
      }
    }
  }

  // ---- Lightning: the bolt striking now, from the clouds to the ground.
  const Lightning bolt = lightning_at(field, cx, cz);
  if (bolt.on) {
    Instance b;
    b.x = bolt.x;
    b.z = bolt.z;
    b.y = kCloudBase - 20;
    b.sx = b.sy = b.sz = b.y + 40;  // down under any ground
    b.rotation = quat_yaw(static_cast<float>(bolt.id % 16) * 0.39f);
    if (bolt.id % 2) b.sx = -b.sx;  // its mirror image (the model is double-sided)
    b.tint = {1.7f, 1.75f, 2.1f, bolt.brightness};
    add(far_list, MeshId::Bolt, b);
  }

  // ---- Rain or snow around the camera: boxes of drops tiled around it,
  // falling (two boxes on top of each other in each place, so that the
  // drops wrap around without a gap), carried by the wind.
  const Weather here = field.at(cx, cz);
  const Frustum near_view = frustum_of(view.view_projection);
  const float light = luminance(view.fog) * 1.05f + 0.08f;
  const float gust = view.wind / weather::kWindSpeed * 4.5f;  // m/s sideways, at most (the wind of the view)
  // `flutter`: leaves, tumbling and carried farther by the wind; `palette`:
  // a color for each box among these (else `tint`).
  const auto fall = [&](MeshId mesh, float amount, float tile, float height, float speed, int tiles, Color tint,
                        bool sway, uint64_t salt, bool flutter = false, std::span<const Color> palette = {}) {
    if (amount < 0.02f) return;
    const float push = flutter ? 1.0f + 1.4f * view.wind : 1.0f;
    const float side_x = wind.x * gust * push, side_z = wind.z * gust * push;
    const float down = positive_mod(time * speed, height);
    const float ox = positive_mod(time * side_x, tile), oz = positive_mod(time * side_z, tile);
    const float len = std::sqrt(side_x * side_x + speed * speed + side_z * side_z);
    const Quat lean = up_to(-side_x / len, speed / len, -side_z / len);  // the drops' tails point upwind
    const auto gi0 = static_cast<int64_t>(std::floor(cx / tile)) - tiles / 2;
    const auto gj0 = static_cast<int64_t>(std::floor(cz / tile)) - tiles / 2;
    for (int64_t gi = gi0; gi < gi0 + tiles; ++gi) {
      for (int64_t gj = gj0; gj < gj0 + tiles; ++gj) {
        // Light rain: not in every box (the same ones while it lasts).
        if (rand01(seed ^ salt, static_cast<uint64_t>(gi), static_cast<uint64_t>(gj)) >= amount * 1.3f) continue;
        float x = (static_cast<float>(gi) + 0.5f) * tile + ox;
        float z = (static_cast<float>(gj) + 0.5f) * tile + oz;
        if (sway) {
          x += 0.45f * std::sin(time * 0.6f + static_cast<float>(gi) * 1.7f + static_cast<float>(gj) * 0.9f);
          z += 0.35f * std::sin(time * 0.5f + static_cast<float>(gj) * 1.3f);
        }
        const float bottom = cy - 0.5f * height - down;
        const float half = tile * 0.5f + 1;
        if (!in_view(near_view, {x - half, bottom, z - half, x + half, bottom + 2 * height, z + half})) continue;
        const float h = rand01(seed ^ salt ^ 0xC0105ULL, static_cast<uint64_t>(gi), static_cast<uint64_t>(gj));
        for (int copy = 0; copy < 2; ++copy) {
          Instance d;
          d.x = x;
          d.y = bottom + static_cast<float>(copy) * height;
          d.z = z;
          d.rotation = lean;
          if (flutter) {  // the leaves of a box turning and rocking together, each box its own way
            const float k = h * kTau + static_cast<float>(copy);
            d.rotation = quat_yaw(time * (0.4f + 0.5f * h) + k) * quat_x(0.5f * std::sin(time * 1.7f + k)) *
                         quat_z(0.4f * std::sin(time * 1.3f + 2 * k));
          }
          d.tint = palette.empty() ? tint : palette[static_cast<size_t>(h * static_cast<float>(palette.size())) % palette.size()];
          add(near_list, mesh, d);
        }
      }
    }
  };
  fall(MeshId::Rain, here.rain, kRainTile, kRainHeight, kRainFall, kRainTiles,
       {0.78f * light, 0.82f * light, 0.92f * light, std::min(1.0f, here.rain * 1.1f)}, false, 0x7A1ULL);
  const float white = light * 1.15f + 0.1f;
  fall(MeshId::Snow, here.snow, kSnowTile, kSnowHeight, kSnowFall, kSnowTiles, {white, white, white * 1.03f, here.snow},
       true, 0x5A0ULL);
  // Leaves blown around in autumn (a few even without wind), petals in spring when it blows.
  const SeasonLook& season = view.season;
  const float dry = 1 - 0.8f * std::max(here.rain, here.snow);
  const Color autumn[] = {{0.95f, 0.72f, 0.22f, 1}, {0.92f, 0.48f, 0.14f, 1}, {0.80f, 0.26f, 0.12f, 1},
                          {0.70f, 0.50f, 0.24f, 1}};
  const Color petals[] = {{1.0f, 0.86f, 0.92f, 1}, {1.0f, 0.96f, 0.96f, 1}};
  fall(MeshId::Leaves, season.autumn * (0.12f + 0.88f * view.wind) * dry, kLeafTile, kLeafHeight, kLeafFall, kLeafTiles,
       {}, true, 0x1EAFULL, true, autumn);
  fall(MeshId::Leaves, season.spring * 0.35f * view.wind * dry, kLeafTile, kLeafHeight, kLeafFall * 0.7f, kLeafTiles,
       {}, true, 0x9E7A1ULL, true, petals);
}

}  // namespace monstres
