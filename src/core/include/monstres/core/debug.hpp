#pragma once

#include "monstres/core/variants.hpp"
#include "monstres/core/world.hpp"

#include <string>

namespace monstres {

// JSON view of the world, read by the browser tests (window.monstres.state()).
std::string to_json(const World& world);

// JSON description of creatures[index] for the info panel of the page:
// index, species, mind, variant, position, distance to the player and
// whether it is on the ground; for monsters also their family, elements,
// genes (look: genes_code) and the identifiers of the variants they show.
// "null" if there is no such creature.
std::string creature_json(const World& world, int index);

// Names used in the JSON (stable identifiers, translated by the page).
const char* zone_name(Zone zone);
const char* element_name(Element element);
const char* object_kind_name(ObjectKind kind);
const char* family_name(Family family);
const char* rarity_name(Rarity rarity);

// The character sheet: name, level, experience and characteristics.
std::string wizard_json(const World& world);

// Every monster species with its base characteristics, elements, family,
// size, speeds and how many live in this world (villagers are not listed),
// and all its variants: identifier, kind, rarity, zones where found (none:
// everywhere), elements, look (genes_code of a monster showing it) and
// status (unknown, seen or captured).
std::string bestiary_json(const World& world);

// What the maps draw, besides the heights of the ground (Heightmap): sizes,
// lake, village, ruins, path and the outline of the objects. Never changes
// during a game.
std::string map_json(const World& world);

// The player's team: per monster its level, experience, hit points,
// characteristics at its level, family, elements, look, variants and its
// three attacks (identifier, shape, element, power, cooldown in seconds).
std::string team_json(const World& world);

// The battle going on ("phase": "fight", "flee", "training" or "none"),
// with both sides (hit points, state, cooldowns left in seconds, last
// hits; when training the magician's shield) and whether the enemy can be
// captured; and how the last battle ended ("outcome"), with the experience
// each team member earned ("gains": levels and experience before and after).
std::string battle_json(const World& world);

// What a signpost (objects[index]) shows: where the places of the world
// are from it ("places": zone, offset dx/dz in meters, distance), the
// nearest first. "null" if it is not a signpost.
std::string sign_json(const World& world, int index);

}  // namespace monstres
