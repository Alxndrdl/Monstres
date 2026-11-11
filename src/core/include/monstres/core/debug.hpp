#pragma once

#include "monstres/core/world.hpp"

#include <string>

namespace monstres {

// JSON view of the world, read by the browser tests (window.monstres.state()).
std::string to_json(const World& world);

// JSON description of creatures[index] for the info panel of the page:
// index, species, mind, variant, position, distance to the player and
// whether it is on the ground. "null" if there is no such creature.
std::string creature_json(const World& world, int index);

// Names used in the JSON (stable identifiers, translated by the page).
const char* zone_name(Zone zone);
const char* element_name(Element element);
const char* object_kind_name(ObjectKind kind);

// The character sheet: name, level, experience and characteristics.
std::string wizard_json(const World& world);

// Every monster species with its base characteristics, element, size, speeds
// and how many live in this world (villagers are not listed).
std::string bestiary_json(const World& world);

// What the maps draw, besides the heights of the ground (Heightmap): sizes,
// lake, village, ruins, path and the outline of the objects. Never changes
// during a game.
std::string map_json(const World& world);

}  // namespace monstres
