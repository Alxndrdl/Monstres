#include "monstres/core/variants.hpp"

#include <iterator>

namespace monstres {

namespace {

using enum Rarity;
using enum VariantKind;

static_assert(kMaxGenes * kMaxAlleles + 2 <= kMaxVariants);
static_assert(kMaxAlleles <= 4, "genes_code packs an allele in 2 bits");

constexpr ZoneMask kRuinsOnly = zone_bit(Zone::Ruins);

// Names and texts of each variant are in web/lore.js, under these identifiers.
constexpr Genetics kGenetics[] = {
    // Companion: Pompon, fox of embers.
    {Family::Beast, 3,
     {{{"fur", Color, 3, {{{"ember", Common}, {"golden", Uncommon}, {"ash", Rare}}}},
       {"ears", Shape, 3, {{{"pointed", Common}, {"long", Uncommon}, {"floppy", Rare}}}},
       {"tails", Shape, 2, {{{"one", Common}, {"two", Rare}}}}}},
     Element::Electric},
    // Slime: Gloubi.
    {Family::Ooze, 3,
     {{{"color", Color, 3, {{{"teal", Common}, {"blue", Uncommon}, {"green", Uncommon}}}},
       {"eyes", Shape, 3, {{{"two", Common}, {"one", Uncommon}, {"three", Rare}}}},
       {"top", Shape, 3, {{{"smooth", Common}, {"horn", Uncommon}, {"antennae", Rare}}}}}},
     Element::Fire},
    // Mushroom: Champi. The blue cap only grows in the ruins.
    {Family::Plant, 3,
     {{{"cap", Color, 3, {{{"red", Common}, {"golden", Uncommon}, {"blue", Rare, kRuinsOnly}}}},
       {"shape", Shape, 3, {{{"round", Common}, {"wide", Uncommon}, {"pointed", Rare}}}},
       {"legs", Shape, 2, {{{"short", Common}, {"long", Uncommon}}}}}},
     Element::Light},
    // Bird: Piou.
    {Family::Bird, 3,
     {{{"plumage", Color, 3, {{{"yellow", Common}, {"blue", Uncommon}, {"pink", Uncommon}}}},
       {"crest", Shape, 2, {{{"none", Common}, {"crest", Uncommon}}}},
       {"wings", Shape, 2, {{{"small", Common}, {"large", Rare}}}}}},
     Element::Electric},
    // Villager: not a monster.
    {Family::None, 0, {}, Element::Count},
    // Crow: Croa.
    {Family::Bird, 3,
     {{{"eyes", Color, 3, {{{"black", Common}, {"white", Uncommon}, {"red", Rare}}}},
       {"horns", Shape, 2, {{{"none", Common}, {"horns", Rare}}}},
       {"wings", Shape, 2, {{{"normal", Common}, {"long", Uncommon}}}}}},
     Element::Light},
    // Fish: Bloup.
    {Family::Fish, 3,
     {{{"scales", Color, 3, {{{"blue", Common}, {"red", Uncommon}, {"green", Uncommon}}}},
       {"fins", Shape, 2, {{{"small", Common}, {"veil", Uncommon}}}},
       {"lure", Shape, 2, {{{"none", Common}, {"lantern", Rare}}}}}},
     Element::Electric},
};
static_assert(std::size(kGenetics) == kSpeciesCount);

uint32_t weight(Rarity r) {
  switch (r) {
    case Common: return 100;
    case Uncommon: return 30;
    case Rare: return 8;
    default: return 2;
  }
}

bool found_in(ZoneMask zones, Zone zone) { return zones == kEverywhere || (zones & zone_bit(zone)) != 0; }

bool has_rare_forms(Species species) { return genetics(species).elemental != Element::Count; }

}  // namespace

const Genetics& genetics(Species species) {
  const auto i = static_cast<size_t>(species);
  return kGenetics[i < kSpeciesCount ? i : static_cast<size_t>(Species::Villager)];
}

size_t variant_count(Species species) {
  const Genetics& g = genetics(species);
  size_t n = 0;
  for (size_t i = 0; i < g.gene_count; ++i) n += g.genes[i].count;
  return n + (has_rare_forms(species) ? 2 : 0);
}

Variant variant_at(Species species, size_t index) {
  const Genetics& g = genetics(species);
  for (uint8_t gene = 0; gene < g.gene_count; ++gene) {
    const Gene& ge = g.genes[gene];
    if (index < ge.count) {
      const Allele& a = ge.alleles[index];
      return {ge.kind, gene, static_cast<uint8_t>(index), a.rarity, a.zones};
    }
    index -= ge.count;
  }
  return {index == 0 ? Chromatic : Elemental, 0, 0, VeryRare, kEverywhere};
}

std::string variant_id(Species species, size_t index) {
  const Variant v = variant_at(species, index);
  if (v.kind == Chromatic) return "chromatic";
  if (v.kind == Elemental) return "elemental";
  const Gene& gene = genetics(species).genes[v.gene];
  return std::string(gene.id) + "." + gene.alleles[v.allele].id;
}

bool has_variant(Species species, const Genes& genes, size_t index) {
  if (index >= variant_count(species)) return false;
  const Variant v = variant_at(species, index);
  if (v.kind == Chromatic) return genes.chromatic;
  if (v.kind == Elemental) return genes.elemental;
  return genes.allele[v.gene] == v.allele;
}

Genes genes_with(Species species, size_t index) {
  Genes g;
  if (index >= variant_count(species)) return g;
  const Variant v = variant_at(species, index);
  if (v.kind == Chromatic) g.chromatic = true;
  else if (v.kind == Elemental) g.elemental = true;
  else g.allele[v.gene] = v.allele;
  return g;
}

Genes roll_genes(Species species, Zone zone, Rng& rng) {
  const Genetics& g = genetics(species);
  Genes out;
  for (size_t i = 0; i < g.gene_count; ++i) {
    const Gene& gene = g.genes[i];
    uint32_t total = 0;
    for (size_t a = 0; a < gene.count; ++a) {
      if (found_in(gene.alleles[a].zones, zone)) total += weight(gene.alleles[a].rarity);
    }
    uint32_t pick = rng.below(total);
    for (uint8_t a = 0; a < gene.count; ++a) {
      if (!found_in(gene.alleles[a].zones, zone)) continue;
      const uint32_t w = weight(gene.alleles[a].rarity);
      if (pick < w) {
        out.allele[i] = a;
        break;
      }
      pick -= w;
    }
  }
  if (has_rare_forms(species)) {
    const uint32_t chromatic = rng.below(rules::kChromaticChance);
    const uint32_t elemental = rng.below(rules::kElementalChance);
    out.chromatic = chromatic == 0;
    out.elemental = elemental == 0;
  }
  return out;
}

void assign_genes(World& w) {
  for (size_t i = 0; i < w.creatures.size(); ++i) {
    Creature& c = w.creatures[i];
    if (c.species == Species::Companion || genetics(c.species).gene_count == 0) {
      c.genes = {};
      continue;
    }
    Rng rng(w.seed ^ 0x47454E4553ULL, i);  // "GENES", one stream per creature
    c.genes = roll_genes(c.species, zone_at(w, c.home.x, c.home.z), rng);
  }
}

Elements creature_elements(Species species, const Genes& genes) {
  const MonsterStats& st = monster_stats(species);
  const Element first = genes.elemental && has_rare_forms(species) ? genetics(species).elemental : st.element;
  // An elemental form never has the same element twice.
  const Element second = st.element2 == first ? Element::Count : st.element2;
  return {first, second};
}

namespace {

void mark(World& w, const Creature& c, Discovery to) {
  auto& entries = w.dex[static_cast<size_t>(c.species)];
  const size_t n = variant_count(c.species);
  for (size_t i = 0; i < n; ++i) {
    if (has_variant(c.species, c.genes, i) && entries[i] < to) entries[i] = to;
  }
}

}  // namespace

void discover(World& w) {
  constexpr float r2 = rules::kDiscoveryDistance * rules::kDiscoveryDistance;
  for (const Creature& c : w.creatures) {
    if (!monster_stats(c.species).is_monster) continue;
    const float dx = c.body.pos.x - w.player.pos.x, dz = c.body.pos.z - w.player.pos.z;
    if (dx * dx + dz * dz < r2) mark(w, c, Discovery::Seen);
  }
}

void capture(World& w, size_t index) {
  if (index < w.creatures.size() && monster_stats(w.creatures[index].species).is_monster) {
    mark(w, w.creatures[index], Discovery::Captured);
  }
}

uint32_t genes_code(const Genes& g) {
  uint32_t code = 0;
  for (size_t i = 0; i < kMaxGenes; ++i) code |= static_cast<uint32_t>(g.allele[i] & 3u) << (2 * i);
  code |= (g.chromatic ? 1u : 0u) << (2 * kMaxGenes);
  code |= (g.elemental ? 1u : 0u) << (2 * kMaxGenes + 1);
  return code;
}

Genes genes_from_code(uint32_t code) {
  Genes g;
  for (size_t i = 0; i < kMaxGenes; ++i) g.allele[i] = static_cast<uint8_t>((code >> (2 * i)) & 3u);
  g.chromatic = ((code >> (2 * kMaxGenes)) & 1u) != 0;
  g.elemental = ((code >> (2 * kMaxGenes + 1)) & 1u) != 0;
  return g;
}

bool valid_genes(Species species, const Genes& genes) {
  const Genetics& g = genetics(species);
  for (size_t i = 0; i < kMaxGenes; ++i) {
    const uint8_t count = i < g.gene_count ? g.genes[i].count : 1;
    if (genes.allele[i] >= count) return false;
  }
  return has_rare_forms(species) || (!genes.chromatic && !genes.elemental);
}

}  // namespace monstres
