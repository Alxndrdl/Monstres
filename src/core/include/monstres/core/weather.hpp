#pragma once

#include "monstres/core/terrain.hpp"

#include <cstdint>

namespace monstres {

// The weather: what the sky does, everywhere and all the time, for the
// eyes only for now (no effect on battles or monsters).
//
// It is a field over the land and the continent: big cells (kCellSize
// meters) each with its kind of weather for a while (kSlotSeconds), all
// drifting with the wind, blending into each other at their edges and from
// one while to the next. So it rains over there while the sun shines here,
// and the storm seen afar may come. Drawn from the seed and the world's
// tick only: nothing to save, the same on every device.

enum class WeatherKind : uint8_t { Clear, FewClouds, Overcast, Rain, Storm, Fog, Snow, Count };
inline constexpr int kWeatherKinds = static_cast<int>(WeatherKind::Count);

// What the weather is like at a place, each in 0..1 (they blend: half-way
// between a storm and the sun, half of a storm).
struct Weather {
  float cover = 0;  // clouds: 0 clear sky, 1 nothing but clouds
  float dark = 0;   // how dark the clouds are (rain, storm)
  float rain = 0;
  float snow = 0;
  float fog = 0;
  float storm = 0;  // lightning
  float wind = 0;
  bool operator==(const Weather&) const = default;
};

// The seasons (chosen in the menu for now; for the eyes only): what the
// land looks like (render: SeasonLook), and what falls from the sky: snow
// instead of rain in winter, rain instead of snow the rest of the year.
enum class Season : uint8_t { Spring, Summer, Autumn, Winter, Count };
inline constexpr int kSeasons = static_cast<int>(Season::Count);
const char* season_name(Season season);
// The weather of a kind in a season (rain turns to snow in winter, snow to rain otherwise).
WeatherKind in_season(WeatherKind kind, Season season);

// A kind of weather at its strongest.
Weather weather_of(WeatherKind kind);
Weather mix(const Weather& a, const Weather& b, float t);
// The kind of weather it looks most like.
WeatherKind kind_of(const Weather& weather);
const char* weather_name(WeatherKind kind);

namespace weather {
inline constexpr float kCellSize = 800.0f;        // meters
inline constexpr float kSlotSeconds = 360.0f;     // a kind of weather lasts this long in a cell...
inline constexpr float kChangeSeconds = 60.0f;    // ...and changes over this long
inline constexpr float kWindSpeed = 3.0f;         // m/s: clouds, storms
inline constexpr float kForcedRadius = 2600.0f;   // debug: the weather chosen, up to here...
inline constexpr float kForcedFade = 1200.0f;     // ...then the natural one again, over this much
inline constexpr float kAfarDistance = 1100.0f;   // debug "storm afar": that far ahead
inline constexpr float kAfarRadius = 380.0f;
}  // namespace weather

// Where the wind blows (m/s, the same everywhere): the weather drifts with it.
Vec2 wind_velocity(uint64_t seed);

// The weather at (x, z): drawn from the seed and the tick, possibly with a
// kind chosen in the debug menu around a point (forced), and a storm
// placed ahead for testing (afar).
struct WeatherField {
  uint64_t seed = 0;
  uint64_t tick = 0;
  Season season = Season::Summer;  // what falls: rain or snow
  // Debug: the weather `forced` (strength forced_amount, 0: none) within
  // kForcedRadius of `center`; with `afar`, a storm at `afar_at`.
  Weather forced;
  float forced_amount = 0;
  Vec2 center;
  bool afar = false;
  Vec2 afar_at;

  Weather natural(float x, float z) const;  // without the debug choices
  Weather at(float x, float z) const;
};

// A lightning bolt: where it strikes and how bright it is now (it flickers
// for kBoltTicks). Only where a storm is (Weather::storm), rarely elsewhere.
struct Lightning {
  bool on = false;
  float x = 0, z = 0;
  float brightness = 0;  // 0..1, flickering
  uint32_t id = 0;       // a new one each time (its shape)
};
namespace weather {
inline constexpr uint64_t kBoltPeriod = 42;    // ticks: at most one bolt each
inline constexpr uint64_t kBoltTicks = 14;
inline constexpr float kBoltReach = 3200.0f;   // bolts are looked for this far from the center
}  // namespace weather
// The bolt striking now within kBoltReach of (cx, cz), if any.
Lightning lightning_at(const WeatherField& field, float cx, float cz);

}  // namespace monstres
