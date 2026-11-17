#pragma once

#include "monstres/core/world.hpp"
#include "monstres/render/renderer.hpp"

namespace monstres {

// Inside (interior.hpp): the floor, the walls and the ceiling as one mesh
// (MeshId::InteriorShell), their faces turned inwards. The cave's is its
// surface (open_distance = 0) on a fine grid, rounded where the floor meets
// the walls and the walls the ceiling: what Cristi climbs is what is drawn.
// Houses and the tower: plain walls of plaster, timber or stone, with
// windows and the door. Seen from outside a wall (the camera behind it),
// the wall is not drawn: the room shows as a doll's house.
MeshData build_interior_mesh(const World& world);

// Its objects (furniture, rocks, crystals, the stair), as a list drawn as is.
void build_interior_list(const World& world, DrawList& list);

}  // namespace monstres
