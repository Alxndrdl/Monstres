#pragma once

#include "monstres/core/world.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace monstres {

// Variants: how monsters of one species differ from each other.
//
// Each species has a few genes: first the sex (males are bigger), then a
// body or eye color, the shape of the ears, the number of tails..., each
// with some alleles of different rarity; and two very rare forms, never
// both on one monster: chromatic (unusual colors) and elemental (another
// element). Each monster carries one allele of each gene, and may have one
// of the rare forms (Genes, in world.hpp). Its build (Form) is normal, but
// very rarely micro (females only) or alpha; omega ones are not found in the
// wild.
//
// The bestiary lists every variant of a species: each allele of each gene,
// in order, then the chromatic form, the elemental form, then the micro,
// alpha and omega builds. A variant is
// "seen" when a monster carrying it comes close to the player, "captured"
// when such a monster is captured. Identifiers are stable (saves, page).

// Kind of creature: beasts, birds, fish...
enum class Family : uint8_t { Beast, Bird, Fish, Reptile, Insect, Plant, Ooze, Construct, None, Count };

enum class Rarity : uint8_t { Common, Uncommon, Rare, VeryRare, Count };

enum class VariantKind : uint8_t { Sex, Color, Shape, Chromatic, Elemental, Build };

namespace rules {
inline constexpr float kDiscoveryDistance = 20.0f;  // a monster closer than this is seen (meters, on the ground)
inline constexpr uint32_t kChromaticChance = 256;   // one monster in ...
inline constexpr uint32_t kElementalChance = 256;
inline constexpr uint32_t kBuildChance = 256;       // one in ... is an alpha; a female, as often, a micro
inline constexpr float kDetectRadius = 50.0f;       // the Detection spell finds very rare monsters this close
}  // namespace rules

inline constexpr size_t kMaxAlleles = 4;

// Zones where an allele can appear, as bits (1 << Zone); 0 = everywhere.
using ZoneMask = uint32_t;
inline constexpr ZoneMask kEverywhere = 0;
inline constexpr ZoneMask zone_bit(Zone zone) { return static_cast<ZoneMask>(1u << static_cast<unsigned>(zone)); }

struct Allele {
  const char* id;
  Rarity rarity;
  ZoneMask zones = kEverywhere;
};

struct Gene {
  const char* id;
  VariantKind kind;  // Sex, Color or Shape
  uint8_t count;     // alleles used
  std::array<Allele, kMaxAlleles> alleles;
};

struct Genetics {
  Family family;
  uint8_t gene_count;
  std::array<Gene, kMaxGenes> genes;
  Element elemental;  // element of the elemental form; Count: the species has no rare forms
};
const Genetics& genetics(Species species);

// The variants of a species, in the bestiary's order.
struct Variant {
  VariantKind kind;
  uint8_t gene;    // Sex, Color and Shape: which gene, and which allele of it (Build: the Form)
  uint8_t allele;
  Rarity rarity;
  ZoneMask zones;
};
size_t variant_count(Species species);
Variant variant_at(Species species, size_t index);
// "ears.long", "chromatic", "elemental".
std::string variant_id(Species species, size_t index);

// Whether a monster with these genes shows this variant.
bool has_variant(Species species, const Genes& genes, size_t index);
// The most common genes, with this one variant.
Genes genes_with(Species species, size_t index);

// Random genes for a monster born in `zone`: alleles by their rarity, among
// those found there.
Genes roll_genes(Species species, Zone zone, Rng& rng);

// Gives every monster of the world its genes, from the seed and its place
// in the list (its own random stream: nothing else changes). The companion
// keeps the most common genes.
void assign_genes(World& world);

// Elements of a monster: its species' ones, the first one replaced in the
// elemental form. second is Element::Count when there is only one.
struct Elements {
  Element first, second;
};
Elements creature_elements(Species species, const Genes& genes);

// Marks the variants of every monster near the player as seen (once per tick, by step()).
void discover(World& world);
// Marks the variants of creatures[index] as captured.
void capture(World& world, size_t index);

// Genes packed in an integer (for the page): alleles 2 bits each, then the
// chromatic and elemental bits.
uint32_t genes_code(const Genes& genes);
Genes genes_from_code(uint32_t code);

// Whether these genes are possible for the species (loading saves).
bool valid_genes(Species species, const Genes& genes);

// Whether a monster carries something very rare: a very rare allele, a rare
// form, or a build other than normal (what the Detection spell finds).
bool very_rare(Species species, const Genes& genes);
// The rarest thing a monster carries: VeryRare as very_rare() says, else its rarest allele.
Rarity rarest(Species species, const Genes& genes);

// The very rare monster closest to the player within rules::kDetectRadius
// while the Detection spell lasts (its index in creatures), or -1. Not the
// companion, nor someone's monster (Mirabelle's lost Piou is found, until
// it is back with her).
int detect_rare(const World& world);

// What a variant changes in a monster: its gifts (strength, dexterity,
// constitution, charisma, intelligence) and some of its fight stats. A build
// moves the gifts (alpha +2 each; micro +5 dexterity and charisma, -5
// strength and constitution; omega the other way round); the chromatic form
// gives +2 charisma, the elemental +1 charisma and +1 intelligence, a very
// rare color +1 charisma; and some variants of some species (the Gloubi's
// eyes and top, for now) their own.
enum class Stat : uint8_t { Strength, Dexterity, Constitution, Charisma, Intelligence, Hp, Attack, Defense, Speed, Magic,
                            Count };
struct StatMod {
  Stat stat;
  int8_t value;
};
inline constexpr size_t kMaxStatMods = 5;
struct StatMods {
  std::array<StatMod, kMaxStatMods> mods{};
  uint8_t count = 0;
};
StatMods variant_mods(Species species, size_t index);
// The sum of the modifiers on one stat for a monster with these genes.
int stat_mod(Species species, const Genes& genes, Stat stat);
// A gift: from its drawn base (5 to 15) and the modifiers, within 0 to 20.
uint8_t gift_value(Species species, const Genes& genes, size_t gift, uint8_t base);
Gifts monster_gifts(const Monster& monster);
inline constexpr uint8_t kGiftBaseMin = 5, kGiftBaseMax = 15, kGiftMax = 20;

// The average size (mm) and weight (g) of a species, males and females
// together, of a normal build (the bestiary).
struct TypicalMeasures {
  float size_mm = 0, weight_g = 0;
};
TypicalMeasures typical_measures(Species species);

// Who a monster is, beyond its genes: its gifts (their drawn base, 5 to
// 15), size, weight and age, drawn from the world's seed and its creature id
// (the same for a monster each time it is asked). Size: its species', by
// sex, build, and a little chance.
struct Traits {
  Gifts gifts{};
  uint16_t size_mm = 0;
  uint32_t weight_g = 0;
  uint16_t age_days = 0;
};
Traits roll_traits(uint64_t seed, uint32_t id, Species species, const Genes& genes);
// Gives a team member its traits (it joins the team today).
void give_traits(Monster& monster, const Traits& traits, int32_t today);
// Size of a build compared to a normal one (drawing, sizes).
float build_scale(Form form);

}  // namespace monstres
