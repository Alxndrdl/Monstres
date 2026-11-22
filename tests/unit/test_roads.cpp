// The roads (roads.hpp): the lanes to the places people go to, the places
// along them; Rochebrune (north.hpp): its arena's tournaments, its breeding
// farm; the mine's galleries going down and the natural cave beyond them.

#include "monstres/core/battle.hpp"
#include "monstres/core/debug.hpp"
#include "monstres/core/interior.hpp"
#include "monstres/core/north.hpp"
#include "monstres/core/quests.hpp"
#include "monstres/core/roads.hpp"
#include "monstres/core/save.hpp"
#include "monstres/core/west.hpp"
#include "test_helpers.hpp"

#include <algorithm>
#include <cmath>

using namespace monstres;

namespace {

Input command(Command c, uint16_t target = 0) {
  Input in;
  in.command = c;
  in.target = target;
  return in;
}

float flat(Vec2 a, Vec2 b) { return std::hypot(a.x - b.x, a.z - b.z); }

void put_player(World& w, Vec2 p) {
  w.player.pos = {p.x, w.ground.height(p.x, p.z), p.z};
  w.player.velocity_y = 0;
  w.creatures[0].body.pos = w.player.pos;
  update_population(w);
}

void go_to_npc(World& w, uint8_t n) {
  const int i = npc_index(w, n);
  REQUIRE(i > 0);
  const Vec3 p = w.creatures[static_cast<size_t>(i)].body.pos;
  put_player(w, {p.x + 1.2f, p.z});
}

size_t count_kind(const World& w, ObjectKind k) {
  size_t n = 0;
  for (const Object& o : w.objects) n += o.kind == k;
  return n;
}

}  // namespace

TEST_CASE("lanes lead from the path or the road to the places, round the water, up gentle slopes") {
  for (uint64_t seed = 0; seed < 8; ++seed) {
    CAPTURE(seed);
    const World& w = cached_game(seed);
    const RoadsLayout& r = w.roads;
    REQUIRE(r.valid());
    CHECK(r.lanes.size() >= 12);
    CHECK(r.lanes.size() == r.lane_to.size());
    size_t signs = 0;
    for (const Object& o : w.objects) signs += o.kind == ObjectKind::Sign;
    CHECK(signs >= r.lanes.size() / 2);
    for (size_t k = 0; k < r.lanes.size(); ++k) {
      CAPTURE(static_cast<int>(r.lane_to[k]));
      const auto& pts = r.lanes[k].points;
      REQUIRE(pts.size() > 5);
      // It leaves the path, the road or a lane made before it.
      const Vec2 a = pts.front();
      float from = std::min(distance_to_path(w.path, a.x, a.z), distance_to_road(w.west.road, a.x, a.z));
      for (size_t j = 0; j < k; ++j) {
        RoadsLayout one;
        one.inn = r.inn;
        one.lanes = {r.lanes[j]};
        from = std::min(from, distance_to_lanes(one, a.x, a.z));
      }
      CHECK(from < 1.0f);
      // A point every meter or so, never in the river, walkable all along.
      for (size_t i = 1; i < pts.size(); ++i) {
        const float step = flat(pts[i], pts[i - 1]);
        CHECK(step < 1.6f);
        CHECK(distance_to_stream(w.west.stream, pts[i].x, pts[i].z, 6.0f) > 4.0f);
        CHECK_FALSE(in_lake(w.village.lake, pts[i].x, pts[i].z));
        if (i % 3 == 0 && i >= 3) {
          const float rise = std::fabs(w.ground.height(pts[i].x, pts[i].z) - w.ground.height(pts[i - 3].x, pts[i - 3].z));
          CHECK(rise / std::max(flat(pts[i], pts[i - 3]), 0.5f) < rules::kMaxWalkSlope);
        }
      }
    }
    // No tree nor rock on a lane.
    for (const Object& o : w.objects) {
      if (o.kind != ObjectKind::Tree && o.kind != ObjectKind::Pine && o.kind != ObjectKind::Rock && o.kind != ObjectKind::Larch) continue;
      CHECK(distance_to_lanes(r, o.pos.x, o.pos.z, 2.0f) > rules::kLaneHalfWidth);
    }
  }
}

TEST_CASE("the places along the roads: the inn, the farmstead, the sawmill, and their people") {
  World w = cached_game(3);
  const RoadsLayout& r = w.roads;
  CHECK(in_roadside_place(r, r.inn.x, r.inn.z));
  CHECK(flat(r.inn, r.farmstead) > rules::kInnRadius + rules::kFarmsteadRadius);
  CHECK(count_kind(w, ObjectKind::Shrine) >= 1);
  CHECK(count_kind(w, ObjectKind::Bench) >= 4);
  CHECK(count_kind(w, ObjectKind::Table) >= 2);  // the inn's, outside
  REQUIRE(npc_index(w, npc::kAubin) > 0);
  REQUIRE(npc_index(w, npc::kBertille) > 0);
  // Aubin's meal heals the team.
  REQUIRE(!w.team.empty());
  w.team[0].hp = 1;
  go_to_npc(w, npc::kAubin);
  CHECK((npc_marks(w, npc::kAubin) & kMarkHeal) != 0);
  step(w, command(Command::HealTeam, npc::kAubin));
  CHECK(w.team[0].hp == monster_fight_stats(w.team[0]).hp);
  // The map knows them, and the lanes.
  const std::string map = map_json(w);
  CHECK(map.find("\"roads\":{\"lanes\":[[") != std::string::npos);
  CHECK(map.find("\"farmstead\":[") != std::string::npos);
}

TEST_CASE("Rochebrune: on the path at the foot of the mountains, its arena, its breeding farm") {
  for (uint64_t seed = 0; seed < 6; ++seed) {
    CAPTURE(seed);
    const World& w = cached_game(seed);
    const NorthLayout& n = w.north;
    CHECK(distance_to_path(w.path, n.hamlet.x, n.hamlet.z) < 1.0f);
    CHECK(n.hamlet.z < n.springs.z);
    CHECK(n.hamlet.z > n.lake.z);
    // Flat: the arena's sand, the paddock.
    CHECK(std::fabs(w.ground.height(n.arena.x, n.arena.z) - n.hamlet_level) < 0.3f);
    CHECK(std::fabs(w.ground.height(n.paddock.x, n.paddock.z) - n.hamlet_level) < 0.3f);
    CHECK(distance_to_path(w.path, n.arena.x, n.arena.z) > rules::kArenaRadius + 5.0f);
    CHECK(in_hamlet(n, n.arena.x, n.arena.z));
    CHECK(zone_at(w, n.hamlet.x, n.hamlet.z) != Zone::Coast);
    // Nothing grows on its ground.
    for (const Object& o : w.objects) {
      if (o.kind == ObjectKind::Tree || o.kind == ObjectKind::Pine || o.kind == ObjectKind::Larch) {
        CHECK_FALSE(in_hamlet(n, o.pos.x, o.pos.z, -2.0f));
      }
    }
  }
}

TEST_CASE("the arena's tournaments: three contenders in a row, the cup won; lost, it is over") {
  World w = new_game(11);
  REQUIRE(!w.team.empty());
  w.team[0].level = 30;
  w.team[0].hp = monster_fight_stats(w.team[0]).hp;
  go_to_npc(w, npc::kBarnabe);
  CHECK((npc_marks(w, npc::kBarnabe) & kMarkFight) != 0);
  CHECK(can_enter_tournament(w, 0));
  CHECK_FALSE(can_enter_tournament(w, 1));  // the bronze one first
  const uint32_t coins = w.bag[static_cast<size_t>(Item::Coin)];
  step(w, command(Command::Tournament, 0));
  REQUIRE(w.tournament.on);
  CHECK_FALSE(can_enter_tournament(w, 0));
  for (uint8_t round = 0; round < rules::kTournamentRounds; ++round) {
    CAPTURE(static_cast<int>(round));
    // The contender comes; both sides take their places in the arena.
    for (int t = 0; t < 6 * rules::kTicksPerSecond && w.battle.phase != BattlePhase::Prepare; ++t) step(w, Input{});
    REQUIRE(w.battle.phase == BattlePhase::Prepare);
    CHECK(w.battle.rival.npc == npc::kContender);
    CHECK(w.battle.rival.count == (round == 0 ? 2 : 3));
    CHECK(w.battle.rival.team[0].level >= rules::kCupLevels[0] + 2 * round);
    const Vec2 c = w.north.arena;
    CHECK(flat(w.battle.rival.foe_spot, c) < rules::kArenaSand);
    CHECK(flat(w.battle.rival.ally_spot, c) < rules::kArenaSand);
    CHECK(w.team[0].hp == monster_fight_stats(w.team[0]).hp);  // healed before each round
    for (int t = 0; t < 30 * rules::kTicksPerSecond && w.battle.phase == BattlePhase::Prepare; ++t) step(w, command(Command::Ready));
    REQUIRE(w.battle.phase == BattlePhase::Fight);
    // Each of its monsters beaten.
    for (int t = 0; t < 60 * rules::kTicksPerSecond && w.battle.phase == BattlePhase::Fight; ++t) {
      for (Foe& f : w.battle.foes) f.hp = 0;
      step(w, Input{});
    }
    CHECK(w.battle.outcome == BattleOutcome::Victory);
    CHECK(w.battle.outcome_rival == npc::kContender);
  }
  CHECK_FALSE(w.tournament.on);
  CHECK((w.cups & 1u) != 0);
  CHECK(w.bag[static_cast<size_t>(Item::Coin)] > coins + rules::kCupCoins[0]);
  go_to_npc(w, npc::kBarnabe);  // (back out of the arena)
  CHECK(can_enter_tournament(w, 1));
  // The contender leaves; the cups are saved (not the contender).
  step(w, Input{});
  CHECK(npc_index(w, npc::kContender) < 0);
  const auto loaded = deserialize(serialize(w));
  REQUIRE(loaded.has_value());
  CHECK(loaded->cups == w.cups);
  // The silver one, lost: over, the team healed.
  step(w, command(Command::Tournament, 1));
  REQUIRE(w.tournament.on);
  for (int t = 0; t < 6 * rules::kTicksPerSecond && w.battle.phase != BattlePhase::Prepare; ++t) step(w, Input{});
  for (int t = 0; t < 30 * rules::kTicksPerSecond && w.battle.phase == BattlePhase::Prepare; ++t) step(w, command(Command::Ready));
  for (int t = 0; t < 30 * rules::kTicksPerSecond && w.battle.phase == BattlePhase::Fight; ++t) {
    for (Monster& m : w.team) m.hp = 0;
    step(w, Input{});
  }
  CHECK(w.battle.outcome == BattleOutcome::Defeat);
  step(w, Input{});
  CHECK_FALSE(w.tournament.on);
  CHECK((w.cups & 2u) == 0);
  CHECK(w.team[0].hp == monster_fight_stats(w.team[0]).hp);
}

TEST_CASE("Margaux keeps the farm's monsters too: swapped at the breeding farm, grazing in its paddock") {
  World w = new_game(5);
  w.quests[static_cast<size_t>(QuestId::Delivery)].stage = QuestStage::Done;  // the transfer stone at the farm
  REQUIRE(farm_open(w));
  Monster m;
  m.species = Species::Goat;
  m.level = 6;
  m.hp = 10;
  w.farm.push_back(m);
  go_to_npc(w, npc::kMargaux);
  CHECK((npc_marks(w, npc::kMargaux) & kMarkFarm) != 0);
  const size_t team = w.team.size();
  step(w, command(Command::FarmSwap, static_cast<uint16_t>(rules::kTeamNone + 8 * 0)));  // take it back
  CHECK(w.team.size() == team + 1);
  CHECK(w.team.back().species == Species::Goat);
  // Left there again: it grazes in the paddock (while its square is lived in).
  step(w, command(Command::FarmSwap, static_cast<uint16_t>((w.team.size() - 1) + 8 * rules::kFarmEmpty)));
  CHECK(w.farm.size() == 1);
  update_population(w);
  size_t grazing = 0;
  for (const Creature& c : w.creatures) grazing += c.tag == Tag::Boarder && in_paddock(w.north, c.home.x, c.home.z);
  CHECK(grazing == 1);
}

TEST_CASE("the mine: galleries every which way, going down; the guardian's hall wide and high; a natural cave out far away") {
  for (uint64_t seed = 0; seed < 5; ++seed) {
    CAPTURE(seed);
    World w = cached_game(seed);
    int mouth = -1;
    for (size_t k = 0; k < w.objects.size(); ++k) {
      if (w.objects[k].kind == ObjectKind::MineEntrance) mouth = static_cast<int>(k);
    }
    REQUIRE(mouth >= 0);
    REQUIRE(enter_place(w, {PlaceKind::Mine, static_cast<uint32_t>(mouth)}));
    const Interior& in = w.interior;
    // Not at right angles only, some going down, a wide high hall, natural pieces.
    size_t slanted = 0, natural = 0;
    float lowest = 0, widest = 0, highest = 0;
    for (const RoomPiece& p : in.pieces) {
      lowest = std::min({lowest, p.ya, p.yb});
      natural += p.natural;
      if (p.a == p.b && !p.natural && p.radius > widest) {
        widest = p.radius;
        highest = p.height;
      }
      const float dx = std::fabs(p.b.x - p.a.x), dz = std::fabs(p.b.z - p.a.z);
      if (!(p.a == p.b) && dx > 1.0f && dz > 1.0f) ++slanted;
    }
    CHECK(slanted >= 4);
    CHECK(lowest < -4.0f);
    CHECK(widest >= 9.0f);
    CHECK(highest >= 9.0f);
    CHECK(natural >= 4);
    // The floor goes down gently (walkable), the player at the entry on it.
    CHECK(w.ground.height(in.entry.x, in.entry.z) == doctest::Approx(0).epsilon(0.05));
    for (const RoomPiece& p : in.pieces) {
      if (p.a == p.b) continue;
      const float len = flat(p.a, p.b);
      for (float t = 0.1f; t < 0.9f; t += 0.1f) {
        const Vec2 q{p.a.x + (p.b.x - p.a.x) * t, p.a.z + (p.b.z - p.a.z) * t};
        const Vec2 q2{p.a.x + (p.b.x - p.a.x) * (t + 0.1f), p.a.z + (p.b.z - p.a.z) * (t + 0.1f)};
        CHECK(std::fabs(w.ground.height(q2.x, q2.z) - w.ground.height(q.x, q.z)) / (0.1f * len) < rules::kMaxWalkSlope * 0.8f);
      }
    }
    // The guardian on its hall's floor, deep down.
    int guardian = -1;
    for (size_t i = 1; i < w.creatures.size(); ++i) {
      if (w.creatures[i].genes.form == Form::Omega) guardian = static_cast<int>(i);
    }
    REQUIRE(guardian > 0);
    const Vec3 g = w.creatures[static_cast<size_t>(guardian)].body.pos;
    CHECK(g.y < -1.0f);
    CHECK(g.y == doctest::Approx(w.ground.height(g.x, g.z)).epsilon(0.05));
    // The far way out: out of the grotto, and back in there.
    const Exit* far = nullptr;
    for (const Exit& e : in.exits) {
      if (e.kind == ExitKind::Far) far = &e;
    }
    REQUIRE(far != nullptr);
    const Exit out = *far;  // (the interior is built again when coming back in)
    w.player.pos = out.at;
    const DoorNear d = door_near(w);
    CHECK(d.action == DoorAction::Leave);
    CHECK(d.far);
    REQUIRE(use_door(w));
    CHECK_FALSE(inside(w));
    CHECK(flat({w.player.pos.x, w.player.pos.z}, w.north.grotto) < 6.0f);
    CHECK(w.ground.floor.empty());
    const DoorNear back = door_near(w);
    CHECK(back.action == DoorAction::Enter);
    CHECK(back.building == ObjectKind::Grotto);
    REQUIRE(use_door(w));
    CHECK(inside(w));
    CHECK(flat({w.player.pos.x, w.player.pos.z}, {out.at.x, out.at.z}) < 3.0f);
    CHECK(w.player.pos.y == doctest::Approx(w.ground.height(w.player.pos.x, w.player.pos.z)).epsilon(0.05));
  }
}

TEST_CASE("a game saved down in the mine loads there, on its floor") {
  World w = cached_game(2);
  int mouth = -1;
  for (size_t k = 0; k < w.objects.size(); ++k) {
    if (w.objects[k].kind == ObjectKind::MineEntrance) mouth = static_cast<int>(k);
  }
  REQUIRE(enter_place(w, {PlaceKind::Mine, static_cast<uint32_t>(mouth)}));
  // At the deepest piece's middle.
  const RoomPiece* deep = &w.interior.pieces[0];
  for (const RoomPiece& p : w.interior.pieces) {
    if (std::min(p.ya, p.yb) < std::min(deep->ya, deep->yb)) deep = &p;
  }
  const Vec2 at{(deep->a.x + deep->b.x) / 2, (deep->a.z + deep->b.z) / 2};
  w.player.pos = {at.x, w.ground.height(at.x, at.z), at.z};
  REQUIRE(w.player.pos.y < -2.0f);
  const auto loaded = deserialize(serialize(w));
  REQUIRE(loaded.has_value());
  CHECK(inside(*loaded));
  CHECK(loaded->player.pos.y == doctest::Approx(w.player.pos.y).epsilon(0.01));
}
