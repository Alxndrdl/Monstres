#include "monstres/audio/sounds.hpp"

#include "monstres/core/battle.hpp"
#include "monstres/core/debug.hpp"
#include "monstres/core/interior.hpp"
#include "monstres/core/routines.hpp"
#include "monstres/core/west.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>

namespace monstres {

const char* sound_name(Sound s) {
  static const char* kNames[] = {"cry",     "swing",       "hit",       "parry",    "impact",   "spell",   "summon",
                                 "recall",  "teleport",    "door",      "rescue",   "heal",     "battleStart",
                                 "victory", "defeat",      "fled",      "runeRight", "runeWrong", "caught", "escaped",
                                 "questAccepted", "questStep", "questDone", "item",  "levelUp",  "thunder"};
  static_assert(std::size(kNames) == static_cast<size_t>(Sound::Count));
  const auto i = static_cast<size_t>(s);
  return i < std::size(kNames) ? kNames[i] : "?";
}

const char* cry_name(Cry c) {
  static const char* kNames[] = {"call", "attack", "hurt"};
  static_assert(std::size(kNames) == static_cast<size_t>(Cry::Count));
  const auto i = static_cast<size_t>(c);
  return i < std::size(kNames) ? kNames[i] : "?";
}

const char* source_name(SourceKind k) {
  static const char* kNames[] = {"waterfall", "river", "waterWheel", "windmill", "fire"};
  static_assert(std::size(kNames) == static_cast<size_t>(SourceKind::Count));
  const auto i = static_cast<size_t>(k);
  return i < std::size(kNames) ? kNames[i] : "?";
}

namespace {

float smooth(float a, float b, float x) {
  const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
  return t * t * (3 - 2 * t);
}

float flat_distance2(Vec3 a, Vec3 b) { return (a.x - b.x) * (a.x - b.x) + (a.z - b.z) * (a.z - b.z); }

uint64_t next_random(uint64_t& state) {  // xorshift64*
  state ^= state >> 12;
  state ^= state << 25;
  state ^= state >> 27;
  return state * 0x2545F4914F6CDD1DULL;
}
float random01(uint64_t& state) { return static_cast<float>(next_random(state) >> 40) / 16777216.0f; }

bool fighting(BattlePhase p) { return p == BattlePhase::Fight || p == BattlePhase::Training || p == BattlePhase::Prepare; }

SoundEvent placed(Sound s, Vec3 at) {
  SoundEvent e;
  e.sound = s;
  e.pos = at;
  e.placed = true;
  return e;
}
SoundEvent unplaced(Sound s) {
  SoundEvent e;
  e.sound = s;
  return e;
}
SoundEvent cry(Species species, Cry c, Vec3 at, uint8_t variant = 0) {
  SoundEvent e = placed(Sound::Cry, at);
  e.species = species;
  e.cry = c;
  e.variant = variant;
  return e;
}

// The cry comes from the head, roughly.
Vec3 voice_of(const Creature& c) { return {c.body.pos.x, c.body.pos.y + 0.4f, c.body.pos.z}; }

}  // namespace

Ambience ambience_of(const World& w, Season season, const Weather& here, float wind) {
  Ambience a;
  switch (w.place.kind) {
    case PlaceKind::Outside: a.place = SoundPlace::Outside; break;
    case PlaceKind::Cave:
    case PlaceKind::Mine: a.place = SoundPlace::Cave; break;
    case PlaceKind::Burrow: a.place = SoundPlace::Burrow; break;
    default: a.place = SoundPlace::Building; break;
  }
  a.season = season;
  const float h = hour_of_day(w);
  a.night = 1.0f - smooth(5.0f, 7.0f, h) * (1.0f - smooth(19.5f, 21.5f, h));
  if (a.place == SoundPlace::Outside) {
    a.wind = std::clamp(wind, 0.0f, 1.0f);
    a.rain = std::clamp(here.rain, 0.0f, 1.0f);
    a.snow = std::clamp(here.snow, 0.0f, 1.0f);
    a.storm = std::clamp(here.storm, 0.0f, 1.0f);
  }
  a.battle = fighting(w.battle.phase);
  a.capture = sealing(w);
  return a;
}

void sound_sources(const World& w, Vec3 listener, std::vector<SoundSource>& out) {
  const float reach2 = kSourceReach * kSourceReach;
  const auto offer = [&](SourceKind k, Vec3 at) {
    if (flat_distance2(at, listener) < reach2) out.push_back({k, at});
  };
  for (const Object& o : w.objects) {
    if (o.kind == ObjectKind::Campfire || o.kind == ObjectKind::Hearth) offer(SourceKind::Fire, {o.pos.x, o.pos.y + 0.4f, o.pos.z});
  }
  if (inside(w) || !w.west.valid()) return;
  const Stream& s = w.west.stream;
  if (s.falls > 0 && s.falls < s.points.size()) {
    const Vec2 f = s.points[s.falls];
    offer(SourceKind::Waterfall, {f.x, 0.5f * (s.level[s.falls] + w.west.cove_level), f.z});
  }
  // The river: its nearest point (the waterfall's own sound covers it there).
  if (distance_to_stream(s, listener.x, listener.z, kSourceReach) < kSourceReach) {
    size_t best = 0;
    float best2 = 1e18f;
    for (size_t k = 0; k < s.points.size(); ++k) {
      const float d2 = (s.points[k].x - listener.x) * (s.points[k].x - listener.x) +
                       (s.points[k].z - listener.z) * (s.points[k].z - listener.z);
      if (d2 < best2) {
        best2 = d2;
        best = k;
      }
    }
    if (!s.points.empty()) offer(SourceKind::River, {s.points[best].x, s.level[best], s.points[best].z});
  }
  for (const Object& o : w.objects) {
    if (o.kind == ObjectKind::WaterMill && wheel_turning(w)) offer(SourceKind::WaterWheel, {o.pos.x, o.pos.y + 1.5f, o.pos.z});
    if (o.kind == ObjectKind::Windmill && mill_turning(w)) offer(SourceKind::Windmill, {o.pos.x, o.pos.y + 6.0f, o.pos.z});
  }
}

void SoundWatch::note(const World& w, const WeatherField* field) {
  known_ = true;
  seed_ = w.seed;
  tick_ = w.tick;
  spell_ = w.spell_tick;
  teleport_ = w.teleport_tick;
  door_ = w.door_tick;
  rescue_ = w.rescue_tick;
  healed_ = w.healed_tick;
  came_out_ = w.came_out_tick;
  went_in_ = w.went_in.tick;
  outcome_ = w.battle.outcome_tick;
  impact_ = 0;
  for (const Impact& i : w.battle.impacts) impact_ = std::max(impact_, i.tick);
  touch_ = w.battle.seal.touch_tick;
  phase_ = w.battle.phase;
  seal_ = w.battle.seal.stage;
  const Fighter& a = w.battle.ally;
  ally_ = {0, a.attack_tick, a.hit_tick, a.parried_tick};
  foes_.clear();
  for (const Foe& f : w.battle.foes) {
    const uint32_t id = f.index < w.creatures.size() ? w.creatures[f.index].id : 0;
    foes_.push_back({id, f.fighter.attack_tick, f.fighter.hit_tick, f.fighter.parried_tick});
  }
  quests_ = w.quests;
  bag_ = w.bag;
  wizard_level_ = w.wizard.level;
  team_levels_.clear();
  for (const Monster& m : w.team) team_levels_.push_back(m.level);
  bolt_ = 0;
  if (field && !inside(w)) {
    const Lightning l = lightning_at(*field, w.player.pos.x, w.player.pos.z);
    if (l.on) bolt_ = l.id;
  }
}

void SoundWatch::watch(const World& w, const WeatherField* field, double seconds, std::vector<SoundEvent>& out) {
  // Another world (a save loaded, a new game), or time went back: take note only.
  if (!known_ || w.seed != seed_ || w.tick < tick_) {
    rng_ ^= w.seed + 0x5851F42D4C957F2DULL;
    note(w, field);
    return;
  }
  const Vec3 player = w.player.pos;
  const Vec3 at_player{player.x, player.y + 1.0f, player.z};
  const Battle& b = w.battle;
  const Creature* companion = w.creatures.empty() ? nullptr : &w.creatures[0];
  const bool out_now = w.active >= 0 && companion && static_cast<size_t>(w.active) < w.team.size();
  const Species ally_species = out_now ? w.team[static_cast<size_t>(w.active)].species : Species::Companion;
  const Vec3 ally_at = companion ? voice_of(*companion) : at_player;

  // The magician.
  if (w.spell_tick > spell_) {
    SoundEvent e = placed(Sound::Spell, at_player);
    e.spell = w.spell_cast;
    out.push_back(e);
  }
  if (w.teleport_tick > teleport_) out.push_back(placed(Sound::Teleport, at_player));
  if (w.door_tick > door_) out.push_back(placed(Sound::Door, at_player));
  if (w.rescue_tick > rescue_) out.push_back(unplaced(Sound::Rescue));
  if (w.healed_tick > healed_) out.push_back(placed(Sound::Heal, at_player));
  // The crystal: out (its call), in.
  if (w.came_out_tick > came_out_ && out_now) {
    out.push_back(placed(Sound::Summon, ally_at));
    out.push_back(cry(ally_species, Cry::Call, ally_at, static_cast<uint8_t>(next_random(rng_) % kCallVariants)));
  }
  if (w.went_in.tick > went_in_ && !w.went_in.captured) out.push_back(placed(Sound::Recall, at_player));

  // Battles: their start and end, the attacks, the blows.
  if (fighting(b.phase) && !fighting(phase_)) out.push_back(unplaced(Sound::BattleStart));
  if (b.outcome_tick > outcome_) {
    switch (b.outcome) {
      case BattleOutcome::Victory: out.push_back(unplaced(Sound::Victory)); break;
      case BattleOutcome::Defeat:
      case BattleOutcome::Forfeit: out.push_back(unplaced(Sound::Defeat)); break;
      case BattleOutcome::Fled: out.push_back(unplaced(Sound::Fled)); break;
      default: break;  // Captured, Escaped: the seal tells it
    }
  }
  const auto fighter_sounds = [&](const Fighter& f, const FighterMarks& before, Species species, Vec3 at, uint16_t max_hp) {
    if (f.attack_tick > before.attack) {
      out.push_back(cry(species, Cry::Attack, at));
      SoundEvent e = placed(Sound::Swing, at);
      e.variant = f.attack_move;
      out.push_back(e);
    }
    if (f.hit_tick > before.hit) {
      out.push_back(cry(species, Cry::Hurt, at));
      SoundEvent e = placed(Sound::Hit, at);
      e.strength = max_hp > 0 ? std::clamp(static_cast<float>(f.last_damage) / static_cast<float>(max_hp) * 3.0f, 0.2f, 1.0f) : 0.5f;
      out.push_back(e);
    }
    if (f.parried_tick > before.parried) out.push_back(placed(Sound::Parry, at));
  };
  if (b.phase != BattlePhase::None && out_now) {
    const Monster& m = w.team[static_cast<size_t>(w.active)];
    fighter_sounds(b.ally, ally_, ally_species, ally_at, monster_fight_stats(m).hp);
  }
  for (const Foe& f : b.foes) {
    if (f.index >= w.creatures.size()) continue;
    const Creature& c = w.creatures[f.index];
    FighterMarks before{c.id, f.fighter.attack_tick, f.fighter.hit_tick, f.fighter.parried_tick};  // new: nothing yet
    for (const FighterMarks& m : foes_) {
      if (m.id == c.id) before = m;
    }
    fighter_sounds(f.fighter, before, c.species, voice_of(c), f.max_hp);
  }
  for (const Impact& i : b.impacts) {
    if (i.tick <= impact_) continue;
    SoundEvent e = placed(Sound::Impact, i.pos);
    e.element = i.element;
    out.push_back(e);
  }

  // Capturing: the runes touched, caught or broken free.
  const Seal& s = b.seal;
  if (s.touch_tick > touch_) {
    SoundEvent e = unplaced(s.stage == SealStage::Wrong ? Sound::RuneWrong : Sound::RuneRight);
    e.variant = s.touched;
    out.push_back(e);
  }
  if (s.stage != seal_) {
    if (s.stage == SealStage::Caught) out.push_back(unplaced(Sound::Caught));
    if (s.stage == SealStage::Escaped) out.push_back(unplaced(Sound::Escaped));
  }

  // Quests, the bag, levels.
  for (size_t q = 0; q < kQuestCount; ++q) {
    const QuestState& now = w.quests[q];
    const QuestState& was = quests_[q];
    if (now.stage == was.stage && now.progress != was.progress && now.stage == QuestStage::Active) {
      out.push_back(unplaced(Sound::QuestStep));
    } else if (now.stage != was.stage) {
      if (now.stage == QuestStage::Active) out.push_back(unplaced(Sound::QuestAccepted));
      if (now.stage == QuestStage::Done) out.push_back(unplaced(Sound::QuestDone));
    }
  }
  for (size_t i = 0; i < kItemCount; ++i) {
    if (w.bag[i] <= bag_[i]) continue;
    SoundEvent e = unplaced(Sound::Item);
    e.variant = static_cast<uint8_t>(i);
    out.push_back(e);
  }
  bool level_up = w.wizard.level > wizard_level_;
  if (w.team.size() == team_levels_.size()) {
    for (size_t i = 0; i < w.team.size(); ++i) level_up = level_up || w.team[i].level > team_levels_[i];
  }
  if (level_up) out.push_back(unplaced(Sound::LevelUp));

  // Thunder: a new bolt (audio.js waits for the sound to travel).
  uint32_t bolt = 0;
  if (field && !inside(w)) {
    const Lightning l = lightning_at(*field, player.x, player.z);
    if (l.on) {
      bolt = l.id;
      if (l.id != bolt_) {
        SoundEvent e = placed(Sound::Thunder, {l.x, player.y + 60.0f, l.z});
        const float d = std::sqrt(flat_distance2({l.x, 0, l.z}, player));
        e.strength = std::clamp(1.0f - d / 2500.0f, 0.1f, 1.0f);
        out.push_back(e);
      }
    }
  }

  // The wild monsters around call now and then (not during a battle).
  next_call_ -= seconds;
  if (next_call_ <= 0) {
    next_call_ = 2.5 + 6.0 * random01(rng_);
    if (b.phase == BattlePhase::None && s.stage == SealStage::None) {
      const Creature* caller = nullptr;
      int seen = 0;
      for (size_t i = 1; i < w.creatures.size(); ++i) {
        const Creature& c = w.creatures[i];
        if (!monster_stats(c.species).is_monster || c.mind == Mind::KnockedOut || asleep(c) || !about(c)) continue;
        if (flat_distance2(c.body.pos, player) > 40.0f * 40.0f) continue;
        if (next_random(rng_) % static_cast<uint64_t>(++seen) == 0) caller = &c;  // one of them, at random
      }
      if (caller) out.push_back(cry(caller->species, Cry::Call, voice_of(*caller), static_cast<uint8_t>(next_random(rng_) % kCallVariants)));
    }
  }

  note(w, field);
  bolt_ = bolt;
}

namespace {
std::string num(float v) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.2f", static_cast<double>(std::isfinite(v) ? v : 0.0f));
  return buf;
}
const char* place_name(SoundPlace p) {
  switch (p) {
    case SoundPlace::Outside: return "outside";
    case SoundPlace::Cave: return "cave";
    case SoundPlace::Burrow: return "burrow";
    case SoundPlace::Building: return "building";
  }
  return "outside";
}
}  // namespace

std::string sound_frame_json(const std::vector<SoundEvent>& events, Vec3 listener, float listener_yaw,
                             const Ambience& a, const std::vector<SoundSource>& sources) {
  std::string s = "{\"events\":[";
  for (size_t i = 0; i < events.size(); ++i) {
    const SoundEvent& e = events[i];
    if (i) s += ',';
    s += std::string("{\"sound\":\"") + sound_name(e.sound) + "\"";
    if (e.placed) s += ",\"x\":" + num(e.pos.x) + ",\"y\":" + num(e.pos.y) + ",\"z\":" + num(e.pos.z);
    if (e.sound == Sound::Cry) {
      s += std::string(",\"species\":\"") + species_name(e.species) + "\",\"cry\":\"" + cry_name(e.cry) + "\"";
    }
    if (e.sound == Sound::Spell) s += std::string(",\"spell\":\"") + spell_info(e.spell).id + "\"";
    if (e.sound == Sound::Impact) s += std::string(",\"element\":\"") + element_name(e.element) + "\"";
    s += ",\"variant\":" + std::to_string(e.variant) + ",\"strength\":" + num(e.strength) + "}";
  }
  s += "],\"listener\":{\"x\":" + num(listener.x) + ",\"y\":" + num(listener.y) + ",\"z\":" + num(listener.z) +
       ",\"yaw\":" + num(listener_yaw) + "}";
  s += std::string(",\"ambience\":{\"place\":\"") + place_name(a.place) + "\",\"season\":\"" + season_name(a.season) + "\"";
  s += ",\"night\":" + num(a.night) + ",\"wind\":" + num(a.wind) + ",\"rain\":" + num(a.rain) + ",\"snow\":" + num(a.snow) +
       ",\"storm\":" + num(a.storm);
  s += std::string(",\"battle\":") + (a.battle ? "true" : "false") + ",\"capture\":" + (a.capture ? "true" : "false") + "}";
  s += ",\"sources\":[";
  for (size_t i = 0; i < sources.size(); ++i) {
    const SoundSource& src = sources[i];
    if (i) s += ',';
    s += std::string("{\"kind\":\"") + source_name(src.kind) + "\",\"x\":" + num(src.pos.x) + ",\"y\":" + num(src.pos.y) +
         ",\"z\":" + num(src.pos.z) + "}";
  }
  return s + "]}";
}

}  // namespace monstres
