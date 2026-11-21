#pragma once

#include "monstres/core/variants.hpp"
#include "monstres/core/world.hpp"

#include <string>

namespace monstres {

// The commands' names, in the order of Command, as a JSON array: the page
// sends a command by its name (post.js), the numbers are written here only.
std::string command_names_json();

// The villagers with something for the player (npc_marks), and where they
// are: [{"npc", "x", "z", "marks": ["newQuest", "quest", "heal", "fight"]}].
std::string marks_json(const World& world);

// A text as a JSON string (quotes, backslashes and control characters escaped).
std::string json_string(const std::string& text);

// JSON view of the world, read by the browser tests (window.monstres.state()).
std::string to_json(const World& world);

// JSON description of creatures[index] for the info panel of the page:
// index, species, mind, variant, position, distance to the player and
// whether it is on the ground; for monsters also their family, elements,
// genes (look: genes_code) and the identifiers of the variants they show.
// "null" if there is no such creature.
std::string creature_json(const World& world, int index);

// Names used in the JSON (stable identifiers, translated by the page).
const char* species_name(Species species);  // "companion", "slime"... (web/lore.js: species)
const char* zone_name(Zone zone);
const char* element_name(Element element);
const char* object_kind_name(ObjectKind kind);
const char* family_name(Family family);
const char* rarity_name(Rarity rarity);

// The character sheet: name, level, experience and characteristics.
std::string wizard_json(const World& world);
// The magician's spells: those chosen (combat, adventure), those it knows,
// and the ones that can be cast now (with their cooldowns).
std::string spells_json(const World& world);

// The types' chart (battle.hpp, type_effect), for the guide:
// {"types":["fire",...],"chart":[[0,-1,...],...]} (rows: the attack, in the order of types).
std::string type_chart_json();

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

// The continent for the map: its grid (the heights come apart, see
// main.cpp), sea level, regions with their remarkable places, waters, the
// active volcano's crater.
struct Continent;
std::string continent_json(const Continent& continent);

// The player's team: per monster its level, experience, hit points,
// characteristics at its level, family, elements, look, variants and its
// three attacks (identifier, shape, element, power, cooldown in seconds).
std::string team_json(const World& world);

// The arena's tournaments (battle.hpp): {"cups": [{"won", "open", "level",
// "coins"}...], "on", "cup", "round", "rounds", "nextIn"}.
std::string tournament_json(const World& world);

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

// What objects[index] is, for the context menu: a signpost (sign_json) or a
// teleporter (its place, whether it is known, whether the player stands at
// it); "null" for anything else.
std::string object_json(const World& world, int index);

// Every teleporter: place, position, known, reachable from here; and the
// one the player stands at ("here", -1: none).
std::string teleporters_json(const World& world);

// The quests: stage (unknown, active, done), whether all steps are done
// ("ready"), and each step; "near": the player can talk to its villager.
std::string quests_json(const World& world);

// The magician's bag: each item (identifier, count, whether it belongs to a
// quest, its price), the coins, and whether a mana potion can be drunk now.
std::string bag_json(const World& world);
// The farm (west.hpp): whether its transfer stone is there, whether Capucine
// is close enough to swap monsters, the team and the monsters kept there.
std::string farm_json(const World& world);
// The merchant's: whether he sells (his quest done), whether he is close by,
// the coins, and what he sells (identifier, price, how many in the bag).
std::string shop_json(const World& world);

}  // namespace monstres
