#include "monstres/core/save.hpp"

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
  return p.x >= -limit && p.x <= limit && p.z >= -limit && p.z <= limit && p.y >= -5 && p.y < 1000;  // fish swim below 0
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
  }
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
  const bool before_village = version < 6;

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
      if (!ok || species >= static_cast<uint8_t>(Species::Count) || mind > static_cast<uint8_t>(Mind::Fly)) {
        return fail(error, LoadError::Corrupt);
      }
      c.species = static_cast<Species>(species);
      c.mind = static_cast<Mind>(mind);
    }
    if (in.remaining() != 0) return fail(error, LoadError::Corrupt);
  }

  if (before_village) {
    // Saves from before the village: build it, and bring everyone anew.
    build_village(w);
    spawn_creatures(w);
    const Box pb = player_box(w.player);
    const bool stuck = in_lake(w.village.lake, w.player.pos.x, w.player.pos.z, 0.5f) ||
                       std::any_of(w.objects.begin(), w.objects.end(),
                                   [&](const Object& o) { return overlaps(pb, object_box(o)); });
    if (stuck) w.player = Body{};  // back to the start
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
