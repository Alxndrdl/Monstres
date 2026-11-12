#include "monstres/render/characters.hpp"

#include "monstres/render/scene.hpp"

#include "monstres/core/variants.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace monstres {

namespace {

constexpr float kTau = 6.28318530718f;
constexpr Color kWhite{1, 1, 1, 1};

// ---- Rigs (characters face -Z, origin between the feet) ------------------------

// The magician: robe, pointed hat, a staff in the right hand with a glowing crystal (tint 1).
enum PlayerPart { kTorso, kHead, kPlayerEyeL, kPlayerEyeR, kArmL, kArmR, kLegL, kLegR, kStaff, kCrystal };
constexpr RigPart kPlayerRig[] = {
    {MeshId::PlayerTorso, -1, {0, 0.37f, 0}},
    {MeshId::PlayerHead, kTorso, {0, 0.37f, 0}},
    {MeshId::Eye, kHead, {-0.075f, 0.2f, -0.185f}, {0.03f, 0.038f, 0.03f}},
    {MeshId::Eye, kHead, {0.075f, 0.2f, -0.185f}, {0.03f, 0.038f, 0.03f}},
    {MeshId::PlayerArm, kTorso, {-0.205f, 0.31f, 0}},
    {MeshId::PlayerArm, kTorso, {0.205f, 0.31f, 0}},
    {MeshId::PlayerLeg, -1, {-0.095f, 0.38f, 0}},
    {MeshId::PlayerLeg, -1, {0.095f, 0.38f, 0}},
    {MeshId::PlayerStaff, kArmR, {0, -0.29f, -0.02f}},
    {MeshId::PlayerCrystal, kStaff, {0, 0.9f, 0}, {1, 1, 1}, 1},
};
constexpr Color kCrystalGlow{1.7f, 1.8f, 1.9f, 1};

enum CompanionPart { kCBody, kCHead, kCEyeL, kCEyeR, kEarL, kEarR, kTail, kLegFL, kLegFR, kLegBL, kLegBR };
constexpr RigPart kCompanionRig[] = {
    {MeshId::CompanionBody, -1, {0, 0.3f, 0}},
    {MeshId::CompanionHead, kCBody, {0, 0.08f, -0.2f}},
    {MeshId::Eye, kCHead, {-0.065f, 0.11f, -0.135f}, {0.026f, 0.032f, 0.026f}},
    {MeshId::Eye, kCHead, {0.065f, 0.11f, -0.135f}, {0.026f, 0.032f, 0.026f}},
    {MeshId::CompanionEar, kCHead, {-0.085f, 0.18f, -0.01f}},
    {MeshId::CompanionEar, kCHead, {0.085f, 0.18f, -0.01f}},
    {MeshId::CompanionTail, kCBody, {0, 0.06f, 0.2f}},
    {MeshId::CompanionLeg, -1, {-0.09f, 0.2f, -0.12f}},
    {MeshId::CompanionLeg, -1, {0.09f, 0.2f, -0.12f}},
    {MeshId::CompanionLeg, -1, {-0.09f, 0.2f, 0.13f}},
    {MeshId::CompanionLeg, -1, {0.09f, 0.2f, 0.13f}},
};

enum SlimePart { kSBody, kSEyeL, kSEyeR };
constexpr RigPart kSlimeRig[] = {
    {MeshId::SlimeBody, -1, {0, 0, 0}, {1, 1, 1}, 1},
    {MeshId::Eye, kSBody, {-0.1f, 0.33f, -0.255f}, {0.04f, 0.05f, 0.04f}},
    {MeshId::Eye, kSBody, {0.1f, 0.33f, -0.255f}, {0.04f, 0.05f, 0.04f}},
};

enum MushroomPart { kStem, kCap, kMEyeL, kMEyeR, kMLegL, kMLegR };
constexpr RigPart kMushroomRig[] = {
    {MeshId::MushroomStem, -1, {0, 0.12f, 0}},
    {MeshId::MushroomCap, kStem, {0, 0.28f, 0}, {1, 1, 1}, 1},
    {MeshId::Eye, kStem, {-0.05f, 0.18f, -0.1f}, {0.024f, 0.03f, 0.024f}},
    {MeshId::Eye, kStem, {0.05f, 0.18f, -0.1f}, {0.024f, 0.03f, 0.024f}},
    {MeshId::MushroomLeg, -1, {-0.07f, 0.125f, 0}},
    {MeshId::MushroomLeg, -1, {0.07f, 0.125f, 0}},
};

enum BirdPart { kBBody, kWingL, kWingR, kBEyeL, kBEyeR, kBLegL, kBLegR };
constexpr RigPart kBirdRig[] = {
    {MeshId::BirdBody, -1, {0, 0.31f, 0}, {1, 1, 1}, 1},
    {MeshId::BirdWing, kBBody, {-0.17f, 0.02f, 0}, {1, 1, 1}, 1},
    {MeshId::BirdWing, kBBody, {0.17f, 0.02f, 0}, {1, 1, 1}, 1},
    {MeshId::Eye, kBBody, {-0.085f, 0.07f, -0.165f}, {0.028f, 0.034f, 0.028f}},
    {MeshId::Eye, kBBody, {0.085f, 0.07f, -0.165f}, {0.028f, 0.034f, 0.028f}},
    {MeshId::BirdLeg, -1, {-0.07f, 0.125f, 0}},
    {MeshId::BirdLeg, -1, {0.07f, 0.125f, 0}},
};

enum VillagerPart { kVTorso, kVHead, kVHair, kVHat, kVEyeL, kVEyeR, kVArmL, kVArmR, kVHandL, kVHandR, kVLegL, kVLegR };
// Tint slots: 1 shirt, 2 pants, 3 skin, 4 hair, 5 hat.
constexpr RigPart kVillagerRig[] = {
    {MeshId::VillagerTorso, -1, {0, 0.37f, 0}, {1, 1, 1}, 1},
    {MeshId::VillagerHead, kVTorso, {0, 0.37f, 0}, {1, 1, 1}, 3},
    {MeshId::HairShort, kVHead, {0, 0, 0}, {1, 1, 1}, 4},
    {MeshId::Count, kVHead, {0, 0, 0}, {1, 1, 1}, 5},
    {MeshId::Eye, kVHead, {-0.075f, 0.2f, -0.185f}, {0.03f, 0.038f, 0.03f}},
    {MeshId::Eye, kVHead, {0.075f, 0.2f, -0.185f}, {0.03f, 0.038f, 0.03f}},
    {MeshId::VillagerArm, kVTorso, {-0.205f, 0.31f, 0}, {1, 1, 1}, 1},
    {MeshId::VillagerArm, kVTorso, {0.205f, 0.31f, 0}, {1, 1, 1}, 1},
    {MeshId::VillagerHand, kVArmL, {0, -0.3f, 0}, {1, 1, 1}, 3},
    {MeshId::VillagerHand, kVArmR, {0, -0.3f, 0}, {1, 1, 1}, 3},
    {MeshId::VillagerLeg, -1, {-0.095f, 0.38f, 0}, {1, 1, 1}, 2},
    {MeshId::VillagerLeg, -1, {0.095f, 0.38f, 0}, {1, 1, 1}, 2},
};

enum CrowPart { kKBody, kKWingL, kKWingR, kKEyeL, kKEyeR, kKLegL, kKLegR };
constexpr RigPart kCrowRig[] = {
    {MeshId::CrowBody, -1, {0, 0.18f, 0}},
    {MeshId::CrowWing, kKBody, {-0.1f, 0.04f, 0}},
    {MeshId::CrowWing, kKBody, {0.1f, 0.04f, 0}},
    {MeshId::Eye, kKBody, {-0.05f, 0.13f, -0.2f}, {0.018f, 0.022f, 0.018f}},
    {MeshId::Eye, kKBody, {0.05f, 0.13f, -0.2f}, {0.018f, 0.022f, 0.018f}},
    {MeshId::CrowLeg, -1, {-0.04f, 0.09f, 0.02f}},
    {MeshId::CrowLeg, -1, {0.04f, 0.09f, 0.02f}},
};

enum FishPart { kFBody, kFTail, kFFinL, kFFinR, kFEyeL, kFEyeR };
constexpr RigPart kFishRig[] = {
    {MeshId::FishBody, -1, {0, 0.28f, 0}, {1, 1, 1}, 1},
    {MeshId::FishTail, kFBody, {0, 0, 0.27f}},
    {MeshId::FishFin, kFBody, {-0.21f, -0.05f, -0.03f}},
    {MeshId::FishFin, kFBody, {0.21f, -0.05f, -0.03f}},
    {MeshId::Eye, kFBody, {-0.12f, 0.08f, -0.21f}, {0.04f, 0.05f, 0.04f}},
    {MeshId::Eye, kFBody, {0.12f, 0.08f, -0.21f}, {0.04f, 0.05f, 0.04f}},
};

// Drawn size of each species' rig (the models are made at a "unit" size).
float rig_scale(Species species) {
  switch (species) {
    case Species::Companion: return 1.15f;
    case Species::Slime: return 1.5f;
    case Species::Mushroom: return 1.5f;
    case Species::Bird: return 1.4f;
    case Species::Crow: return 1.6f;
    case Species::Fish: return 1.3f;
    default: return 1.0f;
  }
}

// ---- Animation helpers -------------------------------------------------------------

float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }

// Blinks for 0.12 s every 3 to 5 seconds.
bool blinking(float time, float seed) {
  const float period = 3.0f + 2.0f * (0.5f + 0.5f * std::sin(seed * 12.9898f));
  return std::fmod(time + seed * 7.0f, period) < 0.12f;
}

void close_eyes(std::vector<PartPose>& poses, int left, int right, float time, float seed) {
  if (blinking(time, seed)) poses[static_cast<size_t>(left)].scale.y = poses[static_cast<size_t>(right)].scale.y = 0.12f;
}

Color times(Color a, Color b, float k = 1) { return {a.r * b.r * k, a.g * b.g * k, a.b * b.b * k, 1}; }

}  // namespace

// Color of the sparks of an element (above 1: glowing).
Color element_glow(Element e) {
  switch (e) {
    case Element::Fire: return {2.0f, 0.95f, 0.35f, 1};
    case Element::Water: return {0.55f, 1.25f, 2.0f, 1};
    case Element::Nature: return {0.85f, 1.8f, 0.6f, 1};
    case Element::Air: return {1.5f, 1.8f, 2.0f, 1};
    case Element::Shadow: return {1.3f, 0.75f, 1.9f, 1};
    case Element::Light: return {2.0f, 1.9f, 1.25f, 1};
    case Element::Electric: return {2.0f, 1.85f, 0.45f, 1};
    default: return {1.5f, 1.5f, 1.5f, 1};
  }
}

namespace {

// Body color of a monster (tint slot 1, multiplying the model's colors):
// its color allele, or its rare form (the elemental one wins).
Color body_tint(const Genes& g, std::span<const Color> alleles, Color chromatic, Color elemental) {
  if (g.elemental) return elemental;
  if (g.chromatic) return chromatic;
  return alleles[g.allele[1] < alleles.size() ? g.allele[1] : 0];  // gene 0 is the sex
}

// Main colors of the models (tools/make_models.py), to color horns and crests alike.
constexpr float kFemaleSize = 0.86f;  // relative to males

constexpr Color kSlimeColor{0.36f, 0.82f, 0.70f, 1};
constexpr Color kFeatherColor{0.99f, 0.82f, 0.26f, 1};
constexpr Color kFishColor{0.30f, 0.62f, 0.86f, 1};

// Animated glints around elemental monsters.
void add_aura(DrawList& list, const Body& b, float radius, float height, Color color, float time, float seed) {
  constexpr int kSparks = 6;
  for (int i = 0; i < kSparks; ++i) {
    const float k = static_cast<float>(i) / kSparks;
    const float t = std::fmod(time * 0.45f + k + seed * 0.37f, 1.0f);
    const float a = kTau * k + time * 1.4f + seed;
    Instance spark;
    spark.x = b.pos.x + std::cos(a) * radius;
    spark.z = b.pos.z + std::sin(a) * radius;
    spark.y = b.pos.y + height * (0.1f + 0.95f * t);
    spark.sx = spark.sy = spark.sz = 0.055f * (1 - 0.5f * t);
    spark.rotation = quat_yaw(time * 2 + static_cast<float>(i));
    spark.tint = {color.r, color.g, color.b, std::min(1.0f, t * 5) * (1 - t)};
    spark.material = kGlowing;
    list.batches[static_cast<size_t>(MeshId::Sparkle)].push_back(spark);
  }
}

bool is_eye(MeshId mesh) { return mesh == MeshId::Eye || mesh == MeshId::EyeIris; }

// Blinks every eye of a rig.
void blink(std::vector<PartPose>& poses, std::span<const RigPart> rig, float time, float seed) {
  if (!blinking(time, seed)) return;
  for (size_t i = 0; i < rig.size(); ++i) {
    if (is_eye(rig[i].mesh)) poses[i].scale.y = 0.12f;
  }
}

// Height of what is under a body: the top of an object, or the ground.
float support_height(const World& w, const Body& b, const BodyShape& shape) {
  const Box bb = body_box(b, shape);
  float support = w.ground.height(b.pos.x, b.pos.z);
  for (const Object& o : w.objects) {
    const Box ob = object_box(o);
    if (bb.min.x < ob.max.x && bb.max.x > ob.min.x && bb.min.z < ob.max.z && bb.max.z > ob.min.z &&
        ob.max.y <= b.pos.y + 0.01f) {
      support = std::max(support, ob.max.y);
    }
  }
  return support;
}

void add_body_shadow(const World& w, const Body& b, const BodyShape& shape, float radius, DrawList& list) {
  const float support = support_height(w, b, shape);
  const float lift = std::clamp(1.0f - (b.pos.y - support) / 3.0f, 0.35f, 1.0f);
  const bool on_ground = support == w.ground.height(b.pos.x, b.pos.z);  // not on top of an object
  add_shadow(list, b.pos.x, b.pos.z, radius * lift, radius * lift, 0, lift, support + kShadowHeight + 0.005f,
             on_ground ? ground_tilt(w, b.pos.x, b.pos.z) : Quat{});
}

}  // namespace

// ---- Rigs ---------------------------------------------------------------------------

std::span<const RigPart> player_rig() { return kPlayerRig; }

std::span<const RigPart> creature_rig(Species species) {
  switch (species) {
    case Species::Companion: return kCompanionRig;
    case Species::Slime: return kSlimeRig;
    case Species::Mushroom: return kMushroomRig;
    case Species::Bird: return kBirdRig;
    case Species::Crow: return kCrowRig;
    case Species::Fish: return kFishRig;
    case Species::Villager: return kVillagerRig;
    default: return {};
  }
}

CreatureLook creature_look(Species species, const Genes& g) {
  CreatureLook look;
  for (const RigPart& part : creature_rig(species)) look.add(part);
  look.tints = {kWhite, kWhite, kWhite, kWhite};
  if (g.elemental) {
    look.aura = true;
    look.aura_color = element_glow(creature_elements(species, g).first);
  }
  if (genetics(species).gene_count > 0 && g.allele[0] == 1) look.size = kFemaleSize;
  auto& parts = look.parts;
  const auto at = [&](int i) -> RigPart& { return parts[static_cast<size_t>(i)]; };
  switch (species) {
    case Species::Companion: {
      static constexpr Color kFur[] = {{1, 1, 1, 1}, {1.0f, 1.35f, 1.6f, 1}, {0.62f, 0.95f, 1.9f, 1}};
      look.tints[0] = body_tint(g, kFur, {0.8f, 1.6f, 4.8f, 1}, {1.05f, 1.6f, 0.9f, 1});
      for (size_t i = 0; i < look.count; ++i) {
        if (!is_eye(parts[i].mesh)) parts[i].tint = 1;
      }
      if (g.allele[2] == 1) at(kEarL).scale = at(kEarR).scale = {0.9f, 1.7f, 0.9f};    // long ears
      if (g.allele[2] == 2) at(kEarL).scale = at(kEarR).scale = {1.15f, 1.2f, 1.0f};   // floppy (see the pose)
      if (g.allele[3] == 1) {                                                           // two tails
        at(kTail).rest = quat_yaw(0.38f);
        RigPart second = at(kTail);
        second.rest = quat_yaw(-0.38f);
        look.add(second);
      }
      break;
    }
    case Species::Slime: {
      static constexpr Color kColors[] = {{1, 1, 1, 1}, {1.5f, 0.75f, 1.25f, 1}, {1.55f, 1.0f, 0.5f, 1}};
      look.tints[0] = body_tint(g, kColors, {2.5f, 1.15f, 1.35f, 1}, {2.7f, 0.8f, 0.4f, 1});
      look.tints[1] = times(kSlimeColor, look.tints[0], 0.75f);
      look.tints[3] = element_glow(creature_elements(species, g).first);
      if (g.allele[2] == 1) {  // one big eye
        at(kSEyeL).pivot.x = 0;
        at(kSEyeL).scale = {0.058f, 0.072f, 0.058f};
        at(kSEyeR).mesh = MeshId::Count;
      } else if (g.allele[2] == 2) {  // a third eye, above
        look.add({MeshId::Eye, kSBody, {0, 0.42f, -0.2f}, {0.034f, 0.042f, 0.034f}});
      }
      if (g.allele[3] == 1) {  // a horn on top
        look.add({MeshId::Horn, kSBody, {0, 0.44f, 0.02f}, {0.09f, 0.18f, 0.09f}, 2});
        look.extra_height = 0.15f;
      } else if (g.allele[3] == 2) {  // two antennae with glowing tips
        for (const float side : {-1.0f, 1.0f}) {
          const int stalk = look.add({MeshId::Horn, kSBody, {side * 0.1f, 0.42f, 0.02f}, {0.025f, 0.2f, 0.025f}, 2,
                                      quat_z(-side * 0.35f)});
          look.add({MeshId::Bulb, stalk, {0, 0.2f, 0.3f * 0.025f}, {0.035f, 0.035f, 0.035f}, 4});
        }
        look.extra_height = 0.2f;
      }
      break;
    }
    case Species::Mushroom: {
      static constexpr Color kCaps[] = {{1, 1, 1, 1}, {1.05f, 3.2f, 0.9f, 1}, {0.35f, 2.0f, 4.2f, 1}};
      look.tints[0] = body_tint(g, kCaps, {1.1f, 3.8f, 4.0f, 1}, {1.45f, 4.6f, 3.4f, 1});
      if (g.allele[2] == 1) at(kCap).scale = {1.35f, 0.75f, 1.35f};  // wide
      if (g.allele[2] == 2) {                                         // pointed
        at(kCap).scale = {0.78f, 1.65f, 0.78f};
        look.extra_height += 0.14f;
      }
      if (g.allele[3] == 1) {  // long legs: the body stands higher
        constexpr float kLift = 0.1f;
        for (int leg : {kMLegL, kMLegR}) {
          at(leg).scale.y = 1.7f;
          at(leg).pivot.y += kLift;
        }
        at(kStem).pivot.y += kLift;
        look.extra_height += kLift;
      }
      break;
    }
    case Species::Bird: {
      static constexpr Color kPlumages[] = {{1, 1, 1, 1}, {0.6f, 0.9f, 3.2f, 1}, {1.0f, 0.75f, 1.9f, 1},
                                            {0.3f, 1.12f, 1.75f, 1}};  // emerald
      look.tints[0] = body_tint(g, kPlumages, {0.8f, 1.0f, 3.1f, 1}, {0.45f, 0.5f, 1.4f, 1});
      look.tints[1] = times(kFeatherColor, look.tints[0], 0.85f);
      if (g.allele[2] == 1) {  // a crest of three feathers
        for (const float k : {-1.0f, 0.0f, 1.0f}) {
          look.add({MeshId::Horn, kBBody, {k * 0.02f, 0.15f, -0.06f}, {0.03f, 0.15f, 0.03f}, 2,
                    quat_z(-0.4f * k) * quat_x(-0.15f)});
        }
        look.extra_height = 0.12f;
      }
      if (g.allele[3] == 1) at(kWingL).scale = at(kWingR).scale = {1.55f, 1.2f, 1.45f};  // large wings
      break;
    }
    case Species::Crow: {
      if (g.chromatic) look.tints[0] = {7.0f, 7.0f, 5.6f, 1};  // white
      if (g.elemental) look.tints[0] = {7.5f, 6.5f, 3.5f, 1};  // golden, like dawn
      at(kKBody).tint = at(kKWingL).tint = at(kKWingR).tint = 1;
      if (g.allele[1] != 0) {  // white or red eyes
        for (int eye : {kKEyeL, kKEyeR}) {
          at(eye).mesh = MeshId::EyeIris;
          at(eye).tint = 3;
          at(eye).scale = {0.024f, 0.028f, 0.024f};  // a bit bigger, to see their color
        }
        look.tints[2] = g.allele[1] == 1 ? Color{1.05f, 1.05f, 1.05f, 1} : Color{1.5f, 0.18f, 0.12f, 1};
      }
      if (g.allele[2] == 1) {  // horns
        for (const float side : {-1.0f, 1.0f}) {
          look.add({MeshId::Horn, kKBody, {side * 0.045f, 0.165f, -0.14f}, {0.028f, 0.1f, 0.028f}, 2,
                    quat_z(-side * 0.5f)});
        }
        look.tints[1] = {0.86f, 0.8f, 0.68f, 1};
        look.extra_height = 0.06f;
      }
      if (g.allele[3] == 1) at(kKWingL).scale = at(kKWingR).scale = {1.45f, 1.0f, 1.25f};  // long wings
      break;
    }
    case Species::Fish: {
      static constexpr Color kScales[] = {{1, 1, 1, 1}, {2.6f, 0.9f, 0.75f, 1}, {1.2f, 1.4f, 0.6f, 1}};
      look.tints[0] = body_tint(g, kScales, {3.2f, 1.25f, 0.3f, 1}, {0.6f, 0.6f, 0.7f, 1});
      look.tints[1] = times(kFishColor, look.tints[0], 0.8f);
      look.tints[3] = {2.4f, 2.1f, 1.0f, 1};
      if (g.allele[2] == 1) {  // veil fins
        at(kFFinL).scale = at(kFFinR).scale = {2.0f, 2.0f, 2.0f};
        at(kFTail).scale = {1.0f, 1.7f, 1.6f};
      }
      if (g.allele[3] == 1) {  // a glowing lure over the head
        const int stalk = look.add({MeshId::Horn, kFBody, {0, 0.19f, -0.1f}, {0.02f, 0.22f, 0.06f}, 2,
                                    quat_yaw(kTau / 2) * quat_x(0.25f)});
        look.add({MeshId::Bulb, stalk, {0, 0.22f, 0.3f * 0.06f}, {0.05f, 0.05f, 0.05f}, 4});
        look.extra_height = 0.12f;
      }
      break;
    }
    default:
      break;
  }
  return look;
}

VillagerLook villager_look(uint8_t variant) {
  static constexpr Color kShirts[] = {{0.85f, 0.30f, 0.28f, 1}, {0.30f, 0.55f, 0.85f, 1}, {0.40f, 0.70f, 0.35f, 1},
                                      {0.95f, 0.80f, 0.35f, 1}, {0.65f, 0.42f, 0.78f, 1}, {0.95f, 0.95f, 0.92f, 1},
                                      {0.95f, 0.55f, 0.25f, 1}, {0.30f, 0.65f, 0.65f, 1}};
  static constexpr Color kPants[] = {{0.30f, 0.32f, 0.45f, 1}, {0.45f, 0.36f, 0.26f, 1}, {0.28f, 0.28f, 0.28f, 1},
                                     {0.55f, 0.50f, 0.40f, 1}, {0.35f, 0.45f, 0.30f, 1}};
  static constexpr Color kSkins[] = {{0.98f, 0.82f, 0.68f, 1}, {0.90f, 0.70f, 0.52f, 1}, {0.72f, 0.52f, 0.38f, 1},
                                     {0.50f, 0.35f, 0.25f, 1}};
  static constexpr Color kHairs[] = {{0.20f, 0.14f, 0.10f, 1}, {0.45f, 0.28f, 0.14f, 1}, {0.90f, 0.75f, 0.40f, 1},
                                     {0.70f, 0.30f, 0.15f, 1}, {0.80f, 0.80f, 0.82f, 1}, {0.12f, 0.12f, 0.14f, 1}};
  static constexpr Color kHats[] = {{0.85f, 0.75f, 0.45f, 1}, {0.80f, 0.25f, 0.25f, 1}, {0.25f, 0.40f, 0.70f, 1},
                                    {0.35f, 0.60f, 0.35f, 1}};
  static constexpr MeshId kHairMeshes[] = {MeshId::HairShort, MeshId::HairLong, MeshId::HairBun};
  static constexpr MeshId kHatMeshes[] = {MeshId::Count, MeshId::HatCap, MeshId::HatStraw, MeshId::HatBeanie};
  // Spread the bits of the variant over the different features.
  const uint32_t h = static_cast<uint32_t>(variant) * 2654435761u;
  VillagerLook look;
  look.shirt = kShirts[(h >> 3) % 8];
  look.pants = kPants[(h >> 7) % 5];
  look.skin = kSkins[(h >> 11) % 4];
  look.hair = kHairs[(h >> 14) % 6];
  look.hair_mesh = kHairMeshes[(h >> 17) % 3];
  look.hat_mesh = kHatMeshes[(h >> 20) % 4];
  look.hat = look.hat_mesh == MeshId::HatStraw ? Color{0.92f, 0.82f, 0.55f, 1} : kHats[(h >> 23) % 4];
  look.child = variant % 5 == 0;
  return look;
}

// ---- Poses --------------------------------------------------------------------------

void pose_player(const Body& b, float time, std::vector<PartPose>& poses) {
  poses.assign(std::size(kPlayerRig), PartPose{});
  const float walk = clamp01(b.speed / rules::kWalkSpeed);
  const float phase = time * kTau * 2.2f;
  const float swing = std::sin(phase) * 0.65f * walk;

  if (b.on_ground) {
    poses[kLegL].rotation = quat_x(swing);
    poses[kLegR].rotation = quat_x(-swing);
    poses[kArmL].rotation = quat_x(-swing * 0.8f) * quat_z(-0.1f);
    poses[kArmR].rotation = quat_x(swing * 0.8f) * quat_z(0.1f);
    poses[kTorso].offset.y = std::fabs(std::sin(phase)) * 0.035f * walk;
    poses[kTorso].rotation = quat_x(-0.08f * walk);
    // Breathing when standing still.
    const float breath = std::sin(time * 2.0f) * (1 - walk);
    poses[kTorso].scale.y = 1 + 0.015f * breath;
    poses[kHead].rotation = quat_x(0.04f * breath);
  } else {
    // In the air: one knee up, arms out.
    const float rising = clamp01(b.velocity_y / rules::kJumpSpeed);
    poses[kLegL].rotation = quat_x(0.6f);
    poses[kLegR].rotation = quat_x(-0.25f);
    poses[kArmL].rotation = quat_z(-0.9f - 0.4f * rising);
    poses[kArmR].rotation = quat_z(0.9f + 0.4f * rising);
    poses[kHead].rotation = quat_x(-0.1f * rising);
  }
  // The right hand holds the staff: the arm swings less, the staff stays
  // nearly upright whatever the arm does.
  if (b.on_ground) poses[kArmR].rotation = quat_x(0.25f + swing * 0.35f) * quat_z(0.12f);
  const Quat arm = poses[kArmR].rotation;
  poses[kStaff].rotation = Quat{-arm.x, -arm.y, -arm.z, arm.w} * quat_x(0.12f);
  poses[kCrystal].rotation = quat_yaw(time * 1.6f);
  poses[kCrystal].offset.y = 0.015f * std::sin(time * 2.5f);
  close_eyes(poses, kPlayerEyeL, kPlayerEyeR, time, 0.3f);
}

void pose_creature(const Creature& c, float time, float seed, std::vector<PartPose>& poses) {
  const Body& b = c.body;
  const SpeciesInfo& info = species_info(c.species);
  const float walk = clamp01(b.speed / info.walk_speed);
  const float t = time + seed * 3.1f;
  const CreatureLook look = creature_look(c.species, c.genes);
  const std::span<const RigPart> rig = look.rig();
  poses.assign(rig.size(), PartPose{});

  switch (c.species) {
    case Species::Companion: {
      const float phase = t * kTau * (2.0f + 1.5f * walk);
      const float swing = std::sin(phase) * 0.7f * clamp01(walk * 1.3f);
      if (b.on_ground) {
        poses[kLegFL].rotation = poses[kLegBR].rotation = quat_x(swing);
        poses[kLegFR].rotation = poses[kLegBL].rotation = quat_x(-swing);
        poses[kCBody].offset.y = std::fabs(std::sin(phase)) * 0.03f * walk;
      } else {
        poses[kLegFL].rotation = poses[kLegFR].rotation = quat_x(0.7f);
        poses[kLegBL].rotation = poses[kLegBR].rotation = quat_x(-0.7f);
      }
      // Tail wags (faster when happy to run), head tilts when idle, ears twitch.
      const Quat wag = quat_yaw(std::sin(t * (5.0f + 9.0f * walk)) * 0.5f) * quat_x(-0.2f);
      for (size_t i = 0; i < rig.size(); ++i) {
        if (rig[i].mesh == MeshId::CompanionTail) poses[i].rotation = wag;
      }
      poses[kCHead].rotation = quat_z(std::sin(t * 0.8f) * 0.18f * (1 - walk)) * quat_x(0.08f * std::sin(phase) * walk);
      const float twitch = std::sin(t * 1.3f) > 0.97f ? 0.35f : 0.0f;
      const float spread = c.genes.allele[2] == 2 ? 1.3f : 0.35f;  // floppy ears hang on the sides
      poses[kEarL].rotation = quat_z(spread) * quat_x(-twitch - (b.on_ground ? 0 : 0.5f));
      poses[kEarR].rotation = quat_z(-spread) * quat_x(-twitch - (b.on_ground ? 0 : 0.5f));
      break;
    }
    case Species::Slime: {
      // Squash and stretch: stretched going up, squashed coming down.
      float stretch = 0;
      if (!b.on_ground) {
        stretch = std::clamp(b.velocity_y / info.jump_speed, -1.0f, 1.0f) * 0.2f;
      } else {
        stretch = std::sin(t * 3.0f) * 0.04f;
      }
      const Vec3f s{1 - stretch * 0.5f, 1 + stretch, 1 - stretch * 0.5f};
      poses[kSBody].scale = s;
      for (size_t i = 0; i < rig.size(); ++i) {  // eyes and horns follow the squashed surface
        if (rig[i].parent != kSBody) continue;
        const Vec3f pivot = rig[i].pivot;
        poses[i].offset = {pivot.x * (s.x - 1), pivot.y * (s.y - 1), pivot.z * (s.z - 1)};
      }
      break;
    }
    case Species::Mushroom: {
      const float phase = t * kTau * 2.6f;
      const float swing = std::sin(phase) * 0.6f * walk;
      poses[kMLegL].rotation = quat_x(swing);
      poses[kMLegR].rotation = quat_x(-swing);
      poses[kStem].rotation = quat_z(std::sin(phase) * 0.14f * walk);  // waddle
      poses[kCap].offset.y = std::fabs(std::sin(phase)) * 0.02f * walk;
      poses[kCap].scale.y = 1 + 0.04f * std::sin(t * 2.0f) * (1 - walk);
      break;
    }
    case Species::Bird: {
      const float phase = t * kTau * 4.0f;
      const float swing = std::sin(phase) * 0.8f * clamp01(walk);
      poses[kBLegL].rotation = quat_x(swing);
      poses[kBLegR].rotation = quat_x(-swing);
      poses[kBBody].offset.y = std::fabs(std::sin(phase)) * 0.025f * walk;
      // Flap when in the air or running away; otherwise peck around.
      const bool flapping = !b.on_ground || c.mind == Mind::Flee;
      const float flap = flapping ? 0.4f + 0.7f * std::sin(t * 28.0f) : 0.0f;
      poses[kWingR].rotation = quat_z(flap);
      poses[kWingL].rotation = quat_yaw(kTau / 2) * quat_z(flap);
      poses[kBBody].rotation = quat_x(flapping ? -0.15f : std::max(0.0f, std::sin(t * 1.4f)) * 0.25f * (1 - walk));
      break;
    }
    case Species::Villager: {
      const float phase = t * kTau * 1.8f;
      const float swing = std::sin(phase) * 0.55f * walk;
      poses[kVLegL].rotation = quat_x(swing);
      poses[kVLegR].rotation = quat_x(-swing);
      poses[kVArmL].rotation = quat_x(-swing * 0.8f) * quat_z(-0.08f);
      poses[kVArmR].rotation = quat_x(swing * 0.8f) * quat_z(0.08f);
      poses[kVTorso].offset.y = std::fabs(std::sin(phase)) * 0.03f * walk;
      const float breath = std::sin(t * 1.8f) * (1 - walk);
      poses[kVTorso].scale.y = 1 + 0.015f * breath;
      if (c.mind == Mind::Greet) {  // waving at the player
        poses[kVArmR].rotation = quat_z(2.5f + 0.35f * std::sin(t * 9.0f));
        poses[kVHead].rotation = quat_x(0.08f * std::sin(t * 4.5f));
      } else {
        poses[kVHead].rotation = quat_yaw(std::sin(t * 0.5f) * 0.25f * (1 - walk));
      }
      break;
    }
    case Species::Crow: {
      if (!b.on_ground) {
        const float flap = 0.2f + 0.9f * std::sin(t * 15.0f);
        poses[kKWingR].rotation = quat_z(flap);
        poses[kKWingL].rotation = quat_yaw(kTau / 2) * quat_z(flap);
        poses[kKLegL].rotation = poses[kKLegR].rotation = quat_x(-1.2f);
        poses[kKBody].rotation = quat_x(0.1f);
      } else {
        poses[kKWingL].rotation = quat_yaw(kTau / 2);
        // Looks around, pecks now and then.
        const float peck = std::max(0.0f, std::sin(t * 0.9f) - 0.85f) * 3.0f;
        poses[kKBody].rotation = quat_yaw(std::sin(t * 0.6f) * 0.5f) * quat_x(peck * 0.5f);
      }
      break;
    }
    case Species::Fish: {
      const float wag = std::sin(t * (5.0f + 6.0f * walk)) * (0.25f + 0.35f * walk);
      poses[kFTail].rotation = quat_yaw(wag);
      poses[kFBody].rotation = quat_yaw(-wag * 0.15f) * quat_x(-std::clamp(b.velocity_y, -6.0f, 6.0f) * 0.12f);
      const float paddle = 0.3f * std::sin(t * 6.0f);
      poses[kFFinR].rotation = quat_z(paddle);
      poses[kFFinL].rotation = quat_yaw(kTau / 2) * quat_z(paddle);
      break;
    }
    default:
      break;
  }
  blink(poses, rig, time, seed);
}

// ---- Assembly -------------------------------------------------------------------------

void add_rig(DrawList& list, std::span<const RigPart> rig, std::span<const PartPose> poses, const Body& body,
             std::span<const Color> tints, float size, uint8_t glow_slot) {
  constexpr size_t kMaxParts = kMaxRigParts;
  std::array<Quat, kMaxParts> world_rotation;
  std::array<Vec3f, kMaxParts> world_position;
  const Quat root = quat_yaw(std::atan2(-body.facing_x, -body.facing_z));

  for (size_t i = 0; i < rig.size() && i < kMaxParts; ++i) {
    const RigPart& part = rig[i];
    const PartPose& pose = poses[i];
    const bool is_root = part.parent < 0;
    const Quat parent_rotation = is_root ? root : world_rotation[static_cast<size_t>(part.parent)];
    const Vec3f parent_position = is_root ? Vec3f{body.pos.x, body.pos.y, body.pos.z}
                                          : world_position[static_cast<size_t>(part.parent)];
    float x = (part.pivot.x + pose.offset.x) * size;
    float y = (part.pivot.y + pose.offset.y) * size;
    float z = (part.pivot.z + pose.offset.z) * size;
    rotate(parent_rotation, x, y, z);
    world_position[i] = {parent_position.x + x, parent_position.y + y, parent_position.z + z};
    world_rotation[i] = parent_rotation * part.rest * pose.rotation;

    Instance in;
    in.x = world_position[i].x;
    in.y = world_position[i].y;
    in.z = world_position[i].z;
    in.rotation = world_rotation[i];
    in.sx = part.scale.x * pose.scale.x * size;
    in.sy = part.scale.y * pose.scale.y * size;
    in.sz = part.scale.z * pose.scale.z * size;
    in.tint = part.tint > 0 && part.tint <= tints.size() ? tints[part.tint - 1] : kWhite;
    if (glow_slot > 0 && part.tint == glow_slot) in.material = kGlowing;
    if (part.mesh != MeshId::Count) list.batches[static_cast<size_t>(part.mesh)].push_back(in);
  }
}

float add_portrait_figure(DrawList& list, const PortraitSubject& subject, float time) {
  Body body;
  body.facing_x = 0.45f;  // three-quarters towards the camera (+Z)
  body.facing_z = 0.89f;
  std::vector<PartPose> poses;
  if (subject.wizard) {
    pose_player(body, time, poses);
    add_rig(list, player_rig(), poses, body, kCrystalGlow, 1, kPlayerGlowSlot);
    return 1.55f;  // with the hat
  }
  Creature c;
  c.species = subject.species;
  c.variant = subject.variant;
  c.genes = subject.genes;
  c.body = body;
  pose_creature(c, time, 0.4f, poses);
  if (c.species == Species::Villager) {
    const VillagerLook look = villager_look(c.variant);
    std::array<RigPart, std::size(kVillagerRig)> rig;
    std::copy(std::begin(kVillagerRig), std::end(kVillagerRig), rig.begin());
    rig[kVHair].mesh = look.hair_mesh;
    rig[kVHat].mesh = look.hat_mesh;
    const Color tints[] = {look.shirt, look.pants, look.skin, look.hair, look.hat};
    add_rig(list, rig, poses, c.body, tints);
    return 1.3f;
  }
  const CreatureLook look = creature_look(c.species, c.genes);
  const float scale = rig_scale(c.species);
  add_rig(list, look.rig(), poses, c.body, look.tints, scale * look.size, kMonsterGlowSlot);
  // Drawn taller than their body for the rules: the fish floats half under
  // water, the crow stands on its tail feathers.
  float height = species_info(c.species).shape.height * 1.1f;
  if (c.species == Species::Fish) height = 0.8f;
  if (c.species == Species::Crow) height = 0.7f;
  height += look.extra_height * scale;
  if (look.aura) {
    const BodyShape& shape = species_info(c.species).shape;
    add_aura(list, c.body, (shape.half_width * 1.5f + 0.1f) * look.size, height * look.size, look.aura_color, time,
             0.4f);
  }
  return height;  // framed for a male: a female looks smaller
}

// A monster appearing (t from 0 to 1) or vanishing (t from 1 to 0) in the
// light of its element: smaller, whiter.
void materialize(CreatureLook& look, Element element, float t) {
  const float grow = t < 0.7f ? t / 0.7f * 1.08f : 1.08f - 0.08f * (t - 0.7f) / 0.3f;  // a small bounce
  look.size *= std::max(grow, 0.05f);
  const Color glow = element_glow(element);
  const float white = std::clamp(1.4f - 1.6f * t, 0.0f, 1.0f);
  for (Color& c : look.tints) {
    c = {c.r + (glow.r * 1.3f - c.r) * white, c.g + (glow.g * 1.3f - c.g) * white,
         c.b + (glow.b * 1.3f - c.b) * white, c.a};
  }
}

// A monster going into the crystal (t from 0 to 1): it flashes, then shrinks to a point.
void vanish(CreatureLook& look, Element element, float t) {
  look.size *= std::max(1 - t * t * 0.4f - t * 0.6f, 0.03f);
  const Color glow = element_glow(element);
  const float white = std::min(1.0f, t * 3);
  for (Color& c : look.tints) {
    c = {c.r + (glow.r * 1.3f - c.r) * white, c.g + (glow.g * 1.3f - c.g) * white,
         c.b + (glow.b * 1.3f - c.b) * white, c.a};
  }
}

// In a battle: a fighter lunges when it bites, and flashes when it is hurt.
void battle_pose(const World& w, size_t index, CreatureLook& look, Body& body) {
  const Battle& b = w.battle;
  if (b.phase != BattlePhase::Fight || (index != 0 && index != b.enemy)) return;
  const Fighter& f = index == 0 ? b.ally : b.foe;
  const uint64_t since_attack = w.tick - f.attack_tick;
  if (f.attack_tick > 0 && f.attack_move == 0 && since_attack < 12) {
    const float k = 0.35f * std::sin((kTau / 2) * static_cast<float>(since_attack) / 12.0f);
    body.pos.x += body.facing_x * k;
    body.pos.z += body.facing_z * k;
  }
  if (f.state == FighterState::Windup) {  // it gathers itself: drawn a little lower and wider
    body.pos.y -= 0.03f;
  }
  if (f.hit_tick > 0 && w.tick - f.hit_tick < 8) {
    for (Color& t : look.tints) t = {t.r * 2.4f + 0.4f, t.g * 1.3f + 0.2f, t.b * 1.3f + 0.2f, t.a};
  }
}

// Turns what was added to the list since `before` around `pivot` by `q`, then lifts it.
void turn_added(DrawList& list, const std::array<size_t, kMeshCount>& before, Vec3 pivot, Quat q, float lift) {
  for (size_t m = 0; m < kMeshCount; ++m) {
    auto& batch = list.batches[m];
    for (size_t i = before[m]; i < batch.size(); ++i) {
      Instance& in = batch[i];
      float x = in.x - pivot.x, y = in.y - pivot.y, z = in.z - pivot.z;
      rotate(q, x, y, z);
      in.x = pivot.x + x;
      in.y = pivot.y + y + lift;
      in.z = pivot.z + z;
      in.rotation = q * in.rotation;
    }
  }
}

// Knocked out: lying on its side (a fish belly up), eyes shut, stars turning above.
void lie_knocked_out(DrawList& list, const std::array<size_t, kMeshCount>& before, const Creature& c, float time) {
  const BodyShape& shape = species_info(c.species).shape;
  const bool fish = c.species == Species::Fish;
  const Quat roll = quat_axis(c.body.facing_x, 0, c.body.facing_z, fish ? kTau / 2 : kTau / 4);
  turn_added(list, before, c.body.pos, roll, fish ? shape.height * 0.9f : shape.half_width * 0.85f);
  const float top = c.body.pos.y + (fish ? shape.height : shape.half_width * 2.0f) + 0.25f;
  for (int k = 0; k < 3; ++k) {
    const float a = time * 3 + kTau * static_cast<float>(k) / 3;
    Instance star;
    star.x = c.body.pos.x + std::cos(a) * 0.3f;
    star.y = top + 0.05f * std::sin(time * 4 + static_cast<float>(k));
    star.z = c.body.pos.z + std::sin(a) * 0.3f;
    star.sx = star.sy = star.sz = 0.07f;
    star.rotation = quat_yaw(a);
    star.tint = {2.0f, 1.9f, 0.6f, 1};
    star.material = kGlowing;
    list.batches[static_cast<size_t>(MeshId::Sparkle)].push_back(star);
  }
}

void add_characters(const World& w, float time, DrawList& list) {
  std::vector<PartPose> poses;
  pose_player(w.player, time, poses);
  if (w.battle.phase == BattlePhase::Training) {
    // The staff held out in front: it keeps the shield up.
    poses[kArmR].rotation = quat_x(1.3f) * quat_z(0.12f);
    const Quat arm = poses[kArmR].rotation;
    poses[kStaff].rotation = Quat{-arm.x, -arm.y, -arm.z, arm.w} * quat_x(0.35f);
  }
  add_rig(list, player_rig(), poses, w.player, kCrystalGlow, 1, kPlayerGlowSlot);
  add_body_shadow(w, w.player, kPlayerShape, 0.5f, list);

  std::array<RigPart, std::size(kVillagerRig)> villager_rig;
  for (size_t i = 0; i < w.creatures.size(); ++i) {
    if (i == 0 && w.active < 0) continue;  // in the crystal
    const Creature& c = w.creatures[i];
    pose_creature(c, time, static_cast<float>(i) * 0.731f, poses);
    float shadow_size = 1;
    if (c.species == Species::Villager) {
      const VillagerLook look = villager_look(c.variant);
      std::copy(std::begin(kVillagerRig), std::end(kVillagerRig), villager_rig.begin());
      villager_rig[kVHair].mesh = look.hair_mesh;
      villager_rig[kVHat].mesh = look.hat_mesh;
      const Color tints[] = {look.shirt, look.pants, look.skin, look.hair, look.hat};
      add_rig(list, villager_rig, poses, c.body, tints, look.child ? 0.75f : 1.0f);
    } else {
      CreatureLook look = creature_look(c.species, c.genes);
      Body body = c.body;
      battle_pose(w, i, look, body);
      const bool released = i > 0 && c.id == w.released_id && w.released_tick > 0;
      const uint64_t came_out = i == 0 ? w.came_out_tick : released ? w.released_tick : 0;
      if (came_out > 0 && w.tick - came_out < kSummonTicks) {
        // Just summoned (or let go): it grows out of the light.
        const float t = static_cast<float>(w.tick - came_out) / kSummonTicks;
        materialize(look, creature_elements(c.species, c.genes).first, t);
      }
      const bool knocked_out = c.mind == Mind::KnockedOut;
      std::array<size_t, kMeshCount> before{};
      if (knocked_out) {
        for (size_t m = 0; m < kMeshCount; ++m) before[m] = list.batches[m].size();
        for (size_t k = 0; k < look.count; ++k) {
          if (is_eye(look.parts[k].mesh)) poses[k].scale.y = 0.12f;
        }
      }
      add_rig(list, look.rig(), poses, body, look.tints, rig_scale(c.species) * look.size, kMonsterGlowSlot);
      if (knocked_out) lie_knocked_out(list, before, c, time);
      if (look.aura) {
        const BodyShape& shape = species_info(c.species).shape;
        add_aura(list, c.body, (shape.half_width * 1.5f + 0.1f) * look.size, shape.height * 1.2f * look.size,
                 look.aura_color, time, static_cast<float>(i));
      }
      shadow_size = look.size;
    }
    if (c.species == Species::Fish) continue;  // no shadow on water
    const BodyShape& shape = species_info(c.species).shape;
    add_body_shadow(w, c.body, shape, shape.half_width * 1.7f * shadow_size, list);
  }
}

}  // namespace monstres

namespace monstres {

Vec3 crystal_position(const Body& p) {
  return {p.pos.x + p.facing_x * 0.15f - p.facing_z * 0.3f, p.pos.y + 1.45f,
          p.pos.z + p.facing_z * 0.15f + p.facing_x * 0.3f};
}

namespace {

void add_glow(DrawList& list, MeshId mesh, Instance in) {
  in.material = kGlowing;
  list.batches[static_cast<size_t>(mesh)].push_back(in);
}

// A beam of light from a to b, fading with `alpha`.
void add_beam(DrawList& list, Vec3 a, Vec3 b, Color color, float alpha, float time) {
  const float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
  const float length = std::sqrt(dx * dx + dy * dy + dz * dz);
  if (length < 1e-3f || alpha <= 0) return;
  Instance beam;
  beam.x = (a.x + b.x) / 2;
  beam.y = (a.y + b.y) / 2;
  beam.z = (a.z + b.z) / 2;
  // Turn the mesh's up axis along the beam.
  const float ax = dz / length, az = -dx / length;
  const float axis = std::sqrt(ax * ax + az * az);
  if (axis > 1e-4f) beam.rotation = quat_axis(ax / axis, 0, az / axis, std::acos(std::clamp(dy / length, -1.0f, 1.0f)));
  beam.sx = beam.sz = 0.07f;
  beam.sy = length / 2;
  beam.tint = {color.r, color.g, color.b, alpha};
  add_glow(list, MeshId::Sparkle, beam);
  beam.sx = beam.sz = 0.16f;  // a softer glow around it
  beam.tint.a = alpha * 0.35f;
  add_glow(list, MeshId::Sparkle, beam);
  for (int k = 0; k < 4; ++k) {  // sparks running along
    const float u = std::fmod(time * 3.0f + static_cast<float>(k) * 0.25f, 1.0f);
    Instance spark;
    spark.x = a.x + dx * u;
    spark.y = a.y + dy * u;
    spark.z = a.z + dz * u;
    spark.sx = spark.sy = spark.sz = 0.09f;
    spark.rotation = quat_yaw(time * 5 + static_cast<float>(k));
    spark.tint = {color.r, color.g, color.b, alpha};
    add_glow(list, MeshId::Sparkle, spark);
  }
}

// Sparks bursting out of a point.
void add_burst(DrawList& list, Vec3 at, Color color, float t, float radius) {
  for (int k = 0; k < 10; ++k) {
    const float a = kTau * static_cast<float>(k) / 10;
    Instance spark;
    spark.x = at.x + std::cos(a) * radius * t;
    spark.y = at.y + 0.3f * t + 0.15f * std::sin(a * 3);
    spark.z = at.z + std::sin(a) * radius * t;
    spark.sx = spark.sy = spark.sz = 0.07f * (1 - 0.5f * t);
    spark.tint = {color.r, color.g, color.b, 1 - t};
    add_glow(list, MeshId::Sparkle, spark);
  }
}

}  // namespace

void add_crystal_effects(const World& w, float time, DrawList& list) {
  const Vec3 crystal = crystal_position(w.player);

  // Let go: out of the crystal one last time.
  if (w.released_tick > 0 && w.tick - w.released_tick < kSummonTicks) {
    const int index = find_creature(w, w.released_id);
    if (index > 0) {
      const Creature& c = w.creatures[static_cast<size_t>(index)];
      const float t = static_cast<float>(w.tick - w.released_tick) / kSummonTicks;
      const Color glow = element_glow(creature_elements(c.species, c.genes).first);
      const float h = species_info(c.species).shape.height * 0.5f;
      add_beam(list, crystal, {c.body.pos.x, c.body.pos.y + h, c.body.pos.z}, glow, 1 - t * t, time);
      add_burst(list, {c.body.pos.x, c.body.pos.y + 0.1f, c.body.pos.z}, glow, t, 0.9f);
    }
  }

  // Summoned: the beam reaches where it appears.
  if (w.came_out_tick > 0 && w.tick - w.came_out_tick < kSummonTicks && w.active >= 0 && !w.creatures.empty()) {
    const Creature& c = w.creatures[0];
    const float t = static_cast<float>(w.tick - w.came_out_tick) / kSummonTicks;
    const Color glow = element_glow(creature_elements(c.species, c.genes).first);
    const float h = species_info(c.species).shape.height * 0.5f;
    add_beam(list, crystal, {c.body.pos.x, c.body.pos.y + h, c.body.pos.z}, glow, 1 - t * t, time);
    add_burst(list, {c.body.pos.x, c.body.pos.y + 0.1f, c.body.pos.z}, glow, t, 0.9f);
  }

  // Gone in: held by runes first if captured, then it vanishes into the beam.
  const CrystalTrace& g = w.went_in;
  if (g.tick == 0) return;
  const uint64_t since = w.tick - g.tick;
  const uint64_t runes = g.captured ? kRuneTicks : 0;
  if (since >= runes + kRecallTicks) return;
  const Color glow = element_glow(creature_elements(g.species, g.genes).first);
  const BodyShape& shape = species_info(g.species).shape;
  if (g.captured) {
    // The seal: it opens under the monster, turns, and fades once it is in.
    const float open = std::min(1.0f, static_cast<float>(since) / (kRuneTicks * 0.6f));
    const float fade = since < runes ? 1.0f : 1 - static_cast<float>(since - runes) / kRecallTicks;
    Instance seal;
    seal.x = g.body.pos.x;
    seal.y = g.body.pos.y + 0.04f;
    seal.z = g.body.pos.z;
    seal.rotation = quat_yaw(time * 2.5f);
    seal.sx = seal.sz = (shape.half_width * 2.4f + 0.5f) * (0.3f + 0.7f * open);
    seal.tint = {1.9f, 1.7f, 1.0f, 0.95f * fade};  // golden runes
    add_glow(list, MeshId::RuneCircle, seal);
    seal.rotation = quat_yaw(-time * 1.5f);  // an inner one, turning the other way, of its element
    seal.sx = seal.sz *= 0.55f;
    seal.tint = {glow.r, glow.g, glow.b, 0.8f * fade};
    add_glow(list, MeshId::RuneCircle, seal);
  }
  // The monster itself, held still, then vanishing.
  Creature ghost;
  ghost.species = g.species;
  ghost.genes = g.genes;
  ghost.body = g.body;
  ghost.body.speed = 0;
  CreatureLook look = creature_look(g.species, g.genes);
  const float t = since < runes ? 0.0f : static_cast<float>(since - runes) / kRecallTicks;  // 0 .. 1: going in
  if (since < runes) {
    // Held by the seal: it trembles and starts to glow.
    const float k = static_cast<float>(since) / static_cast<float>(runes);
    ghost.body.pos.x += 0.03f * std::sin(time * 60.0f) * k;
    for (Color& c : look.tints) c = {c.r * (1 + k), c.g * (1 + k), c.b * (1 + k), c.a};
  } else {
    vanish(look, creature_elements(g.species, g.genes).first, t);
    ghost.body.pos.y += 0.3f * t;
    const float h = shape.height * 0.5f * look.size;
    add_beam(list, {ghost.body.pos.x, ghost.body.pos.y + h, ghost.body.pos.z}, crystal, glow, 1 - t * t, time);
  }
  std::vector<PartPose> poses;
  pose_creature(ghost, time, 0, poses);
  add_rig(list, look.rig(), poses, ghost.body, look.tints, rig_scale(g.species) * look.size, kMonsterGlowSlot);
}

size_t world_lights(const World& w, float x, float z, float night, float time, PointLight* out) {
  struct Candidate {
    PointLight light;
    float distance2;
  };
  std::vector<Candidate> found;
  const auto scaled = [](Color c, float k) { return Color{c.r * k, c.g * k, c.b * k, 1}; };
  const auto offer = [&](float lx, float ly, float lz, float radius, Color color) {
    if (color.r + color.g + color.b < 0.01f) return;
    const float dx = lx - x, dz = lz - z;
    if (dx * dx + dz * dz > kLightReach * kLightReach) return;  // too far to matter
    found.push_back({{lx, ly, lz, radius, color}, dx * dx + dz * dz});
  };

  // The staff's crystal, in the magician's right hand: first, always kept.
  const Body& p = w.player;
  const float flicker = 1 + 0.05f * std::sin(time * 7.0f);
  const Vec3 crystal = crystal_position(p);
  PointLight staff{crystal.x, crystal.y, crystal.z, 6.5f, scaled({0.55f, 0.75f, 1.0f, 1}, 1.2f * night * flicker)};

  for (const Object& o : w.objects) {
    if (o.kind == ObjectKind::Lamp) offer(o.pos.x, o.pos.y + 2.2f, o.pos.z, 8.0f, scaled({1.0f, 0.68f, 0.32f, 1}, 1.5f * night));
    // Crystals glow in the dark of the cave, and at night.
    if (o.kind == ObjectKind::Crystal) {
      const bool violet = o.variant % 2 == 0;
      offer(o.pos.x, o.pos.y + 0.6f * o.size.x, o.pos.z, 3.0f + 3.0f * o.size.x,
            scaled(violet ? Color{0.75f, 0.45f, 1.0f, 1} : Color{0.4f, 0.85f, 1.0f, 1}, 0.35f + 0.9f * night));
    }
    if (o.kind == ObjectKind::Teleporter && ((w.teleporters >> o.variant) & 1u)) {
      offer(o.pos.x, o.pos.y + 0.8f, o.pos.z, 5.0f, scaled({0.45f, 0.9f, 1.0f, 1}, 0.2f + 0.9f * night));
    }
    if (o.kind == ObjectKind::CastleGate) {  // its two torches
      const SinCos sc = sincos16(o.yaw);
      for (float side : {-1.9f, 1.9f}) {
        const float lx = side, lz = -1.7f;
        offer(o.pos.x + lx * sc.cos + lz * sc.sin, o.pos.y + 3.3f, o.pos.z - lx * sc.sin + lz * sc.cos, 7.0f,
              scaled({1.0f, 0.62f, 0.28f, 1}, 1.4f * night));
      }
    }
  }
  for (size_t i = 0; i < w.creatures.size(); ++i) {
    const Creature& c = w.creatures[i];
    if (!monster_stats(c.species).is_monster || (i == 0 && w.active < 0)) continue;
    const CreatureLook look = creature_look(c.species, c.genes);
    const BodyShape& shape = species_info(c.species).shape;
    bool bulbs = false;
    for (size_t k = 0; k < look.count; ++k) bulbs = bulbs || (look.parts[k].tint == kMonsterGlowSlot && look.parts[k].mesh != MeshId::Count);
    if (!look.aura && !bulbs) continue;
    const Color glow = look.aura ? look.aura_color : look.tints[kMonsterGlowSlot - 1];
    offer(c.body.pos.x, c.body.pos.y + shape.height * 0.8f, c.body.pos.z, 3.5f, scaled(glow, 0.4f * night));
  }
  for (const Projectile& pr : w.battle.projectiles) {
    offer(pr.pos.x, pr.pos.y, pr.pos.z, 4.0f, scaled(element_glow(pr.element), 0.15f + 0.5f * night));
  }

  std::sort(found.begin(), found.end(), [](const Candidate& a, const Candidate& b) { return a.distance2 < b.distance2; });
  size_t n = 0;
  if (night > 0.01f) out[n++] = staff;
  for (const Candidate& c : found) {
    if (n >= kMaxPointLights) break;
    out[n++] = c.light;
  }
  return n;
}

}  // namespace monstres
