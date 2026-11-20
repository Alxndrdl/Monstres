#pragma once

#include "monstres/core/weather.hpp"
#include "monstres/render/renderer.hpp"

namespace monstres {

// The weather on screen (the weather itself: core/weather.hpp).
//
// Overhead, the sky draws its own layer of clouds (View::cloud_cover);
// towards the horizon and afar, clouds are models, everywhere on the land
// and over the continent, as many as the weather says where each one is,
// drifting with the wind; under the rainy ones, curtains of rain seen from
// afar; lightning where the storms are. Around the camera, rain or snow
// falls (boxes of drops tiled around it). Clouds hide the sun, darken and
// grey the light; rain, snow and fog thicken the haze.

namespace weather_view {
inline constexpr float kRainTile = 8, kRainHeight = 12;  // the rain and snow models' boxes (make_models.py)
inline constexpr float kSnowTile = 6, kSnowHeight = 8;
inline constexpr float kRainFall = 9.0f, kSnowFall = 1.1f;  // m/s
inline constexpr int kRainTiles = 5, kSnowTiles = 6;  // a side, around the camera
inline constexpr float kLeafTile = 6, kLeafHeight = 6, kLeafFall = 0.8f;  // leaves blown about (make_models.py: leaves)
inline constexpr int kLeafTiles = 6;
inline constexpr float kCloudSpacing = 200;   // meters between two clouds (when the sky is full)
inline constexpr float kCloudReach = 3400;    // clouds up to this far from the camera
inline constexpr float kCloudLowFrom = 700;   // simpler clouds beyond
inline constexpr float kShaftFrom = 380;      // rain curtains beyond this (rain falls around the camera nearer)
inline constexpr float kCloudBase = 150, kCloudTop = 220;  // heights of the clouds' bases
}  // namespace weather_view

// The weather at the camera on a view built by build_view: the sky's
// colors and clouds, the light, the haze; a lightning bolt nearby lights
// everything for a moment. Returns how bright that flash is (0..1).
float apply_weather(View& view, const WeatherField& field);

// Clouds, rain curtains and lightning (added to the far list), rain or snow
// around the camera (added to the near list), for a view after apply_weather;
// with its season (View::season) and wind, leaves blown about in autumn,
// petals in spring.
// `flash`: what apply_weather returned. `time`: seconds of the game.
void build_weather(const WeatherField& field, const View& view, float flash, float time, DrawList& far_list,
                   DrawList& near_list);

}  // namespace monstres
