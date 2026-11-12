// Variants of the monsters: genes, discovery in the bestiary, looks.

#include "monstres/core/debug.hpp"
#include "monstres/core/save.hpp"
#include "monstres/core/variants.hpp"
#include "monstres/render/characters.hpp"
#include "test_helpers.hpp"

#include <array>
#include <cmath>
#include <fstream>
#include <iterator>
#include <set>
#include <string>

using namespace monstres;

namespace {

std::vector<Species> monsters() {
  std::vector<Species> out;
  for (size_t i = 0; i < kSpeciesCount; ++i) {
    const auto s = static_cast<Species>(i);
    if (monster_stats(s).is_monster) out.push_back(s);
  }
  return out;
}

bool same_color(Color a, Color b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; }
bool same_quat(Quat a, Quat b) { return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w; }

bool same_look(const CreatureLook& a, const CreatureLook& b) {
  if (a.count != b.count || a.aura != b.aura || a.size != b.size) return false;
  for (size_t i = 0; i < a.count; ++i) {
    const RigPart& p = a.parts[i];
    const RigPart& q = b.parts[i];
    if (p.mesh != q.mesh || p.parent != q.parent || p.tint != q.tint || !same_quat(p.rest, q.rest) ||
        p.pivot.x != q.pivot.x || p.pivot.y != q.pivot.y || p.pivot.z != q.pivot.z || p.scale.x != q.scale.x ||
        p.scale.y != q.scale.y || p.scale.z != q.scale.z) {
      return false;
    }
    if (p.tint > 0 && !same_color(a.tints[p.tint - 1], b.tints[p.tint - 1])) return false;
  }
  return true;
}

}  // namespace

TEST_CASE("every monster species has variants with unique identifiers") {
  for (const Species s : monsters()) {
    const size_t n = variant_count(s);
    CAPTURE(static_cast<int>(s));
    CHECK(n >= 5);
    CHECK(n <= kMaxVariants);
    CHECK(genetics(s).family != Family::None);
    std::set<std::string> ids;
    for (size_t i = 0; i < n; ++i) ids.insert(variant_id(s, i));
    CHECK(ids.size() == n);
    CHECK(ids.count("chromatic") == 1);
    CHECK(ids.count("elemental") == 1);
    // The elemental form has another element.
    CHECK(genetics(s).elemental != monster_stats(s).element);
  }
  CHECK(variant_count(Species::Villager) == 0);
  CHECK(variant_id(Species::Crow, 0) == "sex.male");
  CHECK(variant_id(Species::Crow, 4) == "eyes.red");
}

TEST_CASE("genes_with shows exactly the asked variant, and packs into a code") {
  for (const Species s : monsters()) {
    for (size_t i = 0; i < variant_count(s); ++i) {
      const Genes g = genes_with(s, i);
      CAPTURE(variant_id(s, i));
      CHECK(has_variant(s, g, i));
      CHECK(valid_genes(s, g));
      CHECK(genes_from_code(genes_code(g)) == g);
    }
  }
  Genes g;
  g.allele = {1, 2, 1, 1};
  g.chromatic = true;
  CHECK(genes_from_code(genes_code(g)) == g);
  CHECK(valid_genes(Species::Crow, g));
  g.allele[2] = 2;  // crows have two kinds of horns only
  CHECK_FALSE(valid_genes(Species::Crow, g));
  CHECK_FALSE(valid_genes(Species::Villager, Genes{{}, true, false}));  // no rare forms
  CHECK_FALSE(valid_genes(Species::Slime, Genes{{}, true, true}));     // never both rare forms
}

TEST_CASE("rarer alleles are rarer, and the rare forms very rare") {
  Rng rng(5);
  std::array<int, 3> eyes{};
  std::array<int, 2> sexes{};
  int chromatic = 0, elemental = 0;
  constexpr int kRolls = 40000;
  for (int i = 0; i < kRolls; ++i) {
    const Genes g = roll_genes(Species::Crow, Zone::Village, rng);
    ++eyes[g.allele[1]];
    ++sexes[g.allele[0]];
    CHECK_FALSE((g.chromatic && g.elemental));
    chromatic += g.chromatic;
    elemental += g.elemental;
  }
  CHECK(eyes[0] > eyes[1]);
  CHECK(eyes[1] > eyes[2]);
  CHECK(eyes[2] > 0);
  CHECK(std::abs(sexes[0] - sexes[1]) < kRolls / 20);  // as many males as females
  // About one in 256.
  CHECK(chromatic > kRolls / 512);
  CHECK(chromatic < kRolls / 128);
  CHECK(elemental > kRolls / 512);
  CHECK(elemental < kRolls / 128);
}

TEST_CASE("some variants are only found in some zones") {
  Rng rng(9);
  int blue_in_meadow = 0, blue_in_ruins = 0;
  for (int i = 0; i < 5000; ++i) {
    blue_in_meadow += roll_genes(Species::Mushroom, Zone::Meadow, rng).allele[1] == 2;
    blue_in_ruins += roll_genes(Species::Mushroom, Zone::Ruins, rng).allele[1] == 2;
  }
  CHECK(blue_in_meadow == 0);
  CHECK(blue_in_ruins > 0);
  CHECK(variant_at(Species::Mushroom, 4).zones == zone_bit(Zone::Ruins));
}

TEST_CASE("the monsters of a world get their genes from the seed") {
  const World a = new_game(42);
  const World b = new_game(42);
  CHECK(a.creatures == b.creatures);
  CHECK(a.creatures[0].genes == Genes{});  // the companion is a common Pompon
  std::set<uint32_t> looks;
  for (const Creature& c : a.creatures) {
    CHECK(valid_genes(c.species, c.genes));
    if (c.species == Species::Slime) looks.insert(genes_code(c.genes));
    if (c.species == Species::Villager) CHECK(c.genes == Genes{});
  }
  CHECK(looks.size() > 1);  // slimes are not all alike
}

TEST_CASE("elements: two for the crow, another one in the elemental form") {
  CHECK(creature_elements(Species::Slime, {}).first == Element::Water);
  CHECK(creature_elements(Species::Slime, {}).second == Element::Count);
  CHECK(creature_elements(Species::Slime, genes_with(Species::Slime, variant_count(Species::Slime) - 1)).first ==
        Element::Fire);
  const Elements crow = creature_elements(Species::Crow, {});
  CHECK(crow.first == Element::Shadow);
  CHECK(crow.second == Element::Air);
  Genes dawn;
  dawn.elemental = true;
  CHECK(creature_elements(Species::Crow, dawn).first == Element::Light);
  CHECK(creature_elements(Species::Crow, dawn).second == Element::Air);
}

TEST_CASE("the companion's variants are captured from the start, the others are discovered when close") {
  World w = cached_game(42);
  const auto& companion = w.dex[static_cast<size_t>(Species::Companion)];
  for (size_t i = 0; i < variant_count(Species::Companion); ++i) {
    CHECK(companion[i] == (has_variant(Species::Companion, Genes{}, i) ? Discovery::Captured : Discovery::Unknown));
  }
  // Nothing else is known before the first tick.
  for (const Species s : monsters()) {
    if (s == Species::Companion) continue;
    for (const Discovery d : w.dex[static_cast<size_t>(s)]) CHECK(d == Discovery::Unknown);
  }

  // One monster next to the player, another one far away.
  size_t near = 0, far = 0;
  for (size_t i = 1; i < w.creatures.size(); ++i) {
    if (w.creatures[i].species != Species::Slime) continue;
    if (!near) near = i;
    else if (!far) far = i;
  }
  REQUIRE(far);
  w.creatures[far].genes = {{1, 2, 2, 2}, true, false};  // nothing else in the world looks like this
  for (Creature& c : w.creatures) {
    if (&c != &w.creatures[near] && monster_stats(c.species).is_monster && c.species != Species::Companion) {
      c.body.pos = {w.player.pos.x + 60, c.body.pos.y, w.player.pos.z + 60};
    }
  }
  w.creatures[near].genes = {{0, 1, 0, 0}, false, false};
  w.creatures[near].body.pos = {w.player.pos.x + 12, w.creatures[near].body.pos.y, w.player.pos.z + 12};
  discover(w);
  const auto& slimes = w.dex[static_cast<size_t>(Species::Slime)];
  CHECK(slimes[0] == Discovery::Seen);  // sex.male
  CHECK(slimes[3] == Discovery::Seen);  // color.blue
  CHECK(slimes[5] == Discovery::Seen);  // eyes.two
  CHECK(slimes[8] == Discovery::Seen);  // top.smooth
  CHECK(slimes[1] == Discovery::Unknown);  // sex.female: only the far one
  CHECK(slimes[2] == Discovery::Unknown);
  CHECK(slimes[11] == Discovery::Unknown);  // chromatic: only the far one

  capture(w, near);
  CHECK(slimes[3] == Discovery::Captured);  // color.blue
  discover(w);  // seeing it again does not undo the capture
  CHECK(slimes[3] == Discovery::Captured);

  // The bestiary tells the page.
  const std::string json = bestiary_json(w);
  CHECK(json.find("{\"id\":\"color.blue\",\"kind\":\"color\",\"rarity\":\"uncommon\",\"zones\":[],"
                  "\"elements\":[\"water\"],\"look\":4,\"status\":\"captured\"}") != std::string::npos);
  CHECK(json.find("{\"id\":\"cap.blue\",\"kind\":\"color\",\"rarity\":\"rare\",\"zones\":[\"ruins\"],") !=
        std::string::npos);
}

TEST_CASE("step discovers the monsters within 20 m") {
  World w = cached_game(42);
  step(w, Input{});
  for (const Creature& c : w.creatures) {
    if (!monster_stats(c.species).is_monster) continue;
    const float d = std::hypot(c.body.pos.x - w.player.pos.x, c.body.pos.z - w.player.pos.z);
    const auto& dex = w.dex[static_cast<size_t>(c.species)];
    for (size_t i = 0; i < variant_count(c.species); ++i) {
      if (has_variant(c.species, c.genes, i) && d < rules::kDiscoveryDistance - 0.1f) CHECK(dex[i] != Discovery::Unknown);
    }
  }
}

TEST_CASE("genes and discoveries are saved") {
  World w = cached_game(7);
  size_t slime = 1;
  while (slime < w.creatures.size() && w.creatures[slime].species != Species::Slime) ++slime;
  REQUIRE(slime < w.creatures.size());
  w.creatures[slime].genes = {{1, 1, 1, 0}, true, false};
  capture(w, slime);
  const auto loaded = deserialize(serialize(w));
  REQUIRE(loaded.has_value());
  CHECK(*loaded == w);

  // A gene out of range is refused.
  World bad = w;
  bad.creatures[slime].genes.allele[0] = 3;
  CHECK_FALSE(deserialize(serialize(bad)).has_value());
}

TEST_CASE("every variant changes how the monster looks") {
  for (const Species s : monsters()) {
    const CreatureLook common = creature_look(s, Genes{});
    for (size_t i = 0; i < variant_count(s); ++i) {
      const Variant v = variant_at(s, i);
      if (v.kind != VariantKind::Chromatic && v.kind != VariantKind::Elemental && v.allele == 0) continue;
      CAPTURE(variant_id(s, i));
      CHECK_FALSE(same_look(creature_look(s, genes_with(s, i)), common));
    }
    // Parents before children, within the rig's size.
    const CreatureLook all = creature_look(s, Genes{{1, 2, 2, 2}, false, true});
    CHECK(all.count <= kMaxRigParts);
    for (size_t i = 0; i < all.count; ++i) CHECK(all.parts[i].parent < static_cast<int>(i));
  }
}

TEST_CASE("females are drawn smaller than males") {
  for (const Species s : {Species::Companion, Species::Slime, Species::Crow, Species::Fish}) {
    CHECK(creature_look(s, Genes{{1, 0, 0, 0}, false, false}).size < creature_look(s, Genes{}).size);
  }
  CHECK(creature_look(Species::Villager, Genes{}).size == 1.0f);
}

TEST_CASE("looks: added and removed parts") {
  const auto count = [](const CreatureLook& l, MeshId mesh) {
    int n = 0;
    for (size_t i = 0; i < l.count; ++i) n += l.parts[i].mesh == mesh;
    return n;
  };
  CHECK(count(creature_look(Species::Companion, {{0, 0, 0, 1}, false, false}), MeshId::CompanionTail) == 2);
  CHECK(count(creature_look(Species::Slime, {{0, 0, 1, 0}, false, false}), MeshId::Eye) == 1);
  CHECK(count(creature_look(Species::Slime, {{0, 0, 2, 0}, false, false}), MeshId::Eye) == 3);
  CHECK(count(creature_look(Species::Slime, {{0, 0, 0, 2}, false, false}), MeshId::Bulb) == 2);
  CHECK(count(creature_look(Species::Crow, {{0, 2, 1, 0}, false, false}), MeshId::Horn) == 2);
  CHECK(count(creature_look(Species::Crow, {{0, 2, 0, 0}, false, false}), MeshId::EyeIris) == 2);
  CHECK(count(creature_look(Species::Fish, {{0, 0, 0, 1}, false, false}), MeshId::Bulb) == 1);
  CHECK(creature_look(Species::Bird, {{0, 0, 0, 0}, false, true}).aura);
  CHECK_FALSE(creature_look(Species::Bird, {{0, 0, 0, 0}, true, false}).aura);
}

TEST_CASE("a version 9 save (before the sex) loads: every monster gets one, discoveries move after it") {
  std::ifstream file(std::string(MONSTRES_TEST_DATA_DIR) + "/saves/v9_reference.sav", std::ios::binary);
  REQUIRE(file);
  const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  const auto w = deserialize(bytes);
  REQUIRE(w.has_value());
  const auto& companion = w->dex[static_cast<size_t>(Species::Companion)];
  CHECK(companion[0] == Discovery::Captured);  // sex.male
  CHECK(companion[2] == Discovery::Captured);  // fur.ember, first in version 9
  CHECK(companion[1] == Discovery::Unknown);   // sex.female
  std::array<int, 2> sexes{};
  for (const Creature& c : w->creatures) {
    CHECK(valid_genes(c.species, c.genes));
    if (genetics(c.species).gene_count && c.species != Species::Companion) ++sexes[c.genes.allele[0]];
  }
  CHECK(sexes[0] > 0);
  CHECK(sexes[1] > 0);
}
