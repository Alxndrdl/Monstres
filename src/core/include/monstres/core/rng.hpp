#pragma once

#include <cstdint>

namespace monstres {

// PCG32 (pcg-random.org). Deterministic and identical on every platform:
// the same seed always gives the same game, natively and in WebAssembly.
class Rng {
public:
  explicit Rng(uint64_t seed = 0, uint64_t stream = 0xda3e39cb94b95bdbULL) {
    state_ = 0;
    inc_ = (stream << 1u) | 1u;
    next();
    state_ += seed;
    next();
  }

  static Rng from_raw(uint64_t state, uint64_t inc) {
    Rng r;
    r.state_ = state;
    r.inc_ = inc | 1u;
    return r;
  }

  uint32_t next() {
    const uint64_t old = state_;
    state_ = old * 6364136223846793005ULL + inc_;
    const auto xorshifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
    const auto rot = static_cast<uint32_t>(old >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
  }

  // Uniform integer in [0, bound), without modulo bias.
  uint32_t below(uint32_t bound) {
    if (bound == 0) return 0;
    const uint32_t threshold = (0u - bound) % bound;
    for (;;) {
      const uint32_t r = next();
      if (r >= threshold) return r % bound;
    }
  }

  uint64_t raw_state() const { return state_; }
  uint64_t raw_inc() const { return inc_; }

  bool operator==(const Rng&) const = default;

private:
  uint64_t state_ = 0;
  uint64_t inc_ = 1;
};

}  // namespace monstres
