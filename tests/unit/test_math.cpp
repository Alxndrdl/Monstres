#include "monstres/core/math.hpp"

#include <doctest.h>

#include <cmath>

using namespace monstres;

TEST_CASE("sincos16 is exact on the axes") {
  CHECK(sincos16(0).sin == 0.0f);
  CHECK(sincos16(0).cos == 1.0f);
  CHECK(sincos16(16384).sin == 1.0f);   // 90°
  CHECK(sincos16(16384).cos == 0.0f);
  CHECK(sincos16(32768).sin == 0.0f);   // 180°
  CHECK(sincos16(32768).cos == -1.0f);
  CHECK(sincos16(49152).sin == -1.0f);  // 270°
  CHECK(sincos16(49152).cos == 0.0f);
}

TEST_CASE("sincos16 matches std::sin and std::cos") {
  double worst = 0;
  for (uint32_t a = 0; a < 65536; a += 7) {
    const double radians = radians_from_angle16(static_cast<Angle16>(a));
    const SinCos sc = sincos16(static_cast<Angle16>(a));
    worst = std::max(worst, std::fabs(sc.sin - std::sin(radians)));
    worst = std::max(worst, std::fabs(sc.cos - std::cos(radians)));
  }
  CHECK(worst < 1e-6);
}

TEST_CASE("angles convert between radians and 16-bit") {
  CHECK(angle16_from_radians(0) == 0);
  CHECK(angle16_from_radians(kPi / 2) == 16384);
  CHECK(angle16_from_radians(-kPi / 2) == 49152);
  CHECK(angle16_from_radians(2 * kPi) == 0);
  CHECK(angle16_from_radians(5 * kPi) == 32768);
  CHECK(radians_from_angle16(32768) == doctest::Approx(kPi));
}
