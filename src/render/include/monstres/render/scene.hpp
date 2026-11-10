#pragma once

#include "monstres/core/world.hpp"
#include "monstres/render/renderer.hpp"

#include <vector>

namespace monstres {

namespace palette {
inline constexpr Color kBackground{0.06f, 0.05f, 0.09f, 1};
inline constexpr Color kTileA{0.16f, 0.15f, 0.22f, 1};
inline constexpr Color kTileB{0.19f, 0.18f, 0.26f, 1};
inline constexpr Color kPlayer{0.35f, 0.80f, 0.95f, 1};
inline constexpr Color kMonster{0.90f, 0.35f, 0.45f, 1};
inline constexpr Color kHpBar{0.40f, 0.90f, 0.45f, 1};
inline constexpr Color kDead{0.35f, 0.35f, 0.40f, 1};
}  // namespace palette

// Fills `out` (cleared first) with every quad needed to draw `world`.
void build_scene(const World& world, std::vector<QuadInstance>& out);

}  // namespace monstres
