#pragma once

#include "monstres/core/world.hpp"

namespace monstres {

// Interiors: the crystal cave, the village's houses, the tower of the ruins,
// the burrow, the mine (its galleries going down, a natural cave beyond them
// leading out of the grotto in the mountains)...
//
// The player goes in through a door (Command::Door, near it). The world
// then becomes the interior's: World::objects are its furniture, rocks and
// crystals, the ground is flat at y = 0 (an empty heightmap), its walls and
// its ceiling are World::interior (RoomPiece's: capsules and boxes), and the
// creatures are the interior's (the companion comes along). What was outside
// waits in World::outdoors and comes back when the player goes out.
//
// Interiors are derived from the seed and their door: built again each time
// (the cave's cristis are drawn anew). A save made inside keeps the outside
// world and where the player stands inside (save.hpp, version 22).
//
// The tower of the ruins: a stair along its wall goes up to a landing under
// a trapdoor (Exit Roof), out onto its broken top, and back down.
//
// Cristi (Species::Cristi) climbs: steered in a battle (or training), it
// walks on every surface: the ground, rocks, trunks, houses, furniture, and
// inside the walls and the ceiling (climb()); wild ones wait in the cave,
// still, until the player comes too close.

namespace rules {
inline constexpr float kDoorReach = 1.9f;      // this close to a door: it can be used
inline constexpr float kDoorGlowReach = 5.0f;  // its opening glows from this far
inline constexpr float kRoofHeight = 9.2f;     // the floor of the tower's broken top
inline constexpr float kHiddenCaveScale = 0.6f; // the hidden way in (behind the waterfall): the cave's mouth, smaller
inline constexpr float kCristiWake = 3.2f;     // a wild Cristi scuttles away when the player comes this close
inline constexpr uint32_t kCaveCristisMin = 7, kCaveCristisMore = 4;  // in the cave: 7 to 10
}  // namespace rules

// A tall hall's stair (the tower, the hollow tree, the lighthouse): its steps
// (ObjectKind::Step, variant = which step, two blocks each: by the wall and
// inside) and the landing at its top (this variant).
inline constexpr uint8_t kLandingVariant = 200;

inline bool inside(const World& w) { return w.place.kind != PlaceKind::Outside; }

// What a door does.
enum class DoorAction : uint8_t { None, Enter, Leave, Climb, Descend };

// The door the player stands at (or near, within `reach`).
struct DoorNear {
  DoorAction action = DoorAction::None;
  PlaceKind place = PlaceKind::Outside;  // the interior it leads into, or out of
  ObjectKind building = ObjectKind::Count;  // the cave mouth, the house, the tower
  uint32_t object = 0;   // outside: the building's index in World::objects
  Vec3 at;               // the middle of the opening (for its glow)
  Vec2 facing;           // the way the opening faces (towards who uses it)
  float width = 1, height = 2;  // of the opening
  float distance = 0;
  bool hidden = false;   // the cave's hidden way, behind the waterfall (ObjectKind::HiddenCave)
  bool far = false;      // the mine's far way, out of the grotto (ObjectKind::Grotto)
};
DoorNear door_near(const World& world, float reach = rules::kDoorReach);

// Through the door the player stands at: in, out, up onto the tower's roof,
// down from it. False if there is none (or a battle goes on).
bool use_door(World& world);
// Out by the door one came in by, at once (the Home spell).
void leave_place(World& world);

// Going in (load: the place a save was made in); the player at the door's
// inside, or at the tower's top (`from_roof`). False if the door is not one.
bool enter_place(World& world, Place place, bool from_roof = false);

// The outside world, as it is (outside) or was left (inside): for the map,
// and the saves. A copy.
World outside_world(const World& world);
// Where the player is on the map: outside, at the door it went through.
const Body& map_body(const World& world);

// The interior's shape. Distances in meters, positive in the open.
float wall_distance(const Interior& in, float x, float z);  // to the walls, across the floor
float floor_height(const Interior& in, float x, float z);   // 0 but where the floor goes up and down (the mine)
float ceiling_height(const Interior& in, float x, float z); // its height (not above the floor)
// How much (x, z) is in a natural part (the cave the mine broke into): 0 dug, 1 natural.
float natural_at(const Interior& in, float x, float z);
float open_distance(const Interior& in, Vec3 p);            // to the nearest surface (floor, walls, ceiling)
Vec3 surface_normal(const Interior& in, Vec3 p);            // unit, towards the open

// The interior built for a door (its shape; the objects with interior_objects).
Interior make_interior(uint64_t seed, PlaceKind kind, const Object& door);
std::vector<Object> interior_objects(uint64_t seed, const Interior& in, const Object& door);

// Climbing (Cristi, inside): the way the joystick goes on the surface the
// body clings to. `forward` (fx, fz): the camera's way on the ground; (mx,
// my): the joystick, right and forward. On the floor, as for walkers; on a
// wall, forward goes up it (when facing it); on the ceiling, as seen from
// below (right is the camera's right, the camera turning over with it).
Vec3 climb_direction(const Body& body, float fx, float fz, float mx, float my);
// What a climber clings to, anywhere: the ground outside (or, inside, the
// floor, the walls and the ceiling), and every object (rocks, trunks,
// houses, furniture...: their boxes, edges rounded, flowing into the ground
// where they meet it). Distance to the nearest of them, positive in the open.
float surface_distance(const World& world, Vec3 p);
Vec3 surface_normal(const World& world, Vec3 p);  // unit, towards the open
// One tick of crawling along the surface, `dir` a unit vector (or zero).
void climb(const World& world, Body& body, Vec3 dir, float speed);
// The companion steered on the walls and the ceiling (a battle, inside):
// aimed at a foe, forward goes towards it along the surface (fx, fz: the
// way to it on the ground); else forward is World::climb_forward, which
// turns with the camera's yaw and follows the surface over its corners (up a
// wall, on over the ceiling: never back and forth at the corner). (mx, my):
// the joystick, right and forward.
void climb_steered(World& world, Body& body, float fx, float fz, bool aimed, Angle16 yaw, float mx, float my,
                   float speed);
// v carried along from a surface of normal `from` to one of normal `to`
// (turned by the smallest rotation between them).
Vec3 carried(Vec3 v, Vec3 from, Vec3 to);
// Whether a body clings to a wall or the ceiling (not standing on the floor).
inline bool clinging(const Body& b) { return b.up.y < 0.95f; }

// The way out, from where the player stands (inside): which way to go now
// along the way to the door, round the walls and the rocks of a winding cave
// (the floor's grid, Interior::nav). False outside, or with no way.
bool way_out(const World& world, Vec2& dir);
// Where the way out is `reach` meters ahead of the player (along it, round
// the walls), or the door when nearer (false: no way known).
bool way_out_ahead(const World& world, float reach, Vec2& at);
// Fills Interior::nav for the interior's objects (enter_place does).
void map_way_out(Interior& in, const std::vector<Object>& objects);

// Wild Cristis in the cave: they wait on the floor, the walls and the
// ceiling; scuttle away from the player; drop to the floor to fight.
void spawn_cristis(World& world);
void step_cristi(World& world, Creature& c);
// Taupis in the burrow, digging around its dens.
void spawn_moles(World& world);

}  // namespace monstres
