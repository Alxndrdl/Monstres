#pragma once

#include "monstres/core/weather.hpp"
#include "monstres/core/world.hpp"

#include <string>
#include <vector>

namespace monstres {

// The sounds of the game (web/audio.js plays them).
//
// The game itself never asks for a sound: a SoundWatch looks at the world
// after its ticks and tells what happened since it last looked (an attack,
// a hit, a spell, a quest done...), from what the world already keeps for
// the drawing and the interface (the ticks of the last attack, hit, spell,
// door...). Nothing in the game's rules knows about sounds, and the sounds
// change nothing in the game (its ticks stay the same: replays, saves).
//
// Besides those events: the ambience where the player is (outside or in a
// cave, the night, the wind, the rain, the season: the music and the
// background sounds), and the sources of continuous sounds around (the
// waterfall, the river, the mill, a fire), placed in the world.

enum class Sound : uint8_t {
  Cry,           // a monster cries (species, cry, variant)
  Swing,         // an attack goes off (move: 0 quick, 1-2 special; element)
  Hit,           // a blow lands (strength: share of the hit points it took)
  Parry,         // a blow blocked
  Impact,        // a projectile stops (element)
  Spell,         // the magician casts a spell (spell)
  Summon,        // a monster comes out of the crystal
  Recall,        // a monster goes back into it
  Teleport,
  Door,
  Rescue,        // sent back to the farm (a battle lost or fled)
  Heal,          // the healer heals the team
  BattleStart,
  Victory,
  Defeat,
  Fled,
  RuneRight,     // capturing: a rune touched right (variant: which one, a note)
  RuneWrong,
  Caught,
  Escaped,
  QuestAccepted,
  QuestStep,
  QuestDone,
  Item,          // something goes into the bag (variant: Item)
  LevelUp,       // the magician or a monster
  Thunder,       // a lightning bolt (strength: how close, 1 right here)
  Count
};
const char* sound_name(Sound sound);

// A monster's cries: its call (in the wild, out of the crystal, the
// bestiary's button: several variants), when it attacks, when it is hurt.
enum class Cry : uint8_t { Call, Attack, Hurt, Count };
const char* cry_name(Cry cry);
inline constexpr uint8_t kCallVariants = 3;

struct SoundEvent {
  Sound sound = Sound::Cry;
  Vec3 pos;                   // where in the world
  bool placed = false;        // false: no place (the interface: quests, items, the magician's level)
  Species species = Species::Companion;  // Cry
  Cry cry = Cry::Call;
  uint8_t variant = 0;        // Cry Call: < kCallVariants; RuneRight: the rune; Item: the item; Swing: the move
  Element element = Element::Normal;
  SpellId spell = SpellId::None;
  float strength = 1;         // 0..1
};

// Where the player is, for the music and the background sounds.
enum class SoundPlace : uint8_t { Outside, Cave, Burrow, Building };
struct Ambience {
  SoundPlace place = SoundPlace::Outside;
  Season season = Season::Summer;
  float night = 0;             // 0 by day, 1 at night
  float wind = 0, rain = 0, snow = 0, storm = 0;  // 0..1, outside (0 inside)
  bool battle = false;         // a battle or training
  bool capture = false;        // the runes of a capture
};
// `here`: the weather at the player; `wind`: what blows now (0..1).
Ambience ambience_of(const World& world, Season season, const Weather& here, float wind);

// Continuous sounds placed in the world, within kSourceReach of the listener.
enum class SourceKind : uint8_t { Waterfall, River, WaterWheel, Windmill, Fire, Count };
const char* source_name(SourceKind kind);
struct SoundSource {
  SourceKind kind = SourceKind::Fire;
  Vec3 pos;
};
inline constexpr float kSourceReach = 90.0f;
void sound_sources(const World& world, Vec3 listener, std::vector<SoundSource>& out);

// Looks at the world now and then (every frame): adds to `out` what happened
// since the last look. The first look (and one after the world was replaced:
// a save loaded, a new game) tells nothing, it only takes note.
// `seconds`: real time since the last look (the wild monsters' calls).
// `field`: the weather (thunder), or nullptr.
class SoundWatch {
public:
  void watch(const World& world, const WeatherField* field, double seconds, std::vector<SoundEvent>& out);
  void forget() { known_ = false; }  // the next look only takes note

private:
  struct FighterMarks {
    uint32_t id = 0;  // Creature::id (foes come and go)
    uint64_t attack = 0, hit = 0, parried = 0;
  };
  void note(const World& world, const WeatherField* field);
  bool known_ = false;
  uint64_t seed_ = 0, tick_ = 0;
  uint64_t spell_ = 0, teleport_ = 0, door_ = 0, rescue_ = 0, healed_ = 0, came_out_ = 0, went_in_ = 0;
  uint64_t outcome_ = 0, impact_ = 0, touch_ = 0;
  BattlePhase phase_ = BattlePhase::None;
  SealStage seal_ = SealStage::None;
  FighterMarks ally_;
  std::vector<FighterMarks> foes_;
  std::array<QuestState, kQuestCount> quests_{};
  Bag bag_{};
  uint16_t wizard_level_ = 1;
  std::vector<uint16_t> team_levels_;
  uint32_t bolt_ = 0;
  // The wild monsters' calls: their own random stream (not the world's).
  uint64_t rng_ = 0x9E3779B97F4A7C15ULL;
  double next_call_ = 4;
};

// What audio.js reads every frame: the events since the last time (then
// cleared by the caller), the listener (the camera: where, which way), the
// ambience and the sources around.
std::string sound_frame_json(const std::vector<SoundEvent>& events, Vec3 listener, float listener_yaw,
                             const Ambience& ambience, const std::vector<SoundSource>& sources);

}  // namespace monstres
