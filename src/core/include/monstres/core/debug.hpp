#pragma once

#include "monstres/core/world.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace monstres {

// JSON view of the world, read by the browser tests (window.monstres.state()).
std::string to_json(const World& world);

std::string_view action_name(Action action);
std::optional<Action> parse_action(std::string_view name);

}  // namespace monstres
