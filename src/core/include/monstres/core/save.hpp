#pragma once

#include "monstres/core/world.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace monstres {

// Binary save format, little-endian:
//   "MNST" magic, u32 version, then the world (see save.cpp).
// Bump kSaveVersion whenever the layout changes, and add a reference save of
// the new version to tests/regression/data/saves/.
//
// Version history:
//   1  grid prototype (2D). Not readable anymore: the game was redesigned
//      before any release, so these saves are refused and a new game starts.
//   2  3D world: player + fixed boxes. Not readable anymore (pre-release).
//   3  objects have a kind: trees, pines, rocks, logs, stumps, bushes, crates.
//      Not readable anymore (pre-release).
//   4  the world seed is saved (path and decoration are rebuilt from it).
//      Still readable: loading one adds the companion and the wild monsters.
//   5  the companion and the wild monsters, and the speed of every body.
//   6  the village (objects), villagers, crows and fish; flying height.
//      Versions 4 and 5 still load: the village is built (removing what was
//      in its way) and the creatures appear anew from the seed.
inline constexpr uint32_t kSaveVersion = 6;
inline constexpr uint32_t kOldestReadableSaveVersion = 4;

enum class LoadError { None, TooShort, BadMagic, UnsupportedVersion, Corrupt };

std::vector<uint8_t> serialize(const World& world);
std::optional<World> deserialize(std::span<const uint8_t> bytes, LoadError* error = nullptr);

const char* load_error_name(LoadError error);

// FNV-1a 64 of the serialized world: a fingerprint used by replay tests.
uint64_t state_hash(const World& world);

}  // namespace monstres
