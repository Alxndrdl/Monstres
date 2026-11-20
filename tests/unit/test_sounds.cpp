// The sounds (audio/sounds.hpp): what the watch hears in the world's
// changes, the ambience, the sources around, what audio.js reads.

#include "monstres/audio/sounds.hpp"
#include "monstres/core/battle.hpp"
#include "monstres/core/interior.hpp"
#include "monstres/core/west.hpp"
#include "test_helpers.hpp"

#include <algorithm>
#include <string>

using namespace monstres;

namespace {

size_t count(const std::vector<SoundEvent>& events, Sound s) {
  return static_cast<size_t>(std::count_if(events.begin(), events.end(), [s](const SoundEvent& e) { return e.sound == s; }));
}
const SoundEvent* find(const std::vector<SoundEvent>& events, Sound s, Cry c = Cry::Call) {
  for (const SoundEvent& e : events) {
    if (e.sound == s && (s != Sound::Cry || e.cry == c)) return &e;
  }
  return nullptr;
}

// A watch that has seen `w` once (the first look tells nothing).
SoundWatch watching(const World& w) {
  SoundWatch watch;
  std::vector<SoundEvent> events;
  watch.watch(w, nullptr, 0, events);
  CHECK(events.empty());
  return watch;
}

std::vector<SoundEvent> listen(SoundWatch& watch, const World& w, double seconds = 0) {
  std::vector<SoundEvent> events;
  watch.watch(w, nullptr, seconds, events);
  return events;
}

}  // namespace

TEST_CASE("the sound watch hears the magician: a spell, a teleport, a door, a heal") {
  World w = cached_game(42);
  SoundWatch watch = watching(w);
  CHECK(listen(watch, w).empty());  // nothing happened
  w.tick += 10;
  w.spell_tick = w.tick;
  w.spell_cast = SpellId::Detect;
  w.door_tick = w.tick;
  w.healed_tick = w.tick;
  auto events = listen(watch, w);
  REQUIRE(find(events, Sound::Spell));
  CHECK(find(events, Sound::Spell)->spell == SpellId::Detect);
  CHECK(find(events, Sound::Spell)->placed);
  CHECK(count(events, Sound::Door) == 1);
  CHECK(count(events, Sound::Heal) == 1);
  CHECK(listen(watch, w).empty());  // told once
  // A real cast, through the game.
  w.adventure_spells = {SpellId::Home, SpellId::None, SpellId::None, SpellId::None};
  Input cast;
  cast.command = Command::Cast;
  step(w, cast);
  events = listen(watch, w);
  CHECK(count(events, Sound::Spell) == 1);
  CHECK(count(events, Sound::Teleport) == 1);
}

TEST_CASE("the sound watch hears the battles: attacks and blows cry, with the species that cries") {
  World w = cached_game(42);
  REQUIRE(w.active >= 0);
  int wild = -1;
  for (size_t i = 1; i < w.creatures.size() && wild < 0; ++i) {
    if (monster_stats(w.creatures[i].species).is_monster && w.creatures[i].species != Species::Villager) wild = static_cast<int>(i);
  }
  REQUIRE(wild > 0);
  SoundWatch watch = watching(w);
  w.battle.phase = BattlePhase::Fight;
  Foe foe;
  foe.index = static_cast<uint16_t>(wild);
  foe.hp = foe.max_hp = 20;
  w.battle.foes.push_back(foe);
  w.tick += 1;
  auto events = listen(watch, w);
  CHECK(count(events, Sound::BattleStart) == 1);
  CHECK(count(events, Sound::Cry) == 0);
  // The companion attacks; the foe is hit.
  w.tick += 30;
  w.battle.ally.attack_tick = w.tick;
  w.battle.foes[0].fighter.hit_tick = w.tick;
  w.battle.foes[0].fighter.last_damage = 10;
  events = listen(watch, w);
  const SoundEvent* attack = find(events, Sound::Cry, Cry::Attack);
  REQUIRE(attack);
  CHECK(attack->species == w.team[static_cast<size_t>(w.active)].species);
  const SoundEvent* hurt = find(events, Sound::Cry, Cry::Hurt);
  REQUIRE(hurt);
  CHECK(hurt->species == w.creatures[static_cast<size_t>(wild)].species);
  CHECK(count(events, Sound::Swing) == 1);
  REQUIRE(find(events, Sound::Hit));
  CHECK(find(events, Sound::Hit)->strength == doctest::Approx(1.0f));  // half its hit points: a big blow
  // Won.
  w.tick += 30;
  w.battle.phase = BattlePhase::None;
  w.battle.foes.clear();
  w.battle.outcome = BattleOutcome::Victory;
  w.battle.outcome_tick = w.tick;
  events = listen(watch, w);
  CHECK(count(events, Sound::Victory) == 1);
}

TEST_CASE("the sound watch hears captures, quests, the bag and levels") {
  World w = cached_game(42);
  SoundWatch watch = watching(w);
  w.tick += 5;
  w.battle.seal.stage = SealStage::Input;
  w.battle.seal.touch_tick = w.tick;
  w.battle.seal.touched = 3;
  auto events = listen(watch, w);
  REQUIRE(find(events, Sound::RuneRight));
  CHECK(find(events, Sound::RuneRight)->variant == 3);
  w.tick += 5;
  w.battle.seal.stage = SealStage::Caught;
  CHECK(count(listen(watch, w), Sound::Caught) == 1);
  w.battle.seal.stage = SealStage::None;
  listen(watch, w);

  w.quests[static_cast<size_t>(QuestId::Merchant)].stage = QuestStage::Active;
  CHECK(count(listen(watch, w), Sound::QuestAccepted) == 1);
  w.quests[static_cast<size_t>(QuestId::Merchant)].progress = 1;
  ++w.bag[static_cast<size_t>(Item::Goods)];
  events = listen(watch, w);
  CHECK(count(events, Sound::QuestStep) == 1);
  REQUIRE(find(events, Sound::Item));
  CHECK(find(events, Sound::Item)->variant == static_cast<uint8_t>(Item::Goods));
  CHECK_FALSE(find(events, Sound::Item)->placed);
  w.quests[static_cast<size_t>(QuestId::Merchant)].stage = QuestStage::Done;
  CHECK(count(listen(watch, w), Sound::QuestDone) == 1);
  ++w.wizard.level;
  CHECK(count(listen(watch, w), Sound::LevelUp) == 1);
  --w.bag[static_cast<size_t>(Item::Goods)];
  CHECK(listen(watch, w).empty());  // spent: no sound
}

TEST_CASE("the wild monsters around call now and then, with a few variants; not during a battle") {
  World w = cached_game(42);
  // Some monster close by.
  int wild = -1;
  for (size_t i = 1; i < w.creatures.size() && wild < 0; ++i) {
    if (monster_stats(w.creatures[i].species).is_monster && w.creatures[i].species != Species::Villager) wild = static_cast<int>(i);
  }
  REQUIRE(wild > 0);
  w.player.pos = w.creatures[static_cast<size_t>(wild)].body.pos;
  w.player.pos.x += 5;
  SoundWatch watch = watching(w);
  size_t calls = 0;
  bool variants[kCallVariants] = {};
  for (int s = 0; s < 120; ++s) {
    for (const SoundEvent& e : listen(watch, w, 1.0)) {
      if (e.sound != Sound::Cry) continue;
      CHECK(e.cry == Cry::Call);
      CHECK(e.placed);
      CHECK(monster_stats(e.species).is_monster);
      REQUIRE(e.variant < kCallVariants);
      variants[e.variant] = true;
      ++calls;
    }
  }
  CHECK(calls >= 8);   // every few seconds
  CHECK(calls <= 60);
  CHECK(variants[0] + variants[1] + variants[2] >= 2);
  w.battle.phase = BattlePhase::Fight;
  listen(watch, w);
  size_t in_battle = 0;
  for (int s = 0; s < 60; ++s) in_battle += count(listen(watch, w, 1.0), Sound::Cry);
  CHECK(in_battle == 0);
}

TEST_CASE("another world loaded: the watch only takes note") {
  World w = cached_game(42);
  SoundWatch watch = watching(w);
  World other = cached_game(7);
  other.spell_tick = other.tick;  // whatever it holds
  other.quests[0].stage = QuestStage::Done;
  CHECK(listen(watch, other).empty());
  watch.forget();
  w.spell_tick = w.tick + 100;
  CHECK(listen(watch, w).empty());
}

TEST_CASE("the ambience: the place, the night, the weather outside only") {
  World w = cached_game(42);
  Weather wet = weather_of(WeatherKind::Rain);
  w.clock = 12 * rules::kTicksPerHour;
  Ambience a = ambience_of(w, Season::Autumn, wet, 0.6f);
  CHECK(a.place == SoundPlace::Outside);
  CHECK(a.season == Season::Autumn);
  CHECK(a.night == doctest::Approx(0.0f));
  CHECK(a.rain > 0.3f);
  CHECK(a.wind == doctest::Approx(0.6f));
  w.clock = 23 * rules::kTicksPerHour;
  CHECK(ambience_of(w, Season::Autumn, wet, 0.6f).night == doctest::Approx(1.0f));
  // In the crystal cave: no rain, no wind.
  int mouth = -1;
  for (size_t k = 0; k < w.objects.size(); ++k) {
    if (w.objects[k].kind == ObjectKind::CaveMouth) mouth = static_cast<int>(k);
  }
  REQUIRE(mouth >= 0);
  REQUIRE(enter_place(w, {PlaceKind::Cave, static_cast<uint32_t>(mouth)}));
  a = ambience_of(w, Season::Autumn, wet, 0.6f);
  CHECK(a.place == SoundPlace::Cave);
  CHECK(a.rain == 0.0f);
  CHECK(a.wind == 0.0f);
}

TEST_CASE("the sources around: the waterfall and the river near the cove; what audio.js reads") {
  const World& w = cached_game(42);
  REQUIRE(w.west.valid());
  const Vec3 cove{w.west.cove.x, w.west.cove_level, w.west.cove.z};
  std::vector<SoundSource> sources;
  sound_sources(w, cove, sources);
  CHECK(std::any_of(sources.begin(), sources.end(), [](const SoundSource& s) { return s.kind == SourceKind::Waterfall; }));
  CHECK(std::any_of(sources.begin(), sources.end(), [](const SoundSource& s) { return s.kind == SourceKind::River; }));
  for (const SoundSource& s : sources) CHECK(std::hypot(s.pos.x - cove.x, s.pos.z - cove.z) < kSourceReach);
  std::vector<SoundSource> far;
  sound_sources(w, {cove.x + 5000, 0, cove.z}, far);
  CHECK(far.empty());

  SoundEvent call;
  call.sound = Sound::Cry;
  call.species = Species::Spark;
  call.cry = Cry::Hurt;
  call.placed = true;
  call.pos = {1, 2, 3};
  const std::string json = sound_frame_json({call}, {0, 1, 0}, 0.5f, Ambience{}, sources);
  CHECK(json.find("\"sound\":\"cry\"") != std::string::npos);
  CHECK(json.find("\"species\":\"spark\"") != std::string::npos);
  CHECK(json.find("\"cry\":\"hurt\"") != std::string::npos);
  CHECK(json.find("\"place\":\"outside\"") != std::string::npos);
  CHECK(json.find("\"season\":\"summer\"") != std::string::npos);
  CHECK(json.find("\"kind\":\"waterfall\"") != std::string::npos);
}
