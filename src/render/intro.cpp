#include "monstres/render/intro.hpp"

#include <algorithm>
#include <cmath>

namespace monstres {

Shot intro_shot(const World& w, float seconds, const Shot& last) {
  if (seconds >= kIntroSeconds) return last;
  // From high above (behind the game's camera, turned by a quarter turn),
  // along a spiral down to it: slow at the start and at the end.
  float t = std::clamp(seconds / kIntroSeconds, 0.0f, 1.0f);
  t = t * t * (3 - 2 * t);
  const Vec3 p = last.target;
  const float dx = last.eye.x - p.x, dz = last.eye.z - p.z;
  const float flat = std::max(std::sqrt(dx * dx + dz * dz), 0.1f);
  const float angle0 = std::atan2(dz, dx);
  const float angle = angle0 + 1.6f * (1 - t);              // it turns around as it comes down
  const float far = flat + 160.0f * (1 - t) * (1 - t);       // away...
  const float high = (last.eye.y - p.y) + 220.0f * (1 - t) * (1 - t) * (1 - t * 0.3f);  // ...and up
  Shot shot;
  shot.eye = {p.x + std::cos(angle) * far, p.y + high, p.z + std::sin(angle) * far};
  shot.target = p;
  shot.eye.y = std::max(shot.eye.y, w.ground.height(shot.eye.x, shot.eye.z) + 2.0f);
  return shot;
}

}  // namespace monstres
