#include "monstres/core/weather.hpp"

#include "monstres/core/math.hpp"
#include "monstres/core/world.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace monstres {

namespace {

uint64_t hash64(uint64_t seed, uint64_t a, uint64_t b, uint64_t c) {
  uint64_t z = seed * 0x9E3779B97F4A7C15ULL + a * 0xC2B2AE3D27D4EB4FULL + b * 0x165667B19E3779F9ULL +
               c * 0xD6E8FEB86659FD93ULL + 0x632BE59BD9B4E019ULL;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}

float hash01(uint64_t seed, uint64_t a, uint64_t b, uint64_t c) {
  return static_cast<float>(hash64(seed, a, b, c) >> 40) / static_cast<float>(1ULL << 24);
}

float smooth(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return t * t * (3 - 2 * t);
}

// Across a cell: mostly its own weather, changing over its last fifth on each side.
float sharpen(float f) { return smooth((f - 0.5f) * 2.2f + 0.5f); }

constexpr uint64_t kSalt = 0x5745415448455221ULL;  // "WEATHER!"

// How often each kind of weather comes (out of 100).
constexpr int kOdds[kWeatherKinds] = {28, 27, 12, 12, 6, 7, 8};
static_assert(kOdds[0] + kOdds[1] + kOdds[2] + kOdds[3] + kOdds[4] + kOdds[5] + kOdds[6] == 100);

WeatherKind cell_kind(uint64_t seed, int64_t i, int64_t j, int64_t slot, Season season) {
  const float h = hash01(seed ^ kSalt, static_cast<uint64_t>(i), static_cast<uint64_t>(j), static_cast<uint64_t>(slot));
  // The first while of a world is fine weather everywhere (a new game starts in the sun).
  if (slot == 0) return h < 0.6f ? WeatherKind::Clear : WeatherKind::FewClouds;
  float sum = 0;
  for (int k = 0; k < kWeatherKinds; ++k) {
    sum += static_cast<float>(kOdds[k]) / 100.0f;
    if (h < sum) return in_season(static_cast<WeatherKind>(k), season);
  }
  return WeatherKind::Clear;
}

}  // namespace

Weather weather_of(WeatherKind kind) {
  switch (kind) {
    //                              cover  dark  rain  snow  fog   storm wind
    case WeatherKind::Clear:     return {0.04f, 0.0f, 0, 0, 0.0f, 0, 0.1f};
    case WeatherKind::FewClouds: return {0.38f, 0.05f, 0, 0, 0.0f, 0, 0.25f};
    case WeatherKind::Overcast:  return {1.0f, 0.3f, 0, 0, 0.08f, 0, 0.35f};
    case WeatherKind::Rain:      return {1.0f, 0.5f, 0.8f, 0, 0.3f, 0, 0.5f};
    case WeatherKind::Storm:     return {1.0f, 0.85f, 1.0f, 0, 0.35f, 1, 1.0f};
    case WeatherKind::Fog:       return {0.7f, 0.1f, 0, 0, 1.0f, 0, 0.0f};
    case WeatherKind::Snow:      return {1.0f, 0.15f, 0, 1.0f, 0.4f, 0, 0.3f};
    case WeatherKind::Count: break;
  }
  return {};
}

Weather mix(const Weather& a, const Weather& b, float t) {
  const auto m = [t](float x, float y) { return x + (y - x) * t; };
  return {m(a.cover, b.cover), m(a.dark, b.dark), m(a.rain, b.rain), m(a.snow, b.snow),
          m(a.fog, b.fog),     m(a.storm, b.storm), m(a.wind, b.wind)};
}

WeatherKind kind_of(const Weather& w) {
  WeatherKind best = WeatherKind::Clear;
  float closest = 1e30f;
  for (int k = 0; k < kWeatherKinds; ++k) {
    const Weather o = weather_of(static_cast<WeatherKind>(k));
    // What is seen first: rain, snow, lightning, fog; then the clouds.
    const float d = 4 * ((w.rain - o.rain) * (w.rain - o.rain) + (w.snow - o.snow) * (w.snow - o.snow) +
                         (w.storm - o.storm) * (w.storm - o.storm) + (w.fog - o.fog) * (w.fog - o.fog)) +
                    (w.cover - o.cover) * (w.cover - o.cover) + (w.dark - o.dark) * (w.dark - o.dark);
    if (d < closest) {
      closest = d;
      best = static_cast<WeatherKind>(k);
    }
  }
  return best;
}

const char* season_name(Season season) {
  static const char* kNames[] = {"spring", "summer", "autumn", "winter"};
  static_assert(std::size(kNames) == kSeasons);
  const auto i = static_cast<size_t>(season);
  return i < std::size(kNames) ? kNames[i] : "?";
}

WeatherKind in_season(WeatherKind kind, Season season) {
  if (season == Season::Winter && kind == WeatherKind::Rain) return WeatherKind::Snow;
  if (season != Season::Winter && kind == WeatherKind::Snow) return WeatherKind::Rain;
  return kind;
}

const char* weather_name(WeatherKind kind) {
  static const char* kNames[] = {"clear", "fewClouds", "overcast", "rain", "storm", "fog", "snow"};
  static_assert(std::size(kNames) == kWeatherKinds);
  const auto i = static_cast<size_t>(kind);
  return i < std::size(kNames) ? kNames[i] : "?";
}

Vec2 wind_velocity(uint64_t seed) {
  const SinCos d = sincos16(static_cast<Angle16>(hash64(seed ^ kSalt, 1, 2, 3) & 0xFFFF));
  return {d.cos * weather::kWindSpeed, d.sin * weather::kWindSpeed};
}

Weather WeatherField::natural(float x, float z) const {
  using namespace weather;
  const double t = static_cast<double>(tick) / rules::kTicksPerSecond;
  const Vec2 wind = wind_velocity(seed);
  // Where (x, z) is among the cells, which drift with the wind.
  const double gx = (x - wind.x * t) / kCellSize - 0.5, gz = (z - wind.z * t) / kCellSize - 0.5;
  const double fi = std::floor(gx), fj = std::floor(gz);
  const auto i = static_cast<int64_t>(fi), j = static_cast<int64_t>(fj);
  const float wx = sharpen(static_cast<float>(gx - fi)), wz = sharpen(static_cast<float>(gz - fj));
  const auto slot = static_cast<int64_t>(std::floor(t / kSlotSeconds));
  const float into = static_cast<float>(t - static_cast<double>(slot) * kSlotSeconds);
  const float next = smooth((into - (kSlotSeconds - kChangeSeconds)) / kChangeSeconds);
  const auto at_slot = [&](int64_t s) {
    const auto cell = [&](int64_t a, int64_t b) { return weather_of(cell_kind(seed, a, b, s, season)); };
    return mix(mix(cell(i, j), cell(i + 1, j), wx), mix(cell(i, j + 1), cell(i + 1, j + 1), wx), wz);
  };
  const Weather now = at_slot(slot);
  return next > 0 ? mix(now, at_slot(slot + 1), next) : now;
}

Weather WeatherField::at(float x, float z) const {
  using namespace weather;
  Weather w = natural(x, z);
  if (forced_amount > 0) {
    const float d = std::sqrt((x - center.x) * (x - center.x) + (z - center.z) * (z - center.z));
    const float k = forced_amount * (1 - std::clamp((d - kForcedRadius) / kForcedFade, 0.0f, 1.0f));
    if (k > 0) w = mix(w, forced, k);
  }
  if (afar) {
    const float d = std::sqrt((x - afar_at.x) * (x - afar_at.x) + (z - afar_at.z) * (z - afar_at.z));
    const float k = 1 - smooth((d - kAfarRadius * 0.5f) / (kAfarRadius * 0.5f));
    if (k > 0) w = mix(w, weather_of(WeatherKind::Storm), k);
  }
  return w;
}

Lightning lightning_at(const WeatherField& field, float cx, float cz) {
  using namespace weather;
  const uint64_t bucket = field.tick / kBoltPeriod;
  const uint64_t seed = field.seed ^ kSalt ^ 0xB017ULL;
  // A few places to strike are tried, more of them near the center; the
  // first one in a storm may get a bolt (more likely in a stronger storm).
  for (uint64_t k = 0; k < 4; ++k) {
    const float r = hash01(seed, bucket, k, 1);
    const float distance = kBoltReach * r * r;
    const SinCos d = sincos16(static_cast<Angle16>(hash64(seed, bucket, k, 2) & 0xFFFF));
    const float x = cx + d.cos * distance, z = cz + d.sin * distance;
    const float storm = field.at(x, z).storm;
    if (storm < 0.5f) continue;
    if (hash01(seed, bucket, k, 3) >= 0.22f * storm) return {};
    const auto start = static_cast<uint64_t>(hash01(seed, bucket, k, 4) * static_cast<float>(kBoltPeriod - kBoltTicks));
    const uint64_t into = field.tick - bucket * kBoltPeriod;
    if (into < start || into >= start + kBoltTicks) return {};
    const uint64_t age = into - start;
    // A flash, a short dark, a second flash, then it fades.
    static constexpr float kFlicker[kBoltTicks] = {1.0f, 0.9f, 0.35f, 0.15f, 0.85f, 1.0f, 0.7f,
                                                    0.5f, 0.38f, 0.28f, 0.2f,  0.13f, 0.08f, 0.04f};
    return {true, x, z, kFlicker[age], static_cast<uint32_t>(bucket * 4 + k)};
  }
  return {};
}

}  // namespace monstres
