#pragma once

#include "monstres/core/world.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace monstres {

// Variants: how monsters of one species differ from each other.
//
// Each species has a few genes (a body or eye color, the shape of the ears,
// the number of tails...), each with some alleles of different rarity, and
// two very rare forms: chromatic (unusual colors) and elemental (another
// element). Each monster carries one allele of each gene, and may have one
// or both rare forms (Genes, in world.hpp).
//
// The bestiary lists every variant of a species: each allele of each gene,
// in order, then the chromatic form, then the elemental form. A variant is
// "seen" when a monster carrying it comes close to the player, "captured"
// when such a monster is captured. Identifiers are stable (saves, page).

// Kind of creature: beasts, birds, fish...
enum class Family : uint8_t { Beast, Bird, Fish, Reptile, Insect, Plant, Ooze, Construct, None, Count };

enum class Rarity : uint8_t { Common, Uncommon, Rare, VeryRare, Count };

enum class VariantKind : uint8_t { Color, Shape, Chromatic, Elemental };

namespace rules {
inline constexpr float kDiscoveryDistance = 20.0f;  // a monster closer than this is seen (meters, on the ground)
inline constexpr uint32_t kChromaticChance = 256;   // one monster in ...
inline constexpr uint32_t kElementalChance = 256;
}  // namespace rules

inline constexpr size_t kMaxAlleles = 4;

// Zones where an allele can appear, as bits (1 << Zone); 0 = everywhere.
using ZoneMask = uint8_t;
inline constexpr ZoneMask kEverywhere = 0;
inline constexpr ZoneMask zone_bit(Zone zone) { return static_cast<ZoneMask>(1u << static_cast<unsigned>(zone)); }

struct Allele {
  const char* id;
  Rarity rarity;
  ZoneMask zones = kEverywhere;
};

struct Gene {
  const char* id;
  VariantKind kind;  // Color or Shape
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
  uint8_t gene;    // Color and Shape: which gene, and which allele of it
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

}  // namespace monstres
