#include "monstres/core/save.hpp"

#include "monstres/core/battle.hpp"
#include "monstres/core/variants.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

namespace monstres {

namespace {

constexpr uint8_t kMagic[4] = {'M', 'N', 'S', 'T'};
constexpr uint32_t kMaxObjectsInSave = 100000;
constexpr size_t kObjectBytes = 1 + 3 * 4 + 3 * 4 + 2 + 1;
constexpr float kMaxObjectSize = 20.0f;

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
  return ok && on_ground <= 1 && inside_world(b.pos);
}

}  // namespace

std::vector<uint8_t> serialize(const World& w) {
  Writer out;
  out.bytes(kMagic, sizeof kMagic);
  out.u32(kSaveVersion);
  out.u64(w.seed);
  out.u64(w.tick);
  out.u64(w.rng.raw_state());
  out.u64(w.rng.raw_inc());
  write_body(out, w.player);
  out.u32(static_cast<uint32_t>(w.objects.size()));
  for (const Object& o : w.objects) {
    out.u8(static_cast<uint8_t>(o.kind));
    out.vec3(o.pos);
    out.vec3(o.size);
    out.u16(o.yaw);
    out.u8(o.variant);
  }
  out.u32(static_cast<uint32_t>(w.creatures.size()));
  for (const Creature& c : w.creatures) {
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
    out.u8(static_cast<uint8_t>((c.genes.chromatic ? 1 : 0) | (c.genes.elemental ? 2 : 0)));
    out.u8(c.level);
  }
  const size_t name_bytes = std::min(w.wizard.name.size(), kMaxNameBytes);
  out.u8(static_cast<uint8_t>(name_bytes));
  out.bytes(reinterpret_cast<const uint8_t*>(w.wizard.name.data()), name_bytes);
  out.u16(w.wizard.level);
  out.u32(w.wizard.xp);
  out.u8(static_cast<uint8_t>(kSpeciesCount));
  out.u8(static_cast<uint8_t>(kMaxVariants));
  for (const auto& species : w.dex) {
    for (const Discovery d : species) out.u8(static_cast<uint8_t>(d));
  }
  out.u8(static_cast<uint8_t>(w.team.size()));
  for (const Monster& m : w.team) {
    out.u8(static_cast<uint8_t>(m.species));
    for (uint8_t a : m.genes.allele) out.u8(a);
    out.u8(static_cast<uint8_t>((m.genes.chromatic ? 1 : 0) | (m.genes.elemental ? 2 : 0)));
    out.u16(m.level);
    out.u32(m.xp);
    out.u16(m.hp);
  }
  out.u32(w.clock);
  return out.take();
}

std::optional<World> deserialize(std::span<const uint8_t> bytes, LoadError* error) {
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
  w.path = make_path(w.seed);
  w.village = make_village_layout(w.seed, w.path);
  w.ruins = make_ruins_layout(w.seed, w.path);
  w.ground = make_heightmap(w.seed, w.path, w.village, w.ruins);

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
      if (!ok || species >= static_cast<uint8_t>(Species::Count) || mind > static_cast<uint8_t>(Mind::Battle)) {
        return fail(error, LoadError::Corrupt);
      }
      c.species = static_cast<Species>(species);
      c.mind = static_cast<Mind>(mind);
      if (c.mind == Mind::Battle) c.mind = Mind::Idle;  // battles are not saved
      if (with_genes) {
        uint8_t forms = 0;
        bool genes_ok = true;
        for (size_t g = without_sex ? 1 : 0; g < kMaxGenes; ++g) genes_ok = genes_ok && in.u8(c.genes.allele[g]);
        if (!genes_ok || !in.u8(forms) || forms > 3) return fail(error, LoadError::Corrupt);
        c.genes.chromatic = (forms & 1) != 0;
        c.genes.elemental = (forms & 2) != 0 && !(without_sex && c.genes.chromatic);  // both: allowed before 10
        if (!valid_genes(c.species, c.genes)) return fail(error, LoadError::Corrupt);
      }
      if (with_team && (!in.u8(c.level) || c.level == 0)) return fail(error, LoadError::Corrupt);
    }
  }

  if (version >= 8) {
    uint8_t name_bytes = 0;
    if (!in.u8(name_bytes) || name_bytes > kMaxNameBytes || !in.text(w.wizard.name, name_bytes) ||
        !in.u16(w.wizard.level) || !in.u32(w.wizard.xp) || w.wizard.level == 0) {
      return fail(error, LoadError::Corrupt);
    }
  }
  if (with_genes) {
    uint8_t species_count = 0, variants = 0;
    if (!in.u8(species_count) || !in.u8(variants) || species_count != kSpeciesCount || variants != kMaxVariants) {
      return fail(error, LoadError::Corrupt);
    }
    for (size_t s = 0; s < kSpeciesCount; ++s) {
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
  if (with_team) {
    uint8_t size = 0;
    if (!in.u8(size) || size == 0 || size > rules::kTeamSize) return fail(error, LoadError::Corrupt);
    w.team.resize(size);
    for (Monster& m : w.team) {
      uint8_t species = 0, forms = 0;
      bool ok = in.u8(species);
      for (uint8_t& a : m.genes.allele) ok = ok && in.u8(a);
      ok = ok && in.u8(forms) && in.u16(m.level) && in.u32(m.xp) && in.u16(m.hp);
      if (!ok || species >= static_cast<uint8_t>(Species::Count) || forms > 2 || m.level == 0) {
        return fail(error, LoadError::Corrupt);
      }
      m.species = static_cast<Species>(species);
      m.genes.chromatic = forms == 1;
      m.genes.elemental = forms == 2;
      if (!monster_stats(m.species).is_monster || !valid_genes(m.species, m.genes) ||
          m.hp > monster_fight_stats(m.species, m.level).hp) {
        return fail(error, LoadError::Corrupt);
      }
    }
    if (w.team[0].species != Species::Companion || w.creatures.empty()) return fail(error, LoadError::Corrupt);
  }
  if (with_clock && (!in.u32(w.clock) || w.clock >= rules::kTicksPerDay)) return fail(error, LoadError::Corrupt);
  if (in.remaining() != 0) return fail(error, LoadError::Corrupt);

  if (version < 8) {
    // Saves from the smaller world: the world is built anew from the seed
    // (it grew, the path and everything along it moved). The player keeps
    // its place if it is free, the time goes on.
    World grown = new_game(w.seed);
    grown.tick = w.tick;
    grown.rng = w.rng;
    Body p = w.player;
    p.pos.y = std::max(p.pos.y, grown.ground.height(p.pos.x, p.pos.z));
    p.on_ground = false;  // falls onto the ground if it is now lower
    const Box pb = player_box(p);
    const bool stuck = in_lake(grown.village.lake, p.pos.x, p.pos.z, 0.5f) ||
                       std::any_of(grown.objects.begin(), grown.objects.end(),
                                   [&](const Object& o) { return overlaps(pb, object_box(o)); });
    if (!stuck) grown.player = p;
    w = std::move(grown);
  } else if (without_sex) {
    // Every monster of a version 9 save gets a sex, from the seed.
    for (size_t i = 1; i < w.creatures.size(); ++i) {
      Creature& c = w.creatures[i];
      if (genetics(c.species).gene_count == 0) continue;
      Rng rng(w.seed ^ 0x534558ULL, i);  // "SEX"
      c.genes.allele[0] = static_cast<uint8_t>(rng.below(2));
    }
    capture(w, 0);
  } else if (!with_genes) {
    // Saves from before variants: the monsters get theirs, nothing is discovered yet.
    assign_genes(w);
    capture(w, 0);
  }
  if (!with_team && version >= 8) {
    // Saves from before battles: the monsters get a level, the companion
    // starts the team.
    assign_levels(w);
    w.team = {starting_companion(w.creatures.empty() ? Genes{} : w.creatures[0].genes)};
  }
  return w;
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
