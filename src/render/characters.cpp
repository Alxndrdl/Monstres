#include "monstres/render/characters.hpp"
#include "monstres/render/cull.hpp"

#include "monstres/render/scene.hpp"

#include "monstres/core/battle.hpp"
#include "monstres/core/interior.hpp"
#include "monstres/core/quests.hpp"
#include "monstres/core/variants.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace monstres {

namespace {

constexpr float kTau = 6.28318530718f;
constexpr Color kWhite{1, 1, 1, 1};

// ---- Rigs (characters face -Z, origin between the feet) ------------------------

// The magician: robe, pointed hat (or hood, or nothing), a staff (or a wand)
// in the right hand with a glowing crystal. As the player chose it
// (WizardLook, wizard_rig): tint slots 1 the crystal (glowing), 2 the robe,
// 3 the skin, 4 the hair, 5 the eyes. The parts after kCrystal are not posed:
// they follow their parent.
enum PlayerPart {
  kTorso, kHead, kPlayerEyeL, kPlayerEyeR, kArmL, kArmR, kLegL, kLegR, kStaff, kCrystal,
  kTrims, kNeck, kHair, kHeadwear, kHatBand, kBeard, kHandL, kHandR, kCuffL, kCuffR
};
constexpr RigPart kPlayerRig[] = {
    {MeshId::PlayerTorso, -1, {0, 0.37f, 0}, {1, 1, 1}, 2},
    {MeshId::PlayerHead, kTorso, {0, 0.37f, 0}, {1, 1, 1}, 3},
    {MeshId::EyeIris, kHead, {-0.075f, 0.2f, -0.185f}, {0.03f, 0.038f, 0.03f}, 5},
    {MeshId::EyeIris, kHead, {0.075f, 0.2f, -0.185f}, {0.03f, 0.038f, 0.03f}, 5},
    {MeshId::PlayerArm, kTorso, {-0.205f, 0.31f, 0}, {1, 1, 1}, 2},
    {MeshId::PlayerArm, kTorso, {0.205f, 0.31f, 0}, {1, 1, 1}, 2},
    {MeshId::PlayerLeg, -1, {-0.095f, 0.38f, 0}},
    {MeshId::PlayerLeg, -1, {0.095f, 0.38f, 0}},
    // The staff in front of the sleeve (not through it), leaning out (clear of the hat's brim).
    {MeshId::PlayerStaff, kArmR, {0.09f, -0.29f, -0.095f}, {1, 1, 1}, 0, Quat{0, 0, -0.10453f, 0.99452f}},  // quat_z(-0.21)
    {MeshId::PlayerCrystal, kStaff, {0, 0.9f, 0}, {1, 1, 1}, 1},
    {MeshId::PlayerTrims, kTorso, {0, 0, 0}},
    {MeshId::PlayerNeck, kTorso, {0, 0, 0}, {1, 1, 1}, 3},
    {MeshId::PlayerHairShort, kHead, {0, 0, 0}, {1, 1, 1}, 4},
    {MeshId::PlayerHat, kHead, {0, 0, 0}, {1, 1, 1}, 2},
    {MeshId::PlayerHatBand, kHead, {0, 0, 0}},
    {MeshId::Count, kHead, {0, 0, 0}, {1, 1, 1}, 4},  // a beard (Isidore's)
    {MeshId::VillagerHand, kArmL, {0, -0.27f, 0}, {0.96f, 0.96f, 0.96f}, 3},
    {MeshId::VillagerHand, kArmR, {0, -0.27f, 0}, {0.96f, 0.96f, 0.96f}, 3},
    {MeshId::PlayerCuff, kArmL, {0, 0, 0}},
    {MeshId::PlayerCuff, kArmR, {0, 0, 0}},
};
static_assert(std::size(kPlayerRig) <= kMaxRigParts);
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

// Zapou and Ploufi walk on four paws like Pompon: the same parts, in the same
// order (CompanionPart), so they move alike. Ploufi's "ears" are its gills.
// Zapou, a hedgehog: its quills (last) glow with their color (tint slot 4).
constexpr RigPart kSparkRig[] = {
    {MeshId::SparkBody, -1, {0, 0.2f, 0}, {1, 1, 1}, 1},
    {MeshId::SparkHead, kCBody, {0, 0.02f, -0.17f}, {1, 1, 1}, 1},
    {MeshId::Eye, kCHead, {-0.055f, 0.1f, -0.1f}, {0.024f, 0.03f, 0.024f}},
    {MeshId::Eye, kCHead, {0.055f, 0.1f, -0.1f}, {0.024f, 0.03f, 0.024f}},
    {MeshId::SparkEar, kCHead, {-0.085f, 0.15f, -0.01f}, {1, 1, 1}, 1},
    {MeshId::SparkEar, kCHead, {0.085f, 0.15f, -0.01f}, {1, 1, 1}, 1},
    {MeshId::SparkTail, kCBody, {0, 0.02f, 0.19f}, {1, 1, 1}, 1},
    {MeshId::SparkLeg, -1, {-0.08f, 0.12f, -0.09f}, {1, 1, 1}, 1},
    {MeshId::SparkLeg, -1, {0.08f, 0.12f, -0.09f}, {1, 1, 1}, 1},
    {MeshId::SparkLeg, -1, {-0.08f, 0.12f, 0.1f}, {1, 1, 1}, 1},
    {MeshId::SparkLeg, -1, {0.08f, 0.12f, 0.1f}, {1, 1, 1}, 1},
    {MeshId::SparkQuills, kCBody, {0, 0, 0}, {1, 1, 1}, 4},
};
constexpr int kQuills = 11;
constexpr RigPart kAxolotlRig[] = {
    {MeshId::AxolotlBody, -1, {0, 0.15f, 0}, {1, 1, 1}, 1},
    {MeshId::AxolotlHead, kCBody, {0, 0.03f, -0.27f}, {1, 1, 1}, 1},
    {MeshId::Eye, kCHead, {-0.12f, 0.1f, -0.08f}, {0.026f, 0.03f, 0.026f}},
    {MeshId::Eye, kCHead, {0.12f, 0.1f, -0.08f}, {0.026f, 0.03f, 0.026f}},
    // The gills fan out sideways and sweep back (the model fans out along +X).
    {MeshId::AxolotlGills, kCHead, {-0.15f, 0.07f, 0.03f}, {1, 1, 1}, 2, Quat{0, 0.97479f, 0, -0.22311f}},  // yaw of half a turn + 0.45,
    {MeshId::AxolotlGills, kCHead, {0.15f, 0.07f, 0.03f}, {1, 1, 1}, 2, Quat{0, -0.22311f, 0, 0.97479f}},  // yaw of -0.45
    {MeshId::AxolotlTail, kCBody, {0, 0.0f, 0.22f}, {1, 1, 1}, 1},
    {MeshId::AxolotlLeg, -1, {-0.13f, 0.09f, -0.14f}, {1, 1, 1}, 1},
    {MeshId::AxolotlLeg, -1, {0.13f, 0.09f, -0.14f}, {1, 1, 1}, 1},
    {MeshId::AxolotlLeg, -1, {-0.13f, 0.09f, 0.14f}, {1, 1, 1}, 1},
    {MeshId::AxolotlLeg, -1, {0.13f, 0.09f, 0.14f}, {1, 1, 1}, 1},
};

// Cristi: a round ball of night-blue fur, big eyes, pointed ears, glowing
// crystals set in its back (tint 4).
enum CristiPart { kRBody, kREyeL, kREyeR, kREarL, kREarR, kRCrystal, kRLegFL, kRLegFR, kRLegBL, kRLegBR };
constexpr RigPart kCristiRig[] = {
    {MeshId::CristiBody, -1, {0, 0.2f, 0}, {1, 1, 1}, 1},
    {MeshId::Eye, kRBody, {-0.075f, 0.035f, -0.165f}, {0.05f, 0.062f, 0.05f}},
    {MeshId::Eye, kRBody, {0.075f, 0.035f, -0.165f}, {0.05f, 0.062f, 0.05f}},
    {MeshId::CristiEar, kRBody, {-0.1f, 0.12f, -0.03f}, {1, 1, 1}, 1, Quat{0, 0, 0.17365f, 0.98481f}},   // quat_z(0.35)
    {MeshId::CristiEar, kRBody, {0.1f, 0.12f, -0.03f}, {1, 1, 1}, 1, Quat{0, 0, -0.17365f, 0.98481f}},   // quat_z(-0.35)
    {MeshId::CristiCrystal, kRBody, {0, 0.14f, 0.07f}, {1, 1, 1}, 4, Quat{0.17365f, 0, 0, 0.98481f}},    // quat_x(0.35)
    {MeshId::CristiLeg, -1, {-0.1f, 0.07f, -0.08f}, {1, 1, 1}, 1},
    {MeshId::CristiLeg, -1, {0.1f, 0.07f, -0.08f}, {1, 1, 1}, 1},
    {MeshId::CristiLeg, -1, {-0.1f, 0.07f, 0.09f}, {1, 1, 1}, 1},
    {MeshId::CristiLeg, -1, {0.1f, 0.07f, 0.09f}, {1, 1, 1}, 1},
};

// Chipou, Laini and Taupi walk on four paws like Pompon: the same parts, in
// the same order (CompanionPart), so they move alike. Laini's body and tail
// are its wool (tint slot 1); Taupi's "ears" are tiny.
constexpr RigPart kRaccoonRig[] = {
    {MeshId::RaccoonBody, -1, {0, 0.26f, 0}, {1, 1, 1}, 1},
    {MeshId::RaccoonHead, kCBody, {0, 0.05f, -0.21f}, {1, 1, 1}, 1},
    {MeshId::Eye, kCHead, {-0.05f, 0.1f, -0.11f}, {0.022f, 0.028f, 0.022f}},
    {MeshId::Eye, kCHead, {0.05f, 0.1f, -0.11f}, {0.022f, 0.028f, 0.022f}},
    {MeshId::RaccoonEar, kCHead, {-0.08f, 0.15f, 0.0f}, {1, 1, 1}, 1},
    {MeshId::RaccoonEar, kCHead, {0.08f, 0.15f, 0.0f}, {1, 1, 1}, 1},
    {MeshId::RaccoonTail, kCBody, {0, 0.02f, 0.2f}, {1, 1, 1}, 1},
    {MeshId::RaccoonLeg, -1, {-0.09f, 0.15f, -0.11f}},
    {MeshId::RaccoonLeg, -1, {0.09f, 0.15f, -0.11f}},
    {MeshId::RaccoonLeg, -1, {-0.09f, 0.15f, 0.12f}},
    {MeshId::RaccoonLeg, -1, {0.09f, 0.15f, 0.12f}},
};
constexpr RigPart kSheepRig[] = {
    {MeshId::SheepBody, -1, {0, 0.33f, 0}, {1, 1, 1}, 1},
    {MeshId::SheepHead, kCBody, {0, 0.08f, -0.27f}, {1, 1, 1}, 1},
    {MeshId::Eye, kCHead, {-0.05f, 0.08f, -0.1f}, {0.02f, 0.026f, 0.02f}},
    {MeshId::Eye, kCHead, {0.05f, 0.08f, -0.1f}, {0.02f, 0.026f, 0.02f}},
    {MeshId::SheepEar, kCHead, {-0.085f, 0.08f, -0.01f}, {1, 1, 1}, 0, Quat{0, 0, 0.56464f, 0.82534f}},   // quat_z(1.2)
    {MeshId::SheepEar, kCHead, {0.085f, 0.08f, -0.01f}, {1, 1, 1}, 0, Quat{0, 0, -0.56464f, 0.82534f}},  // quat_z(-1.2)
    {MeshId::SheepTail, kCBody, {0, 0.03f, 0.26f}, {1, 1, 1}, 1},
    {MeshId::SheepLeg, -1, {-0.1f, 0.17f, -0.13f}},
    {MeshId::SheepLeg, -1, {0.1f, 0.17f, -0.13f}},
    {MeshId::SheepLeg, -1, {-0.1f, 0.17f, 0.13f}},
    {MeshId::SheepLeg, -1, {0.1f, 0.17f, 0.13f}},
};
constexpr RigPart kMoleRig[] = {
    {MeshId::MoleBody, -1, {0, 0.17f, 0}, {1, 1, 1}, 1},
    {MeshId::MoleHead, kCBody, {0, 0.0f, -0.2f}, {1, 1, 1}, 1},
    {MeshId::Eye, kCHead, {-0.045f, 0.06f, -0.08f}, {0.011f, 0.013f, 0.011f}},
    {MeshId::Eye, kCHead, {0.045f, 0.06f, -0.08f}, {0.011f, 0.013f, 0.011f}},
    {MeshId::MoleEar, kCHead, {-0.08f, 0.07f, 0.01f}, {1, 1, 1}, 1},
    {MeshId::MoleEar, kCHead, {0.08f, 0.07f, 0.01f}, {1, 1, 1}, 1},
    {MeshId::MoleTail, kCBody, {0, 0.0f, 0.22f}},
    {MeshId::MoleLeg, -1, {-0.1f, 0.1f, -0.12f}},
    {MeshId::MoleLeg, -1, {0.1f, 0.1f, -0.12f}},
    {MeshId::MoleLeg, -1, {-0.1f, 0.1f, 0.12f}},
    {MeshId::MoleLeg, -1, {0.1f, 0.1f, 0.12f}},
};
// Hulou flies like Croa: the same parts, in the same order (CrowPart). Its
// big eyes are colored (tint slot 3).
constexpr RigPart kOwlRig[] = {
    {MeshId::OwlBody, -1, {0, 0.26f, 0}, {1, 1, 1}, 1},
    {MeshId::OwlWing, kKBody, {-0.13f, 0.0f, 0.02f}, {1, 1, 1}, 1},
    {MeshId::OwlWing, kKBody, {0.13f, 0.0f, 0.02f}, {1, 1, 1}, 1},
    {MeshId::EyeIris, kKBody, {-0.055f, 0.07f, -0.14f}, {0.034f, 0.038f, 0.03f}, 3},
    {MeshId::EyeIris, kKBody, {0.055f, 0.07f, -0.14f}, {0.034f, 0.038f, 0.03f}, 3},
    {MeshId::OwlLeg, -1, {-0.05f, 0.08f, 0.0f}},
    {MeshId::OwlLeg, -1, {0.05f, 0.08f, 0.0f}},
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
    case Species::Spark: return 1.2f;
    case Species::Axolotl: return 1.25f;
    case Species::Cristi: return 1.25f;
    case Species::Raccoon: return 1.25f;
    case Species::Owl: return 1.35f;
    case Species::Sheep: return 1.3f;
    case Species::Mole: return 1.3f;
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
    case Element::Arcane: return {1.75f, 0.6f, 2.1f, 1};
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
constexpr Color kGillColor{0.92f, 0.36f, 0.50f, 1};

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
  Box column = bb;
  column.min.y = -1e9f;
  for_objects_near(w, column, [&](uint32_t, const Box& ob) {
    if (bb.min.x < ob.max.x && bb.max.x > ob.min.x && bb.min.z < ob.max.z && bb.max.z > ob.min.z &&
        ob.max.y <= b.pos.y + 0.01f) {
      support = std::max(support, ob.max.y);
    }
  });
  return support;
}

void add_body_shadow(const World& w, const Body& b, const BodyShape& shape, float radius, DrawList& list) {
  if (b.up.y < 0.95f) return;  // on a wall or the ceiling
  const float support = support_height(w, b, shape);
  const float lift = std::clamp(1.0f - (b.pos.y - support) / 3.0f, 0.35f, 1.0f);
  const bool on_ground = support == w.ground.height(b.pos.x, b.pos.z);  // not on top of an object
  add_shadow(list, b.pos.x, b.pos.z, radius * lift, radius * lift, 0, lift, support + kShadowHeight + 0.005f,
             on_ground ? ground_tilt(w, b.pos.x, b.pos.z) : Quat{});
}

}  // namespace

// ---- Rigs ---------------------------------------------------------------------------

std::span<const RigPart> player_rig() { return kPlayerRig; }

WizardRig wizard_rig(const WizardLook& look, const WizardExtras& extras) {
  static constexpr Color kSkins[look::kSkins] = {{0.96f, 0.78f, 0.62f, 1}, {0.99f, 0.85f, 0.73f, 1},
                                                 {0.86f, 0.64f, 0.47f, 1}, {0.70f, 0.50f, 0.35f, 1},
                                                 {0.52f, 0.36f, 0.25f, 1}, {0.36f, 0.24f, 0.17f, 1}};
  static constexpr Color kHairs[look::kHairColors] = {{0.36f, 0.22f, 0.12f, 1}, {0.10f, 0.09f, 0.10f, 1},
                                                      {0.22f, 0.14f, 0.09f, 1}, {0.62f, 0.26f, 0.12f, 1},
                                                      {0.88f, 0.72f, 0.40f, 1}, {0.86f, 0.86f, 0.88f, 1},
                                                      {0.30f, 0.42f, 0.82f, 1}, {0.76f, 0.40f, 0.70f, 1}};
  static constexpr Color kEyes[look::kEyeColors] = {{0.22f, 0.15f, 0.10f, 1}, {0.45f, 0.27f, 0.12f, 1},
                                                    {0.30f, 0.56f, 0.26f, 1}, {0.26f, 0.52f, 0.88f, 1},
                                                    {0.58f, 0.62f, 0.68f, 1}, {0.58f, 0.32f, 0.78f, 1}};
  static constexpr Color kRobes[look::kRobeColors] = {{0.24f, 0.30f, 0.68f, 1}, {0.68f, 0.18f, 0.20f, 1},
                                                      {0.20f, 0.50f, 0.30f, 1}, {0.45f, 0.25f, 0.62f, 1},
                                                      {0.16f, 0.16f, 0.21f, 1}, {0.88f, 0.88f, 0.90f, 1},
                                                      {0.48f, 0.32f, 0.20f, 1}, {0.18f, 0.52f, 0.56f, 1}};
  // Each style bare, under the hat, under the hood (a bun does not fit under it).
  static constexpr MeshId kHairMeshes[3][look::kHairStyles] = {
      {MeshId::PlayerHairShort, MeshId::PlayerHairLong, MeshId::PlayerHairBun, MeshId::Count},
      {MeshId::PlayerHairShortHat, MeshId::PlayerHairLongHat, MeshId::PlayerHairBunHat, MeshId::Count},
      {MeshId::PlayerHairShortHood, MeshId::PlayerHairLongHood, MeshId::PlayerHairShortHood, MeshId::Count}};
  const auto pick = [](std::span<const Color> colors, uint8_t i) { return colors[i < colors.size() ? i : 0]; };
  WizardRig r;
  std::copy(std::begin(kPlayerRig), std::end(kPlayerRig), r.parts.begin());
  const auto part = [&](int i) -> RigPart& { return r.parts[static_cast<size_t>(i)]; };
  if (look.woman) {  // narrower shoulders, a wider skirt, lashes
    part(kTorso).mesh = MeshId::PlayerTorsoWoman;
    part(kTrims).mesh = MeshId::PlayerTrimsWoman;
    part(kHead).mesh = MeshId::PlayerHeadWoman;
    part(kArmL).pivot.x = -0.19f;
    part(kArmR).pivot.x = 0.19f;
    part(kArmL).scale = part(kArmR).scale = {0.93f, 0.96f, 0.93f};
  }
  const uint8_t headwear = extras.bare_head ? look::kBareHead : look.headwear;
  const size_t covered = headwear == look::kHat ? 1 : headwear == look::kHood ? 2 : 0;
  part(kHair).mesh = kHairMeshes[covered][look.hair < look::kHairStyles ? look.hair : 0];
  part(kHeadwear).mesh = headwear == look::kHat ? MeshId::PlayerHat : headwear == look::kHood ? MeshId::PlayerHood
                                                                                              : MeshId::Count;
  part(kHatBand).mesh = headwear == look::kHat ? MeshId::PlayerHatBand : MeshId::Count;
  if (extras.beard) part(kBeard).mesh = MeshId::PlayerBeard;
  if (look.staff == look::kWand) {  // a short wand, its crystal smaller, at its tip, pointing ahead
    part(kStaff).mesh = MeshId::PlayerWand;
    part(kStaff).pivot = {0, -0.3f, -0.045f};
    part(kStaff).rest = quat_x(-1.15f);
    part(kCrystal).pivot = {0, 0.32f, 0};
    part(kCrystal).scale = {0.5f, 0.5f, 0.5f};
  }
  r.tints = {kCrystalGlow, pick(kRobes, look.robe), pick(kSkins, look.skin), pick(kHairs, look.hair_color),
             pick(kEyes, look.eyes)};
  return r;
}

WizardLook mentor_look() {
  WizardLook l;
  l.skin = 1;
  l.hair = look::kLongHair;
  l.hair_color = 5;  // white
  l.eyes = 4;
  l.robe = 3;        // violet
  return l;
}

std::span<const RigPart> creature_rig(Species species) {
  switch (species) {
    case Species::Companion: return kCompanionRig;
    case Species::Slime: return kSlimeRig;
    case Species::Mushroom: return kMushroomRig;
    case Species::Bird: return kBirdRig;
    case Species::Crow: return kCrowRig;
    case Species::Fish: return kFishRig;
    case Species::Spark: return kSparkRig;
    case Species::Axolotl: return kAxolotlRig;
    case Species::Cristi: return kCristiRig;
    case Species::Raccoon: return kRaccoonRig;
    case Species::Owl: return kOwlRig;
    case Species::Sheep: return kSheepRig;
    case Species::Mole: return kMoleRig;
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
  look.size *= build_scale(g.form);  // micro, alpha, omega
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
    case Species::Spark: {
      // Its quills glow blue, yellow or violet; chromatic: a white hedgehog with
      // white sparks; elemental (shadow): dark, with violet ones.
      static constexpr Color kQuillGlow[] = {{0.55f, 1.35f, 2.2f, 1}, {2.0f, 1.75f, 0.45f, 1}, {1.5f, 0.65f, 2.1f, 1}};
      look.tints[3] = g.elemental ? Color{1.1f, 0.45f, 1.7f, 1} : g.chromatic ? Color{1.9f, 1.9f, 2.0f, 1}
                                                                           : kQuillGlow[g.allele[1] < 3 ? g.allele[1] : 0];
      if (g.chromatic) look.tints[0] = {2.1f, 2.15f, 1.95f, 1};
      if (g.elemental) look.tints[0] = {0.55f, 0.42f, 0.75f, 1};
      if (g.allele[2] == 1) at(kQuills).mesh = MeshId::SparkQuillsLong;               // long quills
      if (g.allele[3] == 1) at(kEarL).scale = at(kEarR).scale = {0.8f, 1.9f, 0.8f};   // tufted ears
      break;
    }
    case Species::Axolotl: {
      // Pink, blue, gold; chromatic: white; elemental (nature): green.
      static constexpr Color kSkin[] = {{1, 1, 1, 1}, {0.55f, 1.05f, 1.4f, 1}, {1.0f, 1.2f, 0.45f, 1}};
      look.tints[0] = body_tint(g, kSkin, {1.02f, 1.48f, 1.34f, 1}, {0.55f, 1.4f, 0.72f, 1});
      look.tints[1] = times(kGillColor, look.tints[0], 1.05f);
      if (g.allele[2] == 1) {  // large gills
        at(kEarL).scale = at(kEarR).scale = {1.6f, 1.6f, 1.6f};
      }
      if (g.allele[3] == 1) at(kTail).scale = {1.0f, 1.25f, 1.7f};  // a long tail
      break;
    }
    case Species::Cristi: {
      // Its crystals glow violet, cyan or rose; chromatic: white fur, white
      // crystals; elemental (light): golden fur.
      static constexpr Color kGlow[] = {{1.55f, 0.75f, 2.2f, 1}, {0.5f, 1.6f, 2.1f, 1}, {2.1f, 0.8f, 1.35f, 1}};
      look.tints[3] = g.chromatic ? Color{2.0f, 2.0f, 2.1f, 1} : g.elemental ? Color{2.1f, 1.8f, 0.9f, 1}
                                                                         : kGlow[g.allele[1] < 3 ? g.allele[1] : 0];
      if (g.chromatic) look.tints[0] = {3.6f, 3.8f, 3.0f, 1};
      if (g.elemental) look.tints[0] = {4.2f, 3.5f, 1.4f, 1};
      if (g.allele[2] == 1) at(kREarL).scale = at(kREarR).scale = {0.9f, 1.55f, 0.9f};  // long ears
      if (g.allele[3] == 1) look.add({MeshId::CristiShards, kRBody, {0, 0.14f, 0.07f}, {1, 1, 1}, 4, at(kRCrystal).rest});
      break;
    }
    case Species::Raccoon: {
      // Grey, rust, silver; chromatic: black and white; elemental (light): golden.
      static constexpr Color kCoats[] = {{1, 1, 1, 1}, {1.45f, 0.9f, 0.6f, 1}, {1.45f, 1.5f, 1.6f, 1}};
      look.tints[0] = body_tint(g, kCoats, {0.45f, 0.45f, 0.5f, 1}, {1.7f, 1.45f, 0.7f, 1});
      if (g.allele[2] == 1) at(kTail).scale = {1.45f, 1.35f, 1.15f};  // bushy
      if (g.allele[3] == 1) at(kEarL).scale = at(kEarR).scale = {0.85f, 1.8f, 0.85f};  // tufted ears
      break;
    }
    case Species::Owl: {
      // Brown, snowy, tawny; amber or blue eyes; chromatic: grey; elemental (shadow): night-dark.
      static constexpr Color kPlumage[] = {{1, 1, 1, 1}, {1.75f, 1.8f, 1.85f, 1}, {1.35f, 0.95f, 0.6f, 1}};
      look.tints[0] = body_tint(g, kPlumage, {1.2f, 1.2f, 1.3f, 1}, {0.45f, 0.4f, 0.65f, 1});
      look.tints[2] = g.allele[3] == 1 ? Color{0.45f, 0.95f, 1.8f, 1} : Color{1.9f, 1.25f, 0.25f, 1};
      if (g.allele[2] == 0) {  // ear tufts
        look.add({MeshId::OwlTufts, kKBody, {0, 0, 0}, {1, 1, 1}, 1});
        look.extra_height = 0.1f;
      }
      break;
    }
    case Species::Sheep: {
      // White, black, pink wool; chromatic: golden fleece; elemental (electric): a blue, crackling one.
      static constexpr Color kWool[] = {{1, 1, 1, 1}, {0.32f, 0.3f, 0.32f, 1}, {1.0f, 0.72f, 0.8f, 1}};
      look.tints[0] = body_tint(g, kWool, {1.0f, 0.85f, 0.4f, 1}, {0.7f, 0.85f, 1.35f, 1});
      if (g.allele[2] == 1) {  // curled horns
        look.add({MeshId::SheepHorns, kCHead, {0, 0, 0}});
        look.extra_height = 0.05f;
      }
      if (g.allele[3] == 1) at(kCBody).scale = {1.25f, 1.2f, 1.15f};  // a cloud of wool
      break;
    }
    case Species::Mole: {
      // Brown, grey, golden; chromatic: white; elemental (fire): ember red.
      static constexpr Color kFur[] = {{1, 1, 1, 1}, {1.25f, 1.3f, 1.45f, 1}, {2.2f, 1.8f, 0.9f, 1}};
      look.tints[0] = body_tint(g, kFur, {2.8f, 2.8f, 2.8f, 1}, {1.9f, 0.7f, 0.45f, 1});
      if (g.allele[2] == 1) {  // big claws
        for (int leg : {kLegFL, kLegFR}) at(leg).scale = {1.45f, 1.0f, 1.45f};
      }
      if (g.allele[3] == 1) look.add({MeshId::MoleStar, kCHead, {0, 0, 0}});  // a star nose
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
    case Species::Companion:
    case Species::Spark:
    case Species::Axolotl:
    case Species::Raccoon:
    case Species::Sheep:
    case Species::Mole: {
      const bool axolotl = c.species == Species::Axolotl;
      const float phase = t * kTau * (axolotl ? 1.6f + 1.2f * walk : 2.0f + 1.5f * walk);
      const float swing = std::sin(phase) * 0.7f * clamp01(walk * 1.3f);
      if (b.on_ground) {
        poses[kLegFL].rotation = poses[kLegBR].rotation = quat_x(swing);
        poses[kLegFR].rotation = poses[kLegBL].rotation = quat_x(-swing);
        poses[kCBody].offset.y = std::fabs(std::sin(phase)) * 0.03f * walk;
      } else {
        poses[kLegFL].rotation = poses[kLegFR].rotation = quat_x(0.7f);
        poses[kLegBL].rotation = poses[kLegBR].rotation = quat_x(-0.7f);
      }
      // Tail wags (faster when happy to run; an axolotl's sweeps slowly), head tilts when idle, ears twitch.
      const Quat wag = axolotl ? quat_yaw(std::sin(phase * 0.5f) * 0.35f)
                               : quat_yaw(std::sin(t * (5.0f + 9.0f * walk)) * 0.5f) * quat_x(-0.2f);
      for (size_t i = 0; i < rig.size(); ++i) {
        if (rig[i].mesh == rig[kTail].mesh) poses[i].rotation = wag;
      }
      if (axolotl) {  // the body sways side to side as it walks, the gills ripple
        poses[kCBody].rotation = quat_yaw(std::sin(phase) * 0.12f * walk);
        const float ripple = 0.15f * std::sin(t * 3.0f);
        poses[kEarL].rotation = quat_z(-ripple);
        poses[kEarR].rotation = quat_z(ripple);
        poses[kCHead].rotation = quat_yaw(std::sin(t * 0.6f) * 0.15f * (1 - walk));
        break;
      }
      poses[kCHead].rotation = quat_z(std::sin(t * 0.8f) * 0.18f * (1 - walk)) * quat_x(0.08f * std::sin(phase) * walk);
      const float twitch = std::sin(t * 1.3f) > 0.97f ? 0.35f : 0.0f;
      // Pompon's floppy ears hang on the sides; Zapou's stand up.
      if (c.species == Species::Sheep) {  // its ears hang on the sides, flicking now and then
        poses[kEarL].rotation = quat_x(-twitch);
        poses[kEarR].rotation = quat_x(-twitch);
        break;
      }
      const float spread = c.species == Species::Spark || c.species == Species::Raccoon || c.species == Species::Mole ? 0.25f
                           : c.genes.allele[2] == 2                                                                   ? 1.3f
                                                                                                                      : 0.35f;
      poses[kEarL].rotation = quat_z(spread) * quat_x(-twitch - (b.on_ground ? 0 : 0.5f));
      poses[kEarR].rotation = quat_z(-spread) * quat_x(-twitch - (b.on_ground ? 0 : 0.5f));
      break;
    }
    case Species::Cristi: {
      // It trots on its little paws, bouncing; its ears twitch, its crystals
      // pulse; it blinks.
      const float phase = t * kTau * (2.4f + 3.0f * walk);
      const float swing = std::sin(phase) * 0.8f * clamp01(walk * 1.5f);
      poses[kRLegFL].rotation = poses[kRLegBR].rotation = quat_x(swing);
      poses[kRLegFR].rotation = poses[kRLegBL].rotation = quat_x(-swing);
      poses[kRBody].offset.y = std::fabs(std::sin(phase)) * 0.035f * walk + 0.006f * std::sin(t * 2.0f) * (1 - walk);
      const float squash = 1.0f + 0.04f * std::sin(t * 2.0f) * (1 - walk);
      poses[kRBody].scale = {squash, 2.0f - squash, squash};
      const float twitch = std::sin(t * 1.7f + seed) > 0.96f ? 0.3f : 0.0f;
      poses[kREarL].rotation = quat_z(0.1f * std::sin(phase) * walk + twitch);
      poses[kREarR].rotation = quat_z(-0.1f * std::sin(phase) * walk - twitch);
      const float pulse = 1.0f + 0.06f * std::sin(t * 2.2f);
      poses[kRCrystal].scale = {pulse, pulse, pulse};
      close_eyes(poses, kREyeL, kREyeR, t, seed + 0.3f);
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
    case Species::Crow:
    case Species::Owl: {
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

Quat body_rotation(const Body& body) {
  if (body.up.y > 0.9999f && body.facing_y == 0) return quat_yaw(std::atan2(-body.facing_x, -body.facing_z));
  // Clinging to a wall or the ceiling (interior.hpp): its up is the
  // surface's normal, it faces along it. Local -Z forward, +Y up, +X right.
  const float ux = body.up.x, uy = body.up.y, uz = body.up.z;
  float fx = body.facing_x, fy = body.facing_y, fz = body.facing_z;
  const float along = fx * ux + fy * uy + fz * uz;
  fx -= ux * along;
  fy -= uy * along;
  fz -= uz * along;
  float l = std::sqrt(fx * fx + fy * fy + fz * fz);
  if (l < 1e-4f) return quat_yaw(std::atan2(-body.facing_x, -body.facing_z));
  fx /= l;
  fy /= l;
  fz /= l;
  // Columns of the rotation: right = forward x up, up, back = -forward.
  const float rx = fy * uz - fz * uy, ry = fz * ux - fx * uz, rz = fx * uy - fy * ux;
  const float m00 = rx, m01 = ux, m02 = -fx;
  const float m10 = ry, m11 = uy, m12 = -fy;
  const float m20 = rz, m21 = uz, m22 = -fz;
  Quat q;
  const float trace = m00 + m11 + m22;
  if (trace > 0) {
    const float s = std::sqrt(trace + 1.0f) * 2;
    q = {(m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s};
  } else if (m00 > m11 && m00 > m22) {
    const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2;
    q = {0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s};
  } else if (m11 > m22) {
    const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2;
    q = {(m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s};
  } else {
    const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2;
    q = {(m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s};
  }
  l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
  return {q.x / l, q.y / l, q.z / l, q.w / l};
}

void add_rig(DrawList& list, std::span<const RigPart> rig, std::span<const PartPose> poses, const Body& body,
             std::span<const Color> tints, float size, uint8_t glow_slot) {
  constexpr size_t kMaxParts = kMaxRigParts;
  std::array<Quat, kMaxParts> world_rotation;
  std::array<Vec3f, kMaxParts> world_position;
  const Quat root = body_rotation(body);

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
    const WizardRig r = wizard_rig(subject.look, {.bare_head = subject.bare_head});
    add_rig(list, r.parts, poses, body, r.tints, 1, kPlayerGlowSlot);
    return subject.look.headwear == look::kHat && !subject.bare_head ? 1.55f : 1.25f;
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
  if (c.species == Species::Axolotl) height = 0.72f;  // low, but long: framed whole
  if (c.species == Species::Cristi) height = 0.62f;   // its ears and crystals above it
  if (c.species == Species::Spark) height = 0.78f;    // with its big ears
  if (c.species == Species::Owl) height = 0.75f;
  if (c.species == Species::Mole) height = 0.6f;
  height += look.extra_height * scale;
  if (look.aura) {
    const BodyShape& shape = species_info(c.species).shape;
    add_aura(list, c.body, (shape.half_width * 1.5f + 0.1f) * look.size, height * look.size, look.aura_color, time,
             0.4f);
  }
  // Framed for a normal male: a female or a micro looks smaller; a bigger build is framed whole.
  return height * std::max(1.0f, build_scale(c.genes.form));
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

// In a battle: a fighter springs forward when it bites, curls up when it
// guards, trembles while a burst gathers, and flashes when it is hurt.
// Zapou's quills flicker from color to color as its discharge gathers and
// goes off.
void battle_pose(const World& w, size_t index, CreatureLook& look, Body& body, std::vector<PartPose>& poses) {
  const Battle& b = w.battle;
  const int slot = index == 0 ? -1 : foe_slot(w, index);
  const bool training = b.phase == BattlePhase::Training;
  if ((b.phase != BattlePhase::Fight && !training) || (index != 0 && slot < 0)) return;
  if (training && index != 0) return;
  const Fighter& f = index == 0 ? b.ally : b.foes[static_cast<size_t>(slot)].fighter;
  const Species species = w.creatures[index].species;
  const bool curls = species == Species::Spark || species == Species::Companion || species == Species::Raccoon ||
                     species == Species::Sheep || species == Species::Mole;  // rigs with a head, ears, legs
  const uint64_t since_attack = w.tick - f.attack_tick;

  // The bite: it draws back a little, springs forward, comes back.
  if (f.attack_tick > 0 && f.attack_move == 0 && since_attack < kLungeTicks) {
    const float t = static_cast<float>(since_attack) / static_cast<float>(kLungeTicks);
    const float k = t < 0.2f ? -0.12f * t / 0.2f
                  : t < 0.45f ? -0.12f + 0.82f * (t - 0.2f) / 0.25f
                              : 0.7f * (1 - (t - 0.45f) / 0.55f) * (1 - (t - 0.45f) / 0.55f);
    body.pos.x += body.facing_x * k;
    body.pos.z += body.facing_z * k;
    body.pos.y += 0.06f * std::sin(std::clamp((t - 0.2f) / 0.5f, 0.0f, 1.0f) * kTau / 2);  // a little hop
    if (curls && poses.size() > kLegBR) {  // stretched out in the leap
      const float reach = std::clamp(k / 0.7f, 0.0f, 1.0f);
      poses[kLegFL].rotation = poses[kLegFR].rotation = quat_x(0.9f * reach);
      poses[kLegBL].rotation = poses[kLegBR].rotation = quat_x(-0.9f * reach);
    }
  }

  // Guarding: curled up into a ball, quills out.
  if (f.state == FighterState::Parry) {
    const float left = static_cast<float>(f.state_ticks);
    const float curl = std::min({1.0f, (static_cast<float>(rules::kParryTicks) - left + 1) / 4.0f, left / 4.0f + 0.2f});
    body.pos.y -= 0.05f * curl;
    if (curls && poses.size() > kLegBR) {
      poses[kCBody].scale = {1 + 0.1f * curl, 1 - 0.12f * curl, 1 - 0.1f * curl};
      poses[kCBody].offset.y -= 0.05f * curl;
      poses[kCHead].rotation = quat_x(-0.55f * curl) * poses[kCHead].rotation;  // the nose tucked in
      poses[kCHead].offset.z += 0.05f * curl;
      poses[kCHead].offset.y -= 0.03f * curl;
      for (int ear : {kEarL, kEarR}) poses[static_cast<size_t>(ear)].rotation = quat_x(0.8f * curl) * poses[static_cast<size_t>(ear)].rotation;
      for (int leg : {kLegFL, kLegFR, kLegBL, kLegBR}) {
        poses[static_cast<size_t>(leg)].rotation = Quat{};
        poses[static_cast<size_t>(leg)].scale.y = 1 - 0.55f * curl;
        poses[static_cast<size_t>(leg)].offset.y += 0.03f * curl;
      }
      poses[kTail].scale = {1 - 0.3f * curl, 1 - 0.3f * curl, 1 - 0.3f * curl};
      if (species == Species::Spark && poses.size() > kQuills) {
        poses[kQuills].scale = {1 + 0.12f * curl, 1 + 0.12f * curl, 1 + 0.12f * curl};
      }
    } else {
      look.size *= 1 - 0.1f * curl;
    }
  }

  // A burst gathering: crouched, trembling more and more.
  const bool gathering = f.state == FighterState::Windup && f.move == 2;
  if (gathering) {
    const float total = static_cast<float>(windup_ticks(2));
    const float progress = 1 - static_cast<float>(f.state_ticks) / total;
    const float shake = 0.012f + 0.03f * progress;
    body.pos.x += shake * std::sin(static_cast<float>(w.tick) * 2.3f);
    body.pos.z += shake * std::cos(static_cast<float>(w.tick) * 3.1f);
    body.pos.y -= 0.04f;
    if (curls && poses.size() > kLegBR) poses[kCBody].scale = {1 + 0.06f * progress, 1 - 0.06f * progress, 1};
  } else if (f.state == FighterState::Windup) {  // it gathers itself: drawn a little lower
    body.pos.y -= 0.03f;
  }

  // Zapou's quills: flickering colors while it gathers, and as it discharges.
  const bool discharging = f.attack_tick > 0 && f.attack_move == 2 && since_attack < 30;
  if (species == Species::Spark && (gathering || discharging)) {
    static constexpr Color kFlicker[] = {{2.4f, 2.2f, 0.6f, 1}, {2.6f, 2.6f, 2.6f, 1}, {0.6f, 1.8f, 2.6f, 1},
                                         {1.9f, 0.8f, 2.6f, 1}};
    const uint64_t rate = discharging ? 2 : (f.state_ticks < 20 ? 3 : 6);
    look.tints[3] = kFlicker[(w.tick / rate + index) % std::size(kFlicker)];
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

// Over a villager's head (`top`: how high it is): what it has for the player,
// side by side, bobbing and turning slowly so that they are seen from anywhere.
void add_marks(DrawList& list, const World& w, const Creature& c, float top, float time) {
  const uint8_t marks = npc_marks(w, c.npc);
  if (!marks) return;
  struct Icon {
    uint8_t mark;
    MeshId mesh;
    Color glow;
  };
  static constexpr Icon kIcons[] = {{kMarkNewQuest, MeshId::IconQuestion, {1.8f, 1.8f, 1.8f, 1}},
                                    {kMarkQuest, MeshId::IconExclaim, {2.1f, 1.6f, 0.35f, 1}},
                                    {kMarkHeal, MeshId::IconPlus, {0.45f, 2.0f, 0.55f, 1}},
                                    {kMarkFight, MeshId::IconCross, {2.2f, 0.38f, 0.32f, 1}},
                                    {kMarkShop, MeshId::IconCoin, {2.3f, 1.8f, 0.4f, 1}},
                                    {kMarkFarm, MeshId::IconFarm, {1.4f, 1.9f, 0.6f, 1}}};
  int count = 0;
  for (const Icon& icon : kIcons) count += (marks & icon.mark) ? 1 : 0;
  constexpr float kSpacing = 0.44f;
  float offset = -kSpacing * static_cast<float>(count - 1) / 2;
  // Side by side across the line of sight: along the villager's right... as
  // it turns, they all turn together around the middle.
  const float spin = time * 1.4f + static_cast<float>(c.id) * 0.7f;
  for (const Icon& icon : kIcons) {
    if (!(marks & icon.mark)) continue;
    Instance in;
    in.x = c.body.pos.x + std::cos(spin) * offset;
    in.z = c.body.pos.z - std::sin(spin) * offset;
    in.y = c.body.pos.y + top + 0.35f + 0.05f * std::sin(time * 2.2f + static_cast<float>(c.id));
    in.rotation = quat_yaw(spin);
    in.sx = in.sy = in.sz = 1.3f;
    in.tint = icon.glow;
    in.material = kGlowing;
    list.batches[static_cast<size_t>(icon.mesh)].push_back(in);
    offset += kSpacing;
  }
}

void add_characters(const World& w, float time, DrawList& list, const Frustum* view) {
  std::vector<PartPose> poses;
  pose_player(w.player, time, poses);
  if (w.battle.phase == BattlePhase::Training) {
    // The staff held out in front: it keeps the shield up.
    poses[kArmR].rotation = quat_x(1.3f) * quat_z(0.12f);
    const Quat arm = poses[kArmR].rotation;
    poses[kStaff].rotation = Quat{-arm.x, -arm.y, -arm.z, arm.w} * quat_x(0.35f);
  }
  const WizardRig me = wizard_rig(w.wizard.look);
  add_rig(list, me.parts, poses, w.player, me.tints, 1, kPlayerGlowSlot);
  add_body_shadow(w, w.player, kPlayerShape, 0.5f, list);

  std::array<RigPart, std::size(kVillagerRig) + 2> villager_rig;
  for (size_t i = 0; i < w.creatures.size(); ++i) {
    if (i == 0 && w.active < 0) continue;  // in the crystal
    const Creature& c = w.creatures[i];
    if (view) {  // out of view: not drawn (room for the biggest ones, their shadows, what they hold)
      const Vec3 p = c.body.pos;
      if (!in_view(*view, Aabb{p.x - 3, p.y - 1.5f, p.z - 3, p.x + 3, p.y + 5, p.z + 3})) continue;
    }
    pose_creature(c, time, static_cast<float>(i) * 0.731f, poses);
    float shadow_size = 1;
    if (c.species == Species::Villager && c.npc == npc::kMentor) {
      // Isidore, the old magician: drawn as one, with his beard; he waves too.
      pose_player(c.body, time + static_cast<float>(i), poses);
      if (c.mind == Mind::Greet) poses[kArmL].rotation = quat_z(-2.5f - 0.35f * std::sin(time * 9.0f));
      const WizardRig him = wizard_rig(mentor_look(), {.beard = true});
      add_rig(list, him.parts, poses, c.body, him.tints, 1, kPlayerGlowSlot);
      add_marks(list, w, c, 1.6f, time);
    } else if (c.species == Species::Villager) {
      const VillagerLook look = villager_look(c.variant);
      std::copy(std::begin(kVillagerRig), std::end(kVillagerRig), villager_rig.begin());
      villager_rig[kVHair].mesh = look.hair_mesh;
      villager_rig[kVHat].mesh = look.hat_mesh;
      const Color tints[] = {look.shirt, look.pants, look.skin, look.hair, look.hat, {1.0f, 2.8f, 1.4f, 1}};
      size_t parts = std::size(kVillagerRig);
      if (c.npc == npc::kOdette) {
        // The healer holds a magician's staff, its crystal glowing green: easy to spot.
        villager_rig[parts] = {MeshId::PlayerStaff, kVArmR, {0, -0.29f, -0.02f}};
        villager_rig[parts + 1] = {MeshId::PlayerCrystal, static_cast<int>(parts), {0, 0.95f, 0}, {1.7f, 1.7f, 1.7f}, 6};
        poses.resize(parts + 2, PartPose{});
        if (c.mind != Mind::Greet) poses[kVArmR].rotation = quat_x(0.2f) * quat_z(0.4f);  // held up, out to her side
        const Quat arm = poses[kVArmR].rotation;
        poses[parts].rotation = Quat{-arm.x, -arm.y, -arm.z, arm.w} * quat_x(0.12f);
        poses[parts + 1].rotation = quat_yaw(time * 1.6f);
        poses[parts + 1].offset.y = 0.015f * std::sin(time * 2.5f);
        parts += 2;
      }
      add_rig(list, std::span<const RigPart>(villager_rig.data(), parts), poses, c.body, tints,
              look.child ? 0.75f : 1.0f, 6);
      if (c.npc) add_marks(list, w, c, (look.hat_mesh != MeshId::Count ? 1.3f : 1.2f) * (look.child ? 0.75f : 1.0f), time);
      if (c.npc == npc::kOdette) {  // two sparks turn around her crystal
        const Vec3 at = crystal_position(c.body);
        for (int k = 0; k < 2; ++k) {
          const float a = time * 2.2f + static_cast<float>(k) * kTau / 2;
          Instance spark;
          spark.x = at.x + std::cos(a) * 0.22f;
          spark.y = at.y + 0.05f + 0.06f * std::sin(time * 3 + static_cast<float>(k));
          spark.z = at.z + std::sin(a) * 0.22f;
          spark.sx = spark.sy = spark.sz = 0.05f;
          spark.rotation = quat_yaw(a);
          spark.tint = {1.0f, 2.6f, 1.3f, 1};
          spark.material = kGlowing;
          list.batches[static_cast<size_t>(MeshId::Sparkle)].push_back(spark);
        }
      }
    } else {
      CreatureLook look = creature_look(c.species, c.genes);
      Body body = c.body;
      battle_pose(w, i, look, body, poses);
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
      if (c.tag == Tag::Raider && !knocked_out) {
        // An aggressive monster: a red lightning bolt over its head, bobbing and turning.
        const float top = species_info(c.species).shape.height * look.size * 1.15f + look.extra_height;
        Instance bolt;
        bolt.x = c.body.pos.x;
        bolt.y = c.body.pos.y + top + 0.3f + 0.06f * std::sin(time * 3.0f + static_cast<float>(c.id));
        bolt.z = c.body.pos.z;
        bolt.rotation = quat_yaw(time * 1.8f + static_cast<float>(c.id));
        bolt.sx = bolt.sy = bolt.sz = 1.4f;
        const float flash = 0.85f + 0.15f * std::sin(time * 9.0f);
        bolt.tint = {2.6f * flash, 0.35f, 0.25f, 1};
        bolt.material = kGlowing;
        list.batches[static_cast<size_t>(MeshId::IconBolt)].push_back(bolt);
      }
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

  for (const Object& o : valley_objects(w)) {
    if (o.kind == ObjectKind::Lamp) offer(o.pos.x, o.pos.y + 2.2f, o.pos.z, 8.0f, scaled({1.0f, 0.68f, 0.32f, 1}, 1.5f * night));
    // Crystals glow in the dark of the cave, and at night.
    if (o.kind == ObjectKind::Crystal) {
      const bool violet = o.variant % 2 == 0;
      offer(o.pos.x, o.pos.y + 0.6f * o.size.x, o.pos.z, 3.0f + 3.0f * o.size.x,
            scaled(violet ? Color{0.75f, 0.45f, 1.0f, 1} : Color{0.4f, 0.85f, 1.0f, 1}, 0.35f + 0.9f * night));
    }
    if (o.kind == ObjectKind::Hearth) {  // a fire, always (inside)
      const SinCos sc = sincos16(o.yaw);
      const float flame = 1 + 0.12f * std::sin(time * 9.0f) + 0.06f * std::sin(time * 23.0f);
      offer(o.pos.x - 0.3f * sc.sin, o.pos.y + 0.6f, o.pos.z - 0.3f * sc.cos, 7.5f, scaled({1.0f, 0.6f, 0.28f, 1}, 1.4f * flame));
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
  // The west's (among the wilds' objects: only those around the player).
  if (!inside(w) && w.west.valid()) {
    const size_t valley = valley_objects(w).size();
    const Box around{{x - kLightReach, -1e9f, z - kLightReach}, {x + kLightReach, 1e9f, z + kLightReach}};
    std::vector<uint32_t> seen;
    for_objects_near(w, around, [&](uint32_t k, const Box&) {
      if (k < valley || std::find(seen.begin(), seen.end(), k) != seen.end()) return;
      seen.push_back(k);
      const Object& o = w.objects[k];
      switch (o.kind) {
        case ObjectKind::Lamp: offer(o.pos.x, o.pos.y + 2.2f, o.pos.z, 8.0f, scaled({1.0f, 0.68f, 0.32f, 1}, 1.5f * night)); break;
        case ObjectKind::Crystal:
          offer(o.pos.x, o.pos.y + 0.6f * o.size.x, o.pos.z, 3.0f + 3.0f * o.size.x,
                scaled({0.75f, 0.45f, 1.0f, 1}, 0.35f + 0.9f * night));
          break;
        case ObjectKind::Meteorite:
          offer(o.pos.x, o.pos.y + 1.2f * o.size.x, o.pos.z, 7.0f, scaled({0.8f, 0.45f, 1.0f, 1}, 0.3f + 1.0f * night));
          break;
        case ObjectKind::Campfire: {
          const float flame = 1 + 0.15f * std::sin(time * 9.0f) + 0.07f * std::sin(time * 23.0f);
          offer(o.pos.x, o.pos.y + 0.7f, o.pos.z, 9.0f, scaled({1.0f, 0.58f, 0.25f, 1}, (0.5f + 1.2f * night) * flame));
          break;
        }
        case ObjectKind::Lighthouse:
          offer(o.pos.x, o.pos.y + 13.2f, o.pos.z, 14.0f, scaled({1.0f, 0.85f, 0.5f, 1}, 2.0f * night));
          break;
        case ObjectKind::Teleporter:
          if ((w.teleporters >> o.variant) & 1u) {
            offer(o.pos.x, o.pos.y + 0.8f, o.pos.z, 5.0f, scaled({0.45f, 0.9f, 1.0f, 1}, 0.2f + 0.9f * night));
          }
          break;
        default: break;
      }
    });
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
  // The healer's staff glows green at night.
  if (const int odette = npc_index(w, npc::kOdette); odette > 0 && night > 0.01f) {
    const Vec3 at = crystal_position(w.creatures[static_cast<size_t>(odette)].body);
    offer(at.x, at.y, at.z, 5.0f, scaled({0.45f, 1.0f, 0.55f, 1}, 0.9f * night));
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
