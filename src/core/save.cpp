#include "monstres/core/save.hpp"

#include "monstres/core/interior.hpp"

#include "monstres/core/battle.hpp"
#include "monstres/core/quests.hpp"
#include "monstres/core/variants.hpp"
#include "monstres/core/west.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

namespace monstres {

namespace {

constexpr uint8_t kMagic[4] = {'M', 'N', 'S', 'T'};
constexpr uint32_t kMaxObjectsInSave = 100000;
constexpr size_t kObjectBytes = 1 + 3 * 4 + 3 * 4 + 2 + 1;
constexpr float kMaxObjectSize = 20.0f;
// Speeds, directions and places read from a file: finite is not enough (a
// huge value becomes infinite, then not a number, a few sums later).
constexpr float kMaxSpeedInSave = 1000.0f;
constexpr float kMaxFacingInSave = 2.0f;
constexpr float kMaxPlaceInSave = 100.0f * rules::kWorldHalfSize;

class Writer {
public:
  void u8(uint8_t v) { out_.push_back(v); }
  void u16(uint16_t v) {
    out_.push_back(static_cast<uint8_t>(v));
    out_.push_back(static_cast<uint8_t>(v >> 8));
  }
  void u32(uint32_t v) {
    for (int i = 0; i < 4; ++i) out_.push_back(static_cast<uint8_t>(v >> (8 * i)));
  }
  void u64(uint64_t v) {
    for (int i = 0; i < 8; ++i) out_.push_back(static_cast<uint8_t>(v >> (8 * i)));
  }
  void f32(float v) { u32(std::bit_cast<uint32_t>(v)); }
  void vec3(Vec3 v) { f32(v.x); f32(v.y); f32(v.z); }
  void bytes(const uint8_t* p, size_t n) { out_.insert(out_.end(), p, p + n); }
  std::vector<uint8_t> take() { return std::move(out_); }

private:
  std::vector<uint8_t> out_;
};

class Reader {
public:
  explicit Reader(std::span<const uint8_t> in) : in_(in) {}

  bool u8(uint8_t& v) {
    if (remaining() < 1) return false;
    v = in_[pos_++];
    return true;
  }
  bool u16(uint16_t& v) {
    if (remaining() < 2) return false;
    v = static_cast<uint16_t>(in_[pos_] | (in_[pos_ + 1] << 8));
    pos_ += 2;
    return true;
  }
  bool u32(uint32_t& v) {
    if (remaining() < 4) return false;
    v = 0;
    for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(in_[pos_++]) << (8 * i);
    return true;
  }
  bool u64(uint64_t& v) {
    if (remaining() < 8) return false;
    v = 0;
    for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(in_[pos_++]) << (8 * i);
    return true;
  }
  bool f32(float& v) {
    uint32_t u = 0;
    if (!u32(u)) return false;
    v = std::bit_cast<float>(u);
    return std::isfinite(v);
  }
  bool vec3(Vec3& v) { return f32(v.x) && f32(v.y) && f32(v.z); }
  bool text(std::string& s, size_t n) {
    if (remaining() < n) return false;
    s.assign(reinterpret_cast<const char*>(in_.data() + pos_), n);
    pos_ += n;
    return s.find('\0') == std::string::npos;
  }
  size_t remaining() const { return in_.size() - pos_; }

private:
  std::span<const uint8_t> in_;
  size_t pos_ = 0;
};

std::optional<World> fail(LoadError* error, LoadError e) {
  if (error) *error = e;
  return std::nullopt;
}

bool inside_world(Vec3 p) {
  const float limit = rules::kWorldHalfSize;
  // Heights: valleys and the lake bed are below 0, but never this deep.
  return p.x >= -limit && p.x <= limit && p.z >= -limit && p.z <= limit && p.y >= -50 && p.y < 1000;
}

}  // namespace

const char* load_error_name(LoadError e) {
  switch (e) {
    case LoadError::None: return "none";
    case LoadError::TooShort: return "too-short";
    case LoadError::BadMagic: return "bad-magic";
    case LoadError::UnsupportedVersion: return "unsupported-version";
    case LoadError::Corrupt: return "corrupt";
  }
  return "?";
}

namespace {

constexpr uint32_t kMaxCreaturesInSave = 4096;
constexpr size_t kSpeciesCountV18 = 7;  // before Zapou and Ploufi
constexpr size_t kSpeciesCountV21 = 9;  // before Cristi
constexpr size_t kSpeciesCountV22 = 10;  // before Chipou, Hulou, Laini and Taupi
constexpr size_t kQuestCountV22 = 6;     // before the west's
constexpr uint8_t kTeleportersV22 = 5;
constexpr uint8_t kPlacesV22 = 4;        // outside, cave, house, tower

// Chromatic (1), elemental (2), the build (bits 2-3).
uint8_t forms_byte(const Genes& g) {
  return static_cast<uint8_t>((g.chromatic ? 1 : 0) | (g.elemental ? 2 : 0) | (static_cast<unsigned>(g.form) << 2));
}
// Before version 18 there was no build: only chromatic and elemental.
bool read_forms(uint8_t forms, bool with_builds, Genes& g) {
  if (forms > (with_builds ? 15 : 3)) return false;
  g.chromatic = (forms & 1) != 0;
  g.elemental = (forms & 2) != 0;
  g.form = static_cast<Form>(forms >> 2);
  return true;
}

void write_body(Writer& out, const Body& b) {
  out.vec3(b.pos);
  out.f32(b.velocity_y);
  out.u8(b.on_ground ? 1 : 0);
  out.f32(b.facing_x);
  out.f32(b.facing_z);
  out.f32(b.speed);
}

// `with_speed`: version 4 bodies had no speed.
bool read_body(Reader& in, Body& b, bool with_speed) {
  uint8_t on_ground = 0;
  const bool ok = in.vec3(b.pos) && in.f32(b.velocity_y) && in.u8(on_ground) && in.f32(b.facing_x) &&
                  in.f32(b.facing_z) && (!with_speed || in.f32(b.speed));
  b.on_ground = on_ground == 1;
  const auto within = [](float v, float limit) { return v >= -limit && v <= limit; };
  return ok && on_ground <= 1 && inside_world(b.pos) && within(b.velocity_y, kMaxSpeedInSave) &&
         within(b.speed, kMaxSpeedInSave) && within(b.facing_x, kMaxFacingInSave) && within(b.facing_z, kMaxFacingInSave);
}

}  // namespace

namespace {

void write_monster(Writer& out, const Monster& m) {
  out.u8(static_cast<uint8_t>(m.species));
  for (uint8_t a : m.genes.allele) out.u8(a);
  out.u8(forms_byte(m.genes));
  out.u16(m.level);
  out.u32(m.xp);
  out.u16(m.hp);
  out.u8(static_cast<uint8_t>(m.tag));
  out.u8(m.beaten ? 1 : 0);
  for (uint8_t g : m.gifts) out.u8(g);
  out.u16(m.size_mm);
  out.u32(m.weight_g);
  out.u32(static_cast<uint32_t>(m.born_day));
}

// Everything but where the player is (inside or out).
void write_world(Writer& out, const World& w) {
  out.bytes(kMagic, sizeof kMagic);
  out.u32(kSaveVersion);
  out.u64(w.seed);
  out.u64(w.tick);
  out.u64(w.rng.raw_state());
  out.u64(w.rng.raw_inc());
  write_body(out, w.player);
  // The wilds' objects are not saved: drawn again from the seed at each load.
  const size_t saved_objects = w.objects.size() - std::min<size_t>(w.wild_objects, w.objects.size());
  out.u32(static_cast<uint32_t>(saved_objects));
  for (size_t k = 0; k < saved_objects; ++k) {
    const Object& o = w.objects[k];
    out.u8(static_cast<uint8_t>(o.kind));
    out.vec3(o.pos);
    out.vec3(o.size);
    out.u16(o.yaw);
    out.u8(o.variant);
  }
  // The farm's grazing monsters are drawn again from World::farm (not saved).
  out.u32(static_cast<uint32_t>(std::count_if(w.creatures.begin(), w.creatures.end(),
                                              [](const Creature& c) { return c.tag != Tag::Boarder; })));
  for (const Creature& c : w.creatures) {
    if (c.tag == Tag::Boarder) continue;
    out.u8(static_cast<uint8_t>(c.species));
    write_body(out, c.body);
    out.u8(static_cast<uint8_t>(c.mind));
    out.u16(c.timer);
    out.f32(c.target.x);
    out.f32(c.target.z);
    out.f32(c.target_y);
    out.f32(c.home.x);
    out.f32(c.home.z);
    out.u8(c.variant);
    for (uint8_t a : c.genes.allele) out.u8(a);
    out.u8(forms_byte(c.genes));
    out.u8(c.level);
    out.u32(c.id);
    out.u8(static_cast<uint8_t>((c.released ? 1 : 0) | (c.beaten ? 2 : 0)));
    out.u8(c.npc);
    out.u8(static_cast<uint8_t>(c.tag));
  }
  const size_t name_bytes = std::min(w.wizard.name.size(), kMaxNameBytes);
  out.u8(static_cast<uint8_t>(name_bytes));
  out.bytes(reinterpret_cast<const uint8_t*>(w.wizard.name.data()), name_bytes);
  out.u16(w.wizard.level);
  out.u32(w.wizard.xp);
  out.u16(w.wizard.mana);
  const WizardLook& look = w.wizard.look;
  for (uint8_t v : {static_cast<uint8_t>(look.woman ? 1 : 0), look.skin, look.hair, look.hair_color, look.eyes,
                    look.staff, look.robe, look.headwear}) {
    out.u8(v);
  }
  out.u8(static_cast<uint8_t>(kSpeciesCount));
  out.u8(static_cast<uint8_t>(kMaxVariants));
  for (const auto& species : w.dex) {
    for (const Discovery d : species) out.u8(static_cast<uint8_t>(d));
  }
  out.u8(static_cast<uint8_t>(w.team.size()));
  for (const Monster& m : w.team) write_monster(out, m);
  out.u32(w.clock);
  out.u8(w.active < 0 ? 0xff : static_cast<uint8_t>(w.active));
  out.u32(w.next_id);
  out.u8(static_cast<uint8_t>(rules::kPopulationCells));
  for (size_t k = 0; k < kPopulationCellCount; ++k) {
    out.u8(w.population.active[k]);
    out.u16(w.population.visits[k]);
  }
  out.u8(w.teleporters);
  out.u8(static_cast<uint8_t>(kQuestCount));
  for (const QuestState& q : w.quests) {
    out.u8(static_cast<uint8_t>(q.stage));
    out.u8(q.progress);
  }
  out.u8(w.rival_wins);
  for (SpellId sp : w.combat_spells) out.u8(static_cast<uint8_t>(sp));
  for (SpellId sp : w.adventure_spells) out.u8(static_cast<uint8_t>(sp));
  out.u16(w.detect_ticks);
  out.u8(static_cast<uint8_t>(kSpeciesCount));
  for (size_t s = 0; s < kSpeciesCount; ++s) {
    out.u16(w.captured_count[s]);
    out.u16(w.beaten_count[s]);
    out.u16(w.released_count[s]);
  }
  out.u32(w.battles_won);
  out.u8(w.romane_wins);
  out.u8(static_cast<uint8_t>(kItemCount));
  for (uint32_t n : w.bag) out.u32(n);
  out.u16(static_cast<uint16_t>(w.farm.size()));
  for (const Monster& m : w.farm) write_monster(out, m);
}

}  // namespace

std::vector<uint8_t> serialize(const World& w) {
  Writer out;
  // Inside: the world outside as it was left, then where the player is inside.
  if (inside(w)) {
    write_world(out, outside_world(w));
  } else {
    write_world(out, w);
  }
  out.u8(static_cast<uint8_t>(w.place.kind));
  if (inside(w)) {
    out.u32(w.place.door);
    out.vec3(w.player.pos);
    out.f32(w.player.facing_x);
    out.f32(w.player.facing_z);
  }
  return out.take();
}

namespace {

// Reads and checks a save. `whole`: the world is built around what was read
// (its ground, the wilds, what older versions lacked), ready to play; else
// only what the file holds (peek_save).
std::optional<World> read_world(std::span<const uint8_t> bytes, LoadError* error, bool whole) {
  if (error) *error = LoadError::None;
  if (bytes.size() < sizeof kMagic + 4) return fail(error, LoadError::TooShort);
  for (size_t i = 0; i < sizeof kMagic; ++i) {
    if (bytes[i] != kMagic[i]) return fail(error, LoadError::BadMagic);
  }
  Reader in(bytes.subspan(sizeof kMagic));
  uint32_t version = 0;
  in.u32(version);
  if (version < kOldestReadableSaveVersion || version > kSaveVersion) {
    return fail(error, LoadError::UnsupportedVersion);
  }
  const bool v4 = version == 4;
  const bool before_village = version < 6;  // creatures had no flying height
  const bool with_genes = version >= 9;     // creatures' variants, and the bestiary's discoveries
  const bool without_sex = version == 9;   // genes had no sex (it is now the first gene)
  const bool with_team = version >= 11;    // levels of the monsters, and the team
  const bool with_clock = version >= 12;   // time of day
  const bool with_active = version >= 13;  // which team member is out of the crystal
  const bool grown = version >= 14;        // the bigger world: creatures have ids, squares are lived in
  const bool with_quests = version >= 15;  // teleporters, quests, named villagers
  const bool with_rival = version >= 16;   // Bastien, the trainer: battles won against him
  const bool with_spells = version >= 17;  // the magician's spells
  const bool with_builds = version >= 18;  // builds, mana, traits of the team, counts of the bestiary
  const bool with_looks = version >= 19;   // the magician's look, Zapou and Ploufi, the main quest
  const bool with_victories = version >= 20;  // the battles won
  const bool with_new_forest = version >= 21;  // the bigger forest and its clearing
  const bool with_places = version >= 22;      // inside a cave, a house, the tower; Cristi
  const bool with_west = version >= 23;        // the west: its species, quests, places; the bag, the farm
  // Species in the bestiary and the counts: two more from version 19.
  const size_t species_saved = with_west     ? kSpeciesCount
                               : with_places ? kSpeciesCountV22
                               : with_looks  ? kSpeciesCountV21
                                             : kSpeciesCountV18;

  World w;
  uint64_t rng_state = 0, rng_inc = 0;
  uint32_t count = 0;
  if (!(in.u64(w.seed) && in.u64(w.tick) && in.u64(rng_state) && in.u64(rng_inc))) {
    return fail(error, LoadError::TooShort);
  }
  if (in.remaining() < 4 * 4 + 1 + 4 * 2 + 4) return fail(error, LoadError::TooShort);
  if (!read_body(in, w.player, !v4) || !in.u32(count)) return fail(error, LoadError::Corrupt);
  if (count > kMaxObjectsInSave || in.remaining() < size_t{count} * kObjectBytes) {
    return fail(error, LoadError::Corrupt);
  }
  w.rng = Rng::from_raw(rng_state, rng_inc);
  if (whole) build_layouts(w);

  w.objects.resize(count);
  const auto valid_size = [](float v) { return v >= 0 && v <= kMaxObjectSize; };
  for (Object& o : w.objects) {
    uint8_t kind = 0;
    if (!(in.u8(kind) && in.vec3(o.pos) && in.vec3(o.size) && in.u16(o.yaw) && in.u8(o.variant)) ||
        kind >= static_cast<uint8_t>(ObjectKind::Count) || !inside_world(o.pos) ||
        !valid_size(o.size.x) || !valid_size(o.size.y) || !valid_size(o.size.z)) {
      return fail(error, LoadError::Corrupt);
    }
    o.kind = static_cast<ObjectKind>(kind);
    // A teleporter's variant is its TeleporterId (a bit of World::teleporters).
    if (o.kind == ObjectKind::Teleporter && o.variant >= kTeleporterCount) return fail(error, LoadError::Corrupt);
  }

  if (v4 && in.remaining() != 0) return fail(error, LoadError::Corrupt);

  if (!v4) {
    if (!in.u32(count) || count > kMaxCreaturesInSave) return fail(error, LoadError::Corrupt);
    w.creatures.resize(count);
    for (Creature& c : w.creatures) {
      uint8_t species = 0, mind = 0;
      const bool ok = in.u8(species) && read_body(in, c.body, true) && in.u8(mind) && in.u16(c.timer) &&
                      in.f32(c.target.x) && in.f32(c.target.z) && (before_village || in.f32(c.target_y)) &&
                      in.f32(c.home.x) && in.f32(c.home.z) && in.u8(c.variant);
      const auto place = [](float v) { return v >= -kMaxPlaceInSave && v <= kMaxPlaceInSave; };
      if (!ok || species >= species_saved || mind > static_cast<uint8_t>(Mind::KnockedOut) ||
          !place(c.target.x) || !place(c.target.z) || !place(c.target_y) || !place(c.home.x) || !place(c.home.z)) {
        return fail(error, LoadError::Corrupt);
      }
      c.species = static_cast<Species>(species);
      c.mind = static_cast<Mind>(mind);
      if (c.mind == Mind::Battle) c.mind = Mind::Idle;  // battles are not saved
      if (with_genes) {
        uint8_t forms = 0;
        bool genes_ok = true;
        for (size_t g = without_sex ? 1 : 0; g < kMaxGenes; ++g) genes_ok = genes_ok && in.u8(c.genes.allele[g]);
        if (!genes_ok || !in.u8(forms) || !read_forms(forms, with_builds, c.genes)) return fail(error, LoadError::Corrupt);
        c.genes.elemental = c.genes.elemental && !(without_sex && c.genes.chromatic);  // both: allowed before 10
        if (!valid_genes(c.species, c.genes)) return fail(error, LoadError::Corrupt);
      }
      if (with_team && (!in.u8(c.level) || c.level == 0)) return fail(error, LoadError::Corrupt);
      if (grown) {
        uint8_t flags = 0;
        if (!in.u32(c.id) || !in.u8(flags) || flags > (with_quests ? 3 : 1)) return fail(error, LoadError::Corrupt);
        c.released = (flags & 1) != 0;
        c.beaten = (flags & 2) != 0;
      }
      if (with_quests) {
        uint8_t tag = 0;
        const Tag last = with_west ? Tag::Lamb : with_builds ? Tag::Giant : Tag::Rival;
        if (!in.u8(c.npc) || !in.u8(tag) || tag > static_cast<uint8_t>(last)) {
          return fail(error, LoadError::Corrupt);
        }
        c.tag = static_cast<Tag>(tag);
      }
    }
  }

  if (version >= 8) {
    uint8_t name_bytes = 0;
    if (!in.u8(name_bytes) || name_bytes > kMaxNameBytes || !in.text(w.wizard.name, name_bytes) ||
        !in.u16(w.wizard.level) || !in.u32(w.wizard.xp) || w.wizard.level == 0) {
      return fail(error, LoadError::Corrupt);
    }
    if (with_builds && (!in.u16(w.wizard.mana) || w.wizard.mana > wizard_stats(w.wizard).mana)) {
      return fail(error, LoadError::Corrupt);
    }
    if (with_looks) {
      uint8_t v[8] = {};
      for (uint8_t& b : v) {
        if (!in.u8(b)) return fail(error, LoadError::Corrupt);
      }
      WizardLook& l = w.wizard.look;
      l = {v[0] == 1, v[1], v[2], v[3], v[4], v[5], v[6], v[7]};
      if (v[0] > 1 || !valid_look(l)) return fail(error, LoadError::Corrupt);
    }
  }
  if (!with_builds) w.wizard.mana = wizard_stats(w.wizard).mana;
  if (with_genes) {
    uint8_t species_count = 0, variants = 0;
    if (!in.u8(species_count) || !in.u8(variants) || species_count != species_saved || variants != kMaxVariants) {
      return fail(error, LoadError::Corrupt);
    }
    for (size_t s = 0; s < species_saved; ++s) {
      auto& species = w.dex[s];
      // Version 9: the variants of the sex were not listed yet, the others come after them now.
      const size_t shift = without_sex ? genetics(static_cast<Species>(s)).genes[0].count : 0;
      for (size_t i = 0; i < kMaxVariants; ++i) {
        uint8_t v = 0;
        if (!in.u8(v) || v > static_cast<uint8_t>(Discovery::Captured)) return fail(error, LoadError::Corrupt);
        if (i + shift < kMaxVariants) species[i + shift] = static_cast<Discovery>(v);
      }
    }
  }
  const auto read_monster = [&](Monster& m) {
    uint8_t species = 0, forms = 0;
    bool ok = in.u8(species);
    for (uint8_t& a : m.genes.allele) ok = ok && in.u8(a);
    ok = ok && in.u8(forms) && in.u16(m.level) && in.u32(m.xp) && in.u16(m.hp);
    uint8_t tag = 0, beaten = 0;
    if (with_quests) ok = ok && in.u8(tag) && in.u8(beaten) && beaten <= 1;
    // Never a trainer's monster; the giant Champi from version 18.
    ok = ok && (tag <= static_cast<uint8_t>(Tag::LostPiou) || (with_builds && tag == static_cast<uint8_t>(Tag::Giant)));
    m.tag = static_cast<Tag>(tag);
    m.beaten = beaten == 1;
    uint32_t born = 0;
    if (with_builds) {
      for (uint8_t& g : m.gifts) ok = ok && in.u8(g) && g >= kGiftBaseMin && g <= kGiftBaseMax;  // drawn bases
      ok = ok && in.u16(m.size_mm) && in.u32(m.weight_g) && in.u32(born);
      m.born_day = static_cast<int32_t>(born);
    }
    // Before version 18, forms was 0, 1 (chromatic) or 2 (elemental).
    if (!ok || species >= species_saved || (!with_builds && forms > 2) || !read_forms(forms, with_builds, m.genes) ||
        m.level == 0) {
      return false;
    }
    m.species = static_cast<Species>(species);
    return monster_stats(m.species).is_monster && valid_genes(m.species, m.genes) && m.hp <= monster_fight_stats(m).hp;
  };
  if (with_team) {
    uint8_t size = 0;
    if (!in.u8(size) || size == 0 || size > rules::kTeamSize) return fail(error, LoadError::Corrupt);
    w.team.resize(size);
    for (Monster& m : w.team) {
      if (!read_monster(m)) return fail(error, LoadError::Corrupt);
    }
    if ((!grown && w.team[0].species != Species::Companion) || w.creatures.empty()) {
      return fail(error, LoadError::Corrupt);
    }
  }
  if (with_clock && (!in.u32(w.clock) || w.clock >= rules::kTicksPerDay)) return fail(error, LoadError::Corrupt);
  if (with_active) {
    uint8_t active = 0;
    if (!in.u8(active) || (active != 0xff && active >= w.team.size())) return fail(error, LoadError::Corrupt);
    w.active = active == 0xff ? int8_t{-1} : static_cast<int8_t>(active);
    if (w.active >= 0 && w.creatures[0].species != w.team[static_cast<size_t>(w.active)].species) {
      return fail(error, LoadError::Corrupt);
    }
  }
  if (grown) {
    // Before version 18 the world was the valley: 7 squares a side, now in
    // the middle of the grown world's (the valley's edge on a square's edge).
    uint8_t cells = 0;
    constexpr int kOldCells = 7;
    const int offset = static_cast<int>((rules::kWorldHalfSize - rules::kValleyHalfSize) / rules::kPopulationCell);
    if (!in.u32(w.next_id) || !in.u8(cells) || cells != (with_builds ? rules::kPopulationCells : kOldCells)) {
      return fail(error, LoadError::Corrupt);
    }
    for (int k = 0; k < cells * cells; ++k) {
      uint8_t active = 0;
      uint16_t visits = 0;
      if (!in.u8(active) || !in.u16(visits) || active > 1) return fail(error, LoadError::Corrupt);
      const int i = k / cells + (with_builds ? 0 : offset), j = k % cells + (with_builds ? 0 : offset);
      const auto at = static_cast<size_t>(i * rules::kPopulationCells + j);
      w.population.active[at] = active;
      w.population.visits[at] = visits;
    }
    for (const Creature& c : w.creatures) {
      if (c.id == 0 || c.id >= w.next_id) return fail(error, LoadError::Corrupt);
    }
  }
  if (with_quests) {
    uint8_t quests = 0;
    // Before version 18 there were two quests (the giant Champi's came then),
    // before 19 three (fighting, healing and the main quest came then).
    const uint8_t teleporters = with_west ? static_cast<uint8_t>(kTeleporterCount) : kTeleportersV22;
    if (!in.u8(w.teleporters) || w.teleporters >= (1u << teleporters) || !in.u8(quests) ||
        quests != (with_west ? kQuestCount : with_looks ? kQuestCountV22 : with_builds ? 3 : 2)) {
      return fail(error, LoadError::Corrupt);
    }
    for (size_t k = 0; k < quests; ++k) {
      QuestState& q = w.quests[k];
      uint8_t stage = 0;
      if (!in.u8(stage) || stage > static_cast<uint8_t>(QuestStage::Done) || !in.u8(q.progress)) {
        return fail(error, LoadError::Corrupt);
      }
      q.stage = static_cast<QuestStage>(stage);
    }
  }
  if (with_rival && !in.u8(w.rival_wins)) return fail(error, LoadError::Corrupt);
  if (with_spells) {
    const auto read_spells = [&](std::array<SpellId, kSpellSlots>& slots, bool combat) {
      for (SpellId& sp : slots) {
        uint8_t v = 0;
        if (!in.u8(v) || v >= kSpellCount) return false;
        sp = static_cast<SpellId>(v);
        const SpellInfo& info = spell_info(sp);
        if (sp != SpellId::None && !(combat ? info.combat : info.adventure)) return false;
      }
      return true;
    };
    if (!read_spells(w.combat_spells, true) || !read_spells(w.adventure_spells, false)) {
      return fail(error, LoadError::Corrupt);
    }
  }
  if (with_builds) {
    uint8_t species = 0;
    if (!in.u16(w.detect_ticks) || w.detect_ticks > rules::kDetectTicks || !in.u8(species) || species != species_saved) {
      return fail(error, LoadError::Corrupt);
    }
    for (size_t s = 0; s < species_saved; ++s) {
      if (!in.u16(w.captured_count[s]) || !in.u16(w.beaten_count[s]) || !in.u16(w.released_count[s])) {
        return fail(error, LoadError::Corrupt);
      }
    }
  }
  if (with_victories && !in.u32(w.battles_won)) return fail(error, LoadError::Corrupt);
  if (with_west) {
    uint8_t items = 0;
    uint16_t kept = 0;
    if (!in.u8(w.romane_wins) || !in.u8(items) || items != kItemCount) return fail(error, LoadError::Corrupt);
    for (uint32_t& n : w.bag) {
      if (!in.u32(n)) return fail(error, LoadError::Corrupt);
    }
    if (!in.u16(kept) || kept > rules::kMaxFarm) return fail(error, LoadError::Corrupt);
    w.farm.resize(kept);
    for (Monster& m : w.farm) {
      if (!read_monster(m)) return fail(error, LoadError::Corrupt);
    }
  }
  // Where the player is: inside a cave, a house, the tower (version 22).
  uint8_t place = 0;
  Body inner;
  if (with_places) {
    if (!in.u8(place) || place >= (with_west ? static_cast<uint8_t>(PlaceKind::Count) : kPlacesV22)) {
      return fail(error, LoadError::Corrupt);
    }
    if (place != 0) {
      const bool ok = in.u32(w.place.door) && in.vec3(inner.pos) && in.f32(inner.facing_x) && in.f32(inner.facing_z);
      if (!ok || !std::isfinite(inner.pos.x) || !std::isfinite(inner.pos.y) || !std::isfinite(inner.pos.z) ||
          !std::isfinite(inner.facing_x) || !std::isfinite(inner.facing_z)) {
        return fail(error, LoadError::Corrupt);
      }
    }
  }
  if (in.remaining() != 0) return fail(error, LoadError::Corrupt);
  if (!whole) return w;

  if (!grown) {
    // Saves from the smaller worlds: the world is built anew from the seed
    // (it grew: the path, the places and everything along them moved). The
    // player starts again at the crossroads, with all it gained: its
    // magician, its team, its bestiary, the time of day.
    World bigger = new_game(w.seed);
    bigger.tick = w.tick;
    bigger.rng = w.rng;
    bigger.clock = w.clock;
    if (version >= 8) bigger.wizard = w.wizard;
    if (with_genes) {
      bigger.dex = w.dex;
      if (without_sex) capture(bigger, 0);  // the companion's sex was not listed yet
    }
    if (with_team) {
      bigger.team = w.team;
      bigger.active = w.active;
      if (w.active >= 0) {
        Creature& c = bigger.creatures[0];
        const Monster& m = w.team[static_cast<size_t>(w.active)];
        c.species = m.species;
        c.genes = m.genes;
        c.level = static_cast<uint8_t>(std::min<uint16_t>(m.level, 255));
      }
    } else if (!w.creatures.empty()) {
      bigger.team = {starting_companion(w.creatures[0].genes)};
      bigger.creatures[0].genes = w.creatures[0].genes;
    }
    w = std::move(bigger);
  } else if (!with_quests) {
    // Version 14: the teleporters are built (trees and rocks around them give
    // way), the villagers get their names, Gaspard goes to the village's teleporter.
    build_teleporters(w);
    uint8_t n = 0;
    for (Creature& c : w.creatures) {
      if (c.species != Species::Villager || n >= rules::kVillagers) continue;
      c.npc = ++n;
      Vec3 t;
      if (c.npc == npc::kGaspard && teleporter_position(w, TeleporterId::Village, t)) {
        c.home = {t.x, t.z};
        c.body.pos = {t.x + 2.4f, w.ground.height(t.x + 2.4f, t.z), t.z};
        c.mind = Mind::Idle;
      }
    }
  }
  if (grown && !with_rival) {
    // Before version 16: Bastien goes to the edge of the village, where he
    // offers battles (his monster comes to his side, see update_population).
    for (Creature& c : w.creatures) {
      if (c.species == Species::Villager && c.npc == npc::kBastien) {
        place_trainer(w, c);
        c.mind = Mind::Idle;
      }
    }
  }
  // The teleporters stand on the ground. Versions 14 patched by an earlier
  // build left them at height 0, under the hills (trees making way for them
  // moved them in the list, and they were missed): put them back up.
  for (Object& o : w.objects) {
    if (o.kind == ObjectKind::Teleporter && !w.ground.empty()) o.pos.y = w.ground.height(o.pos.x, o.pos.z) - 0.05f;
  }
  // The wilds around the valley (not saved). Before version 18 the relief at
  // the valley's edge rose into hills: it changed, the objects there settle again.
  if (grown && !with_builds) settle_objects(w);
  // Before version 21: a smaller forest, no clearing, its teleporter in its middle.
  if (!with_new_forest) regrow_forest(w);
  generate_wilds(w);
  // The west's villagers (before version 23: they come).
  add_west_npcs(w);
  // Before version 19: Isidore comes to the village (and Bastien's lesson is
  // learned if he was beaten already: told to him, it is done).
  if (!with_looks) add_mentor(w);
  // Before version 20 the victories were not counted: at least the beaten monsters' and Bastien's.
  if (!with_victories) {
    for (const uint16_t n : w.beaten_count) w.battles_won += n > 0 ? 1 : 0;
    w.battles_won += w.rival_wins;
  }
  // Saved inside: in again, where the player stood (if it is a place to stand).
  if (place != 0) {
    const Place where{static_cast<PlaceKind>(place), w.place.door};
    w.place = {};
    if (enter_place(w, where)) {
      const Interior& room = w.interior;
      const Vec3 p = inner.pos;
      const bool fits = p.x > room.bounds.min.x && p.x < room.bounds.max.x && p.z > room.bounds.min.z &&
                        p.z < room.bounds.max.z && p.y >= 0 && p.y < room.bounds.max.y &&
                        wall_distance(room, p.x, p.z) > 0.3f && open_distance(room, p + Vec3{0, 0.5f, 0}) > 0.2f;
      if (fits) {
        w.player.pos = p;
        const float l = std::sqrt(inner.facing_x * inner.facing_x + inner.facing_z * inner.facing_z);
        if (l > 1e-3f) {
          w.player.facing_x = inner.facing_x / l;
          w.player.facing_z = inner.facing_z / l;
        }
        w.player.on_ground = false;
        if (!w.creatures.empty()) w.creatures[0].body.pos = p;
      }
    }
  }
  if (!with_builds) {
    // Before version 18: the team members get their traits (as if they
    // joined today), the captures are counted from the team.
    w.captured_count = {};
    for (size_t k = 0; k < w.team.size(); ++k) {
      Monster& m = w.team[k];
      give_traits(m, roll_traits(w.seed, 0xFFFF0000u + static_cast<uint32_t>(k), m.species, m.genes), world_day(w));
      if (m.species != Species::Companion) ++w.captured_count[static_cast<size_t>(m.species)];
    }
  }
  return w;
}

}  // namespace

std::optional<World> deserialize(std::span<const uint8_t> bytes, LoadError* error) {
  return read_world(bytes, error, true);
}

std::optional<SaveSummary> peek_save(std::span<const uint8_t> bytes, LoadError* error) {
  const auto w = read_world(bytes, error, false);
  if (!w) return std::nullopt;
  return SaveSummary{w->wizard.name, w->wizard.level, w->seed, w->tick};
}

uint64_t state_hash(const World& w) {
  uint64_t h = 0xcbf29ce484222325ULL;
  for (uint8_t b : serialize(w)) {
    h ^= b;
    h *= 0x100000001b3ULL;
  }
  return h;
}

}  // namespace monstres
