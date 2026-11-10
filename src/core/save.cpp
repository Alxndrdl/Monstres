#include "monstres/core/save.hpp"

namespace monstres {

namespace {

constexpr uint8_t kMagic[4] = {'M', 'N', 'S', 'T'};
constexpr uint32_t kMaxMonstersInSave = 4096;

class Writer {
public:
  void u32(uint32_t v) {
    for (int i = 0; i < 4; ++i) out_.push_back(static_cast<uint8_t>(v >> (8 * i)));
  }
  void u64(uint64_t v) {
    for (int i = 0; i < 8; ++i) out_.push_back(static_cast<uint8_t>(v >> (8 * i)));
  }
  void i32(int32_t v) { u32(static_cast<uint32_t>(v)); }
  void bytes(const uint8_t* p, size_t n) { out_.insert(out_.end(), p, p + n); }
  std::vector<uint8_t> take() { return std::move(out_); }

private:
  std::vector<uint8_t> out_;
};

class Reader {
public:
  explicit Reader(std::span<const uint8_t> in) : in_(in) {}

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
  bool i32(int32_t& v) {
    uint32_t u = 0;
    if (!u32(u)) return false;
    v = static_cast<int32_t>(u);
    return true;
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

}  // namespace

std::vector<uint8_t> serialize(const World& w) {
  Writer out;
  out.bytes(kMagic, sizeof kMagic);
  out.u32(kSaveVersion);
  out.u64(w.tick);
  out.u64(w.rng.raw_state());
  out.u64(w.rng.raw_inc());
  out.i32(w.player.pos.x);
  out.i32(w.player.pos.y);
  out.i32(w.player.hp);
  out.u32(w.player.score);
  out.u32(w.next_monster_id);
  out.u32(static_cast<uint32_t>(w.monsters.size()));
  for (const Monster& m : w.monsters) {
    out.u32(m.id);
    out.i32(m.pos.x);
    out.i32(m.pos.y);
    out.i32(m.hp);
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
  uint32_t count = 0;
  const bool ok = in.u64(w.tick) && in.u64(rng_state) && in.u64(rng_inc) &&
                  in.i32(w.player.pos.x) && in.i32(w.player.pos.y) && in.i32(w.player.hp) &&
                  in.u32(w.player.score) && in.u32(w.next_monster_id) && in.u32(count);
  if (!ok) return fail(error, LoadError::TooShort);
  if (count > kMaxMonstersInSave || in.remaining() != size_t{count} * 16) {
    return fail(error, LoadError::Corrupt);
  }
  w.rng = Rng::from_raw(rng_state, rng_inc);
  w.monsters.resize(count);
  for (Monster& m : w.monsters) {
    in.u32(m.id);
    in.i32(m.pos.x);
    in.i32(m.pos.y);
    in.i32(m.hp);
    if (!inside(m.pos)) return fail(error, LoadError::Corrupt);
  }
  if (!inside(w.player.pos)) return fail(error, LoadError::Corrupt);
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
