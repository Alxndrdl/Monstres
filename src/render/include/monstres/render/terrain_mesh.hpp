#pragma once

#include "monstres/core/world.hpp"
#include "monstres/render/renderer.hpp"

namespace monstres {

namespace terrain {
inline constexpr float kDetailHalfSize = 72.0f;   // fine ground grid, 1 m cells
inline constexpr float kHorizonHalfSize = 600.0f; // coarse ground up to the horizon
inline constexpr float kPathHeight = 0.015f;      // just above the ground
}  // namespace terrain

// Smooth pseudo-random field in [0, 1] (value noise), stable for a seed.
float value_noise(uint64_t seed, float x, float z, float cell_size);

// The ground: grass with color variations and patches of bare earth, out to
// the horizon. Flat (the world is flat), opaque.
MeshData build_ground_mesh(const World& world);

// Height of the ground (visual only: the lake bed dips under the water).
float ground_height(const World& world, float x, float z);

// The lake surface, transparent, slightly darker towards the middle.
MeshData build_water_mesh(const World& world);

// The dirt path as a ribbon slightly above the ground, with soft
// transparent edges and faded ends.
MeshData build_path_mesh(const World& world);

}  // namespace monstres
