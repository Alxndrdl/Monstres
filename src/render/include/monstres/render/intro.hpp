#pragma once

#include "monstres/core/world.hpp"

#include <array>

namespace monstres {

// The arrival (a new game): the camera starts high above the magician and
// comes down to it in one smooth movement, turning a little, until it is
// where the game's camera is and takes over. Visual only (the world goes on
// meanwhile); it can be skipped.

inline constexpr float kIntroSeconds = 7.0f;

struct Shot {
  Vec3 eye, target;
};
// Where the camera is `seconds` into the arrival; at the end, `last` (the
// game's camera). Never under the ground.
Shot intro_shot(const World& world, float seconds, const Shot& last);

}  // namespace monstres
