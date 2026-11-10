#pragma once

#include <cmath>
#include <cstdint>

namespace monstres {

// Game simulation math. Only +, -, *, / and sqrt are used: IEEE 754 rounds
// them identically on every platform (with -ffp-contract=off, see
// cmake/MonstresOptions.cmake), so the simulation gives bit-identical results
// natively and in WebAssembly. std::sin/std::cos are NOT used here because
// glibc and Emscripten's libc do not return exactly the same values.

struct Vec3 {
  float x = 0, y = 0, z = 0;
  bool operator==(const Vec3&) const = default;
};

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }

// Angles in the simulation are 16-bit fractions of a turn: 65536 = 360°.
using Angle16 = uint16_t;

inline constexpr double kPi = 3.14159265358979323846;

struct SinCos {
  float sin = 0, cos = 1;
};

// Deterministic sine and cosine of a 16-bit angle (error < 1e-7).
inline SinCos sincos16(Angle16 angle) {
  const unsigned quadrant = angle >> 14;               // 0..3
  const double x = (angle & 0x3FFF) * (kPi / 32768.0);  // [0, pi/2)
  const double x2 = x * x;
  // Taylor series, enough terms for double precision on [0, pi/2).
  double s = x * (1 - x2 / 6 * (1 - x2 / 20 * (1 - x2 / 42 * (1 - x2 / 72 * (1 - x2 / 110 * (1 - x2 / 156))))));
  double c = 1 - x2 / 2 * (1 - x2 / 12 * (1 - x2 / 30 * (1 - x2 / 56 * (1 - x2 / 90 * (1 - x2 / 132 * (1 - x2 / 182))))));
  double rs = s, rc = c;
  switch (quadrant) {
    case 1: rs = c;  rc = -s; break;
    case 2: rs = -s; rc = -c; break;
    case 3: rs = -c; rc = s;  break;
    default: break;
  }
  return {static_cast<float>(rs), static_cast<float>(rc)};
}

// Radians -> nearest 16-bit angle (for the platform layer, not the simulation).
inline Angle16 angle16_from_radians(double radians) {
  const double turns = radians / (2 * kPi);
  const double frac = turns - std::floor(turns);
  return static_cast<Angle16>(static_cast<uint32_t>(std::lround(frac * 65536.0)) & 0xFFFF);
}

inline double radians_from_angle16(Angle16 a) { return a * (2 * kPi / 65536.0); }

}  // namespace monstres
