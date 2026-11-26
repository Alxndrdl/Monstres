#pragma once

// Small helpers shared by the files that fill draw lists (scene.cpp,
// landscape.cpp). Not part of the library's interface.

#include "monstres/render/models.hpp"
#include "monstres/render/renderer.hpp"

#include <cstdint>

namespace monstres::scene_util {

inline constexpr float kTau = 6.28318530718f;

// Deterministic pseudo-random number in [0, 1) for decoration.
inline float rand01(uint64_t seed, uint64_t a, uint64_t b = 0) {
  uint64_t z = seed * 0x9E3779B97F4A7C15ULL + a * 0xC2B2AE3D27D4EB4FULL + b * 0x165667B19E3779F9ULL + 0x632BE59BD9B4E019ULL;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  z ^= z >> 31;
  return static_cast<float>(z >> 40) / static_cast<float>(1ULL << 24);
}

inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

// Magic glows: these meshes give their own light.
inline float material_for(MeshId id, float material) {
  return id == MeshId::Sparkle || id == MeshId::Bubble || id == MeshId::RuneCircle || id == MeshId::Crystal || id == MeshId::Bolt ||
                 id == MeshId::Rain || id == MeshId::Snow || id == MeshId::RainShaft || id == MeshId::HearthFire ||
                 id == MeshId::CristiCrystal || id == MeshId::DoorGlow || id == MeshId::CampfireFire ||
                 id == MeshId::LighthouseLamp || id == MeshId::Gust
             ? kGlowing
             : material;
}

inline void add(DrawList& list, MeshId id, const Instance& in) {
  if (id == MeshId::Count) return;
  list.batches[static_cast<size_t>(id)].push_back(in);
  list.batches[static_cast<size_t>(id)].back().material = material_for(id, in.material);
}

}  // namespace monstres::scene_util
