#pragma once

#include "monstres/core/world.hpp"
#include "monstres/render/models.hpp"
#include "monstres/render/renderer.hpp"

#include <array>
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
  Quat rest{};          // rotation at rest, relative to the parent (before the pose)
};
inline constexpr size_t kMaxRigParts = 16;

// How a part is moved this frame, relative to its rest position.
struct PartPose {
  Quat rotation;
  Vec3f offset;
  Vec3f scale{1, 1, 1};  // multiplies the part's scale (not inherited by children)
};

// Rest rigs, one per character type, as the most common monster of each
// species looks. Villagers differ from one another: their rig (hair, hat)
// depends on the variant (villager_look).
std::span<const RigPart> player_rig();
std::span<const RigPart> creature_rig(Species species);

// How one monster looks, from its genes (variants.hpp): its rig, with parts
// resized, moved, added (horns, a second tail...) or removed, and the colors
// of its tint slots: 1 body, 2 horns and crests, 3 eyes, 4 glowing parts.
// Elemental monsters are wrapped in sparks of their element (aura).
struct CreatureLook {
  std::array<RigPart, kMaxRigParts> parts{};
  size_t count = 0;
  std::array<Color, 4> tints{};
  bool aura = false;
  Color aura_color;
  float extra_height = 0;  // drawn higher than usual (unit size, before the species' scale)

  std::span<const RigPart> rig() const { return {parts.data(), count}; }
  int add(const RigPart& part) {
    parts[count] = part;
    return static_cast<int>(count++);
  }
};
CreatureLook creature_look(Species species, const Genes& genes);

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

// Who a portrait shows: the magician, or a species.
struct PortraitSubject {
  bool wizard = false;
  Species species = Species::Companion;
  uint8_t variant = 0;  // villagers
  Genes genes{};        // monsters
};

// One figure alone, standing at the origin, turned three-quarters towards
// +Z, at rest; returns how high it is drawn (meters), to frame it.
float add_portrait_figure(DrawList& list, const PortraitSubject& subject, float time);

}  // namespace monstres
