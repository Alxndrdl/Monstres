#include "monstres/core/rng.hpp"

#include <doctest.h>

using monstres::Rng;

TEST_CASE("Rng matches the reference PCG32 output") {
  // First outputs of the official pcg32 demo, seeded with (42, 54).
  Rng rng(42, 54);
  CHECK(rng.next() == 0xa15c02b7u);
  CHECK(rng.next() == 0x7b47f409u);
  CHECK(rng.next() == 0xba1d3330u);
}

TEST_CASE("Rng is deterministic for a given seed") {
  Rng a(1234), b(1234), c(1235);
  bool differs = false;
  for (int i = 0; i < 100; ++i) {
    const uint32_t va = a.next();
    CHECK(va == b.next());
    differs |= va != c.next();
  }
  CHECK(differs);
}

TEST_CASE("Rng::below stays in range and reaches every value") {
  Rng rng(7);
  int seen[6] = {};
  for (int i = 0; i < 6000; ++i) {
    const uint32_t v = rng.below(6);
    REQUIRE(v < 6);
    ++seen[v];
  }
  for (int count : seen) CHECK(count > 800);  // ~1000 each
  CHECK(rng.below(0) == 0);
  CHECK(rng.below(1) == 0);
}

TEST_CASE("Rng raw state round-trips") {
  Rng a(99);
  a.next();
  Rng b = Rng::from_raw(a.raw_state(), a.raw_inc());
  CHECK(a == b);
  CHECK(a.next() == b.next());
}
