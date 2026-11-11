#pragma once

#include "monstres/core/world.hpp"
#include "monstres/render/models.hpp"
#include "monstres/render/renderer.hpp"

#include <span>
#include <vector>

namespace monstres {

// Characters are made of rigid parts (model files with their origin at the
// joint), assembled by a rig and animated by code from the body's state:
// walking speed, in the air or not, time. Visual only: nothing here changes
// the simulation.

struct Vec3f {
  float x = 0, y = 0, z = 0;
};

struct RigPart {
  MeshId mesh;
  int parent;           // index of the parent part, -1 = the character itself
  Vec3f pivot;          // joint position in the parent's space
  Vec3f scale{1, 1, 1};
  uint8_t tint = 0;     // 0: own colors; n: multiplied by the character's n-th color
};

// How a part is moved this frame, relative to its rest position.
struct PartPose {
  Quat rotation;
  Vec3f offset;
  Vec3f scale{1, 1, 1};  // multiplies the part's scale (not inherited by children)
};

// Rest rigs, one per character type. Villagers differ from one another:
// their rig (hair, hat) depends on the variant.
std::span<const RigPart> player_rig();
std::span<const RigPart> creature_rig(Species species, uint8_t variant = 0);

// A villager's look, derived from its variant.
struct VillagerLook {
  Color shirt, pants, skin, hair, hat;
  MeshId hair_mesh;
  MeshId hat_mesh;  // MeshId::Count: no hat
  bool child;
};
VillagerLook villager_look(uint8_t variant);

// Animation of each rig: fills `poses` (one per part). `phase_offset` keeps
// creatures of the same kind out of sync.
void pose_player(const Body& body, float time, std::vector<PartPose>& poses);
void pose_creature(const Creature& creature, float time, float phase_offset, std::vector<PartPose>& poses);

// Turns a posed rig into instances, placed at the body and facing its way,
// drawn `size` times larger than the models. tints[n - 1] colors the parts
// whose `tint` is n. Parts whose mesh is MeshId::Count are skipped.
void add_rig(DrawList& list, std::span<const RigPart> rig, std::span<const PartPose> poses, const Body& body,
             std::span<const Color> tints, float size = 1);
inline void add_rig(DrawList& list, std::span<const RigPart> rig, std::span<const PartPose> poses,
                    const Body& body, Color tint, float size = 1) {
  add_rig(list, rig, poses, body, std::span<const Color>(&tint, 1), size);
}

// The player, the companion and the wild monsters, with their shadows.
void add_characters(const World& world, float time, DrawList& list);

}  // namespace monstres
