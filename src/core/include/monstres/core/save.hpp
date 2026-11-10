#pragma once

#include "monstres/core/world.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace monstres {

// Binary save format, little-endian:
//   "MNST" magic, u32 version, then the world (see save.cpp).
// Bump kSaveVersion whenever the layout changes, keep reading the old
// versions, and add an old save file to tests/regression/data/saves/.
inline constexpr uint32_t kSaveVersion = 1;

enum class LoadError { None, TooShort, BadMagic, UnsupportedVersion, Corrupt };

std::vector<uint8_t> serialize(const World& world);
std::optional<World> deserialize(std::span<const uint8_t> bytes, LoadError* error = nullptr);

// FNV-1a 64 of the serialized world: a fingerprint used by replay tests.
uint64_t state_hash(const World& world);

}  // namespace monstres
