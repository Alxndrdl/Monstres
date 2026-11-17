#include "monstres/core/variants.hpp"

#include "monstres/core/battle.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace monstres {

namespace {

using enum Rarity;
using enum VariantKind;

static_assert(kMaxAlleles <= 4, "genes_code packs an allele in 2 bits");

constexpr ZoneMask kRuinsOnly = zone_bit(Zone::Ruins);

// Every monster is a male or a female (males are drawn bigger): the first gene.
constexpr Gene kSex{"sex", Sex, 2, {{{"male", Common}, {"female", Common}}}};

// Names and texts of each variant are in web/lore.js, under these identifiers.
constexpr Genetics kGenetics[] = {
    // Companion: Pompon, fox of embers.
    {Family::Beast, 4,
     {{kSex,
       {"fur", Color, 3, {{{"ember", Common}, {"golden", Uncommon}, {"ash", Rare}}}},
       {"ears", Shape, 3, {{{"pointed", Common}, {"long", Uncommon}, {"floppy", Rare}}}},
       {"tails", Shape, 2, {{{"one", Common}, {"two", Rare}}}}}},
     Element::Electric},
    // Slime: Gloubi.
    {Family::Ooze, 4,
     {{kSex,
       {"color", Color, 3, {{{"teal", Common}, {"blue", Uncommon}, {"green", Uncommon}}}},
       {"eyes", Shape, 3, {{{"two", Common}, {"one", Uncommon}, {"three", Rare}}}},
       {"top", Shape, 3, {{{"smooth", Common}, {"horn", Uncommon}, {"antennae", Rare}}}}}},
     Element::Fire},
    // Mushroom: Champi. The blue cap only grows in the ruins.
    {Family::Plant, 4,
     {{kSex,
       {"cap", Color, 3, {{{"red", Common}, {"golden", Uncommon}, {"blue", Rare, kRuinsOnly}}}},
       {"shape", Shape, 3, {{{"round", Common}, {"wide", Uncommon}, {"pointed", Rare}}}},
       {"legs", Shape, 2, {{{"short", Common}, {"long", Uncommon}}}}}},
     Element::Light},
    // Bird: Piou.
    {Family::Bird, 4,
     {{kSex,
       // Emerald: about 1 Piou in 128 (Mirabelle's is one of them).
       {"plumage", Color, 4, {{{"yellow", Common}, {"blue", Uncommon}, {"pink", Uncommon}, {"emerald", VeryRare}}}},
       {"crest", Shape, 2, {{{"none", Common}, {"crest", Uncommon}}}},
       {"wings", Shape, 2, {{{"small", Common}, {"large", Rare}}}}}},
     Element::Electric},
    // Villager: not a monster.
    {Family::None, 0, {}, Element::Count},
    // Crow: Croa.
    {Family::Bird, 4,
     {{kSex,
       {"eyes", Color, 3, {{{"black", Common}, {"white", Uncommon}, {"red", Rare}}}},
       {"horns", Shape, 2, {{{"none", Common}, {"horns", Rare}}}},
       {"wings", Shape, 2, {{{"normal", Common}, {"long", Uncommon}}}}}},
     Element::Light},
    // Fish: Bloup.
    {Family::Fish, 4,
     {{kSex,
       {"scales", Color, 3, {{{"blue", Common}, {"red", Uncommon}, {"green", Uncommon}}}},
       {"fins", Shape, 2, {{{"small", Common}, {"veil", Uncommon}}}},
       {"lure", Shape, 2, {{{"none", Common}, {"lantern", Rare}}}}}},
     Element::Electric},
    // Spark: Zapou, a hedgehog whose quills crackle.
    {Family::Beast, 4,
     {{kSex,
       {"quills", Color, 3, {{{"blue", Common}, {"yellow", Uncommon}, {"violet", Rare}}}},
       {"spines", Shape, 2, {{{"short", Common}, {"long", Uncommon}}}},
       {"ears", Shape, 2, {{{"round", Common}, {"tufted", Rare}}}}}},
     Element::Shadow},
    // Axolotl: Ploufi.
    {Family::Reptile, 4,
     {{kSex,
       {"skin", Color, 3, {{{"pink", Common}, {"blue", Uncommon}, {"gold", Rare}}}},
       {"gills", Shape, 2, {{{"small", Common}, {"large", Uncommon}}}},
       {"tail", Shape, 2, {{{"short", Common}, {"long", Rare}}}}}},
     Element::Nature},
    // Cristi: a ball of fur, the crystals on its back glow like the cave's.
    {Family::Beast, 4,
     {{kSex,
       {"crystal", Color, 3, {{{"violet", Common}, {"cyan", Uncommon}, {"rose", Rare}}}},
       {"ears", Shape, 2, {{{"pointed", Common}, {"long", Uncommon}}}},
       {"crown", Shape, 2, {{{"three", Common}, {"crown", Rare}}}}}},
     Element::Light},
};
static_assert(std::size(kGenetics) == kSpeciesCount);

uint32_t weight(Rarity r) {
  switch (r) {
    case Common: return 400;
    case Uncommon: return 120;
    case Rare: return 32;
    default: return 5;  // with a common and two uncommon: 1 in 129
  }
}

bool found_in(ZoneMask zones, Zone zone) { return zones == kEverywhere || (zones & zone_bit(zone)) != 0; }

bool has_rare_forms(Species species) { return genetics(species).elemental != Element::Count; }
bool has_builds(Species species) { return genetics(species).gene_count > 0; }
size_t rare_count(Species species) { return has_rare_forms(species) ? 2 : 0; }
constexpr Form kBuildVariants[] = {Form::Micro, Form::Alpha, Form::Omega};  // in the bestiary, after the rare forms
bool is_female(const Genes& g) { return g.allele[0] == 1; }  // the sex is the first gene

}  // namespace

const Genetics& genetics(Species species) {
  const auto i = static_cast<size_t>(species);
  return kGenetics[i < kSpeciesCount ? i : static_cast<size_t>(Species::Villager)];
}

size_t variant_count(Species species) {
  const Genetics& g = genetics(species);
  size_t n = 0;
  for (size_t i = 0; i < g.gene_count; ++i) n += g.genes[i].count;
  return n + rare_count(species) + (has_builds(species) ? std::size(kBuildVariants) : 0);
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
  if (index < rare_count(species)) return {index == 0 ? Chromatic : Elemental, 0, 0, VeryRare, kEverywhere};
  index -= rare_count(species);
  const Form form = index < std::size(kBuildVariants) ? kBuildVariants[index] : Form::Normal;
  return {Build, static_cast<uint8_t>(form), 0, VeryRare, kEverywhere};
}

std::string variant_id(Species species, size_t index) {
  const Variant v = variant_at(species, index);
  if (v.kind == Chromatic) return "chromatic";
  if (v.kind == Elemental) return "elemental";
  if (v.kind == Build) {
    const auto form = static_cast<Form>(v.gene);
    return form == Form::Micro ? "micro" : form == Form::Alpha ? "alpha" : "omega";
  }
  const Gene& gene = genetics(species).genes[v.gene];
  return std::string(gene.id) + "." + gene.alleles[v.allele].id;
}

bool has_variant(Species species, const Genes& genes, size_t index) {
  if (index >= variant_count(species)) return false;
  const Variant v = variant_at(species, index);
  if (v.kind == Chromatic) return genes.chromatic;
  if (v.kind == Elemental) return genes.elemental;
  if (v.kind == Build) return genes.form == static_cast<Form>(v.gene);
  return genes.allele[v.gene] == v.allele;
}

Genes genes_with(Species species, size_t index) {
  Genes g;
  if (index >= variant_count(species)) return g;
  const Variant v = variant_at(species, index);
  if (v.kind == Chromatic) g.chromatic = true;
  else if (v.kind == Elemental) g.elemental = true;
  else if (v.kind == Build) {
    g.form = static_cast<Form>(v.gene);
    if (g.form == Form::Micro) g.allele[0] = 1;  // a female
  } else g.allele[v.gene] = v.allele;
  return g;
}

Genes roll_genes(Species species, Zone zone, Rng& rng) {
  const Genetics& g = genetics(species);
  Genes out;
  // The sex is drawn last: adding it kept the other genes of every world as they were.
  const auto roll = [&](size_t i) {
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
  };
  for (size_t i = 0; i < g.gene_count; ++i) {
    if (g.genes[i].kind != Sex) roll(i);
  }
  if (has_rare_forms(species)) {
    const uint32_t chromatic = rng.below(rules::kChromaticChance);
    const uint32_t elemental = rng.below(rules::kElementalChance);
    out.chromatic = chromatic == 0;
    out.elemental = elemental == 0 && !out.chromatic;  // never both
  }
  for (size_t i = 0; i < g.gene_count; ++i) {
    if (g.genes[i].kind == Sex) roll(i);
  }
  // The build last: the genes drawn before it stayed as they were.
  if (has_builds(species)) {
    const uint32_t build = rng.below(rules::kBuildChance);
    if (build == 0) out.form = Form::Alpha;
    else if (build <= 2 && is_female(out)) out.form = Form::Micro;
  }
  return out;
}

void assign_genes(World& w) {
  for (size_t i = 0; i < w.creatures.size(); ++i) {
    Creature& c = w.creatures[i];
    if (i == 0 || genetics(c.species).gene_count == 0) {  // the companion's genes are the team member's
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
  code |= (g.chromatic ? 1u : 0u) << (2 * kMaxGenes);  // bits 0-7: alleles
  code |= (g.elemental ? 1u : 0u) << (2 * kMaxGenes + 1);
  code |= (static_cast<uint32_t>(g.form) & 3u) << (2 * kMaxGenes + 2);
  return code;
}

Genes genes_from_code(uint32_t code) {
  Genes g;
  for (size_t i = 0; i < kMaxGenes; ++i) g.allele[i] = static_cast<uint8_t>((code >> (2 * i)) & 3u);
  g.chromatic = ((code >> (2 * kMaxGenes)) & 1u) != 0;
  g.elemental = ((code >> (2 * kMaxGenes + 1)) & 1u) != 0;
  g.form = static_cast<Form>((code >> (2 * kMaxGenes + 2)) & 3u);
  return g;
}

bool valid_genes(Species species, const Genes& genes) {
  const Genetics& g = genetics(species);
  for (size_t i = 0; i < kMaxGenes; ++i) {
    const uint8_t count = i < g.gene_count ? g.genes[i].count : 1;
    if (genes.allele[i] >= count) return false;
  }
  if (genes.chromatic && genes.elemental) return false;
  if (genes.form >= Form::Count) return false;
  if (genes.form != Form::Normal) {
    if (!has_builds(species)) return false;
    if (genes.form == Form::Micro && !is_female(genes)) return false;
    if (genes.form == Form::Omega && is_female(genes)) return false;
  }
  return has_rare_forms(species) || (!genes.chromatic && !genes.elemental);
}

bool very_rare(Species species, const Genes& genes) {
  if (genes.chromatic || genes.elemental || genes.form != Form::Normal) return true;
  const Genetics& g = genetics(species);
  for (size_t i = 0; i < g.gene_count; ++i) {
    if (genes.allele[i] < g.genes[i].count && g.genes[i].alleles[genes.allele[i]].rarity == VeryRare) return true;
  }
  return false;
}

int detect_rare(const World& w) {
  if (w.detect_ticks == 0) return -1;
  int best = -1;
  float best_d = rules::kDetectRadius * rules::kDetectRadius;
  for (size_t i = 1; i < w.creatures.size(); ++i) {
    const Creature& c = w.creatures[i];
    if (!monster_stats(c.species).is_monster || owned(w, c) || !very_rare(c.species, c.genes)) continue;
    const float dx = c.body.pos.x - w.player.pos.x, dz = c.body.pos.z - w.player.pos.z;
    if (dx * dx + dz * dz < best_d) {
      best = static_cast<int>(i);
      best_d = dx * dx + dz * dz;
    }
  }
  return best;
}

float build_scale(Form form) {
  switch (form) {
    case Form::Micro: return 0.62f;
    case Form::Alpha: return 1.4f;
    case Form::Omega: return 2.3f;
    default: return 1.0f;
  }
}

namespace {

// Variants of one species with modifiers of their own.
struct OwnMods {
  Species species;
  const char* id;  // variant_id
  StatMods mods;
};
const OwnMods kOwnMods[] = {
    // Gloubi: more eyes, sharper but frailer; a horn, tougher but less charming.
    {Species::Slime, "eyes.one", {{{{Stat::Intelligence, -1}, {Stat::Constitution, 1}}}, 2}},
    {Species::Slime, "eyes.three", {{{{Stat::Intelligence, 1}, {Stat::Constitution, -1}}}, 2}},
    {Species::Slime, "top.horn", {{{{Stat::Charisma, -1}, {Stat::Defense, 2}}}, 2}},
    {Species::Slime, "top.antennae", {{{{Stat::Intelligence, 1}}}, 1}},
};

void add(StatMods& m, Stat stat, int value) {
  if (m.count < kMaxStatMods) m.mods[m.count++] = {stat, static_cast<int8_t>(value)};
}

}  // namespace

StatMods variant_mods(Species species, size_t index) {
  StatMods m;
  if (index >= variant_count(species)) return m;
  const Variant v = variant_at(species, index);
  switch (v.kind) {
    case Chromatic: add(m, Stat::Charisma, 2); return m;
    case Elemental: add(m, Stat::Charisma, 1); add(m, Stat::Intelligence, 1); return m;
    case Build:
      switch (static_cast<Form>(v.gene)) {
        case Form::Alpha:
          for (Stat s : {Stat::Strength, Stat::Dexterity, Stat::Constitution, Stat::Charisma, Stat::Intelligence}) {
            add(m, s, 2);
          }
          break;
        case Form::Micro:
          add(m, Stat::Strength, -5);
          add(m, Stat::Dexterity, 5);
          add(m, Stat::Constitution, -5);
          add(m, Stat::Charisma, 5);
          break;
        case Form::Omega:
          add(m, Stat::Strength, 5);
          add(m, Stat::Dexterity, -5);
          add(m, Stat::Constitution, 5);
          add(m, Stat::Charisma, -5);
          break;
        default: break;
      }
      return m;
    case Color:
      if (v.rarity == VeryRare) add(m, Stat::Charisma, 1);
      break;
    default: break;
  }
  if (std::none_of(std::begin(kOwnMods), std::end(kOwnMods), [&](const OwnMods& o) { return o.species == species; })) {
    return m;
  }
  const std::string id = variant_id(species, index);
  for (const OwnMods& o : kOwnMods) {
    if (o.species == species && id == o.id) {
      for (uint8_t k = 0; k < o.mods.count; ++k) add(m, o.mods.mods[k].stat, o.mods.mods[k].value);
    }
  }
  return m;
}

int stat_mod(Species species, const Genes& genes, Stat stat) {
  int total = 0;
  for (size_t i = 0; i < variant_count(species); ++i) {
    if (!has_variant(species, genes, i)) continue;
    const StatMods m = variant_mods(species, i);
    for (uint8_t k = 0; k < m.count; ++k) total += m.mods[k].stat == stat ? m.mods[k].value : 0;
  }
  return total;
}

uint8_t gift_value(Species species, const Genes& genes, size_t gift, uint8_t base) {
  const int v = base + stat_mod(species, genes, static_cast<Stat>(gift));
  return static_cast<uint8_t>(std::clamp(v, 0, static_cast<int>(kGiftMax)));
}

Gifts monster_gifts(const Monster& m) {
  Gifts g{};
  for (size_t k = 0; k < kGiftCount; ++k) g[k] = gift_value(m.species, m.genes, k, m.gifts[k]);
  return g;
}

namespace {
// Typical size (mm) and weight (g) of a normal male.
struct Measures {
  uint16_t size;
  uint32_t weight;
};
constexpr Measures kMeasures[] = {
      {450, 6500},  // Companion
      {380, 5200},  // Slime
      {300, 1800},  // Mushroom
      {180, 120},   // Bird
      {0, 0},       // Villager
      {420, 650},   // Crow
      {350, 2400},  // Fish
      {260, 900},   // Spark
      {300, 1300},  // Axolotl
      {220, 300},   // Cristi
};
static_assert(std::size(kMeasures) == kSpeciesCount);
}  // namespace

TypicalMeasures typical_measures(Species species) {
  const Measures& m = kMeasures[static_cast<size_t>(species) < kSpeciesCount ? static_cast<size_t>(species) : 0];
  // As many females as males, drawn 0.92 times smaller (roll_traits).
  constexpr float kFemale = 0.92f;
  return {static_cast<float>(m.size) * (1 + kFemale) / 2,
          static_cast<float>(m.weight) * (1 + kFemale * kFemale * kFemale) / 2};
}

Traits roll_traits(uint64_t seed, uint32_t id, Species species, const Genes& genes) {
  Rng rng(seed ^ 0x545241495453ULL, id);  // "TRAITS", one stream per creature
  Traits t;
  for (size_t k = 0; k < kGiftCount; ++k) {
    t.gifts[k] = static_cast<uint8_t>(kGiftBaseMin + rng.below(kGiftBaseMax - kGiftBaseMin + 1u));
  }
  const Measures& m = kMeasures[static_cast<size_t>(species) < kSpeciesCount ? static_cast<size_t>(species) : 0];
  const float sex = is_female(genes) ? 0.92f : 1.0f;
  const float chance = 0.88f + 0.01f * static_cast<float>(rng.below(25));  // 0.88 to 1.12
  const float size = sex * build_scale(genes.form) * chance;
  const float heavy = 0.9f + 0.01f * static_cast<float>(rng.below(21));  // 0.9 to 1.1
  t.size_mm = static_cast<uint16_t>(std::lround(static_cast<float>(m.size) * size));
  t.weight_g = static_cast<uint32_t>(std::lround(static_cast<double>(m.weight) * size * size * size * heavy));
  t.age_days = static_cast<uint16_t>(20 + rng.below(1200));
  return t;
}

void give_traits(Monster& m, const Traits& t, int32_t today) {
  m.gifts = t.gifts;
  m.size_mm = t.size_mm;
  m.weight_g = t.weight_g;
  m.born_day = today - t.age_days;
}

}  // namespace monstres
