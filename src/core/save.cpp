#include "monstres/core/save.hpp"

#include <bit>
#include <cmath>

namespace monstres {

namespace {

constexpr uint8_t kMagic[4] = {'M', 'N', 'S', 'T'};
constexpr uint32_t kMaxObjectsInSave = 100000;
constexpr size_t kObjectBytes = 7 * 4;

class Writer {
public:
  void u8(uint8_t v) { out_.push_back(v); }
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
  return p.x >= -limit && p.x <= limit && p.z >= -limit && p.z <= limit && p.y >= 0 && p.y < 1000;
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

std::vector<uint8_t> serialize(const World& w) {
  Writer out;
  out.bytes(kMagic, sizeof kMagic);
  out.u32(kSaveVersion);
  out.u64(w.tick);
  out.u64(w.rng.raw_state());
  out.u64(w.rng.raw_inc());
  out.vec3(w.player.pos);
  out.f32(w.player.velocity_y);
  out.u8(w.player.on_ground ? 1 : 0);
  out.f32(w.player.facing_x);
  out.f32(w.player.facing_z);
  out.u32(static_cast<uint32_t>(w.objects.size()));
  for (const Object& o : w.objects) {
    out.vec3(o.center);
    out.vec3(o.half);
    out.u32(o.color);
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
  if (version != kSaveVersion) return fail(error, LoadError::UnsupportedVersion);

  World w;
  uint64_t rng_state = 0, rng_inc = 0;
  uint8_t on_ground = 0;
  uint32_t count = 0;
  if (!(in.u64(w.tick) && in.u64(rng_state) && in.u64(rng_inc))) {
    return fail(error, LoadError::TooShort);
  }
  if (in.remaining() < 4 * 4 + 1 + 4 * 2 + 4) return fail(error, LoadError::TooShort);
  const bool ok = in.vec3(w.player.pos) && in.f32(w.player.velocity_y) && in.u8(on_ground) &&
                  in.f32(w.player.facing_x) && in.f32(w.player.facing_z) && in.u32(count);
  if (!ok || on_ground > 1) return fail(error, LoadError::Corrupt);
  if (count > kMaxObjectsInSave || in.remaining() != size_t{count} * kObjectBytes) {
    return fail(error, LoadError::Corrupt);
  }
  w.rng = Rng::from_raw(rng_state, rng_inc);
  w.player.on_ground = on_ground == 1;
  if (!inside_world(w.player.pos)) return fail(error, LoadError::Corrupt);

  w.objects.resize(count);
  for (Object& o : w.objects) {
    uint32_t color = 0;
    if (!(in.vec3(o.center) && in.vec3(o.half) && in.u32(color)) || color > 255 ||
        !inside_world(o.center) || o.half.x <= 0 || o.half.y <= 0 || o.half.z <= 0) {
      return fail(error, LoadError::Corrupt);
    }
    o.color = static_cast<uint8_t>(color);
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
