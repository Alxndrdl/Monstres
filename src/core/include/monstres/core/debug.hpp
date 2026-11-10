#pragma once

#include "monstres/core/world.hpp"

#include <string>

namespace monstres {

// JSON view of the world, read by the browser tests (window.monstres.state()).
std::string to_json(const World& world);

}  // namespace monstres
