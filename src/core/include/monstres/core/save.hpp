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
//   2  3D world: player + fixed objects.
inline constexpr uint32_t kSaveVersion = 2;

enum class LoadError { None, TooShort, BadMagic, UnsupportedVersion, Corrupt };

std::vector<uint8_t> serialize(const World& world);
std::optional<World> deserialize(std::span<const uint8_t> bytes, LoadError* error = nullptr);

const char* load_error_name(LoadError error);

// FNV-1a 64 of the serialized world: a fingerprint used by replay tests.
uint64_t state_hash(const World& world);

}  // namespace monstres
