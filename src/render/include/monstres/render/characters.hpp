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
  float size = 1;          // females are drawn smaller than males

  std::span<const RigPart> rig() const { return {parts.data(), count}; }
  int add(const RigPart& part) {
    parts[count] = part;
    return static_cast<int>(count++);
  }
};
CreatureLook creature_look(Species species, const Genes& genes);

// Color of the sparks of an element (above 1: glowing).
Color element_glow(Element element);

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
// Parts whose `tint` is `glow_slot` (0: none) give their own light (kGlowing).
void add_rig(DrawList& list, std::span<const RigPart> rig, std::span<const PartPose> poses, const Body& body,
             std::span<const Color> tints, float size = 1, uint8_t glow_slot = 0);
inline void add_rig(DrawList& list, std::span<const RigPart> rig, std::span<const PartPose> poses,
                    const Body& body, Color tint, float size = 1, uint8_t glow_slot = 0) {
  add_rig(list, rig, poses, body, std::span<const Color>(&tint, 1), size, glow_slot);
}

// Where the crystal of the magician's staff is (its right hand).
Vec3 crystal_position(const Body& player);

// Summons and recalls (World::came_out_tick, World::went_in): a beam of
// light of the monster's element between the crystal and the monster, which
// appears or vanishes in light; a captured monster is first held in a circle
// of runes. Durations in ticks:
inline constexpr uint64_t kSummonTicks = 24;   // 0.4 s
inline constexpr uint64_t kRecallTicks = 24;
inline constexpr uint64_t kRuneTicks = 18;     // before a captured monster goes in
void add_crystal_effects(const World& world, float time, DrawList& list);

// Tint slot of the glowing parts: the magician's crystal, monsters' bulbs.
inline constexpr uint8_t kPlayerGlowSlot = 1;
inline constexpr uint8_t kMonsterGlowSlot = 4;

// The small lights a world gives at this time (lanterns at night, the
// staff's crystal, glowing monsters, battle projectiles), the nearest to
// (x, z) first, at most kMaxPointLights. `night`: 0 by day, 1 at night.
// Lights farther than kLightReach from (x, z) are left out.
inline constexpr float kLightReach = 60.0f;
size_t world_lights(const World& world, float x, float z, float night, float time, PointLight* out);

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
