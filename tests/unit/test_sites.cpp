// The sites (sites.hpp): the fishing port and its piers, the abbey up in the
// mountains and its temple's trials, the camp by the sea, the other places.

#include "monstres/core/battle.hpp"
#include "monstres/core/interior.hpp"
#include "monstres/core/quests.hpp"
#include "monstres/core/roads.hpp"
#include "monstres/core/save.hpp"
#include "monstres/core/sites.hpp"
#include "test_helpers.hpp"

#include <algorithm>
#include <cmath>

using namespace monstres;

namespace {

float flat(Vec2 a, Vec2 b) { return std::hypot(a.x - b.x, a.z - b.z); }

void put_player(World& w, Vec2 p) {
  w.player.pos = {p.x, w.ground.height(p.x, p.z), p.z};
  w.player.velocity_y = 0;
  w.creatures[0].body.pos = w.player.pos;
  update_population(w);
}

// The camera's yaw whose forward runs along an axis.
Angle16 yaw_towards(Vec2 d) {
  if (std::fabs(d.x) >= std::fabs(d.z)) return d.x < 0 ? Angle16{16384} : Angle16{49152};
  return d.z > 0 ? Angle16{32768} : Angle16{0};
}

size_t count_kind(const World& w, ObjectKind k) {
  size_t n = 0;
  for (const Object& o : w.objects) n += o.kind == k;
  return n;
}

}  // namespace

TEST_CASE("the sites: the port, the abbey, the camp and the others, apart, each its lane") {
  for (uint64_t seed = 0; seed < kManySeeds; ++seed) {
    CAPTURE(seed);
    const World& w = cached_game(seed);
    const SitesLayout& s = w.sites;
    REQUIRE(s.valid());
    // The port on the coast, its piers out over the sea.
    REQUIRE((s.port.x != 0 || s.port.z != 0));
    CHECK(!s.piers.empty());
    CHECK(count_kind(w, ObjectKind::Pier) >= 3);
    CHECK(s.port_level == doctest::Approx(rules::kPortLevel));
    for (size_t k = 0; k < s.piers.size(); ++k) {
      const Vec2 end{s.piers[k].x + s.port_sea.x * (s.pier_lengths[k] - 1.0f), s.piers[k].z + s.port_sea.z * (s.pier_lengths[k] - 1.0f)};
      CHECK(w.ground.sea(end.x, end.z));
      CHECK(w.ground.height(end.x, end.z) < rules::kSeaLevel);
    }
    // The abbey in the mountains, within the world, its ground level and high; its way up.
    CHECK(s.abbey.z < -1050.0f);
    CHECK(rules::in_world(s.abbey.x, s.abbey.z, std::max(s.abbey_hx, s.abbey_hz)));
    CHECK(std::fabs(w.ground.height(s.temple.x, s.temple.z) - s.abbey_level) < 0.3f);
    CHECK(count_kind(w, ObjectKind::Temple) == 1);
    CHECK(count_kind(w, ObjectKind::AbbeyWall) > 40);
    REQUIRE(s.abbey_way.size() > 100);
    CHECK(flat(s.abbey_way.back(), {s.abbey.x, s.abbey.z + s.abbey_hz}) < 3.0f);
    // The sites apart from one another.
    const Vec2 places[] = {s.port, s.abbey, s.camp, s.lookout, s.quarry, s.chapel, s.apiary, s.dolmen};
    for (size_t a = 0; a < std::size(places); ++a) {
      for (size_t b = a + 1; b < std::size(places); ++b) CHECK(flat(places[a], places[b]) > 30.0f);
    }
    // A lane to each of those people go to (the abbey's: to the foot of its way, then its way).
    for (const LaneTo to : {LaneTo::Port, LaneTo::Abbey, LaneTo::AbbeyWay, LaneTo::Camp, LaneTo::Lookout, LaneTo::Quarry,
                            LaneTo::Chapel, LaneTo::Apiary}) {
      CAPTURE(static_cast<int>(to));
      CHECK(std::find(w.roads.lane_to.begin(), w.roads.lane_to.end(), to) != w.roads.lane_to.end());
    }
    // Their teleporters: thirteen in all.
    CHECK(count_kind(w, ObjectKind::Teleporter) == kTeleporterCount);
  }
}

TEST_CASE("a pier is walked onto from the quay, and its rails keep one out of the sea") {
  World w = cached_game(3);
  const SitesLayout& s = w.sites;
  REQUIRE(!s.piers.empty());
  size_t longest = 0;
  for (size_t k = 1; k < s.piers.size(); ++k) longest = s.pier_lengths[k] > s.pier_lengths[longest] ? k : longest;
  const Vec2 d = s.port_sea, root = s.piers[longest];
  put_player(w, {root.x - d.x * 3.0f, root.z - d.z * 3.0f});
  w.creatures[0].body.pos = {root.x - d.x * 5.0f, w.player.pos.y, root.z - d.z * 5.0f};
  run(w, forward(yaw_towards(d)), 4 * rules::kTicksPerSecond);
  const Vec2 at{w.player.pos.x, w.player.pos.z};
  const float out = (at.x - root.x) * d.x + (at.z - root.z) * d.z;
  CHECK(out > 8.0f);                                    // out over the sea
  CHECK(w.ground.height(at.x, at.z) < rules::kSeaLevel);
  CHECK(w.player.pos.y == doctest::Approx(s.port_level + rules::kPierDeck).epsilon(0.02));  // on its deck
  // Sideways: the rail stops the magician.
  run(w, move(127, 0, yaw_towards(d)), 2 * rules::kTicksPerSecond);
  CHECK(w.player.pos.y > rules::kSeaLevel);
  const Vec2 now{w.player.pos.x, w.player.pos.z};
  const Vec2 across{-d.z, d.x};
  CHECK(std::fabs((now.x - root.x) * across.x + (now.z - root.z) * across.z) < rules::kPierHalf);
}

TEST_CASE("the abbey's temple: its monks fight one after the other, then the abbot; the trials are saved") {
  World w = cached_game(5);
  const SitesLayout& s = w.sites;
  // In through the temple's door.
  put_player(w, {s.temple.x, s.temple.z + rules::kTempleHalfZ + 0.8f});
  const DoorNear d = door_near(w);
  REQUIRE(d.action == DoorAction::Enter);
  CHECK(d.place == PlaceKind::Temple);
  REQUIRE(use_door(w));
  REQUIRE(w.place.kind == PlaceKind::Temple);
  CHECK(zone_at(w, w.player.pos.x, w.player.pos.z) == Zone::Frost);
  // The monks and the abbot along the nave, each its monster beside it.
  for (uint8_t n = npc::kHugues; n <= npc::kAmbroise; ++n) {
    CAPTURE(int(n));
    const int i = npc_index(w, n);
    REQUIRE(i > 0);
    uint8_t count = 0;
    rival_team(w, n, count);
    CHECK(count >= 2);
    const auto has_monster = std::any_of(w.creatures.begin(), w.creatures.end(),
                                         [&](const Creature& c) { return c.tag == Tag::Rival && c.npc == n; });
    CHECK(has_monster);
  }
  // Next to a monk: a battle can start, down the nave.
  const auto next_to = [&](uint8_t n) {
    const Vec3 p = w.creatures[static_cast<size_t>(npc_index(w, n))].body.pos;
    w.player.pos = {p.x + 1.1f, 0, p.z};
  };
  next_to(npc::kHugues);
  CHECK(can_challenge(w, npc::kHugues));
  Vec2 center, axis;
  REQUIRE(rival_arena(w, npc::kHugues, center, axis));
  CHECK(wall_distance(w.interior, center.x, center.z) > 5.0f);
  // The abbot waits for the three monks to be beaten.
  next_to(npc::kAmbroise);
  CHECK_FALSE(can_challenge(w, npc::kAmbroise));
  w.trials = 0x07;
  CHECK(can_challenge(w, npc::kAmbroise));
  CHECK((npc_marks(w, npc::kAmbroise) & kMarkFight) != 0);
  CHECK((npc_marks(w, npc::kHugues) & kMarkFight) == 0);
  // Saved (inside the temple too), as are Tristan's wins.
  w.tristan_wins = 2;
  const auto loaded = deserialize(serialize(w));
  REQUIRE(loaded.has_value());
  CHECK(loaded->trials == 0x07);
  CHECK(loaded->tristan_wins == 2);
  CHECK(loaded->place.kind == PlaceKind::Temple);
  CHECK(npc_index(*loaded, npc::kAmbroise) > 0);
}

TEST_CASE("the sites' villagers: the healers heal, Tristan fights in the camp") {
  World w = cached_game(6);
  for (const uint8_t n : {npc::kLoic, npc::kNolwenn, npc::kGauthier, npc::kYsolde, npc::kTristan, npc::kRaoul, npc::kSylvain,
                          npc::kHeloise, npc::kArmand}) {
    CAPTURE(int(n));
    CHECK(npc_index(w, n) > 0);
  }
  CHECK((npc_marks(w, npc::kGauthier) & kMarkHeal) != 0);
  CHECK((npc_marks(w, npc::kYsolde) & kMarkHeal) != 0);
  CHECK((npc_marks(w, npc::kTristan) & kMarkFight) != 0);
  const Vec3 p = w.creatures[static_cast<size_t>(npc_index(w, npc::kTristan))].body.pos;
  put_player(w, {p.x + 1.1f, p.z});
  CHECK(can_challenge(w, npc::kTristan));
  Vec2 center, axis;
  REQUIRE(rival_arena(w, npc::kTristan, center, axis));
  CHECK(flat(center, w.sites.camp) < rules::kSeaCampRadius);
}
