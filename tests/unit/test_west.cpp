// The west (west.hpp): its places, its villagers, its quests, the farm, the
// aggressive monsters, the mill, the shepherd's lambs, the bag.

#include "monstres/core/battle.hpp"
#include "monstres/core/debug.hpp"
#include "monstres/core/interior.hpp"
#include "monstres/core/quests.hpp"
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
Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.z + b.z}; }

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

QuestState& quest(World& w, QuestId q) { return w.quests[static_cast<size_t>(q)]; }

size_t count_kind(const World& w, ObjectKind k) {
  size_t n = 0;
  for (const Object& o : w.objects) n += o.kind == k;
  return n;
}

int kind_index(const World& w, ObjectKind k) {
  for (size_t i = 0; i < w.objects.size(); ++i) {
    if (w.objects[i].kind == k) return static_cast<int>(i);
  }
  return -1;
}

int first_tagged(const World& w, Tag t) {
  for (size_t i = 1; i < w.creatures.size(); ++i) {
    if (w.creatures[i].tag == t) return static_cast<int>(i);
  }
  return -1;
}

// A monster to fill a team with.
Monster any_monster(Species s = Species::Slime, uint16_t level = 5) {
  Monster m;
  m.species = s;
  m.level = level;
  m.hp = monster_fight_stats(m).hp;
  return m;
}

}  // namespace

TEST_CASE("the west: its places on solid ground, apart, beyond the valley; the road, the stream and the bridge") {
  for (uint64_t seed = 0; seed < kManySeeds; ++seed) {
    CAPTURE(seed);
    const World& w = cached_game(seed);
    const WestLayout& v = w.west;
    REQUIRE(v.valid());
    struct P {
      Vec2 at;
      float r;
    };
    const P places[] = {{v.farm, rules::kFarmRadius}, {v.mill, rules::kMillRadius + 4}, {v.pasture, rules::kPastureRadius},
                        {v.burrow, rules::kBurrowRadius}, {v.lighthouse, rules::kLighthouseRadius},
                        {v.menhirs, rules::kMenhirsRadius}, {v.crater, rules::kCraterRim}};
    for (size_t i = 0; i < std::size(places); ++i) {
      const P& p = places[i];
      CAPTURE(i);
      CHECK_FALSE(w.ground.sea(p.at.x, p.at.z));
      CHECK(std::max(std::fabs(p.at.x), std::fabs(p.at.z)) > rules::kValleyHalfSize);
      CHECK(std::fabs(p.at.x) < rules::kWorldHalfSize - p.r);
      for (size_t j = i + 1; j < std::size(places); ++j) CHECK(flat(p.at, places[j].at) > p.r + places[j].r);
    }
    // The road: from the path near the start to the lighthouse's door.
    CHECK(flat(v.road.points.front(), {0, rules::kForkZ}) < rules::kPathMaxX + 2.0f);
    CHECK(flat(v.road.points.back(), v.lighthouse) < rules::kLighthouseRadius + 6.0f);
    // The stream goes down all along, under the bridge, to the sea.
    for (size_t k = 1; k < v.stream.level.size(); ++k) CHECK(v.stream.level[k] <= v.stream.level[k - 1]);
    CHECK(distance_to_stream(v.stream, v.bridge.x, v.bridge.z) < 2.0f);
    CHECK(distance_to_road(v.road, v.bridge.x, v.bridge.z) < 0.5f);
    CHECK(w.ground.sea(v.stream.points.back().x, v.stream.points.back().z));
    // Its zones.
    CHECK(zone_at(w, v.farm.x, v.farm.z) == Zone::Farm);
    CHECK(zone_at(w, v.crater.x, v.crater.z) == Zone::Crater);
    CHECK(zone_at(w, v.camp.x, v.camp.z) == Zone::Woods);
  }
}

TEST_CASE("the west's buildings and villagers") {
  const World& w = cached_game(4);
  for (ObjectKind k : {ObjectKind::Farmhouse, ObjectKind::Barn, ObjectKind::Windmill, ObjectKind::Cabin, ObjectKind::Tent,
                       ObjectKind::Campfire, ObjectKind::HollowTree, ObjectKind::Burrow, ObjectKind::Lighthouse,
                       ObjectKind::Meteorite, ObjectKind::Cart, ObjectKind::WaterMill, ObjectKind::HiddenCave}) {
    CAPTURE(object_kind_name(k));
    CHECK(count_kind(w, k) == 1);
  }
  CHECK(count_kind(w, ObjectKind::Bridge) == 2);  // the road's and the path's, over the river
  CHECK(count_kind(w, ObjectKind::FruitTree) == 16);
  CHECK(count_kind(w, ObjectKind::Menhir) >= 8);
  Vec3 t;
  CHECK(teleporter_position(w, TeleporterId::Farm, t));
  CHECK(teleporter_position(w, TeleporterId::Menhirs, t));
  for (uint8_t n = npc::kAnselme; n <= npc::kLast; ++n) {
    CAPTURE(int{n});
    const int i = npc_index(w, n);
    REQUIRE(i > 0);
    const Creature& c = w.creatures[static_cast<size_t>(i)];
    CHECK_FALSE(inside_any_object(w, c.body, species_info(Species::Villager).shape));
  }
  // Each one where it belongs.
  CHECK(flat({w.creatures[static_cast<size_t>(npc_index(w, npc::kCapucine))].body.pos.x,
              w.creatures[static_cast<size_t>(npc_index(w, npc::kCapucine))].body.pos.z}, w.west.farm) < rules::kFarmRadius);
  CHECK(flat({w.creatures[static_cast<size_t>(npc_index(w, npc::kAnselme))].body.pos.x,
              w.creatures[static_cast<size_t>(npc_index(w, npc::kAnselme))].body.pos.z}, w.west.cart) < 5.0f);
}

TEST_CASE("the merchant: his bundles found, his cart stands again, he sells mana potions") {
  World w = cached_game(5);
  go_to_npc(w, npc::kAnselme);
  CHECK((npc_marks(w, npc::kAnselme) & kMarkNewQuest) != 0);
  step(w, command(Command::QuestAccept, static_cast<uint16_t>(QuestId::Merchant)));
  REQUIRE(quest(w, QuestId::Merchant).stage == QuestStage::Active);
  CHECK_FALSE(quest_ready(w, QuestId::Merchant));
  for (const Vec2 p : goods_spots(w)) {
    put_player(w, p);
    for (int k = 0; k < 8; ++k) step(w, Input{});
  }
  CHECK(w.bag[static_cast<size_t>(Item::Goods)] == rules::kGoods);
  CHECK(quest_ready(w, QuestId::Merchant));
  // Quest items stay in the bag.
  step(w, command(Command::Discard, static_cast<uint16_t>(Item::Goods)));
  CHECK(w.bag[static_cast<size_t>(Item::Goods)] == rules::kGoods);
  go_to_npc(w, npc::kAnselme);
  step(w, command(Command::QuestComplete, static_cast<uint16_t>(QuestId::Merchant)));
  CHECK(quest(w, QuestId::Merchant).stage == QuestStage::Done);
  CHECK(w.bag[static_cast<size_t>(Item::Goods)] == 0);
  CHECK(w.bag[static_cast<size_t>(Item::Coin)] == rules::kMerchantCoins);
  CHECK((npc_marks(w, npc::kAnselme) & kMarkShop) != 0);
  // Buying: a potion for its price; nothing without the coins.
  const uint32_t potions = w.bag[static_cast<size_t>(Item::ManaPotion)];
  step(w, command(Command::Buy, static_cast<uint16_t>(Item::ManaPotion)));
  CHECK(w.bag[static_cast<size_t>(Item::ManaPotion)] == potions + 1);
  CHECK(w.bag[static_cast<size_t>(Item::Coin)] == rules::kMerchantCoins - rules::kPotionPrice);
  w.bag[static_cast<size_t>(Item::Coin)] = 3;
  step(w, command(Command::Buy, static_cast<uint16_t>(Item::ManaPotion)));
  CHECK(w.bag[static_cast<size_t>(Item::ManaPotion)] == potions + 1);
  // Drinking one fills the mana, up to its most.
  w.wizard.mana = 2;
  step(w, command(Command::UseItem, static_cast<uint16_t>(Item::ManaPotion)));
  CHECK(w.wizard.mana == std::min<uint16_t>(2 + rules::kPotionMana, wizard_stats(w.wizard).mana));
  CHECK(w.bag[static_cast<size_t>(Item::ManaPotion)] == potions);
  // Throwing away: some, or all.
  w.bag[static_cast<size_t>(Item::Coin)] = 10;
  step(w, command(Command::Discard, static_cast<uint16_t>(Item::Coin) + 256 * 4));
  CHECK(w.bag[static_cast<size_t>(Item::Coin)] == 6);
  step(w, command(Command::Discard, static_cast<uint16_t>(Item::Coin)));
  CHECK(w.bag[static_cast<size_t>(Item::Coin)] == 0);
}

TEST_CASE("the main quest goes on: the strange stone, the transfer to the farm, the orchard's raiders") {
  World w = cached_game(6);
  quest(w, QuestId::Main) = {QuestStage::Done, kMainPart1Done};
  // Isidore sends the stone.
  go_to_npc(w, npc::kMentor);
  CHECK(npc_marks(w, npc::kMentor) == kMarkNewQuest);
  step(w, command(Command::QuestAccept, static_cast<uint16_t>(QuestId::Delivery)));
  REQUIRE(quest(w, QuestId::Delivery).stage == QuestStage::Active);
  CHECK(w.bag[static_cast<size_t>(Item::StrangeStone)] == 1);
  step(w, command(Command::Discard, static_cast<uint16_t>(Item::StrangeStone)));
  CHECK(w.bag[static_cast<size_t>(Item::StrangeStone)] == 1);  // a quest's: kept
  CHECK((npc_marks(w, npc::kCapucine) & kMarkQuest) != 0);
  step(w, command(Command::QuestComplete, static_cast<uint16_t>(QuestId::Delivery)));  // not to Isidore
  CHECK(quest(w, QuestId::Delivery).stage == QuestStage::Active);
  go_to_npc(w, npc::kCapucine);
  step(w, command(Command::QuestComplete, static_cast<uint16_t>(QuestId::Delivery)));
  CHECK(quest(w, QuestId::Delivery).stage == QuestStage::Done);
  CHECK(w.bag[static_cast<size_t>(Item::StrangeStone)] == 0);
  CHECK(farm_open(w));
  CHECK((npc_marks(w, npc::kCapucine) & kMarkFarm) != 0);

  // The transfer: a monster caught with a full team goes to the farm.
  step(w, command(Command::QuestAccept, static_cast<uint16_t>(QuestId::Transfer)));
  REQUIRE(quest(w, QuestId::Transfer).stage == QuestStage::Active);
  while (w.team.size() < rules::kTeamSize) w.team.push_back(any_monster());
  Creature wild;
  wild.species = Species::Bird;
  wild.level = 2;
  wild.body.pos = w.player.pos + Vec3{3, 0, 0};
  wild.body.pos.y = w.ground.height(wild.body.pos.x, wild.body.pos.z);
  const size_t index = add_creature(w, wild);
  REQUIRE(active_monster(w));
  w.team[static_cast<size_t>(w.active)].hp = monster_fight_stats(w.team[static_cast<size_t>(w.active)]).hp;
  step(w, command(Command::Engage, static_cast<uint16_t>(index)));
  REQUIRE(w.battle.phase == BattlePhase::Fight);
  w.battle.foes[0].hp = 1;
  CHECK(can_capture(w));
  REQUIRE(seal(w));
  CHECK(w.team.size() == rules::kTeamSize);
  REQUIRE(w.farm.size() == 1);
  CHECK(w.farm[0].species == Species::Bird);
  CHECK((quest(w, QuestId::Transfer).progress & kSentToFarm) != 0);
  // Its monsters graze in the paddock (not saved).
  go_to_npc(w, npc::kCapucine);
  CHECK(first_tagged(w, Tag::Boarder) > 0);
  // Swapped with Capucine: one of the team's for the farm's.
  const Species before = w.team[1].species;
  step(w, command(Command::FarmSwap, static_cast<uint16_t>(1 + 8 * 0)));
  CHECK(w.team[1].species == Species::Bird);
  CHECK(w.farm[0].species == before);
  CHECK(quest_ready(w, QuestId::Transfer));
  // Left there, and taken back.
  step(w, command(Command::FarmSwap, static_cast<uint16_t>(2 + 8 * rules::kFarmEmpty)));
  CHECK(w.team.size() == rules::kTeamSize - 1);
  CHECK(w.farm.size() == 2);
  step(w, command(Command::FarmSwap, static_cast<uint16_t>(rules::kTeamNone + 8 * 1)));
  CHECK(w.team.size() == rules::kTeamSize);
  CHECK(w.farm.size() == 1);
  step(w, command(Command::QuestComplete, static_cast<uint16_t>(QuestId::Transfer)));
  CHECK(quest(w, QuestId::Transfer).stage == QuestStage::Done);

  // The orchard's raiders: aggressive, they attack as the player comes close.
  step(w, command(Command::QuestAccept, static_cast<uint16_t>(QuestId::Orchard)));
  REQUIRE(quest(w, QuestId::Orchard).stage == QuestStage::Active);
  CHECK(raiders_left(w) == rules::kRaiders);
  put_player(w, orchard_center(w.west) + Vec2{30, 0});
  const int raider = first_tagged(w, Tag::Raider);
  REQUIRE(raider > 0);
  CHECK(owned(w, w.creatures[static_cast<size_t>(raider)]) == false);
  const Vec3 r = w.creatures[static_cast<size_t>(raider)].body.pos;
  put_player(w, {r.x + 2.0f, r.z});
  step(w, Input{});
  REQUIRE(w.battle.phase == BattlePhase::Fight);
  CHECK(w.battle.forced);
  // Lost: the magician is sent back to the farm.
  for (Monster& m : w.team) m.hp = 0;
  for (int k = 0; k < 3 && w.battle.phase != BattlePhase::None; ++k) step(w, Input{});
  CHECK(w.battle.phase == BattlePhase::None);
  CHECK(w.rescue_tick > 0);
  CHECK(flat({w.player.pos.x, w.player.pos.z}, farm_rescue_spot(w.west)) < 1.0f);
  CHECK(w.active == -1);
  // Healed by Violette, then the raiders beaten one by one.
  go_to_npc(w, npc::kViolette);
  step(w, command(Command::HealTeam, npc::kViolette));
  CHECK(w.team[0].hp == monster_fight_stats(w.team[0]).hp);
  for (uint32_t k = 0; k < rules::kRaiders; ++k) {
    Creature c;
    c.tag = Tag::Raider;
    c.species = Species::Raccoon;
    raider_down(w, c);
  }
  CHECK(quest_ready(w, QuestId::Orchard));
  go_to_npc(w, npc::kCapucine);
  step(w, command(Command::QuestComplete, static_cast<uint16_t>(QuestId::Orchard)));
  CHECK(quest(w, QuestId::Orchard).stage == QuestStage::Done);
  CHECK((quest(w, QuestId::Main).progress & kMainPart2Done) != 0);
}

TEST_CASE("fleeing an aggressive monster sends the magician back to the farm too") {
  World w = cached_game(7);
  quest(w, QuestId::Transfer).stage = QuestStage::Done;
  quest(w, QuestId::Orchard).stage = QuestStage::Active;
  put_player(w, orchard_center(w.west) + Vec2{30, 0});
  put_player(w, orchard_center(w.west) + Vec2{25, 0});
  const int raider = first_tagged(w, Tag::Raider);
  REQUIRE(raider > 0);
  const Vec3 r = w.creatures[static_cast<size_t>(raider)].body.pos;
  put_player(w, {r.x + 2.0f, r.z});
  step(w, Input{});
  REQUIRE(w.battle.forced);
  step(w, command(Command::Flee));
  for (int k = 0; k < 10 * rules::kTicksPerSecond && w.battle.phase != BattlePhase::None; ++k) step(w, Input{});
  CHECK(w.battle.phase == BattlePhase::None);
  CHECK(flat({w.player.pos.x, w.player.pos.z}, farm_rescue_spot(w.west)) < 1.0f);
  // A moment of peace after it.
  CHECK(w.battle.phase == BattlePhase::None);
}

TEST_CASE("the mill: an air monster's throw into its sails, in training, starts them again") {
  World w = cached_game(8);
  go_to_npc(w, npc::kLucien);
  step(w, command(Command::QuestAccept, static_cast<uint16_t>(QuestId::Mill)));
  REQUIRE(quest(w, QuestId::Mill).stage == QuestStage::Active);
  Vec3 hub;
  Vec2 facing;
  REQUIRE(mill_sails(w, hub, facing));
  CHECK_FALSE(mill_turning(w));
  // Not a wind monster: nothing.
  CHECK_FALSE(push_sails(w, hub + Vec3{facing.x * 3.0f, 0, facing.z * 3.0f}));
  CHECK(push_sails(w, hub + Vec3{facing.x * 0.3f, -2.0f, facing.z * 0.3f}));
  CHECK(mill_turning(w));
  CHECK(quest_ready(w, QuestId::Mill));
  step(w, command(Command::QuestComplete, static_cast<uint16_t>(QuestId::Mill)));
  CHECK(quest(w, QuestId::Mill).stage == QuestStage::Done);
  CHECK(w.bag[static_cast<size_t>(Item::Coin)] == rules::kMillCoins);
}

TEST_CASE("the mill: Hulou's gust thrown while training") {
  World w = cached_game(8);
  quest(w, QuestId::Mill).stage = QuestStage::Active;
  Vec3 hub;
  Vec2 facing;
  REQUIRE(mill_sails(w, hub, facing));
  // An owl out of the crystal, in front of the sails, flying at their height.
  Monster owl = any_monster(Species::Owl, 6);
  w.team.push_back(owl);
  w.player.pos = {hub.x + facing.x * 7.0f, w.ground.height(hub.x + facing.x * 7.0f, hub.z + facing.z * 7.0f),
                  hub.z + facing.z * 7.0f};
  step(w, command(Command::Summon, static_cast<uint16_t>(w.team.size() - 1)));
  REQUIRE(active_monster(w));
  REQUIRE(active_monster(w)->species == Species::Owl);
  step(w, command(Command::Train));
  REQUIRE(w.battle.phase == BattlePhase::Training);
  step(w, command(Command::Target, kNoTarget));
  Creature& c = w.creatures[0];
  c.body.pos = {hub.x + facing.x * 6.0f, hub.y - 1.0f, hub.z + facing.z * 6.0f};
  c.body.facing_x = -facing.x;
  c.body.facing_z = -facing.z;
  // Its throw straight at the sails.
  Projectile p;
  p.pos = c.body.pos;
  p.dir_x = -facing.x;
  p.dir_z = -facing.z;
  p.ticks = 60;
  p.from_ally = true;
  p.element = Element::Air;
  w.battle.projectiles.push_back(p);
  for (int k = 0; k < 40 && !mill_turning(w); ++k) {
    c.body.pos = {hub.x + facing.x * 6.0f, hub.y - 1.0f, hub.z + facing.z * 6.0f};
    step(w, Input{});
  }
  CHECK(mill_turning(w));
}

TEST_CASE("the shepherd's lambs follow the player back to the pen; a Laini of his flock for it") {
  World w = cached_game(9);
  go_to_npc(w, npc::kBasile);
  CHECK(first_tagged(w, Tag::Flock) > 0);
  step(w, command(Command::QuestAccept, static_cast<uint16_t>(QuestId::Shepherd)));
  REQUIRE(quest(w, QuestId::Shepherd).stage == QuestStage::Active);
  for (const Vec2 spot : lamb_spots(w)) {
    put_player(w, spot);
    int lamb = -1;
    for (size_t i = 1; i < w.creatures.size(); ++i) {
      if (w.creatures[i].tag == Tag::Lamb && flat({w.creatures[i].body.pos.x, w.creatures[i].body.pos.z}, spot) < 8.0f) {
        lamb = static_cast<int>(i);
      }
    }
    REQUIRE(lamb > 0);
    const uint32_t id = w.creatures[static_cast<size_t>(lamb)].id;
    put_player(w, {w.creatures[static_cast<size_t>(lamb)].body.pos.x + 1.5f, w.creatures[static_cast<size_t>(lamb)].body.pos.z});
    step(w, Input{});
    lamb = find_creature(w, id);
    REQUIRE(lamb > 0);
    CHECK(w.creatures[static_cast<size_t>(lamb)].mind == Mind::Follow);
    CHECK(owned(w, w.creatures[static_cast<size_t>(lamb)]));
    // Brought into the pen.
    w.creatures[static_cast<size_t>(lamb)].body.pos = {w.west.pasture.x, w.ground.height(w.west.pasture.x, w.west.pasture.z),
                                                        w.west.pasture.z};
    step(w, Input{});
    lamb = find_creature(w, id);
    REQUIRE(lamb > 0);
    CHECK(w.creatures[static_cast<size_t>(lamb)].tag == Tag::Flock);
  }
  CHECK(quest_ready(w, QuestId::Shepherd));
  const size_t team = w.team.size();
  go_to_npc(w, npc::kBasile);
  step(w, command(Command::QuestComplete, static_cast<uint16_t>(QuestId::Shepherd)));
  CHECK(quest(w, QuestId::Shepherd).stage == QuestStage::Done);
  REQUIRE(w.team.size() == team + 1);
  CHECK(w.team.back().species == Species::Sheep);
  CHECK(w.dex[static_cast<size_t>(Species::Sheep)][0] == Discovery::Captured);
}

TEST_CASE("the shepherd's gift waits for room: a full team, no farm yet") {
  World w = cached_game(9);
  quest(w, QuestId::Shepherd) = {QuestStage::Active, kLambsHome};
  while (w.team.size() < rules::kTeamSize) w.team.push_back(any_monster());
  go_to_npc(w, npc::kBasile);
  step(w, command(Command::QuestComplete, static_cast<uint16_t>(QuestId::Shepherd)));
  CHECK(quest(w, QuestId::Shepherd).stage == QuestStage::Active);
  quest(w, QuestId::Delivery).stage = QuestStage::Done;  // the farm takes it
  step(w, command(Command::QuestComplete, static_cast<uint16_t>(QuestId::Shepherd)));
  CHECK(quest(w, QuestId::Shepherd).stage == QuestStage::Done);
  REQUIRE(w.farm.size() == 1);
  CHECK(w.farm[0].species == Species::Sheep);
}

TEST_CASE("Romane, in the woods, offers battles of her own") {
  World w = cached_game(10);
  go_to_npc(w, npc::kRomane);
  CHECK((npc_marks(w, npc::kRomane) & kMarkFight) != 0);
  REQUIRE(active_monster(w));
  CHECK(can_challenge(w, npc::kRomane));
  step(w, command(Command::Challenge, npc::kRomane));
  REQUIRE(w.battle.phase == BattlePhase::Prepare);
  CHECK(w.battle.rival.npc == npc::kRomane);
  CHECK(w.battle.rival.count == 3);
  CHECK(w.creatures[w.battle.foes[0].index].species == Species::Owl);
}

TEST_CASE("the bag, the farm and the west's quests are saved") {
  World w = cached_game(11);
  w.bag[static_cast<size_t>(Item::Coin)] = 123;
  w.bag[static_cast<size_t>(Item::ManaPotion)] = 4;
  w.farm.push_back(any_monster(Species::Mole, 7));
  w.romane_wins = 2;
  quest(w, QuestId::Delivery).stage = QuestStage::Done;
  quest(w, QuestId::Mill) = {QuestStage::Done, kMillTurning};
  go_to_npc(w, npc::kCapucine);  // the farm's monsters graze close by
  const auto loaded = deserialize(serialize(w));
  REQUIRE(loaded.has_value());
  CHECK(loaded->bag == w.bag);
  CHECK(loaded->farm == w.farm);
  CHECK(loaded->romane_wins == 2);
  CHECK(loaded->quests == w.quests);
  for (const Creature& c : loaded->creatures) CHECK(c.tag != Tag::Boarder);  // drawn again
  size_t west = 0;
  for (const Creature& c : loaded->creatures) west += c.species == Species::Villager && c.npc > npc::kMentor;
  CHECK(west == 9 + 2);  // the water miller too, and the north's
  CHECK(bag_json(w).find("\"coins\":123") != std::string::npos);
  CHECK(farm_json(w).find("\"species\":\"mole\"") != std::string::npos);
}

TEST_CASE("inside the burrow, the hollow tree and the lighthouse") {
  World w = cached_game(12);
  const auto enter = [&](ObjectKind kind) {
    for (size_t k = 0; k < w.objects.size(); ++k) {
      if (w.objects[k].kind != kind) continue;
      const PlaceKind place = kind == ObjectKind::Burrow       ? PlaceKind::Burrow
                              : kind == ObjectKind::HollowTree ? PlaceKind::HollowTree
                              : kind == ObjectKind::Lighthouse ? PlaceKind::Lighthouse
                                                               : PlaceKind::House;
      return enter_place(w, {place, static_cast<uint32_t>(k)});
    }
    return false;
  };
  REQUIRE(enter(ObjectKind::Burrow));
  size_t moles = 0;
  for (const Creature& c : w.creatures) moles += c.species == Species::Mole;
  CHECK(moles >= 4);
  CHECK(zone_at(w, 0, 0) == Zone::Plains);
  // Out through the hole.
  w.player.pos = w.interior.exits[0].at;
  CHECK(use_door(w));
  CHECK_FALSE(inside(w));
  for (const ObjectKind tall : {ObjectKind::HollowTree, ObjectKind::Lighthouse}) {
    CAPTURE(object_kind_name(tall));
    REQUIRE(enter(tall));
    // Up the stair to the landing, out onto the top.
    w.player.pos = w.interior.top;
    const DoorNear d = door_near(w);
    CHECK(d.action == DoorAction::Climb);
    REQUIRE(use_door(w));
    CHECK_FALSE(inside(w));
    const float top = tall == ObjectKind::HollowTree ? rules::kHollowTreeTop : rules::kLighthouseGallery;
    const Object* o = nullptr;
    for (const Object& x : w.objects) {
      if (x.kind == tall) o = &x;
    }
    REQUIRE(o);
    CHECK(w.player.pos.y == doctest::Approx(o->pos.y + top));
    // It stands there (not inside the lantern), and the trapdoor takes it back down.
    CHECK_FALSE(inside_any_object(w, w.player, kPlayerShape));
    CHECK(door_near(w).action == DoorAction::Descend);
    REQUIRE(use_door(w));
    CHECK(inside(w));
    CHECK(w.player.pos.y == doctest::Approx(w.interior.top.y));
    w.player.pos = w.interior.exits[0].at;
    REQUIRE(use_door(w));
  }
  // The farmhouse and the cabin: rooms with furniture.
  REQUIRE(enter(ObjectKind::Farmhouse));
  CHECK(w.place.kind == PlaceKind::House);
  CHECK(w.objects.size() > 5);
}

namespace {
// Walks the whole land, square after square (update_population at each
// spot); `look` is called at each one.
template <typename F>
void walk_land(World& w, F look) {
  const float edge = rules::kWorldHalfSize - 30.0f;
  for (float x = -edge; x <= edge; x += 40.0f) {
    for (float z = -edge; z <= edge; z += 40.0f) {
      put_player(w, {x, z});
      if (look(w)) return;
    }
  }
}
}  // namespace

TEST_CASE("after the orchard, other aggressive monsters roam: a few, never close to a safe place") {
  World w = cached_game(13);
  size_t before = 0;
  walk_land(w, [&](const World& v) {
    for (const Creature& c : v.creatures) before += c.tag == Tag::Raider ? 1 : 0;
    return false;
  });
  CHECK(before == 0);  // the orchard's raiders were not called yet
  quest(w, QuestId::Orchard).stage = QuestStage::Done;
  size_t seen = 0, most = 0;
  walk_land(w, [&](const World& v) {
    size_t now = 0;
    for (const Creature& c : v.creatures) {
      if (c.tag != Tag::Raider || c.species == Species::Villager) continue;
      ++now;
      CHECK(monster_stats(c.species).is_monster);
      CHECK(far_from_safety(v, c.home.x, c.home.z));
    }
    seen += now;
    most = std::max(most, now);
    return false;
  });
  CHECK(seen > 0);
  CHECK(most <= rules::kRoamingRaiders);
  CHECK(far_from_safety(w, farm_rescue_spot(w.west).x, farm_rescue_spot(w.west).z) == false);
}

TEST_CASE("trainers passing by offer battles; aggressive ones start them, and send the magician back to the farm") {
  World w = cached_game(14);
  const auto find = [](const World& v) {
    for (size_t i = 1; i < v.creatures.size(); ++i) {
      if (v.creatures[i].species == Species::Villager && npc::wanderer(v.creatures[i].npc)) return static_cast<int>(i);
    }
    return -1;
  };
  walk_land(w, [&](const World& v) { return find(v) > 0; });
  CHECK(find(w) < 0);  // not before Bastien's lesson
  quest(w, QuestId::Combat).stage = QuestStage::Done;
  walk_land(w, [&](const World& v) { return find(v) > 0; });
  const int t = find(w);
  REQUIRE(t > 0);
  Creature& trainer = w.creatures[static_cast<size_t>(t)];
  const uint8_t n = trainer.npc;
  CHECK_FALSE(in_village(w.village, trainer.body.pos.x, trainer.body.pos.z));
  // Its team, its first monster beside it.
  uint8_t count = 0;
  const auto& team = rival_team(w, n, count);
  CHECK(count >= 1);
  CHECK(count <= kRivalTeamSize);
  bool beside = false;
  for (const Creature& c : w.creatures) beside = beside || (c.tag == Tag::Rival && c.npc == n && c.species == team[0].species);
  CHECK(beside);
  CHECK(creature_json(w, t).find("\"wanderer\":true") != std::string::npos);
  // Not saved: they pass by.
  const auto loaded = deserialize(serialize(w));
  REQUIRE(loaded);
  CHECK(find(*loaded) < 0);

  // A peaceful one: the battle's icon; talked to, a battle; beaten, no more.
  trainer.tag = Tag::None;
  go_to_npc(w, n);
  CHECK((npc_marks(w, n) & kMarkFight) != 0);
  REQUIRE(can_challenge(w, n));
  step(w, command(Command::Challenge, n));
  REQUIRE(w.battle.phase == BattlePhase::Prepare);
  CHECK(w.battle.rival.npc == n);
  CHECK_FALSE(w.battle.forced);
  for (int k = 0; k < 20 * rules::kTicksPerSecond && w.battle.phase == BattlePhase::Prepare; ++k) {
    step(w, command(Command::Ready));
  }
  REQUIRE(w.battle.phase == BattlePhase::Fight);
  for (RivalMonster& m : w.battle.rival.team) m.hp = 0;
  for (Foe& f : w.battle.foes) f.hp = 0;
  for (int k = 0; k < 10 && w.battle.phase != BattlePhase::None; ++k) step(w, Input{});
  CHECK(w.battle.outcome == BattleOutcome::Victory);
  CHECK(w.battle.outcome_xp > 0);
  CHECK(w.creatures[static_cast<size_t>(npc_index(w, n))].beaten);
  CHECK((npc_marks(w, n) & kMarkFight) == 0);
  CHECK_FALSE(can_challenge(w, n));

  // An aggressive one: coming close starts the battle; forfeiting it sends the magician to the farm.
  Creature& again = w.creatures[static_cast<size_t>(npc_index(w, n))];
  again.beaten = false;
  again.tag = Tag::Raider;
  CHECK((npc_marks(w, n) & kMarkFight) == 0);  // the bolt instead
  CHECK_FALSE(can_challenge(w, n));
  for (RivalMonster& m : w.wanderers[n - npc::kWanderer].team) m.hp = 0;
  w.rescue_tick = 0;
  const Vec3 p = again.body.pos;
  put_player(w, {p.x + 2.0f, p.z});
  step(w, Input{});
  REQUIRE(w.battle.phase == BattlePhase::Prepare);
  CHECK(w.battle.forced);
  CHECK(w.battle.rival.npc == n);
  step(w, command(Command::Flee));
  CHECK(w.battle.phase == BattlePhase::None);
  CHECK(w.battle.outcome == BattleOutcome::Forfeit);
  CHECK(flat({w.player.pos.x, w.player.pos.z}, farm_rescue_spot(w.west)) < 1.0f);
}

TEST_CASE("the monsters kept at the farm are cared for: their hit points full") {
  World w = cached_game(11);
  Monster hurt = any_monster(Species::Spark, 7);
  hurt.hp = 1;
  send_to_farm(w, hurt);
  REQUIRE(w.farm.size() == 1);
  CHECK(w.farm[0].hp == monster_fight_stats(w.farm[0]).hp);
  // Loaded hurt (an older save): cared for all the same.
  w.farm[0].hp = 2;
  const auto loaded = deserialize(serialize(w));
  REQUIRE(loaded);
  CHECK(loaded->farm[0].hp == monster_fight_stats(loaded->farm[0]).hp);
}

TEST_CASE("owls fly over the hollow tree's crown of leaves, never through it or its trunk") {
  World w = cached_game(3);
  const size_t tree = static_cast<size_t>(std::find_if(w.objects.begin(), w.objects.end(), [](const Object& o) {
                                            return o.kind == ObjectKind::HollowTree;
                                          }) - w.objects.begin());
  REQUIRE(tree < w.objects.size());
  const Vec3 t = w.objects[tree].pos;
  put_player(w, {t.x + 18.0f, t.z});
  int flights = 0, through = 0;
  for (int tick = 0; tick < 90 * rules::kTicksPerSecond; ++tick) {
    step(w, Input{});
    for (const Creature& c : w.creatures) {
      if (c.species != Species::Owl || c.mind != Mind::Fly) continue;
      ++flights;
      const float r = std::hypot(c.body.pos.x - t.x, c.body.pos.z - t.z), y = c.body.pos.y - t.y;
      const bool trunk = r < 4.4f && y < rules::kHollowTreeTop - 0.05f;
      const bool leaves = r > 4.2f && r < 9.0f && y > 11.0f && y < 16.0f;
      if (trunk || leaves) ++through;
    }
  }
  CHECK(flights > 0);
  CHECK(through == 0);
}

TEST_CASE("the river comes from the Forêt Profonde along the terrace, over its edge by the cave, and out to the sea") {
  for (uint64_t seed : {3ull, 4ull, 42ull}) {
    CAPTURE(seed);
    const World& w = cached_game(seed);
    const WestLayout& v = w.west;
    REQUIRE(v.valid());
    const Stream& st = v.stream;
    const size_t n = st.points.size();
    REQUIRE(st.width.size() == n);
    // From the relief's eastern edge (towards the Forêt Profonde), down all along, to the sea.
    CHECK(st.points.front().x > rules::kReliefHalfSize);
    for (size_t k = 1; k < st.level.size(); ++k) CHECK(st.level[k] <= st.level[k - 1]);
    CHECK(w.ground.sea(st.points.back().x, st.points.back().z));
    // The cave in a spur of the terrace; the river on it, then over its edge
    // by the cave: a real waterfall (its river above it), into a pool.
    CHECK(scarp_side(v, w.cave.center.x, w.cave.center.z) < 0);
    REQUIRE(st.falls > 40);
    const Vec2 above = st.points[st.falls - 30];
    CHECK(scarp_side(v, above.x, above.z) < -5.0f);
    CHECK(st.level[st.falls - 3] > v.cove_level + 5.0f);
    CHECK(flat(v.falls, {w.cave.center.x, w.cave.center.z}) < w.cave.radius + 30.0f);
    CHECK(w.ground.height(v.cove.x, v.cove.z) < v.cove_level - 0.5f);
    CHECK(flat(v.hidden, v.falls) < 7.0f);
    // Under the path's bridge.
    CHECK(distance_to_path(w.path, v.path_bridge.x, v.path_bridge.z) < 0.5f);
    CHECK(distance_to_stream(st, v.path_bridge.x, v.path_bridge.z) < 2.0f);
    // A river, not a ditch: it bends, its width changes, it opens into the sea.
    float length = 0;
    for (size_t k = 1; k < n; ++k) length += flat(st.points[k], st.points[k - 1]);
    CHECK(length > 1.15f * (flat(st.points.front(), v.falls) + flat(v.falls, st.points.back())));
    float narrow = 1e9f, wide = 0;
    for (size_t k = st.falls + 60; k + 200 < n; ++k) {
      narrow = std::min(narrow, st.width[k]);
      wide = std::max(wide, st.width[k]);
    }
    CHECK(wide > 1.4f * narrow);
    CHECK(st.width.back() > 2.5f * st.width[n / 2]);
    // The water mill's wheel in the water, over a deeper race.
    float level = 0;
    const float d = distance_to_stream(st, v.water_wheel.x, v.water_wheel.z, 20.0f, &level);
    size_t seg = 0;
    float t = 0;
    stream_segment(st, v.water_wheel.x, v.water_wheel.z, 20.0f, seg, t);
    CHECK(d < st.width[seg] - 0.2f);
    CHECK(w.ground.height(v.water_wheel.x, v.water_wheel.z) < level - 0.8f);
    // Ploufis along it.
    const Vec2 mid = st.points[st.falls + (n - st.falls) / 3];
    CAPTURE(mid.x);
    CAPTURE(mid.z);
    CHECK(zone_at(w, mid.x, mid.z) == Zone::River);
  }
}

TEST_CASE("behind the waterfall, a hidden way into the crystal cave, and back out there") {
  World w = cached_game(4);
  const int hidden = kind_index(w, ObjectKind::HiddenCave);
  REQUIRE(hidden >= 0);
  const Object h = w.objects[static_cast<size_t>(hidden)];
  // On the ledge in front of it (beside the falling water).
  const Vec2 o = w.west.falls_out;
  w.player.pos = {h.pos.x + o.x * 0.6f, h.pos.y, h.pos.z + o.z * 0.6f};
  const DoorNear d = door_near(w);
  REQUIRE(d.action == DoorAction::Enter);
  CHECK(d.hidden);
  CHECK(d.place == PlaceKind::Cave);
  REQUIRE(use_door(w));
  REQUIRE(w.place.kind == PlaceKind::Cave);
  // The crystal cave itself (the one of its mouth), at its hidden tunnel's end.
  CHECK(w.outdoors.objects[w.place.door].kind == ObjectKind::CaveMouth);
  const Exit* out = nullptr;
  for (const Exit& e : w.interior.exits) {
    if (e.kind == ExitKind::Hidden) out = &e;
  }
  REQUIRE(out);
  CHECK(std::hypot(w.player.pos.x - out->at.x, w.player.pos.z - out->at.z) < 2.5f);
  CHECK(wall_distance(w.interior, w.player.pos.x, w.player.pos.z) > 0.3f);
  // The way back out there: behind the waterfall again.
  w.player.pos = {out->at.x, 0, out->at.z};
  const DoorNear leave = door_near(w);
  REQUIRE(leave.action == DoorAction::Leave);
  CHECK(leave.hidden);
  REQUIRE(use_door(w));
  REQUIRE_FALSE(inside(w));
  CHECK(std::hypot(w.player.pos.x - h.pos.x, w.player.pos.z - h.pos.z) < 2.0f);
  CHECK((w.player.pos.x - h.pos.x) * o.x + (w.player.pos.z - h.pos.z) * o.z > 0.3f);  // out in front of the cliff, not in it
  // And through the cave's mouth, out at the mouth.
  World v = cached_game(4);
  const int mouth = kind_index(v, ObjectKind::CaveMouth);
  REQUIRE(mouth >= 0);
  REQUIRE(enter_place(v, {PlaceKind::Cave, static_cast<uint32_t>(mouth)}));
  CHECK(std::hypot(v.player.pos.x - v.interior.entry.x, v.player.pos.z - v.interior.entry.z) < 0.01f);
}

TEST_CASE("the water mill's wheel: Ploufis jammed it; beaten, it turns, and Mathurin rewards the player") {
  World w = cached_game(6);
  go_to_npc(w, npc::kMathurin);
  CHECK(npc_marks(w, npc::kMathurin) == kMarkNewQuest);
  CHECK_FALSE(wheel_turning(w));
  step(w, command(Command::QuestAccept, static_cast<uint16_t>(QuestId::WaterWheel)));
  REQUIRE(quest(w, QuestId::WaterWheel).stage == QuestStage::Active);
  CHECK(wheel_raiders_left(w) == rules::kWheelRaiders);
  // By the wheel, in the river: aggressive Ploufis.
  put_player(w, wheel_spot(w.west) + Vec2{0, 25});
  size_t ploufis = 0;
  for (const Creature& c : w.creatures) {
    if (c.tag != Tag::Raider || c.species != Species::Axolotl) continue;
    ++ploufis;
    CHECK(flat({c.body.pos.x, c.body.pos.z}, wheel_spot(w.west)) < 10.0f);
  }
  CHECK(ploufis == rules::kWheelRaiders);
  // Beaten one by one (the orchard's quest untouched).
  for (const Creature& c : std::vector<Creature>(w.creatures)) {
    if (c.tag == Tag::Raider && c.species == Species::Axolotl) raider_down(w, c);
  }
  CHECK(quest(w, QuestId::Orchard).progress == 0);
  CHECK(quest_ready(w, QuestId::WaterWheel));
  CHECK(wheel_turning(w));
  const uint32_t coins = w.bag[static_cast<size_t>(Item::Coin)];
  go_to_npc(w, npc::kMathurin);
  step(w, command(Command::QuestComplete, static_cast<uint16_t>(QuestId::WaterWheel)));
  CHECK(quest(w, QuestId::WaterWheel).stage == QuestStage::Done);
  CHECK(w.bag[static_cast<size_t>(Item::Coin)] == coins + rules::kWheelCoins);
  CHECK(wheel_turning(w));
  const auto loaded = deserialize(serialize(w));
  REQUIRE(loaded.has_value());
  CHECK(loaded->quests == w.quests);
}

TEST_CASE("the Home spell, free, once every 5 minutes: back to the farm, out of a cave too, the companion along") {
  World w = cached_game(5);
  REQUIRE(w.west.valid());
  if (w.active < 0) step(w, command(Command::Summon, 0));
  REQUIRE(w.active >= 0);
  w.adventure_spells = {SpellId::Home, SpellId::None, SpellId::None, SpellId::None};
  const Vec2 farm = farm_rescue_spot(w.west);
  // Into the crystal cave, far from the farm.
  const int mouth = kind_index(w, ObjectKind::CaveMouth);
  REQUIRE(mouth >= 0);
  const Object& o = w.objects[static_cast<size_t>(mouth)];
  REQUIRE(enter_place(w, {PlaceKind::Cave, static_cast<uint32_t>(mouth)}));
  REQUIRE(inside(w));
  CHECK(o.kind == ObjectKind::CaveMouth);
  REQUIRE(cannot_cast(w, 0) == nullptr);
  const uint16_t mana = w.wizard.mana;
  step(w, command(Command::Cast, 0));
  CHECK_FALSE(inside(w));
  CHECK(flat({w.player.pos.x, w.player.pos.z}, farm) < 1.0f);
  CHECK(w.wizard.mana == mana);  // free
  CHECK(w.teleport_tick > 0);
  // The companion came along, out of its crystal still.
  REQUIRE(w.active >= 0);
  CHECK(flat({w.creatures[0].body.pos.x, w.creatures[0].body.pos.z}, farm) < 3.0f);
  // Not again before 5 minutes.
  put_player(w, w.west.cart);
  CHECK(std::string(cannot_cast(w, 0)) == "cooldown");
  for (int t = 0; t < 5 * 60 * rules::kTicksPerSecond; ++t) step(w, Input{});
  CHECK(cannot_cast(w, 0) == nullptr);
  step(w, command(Command::Cast, 0));
  CHECK(flat({w.player.pos.x, w.player.pos.z}, farm) < 1.0f);
}

namespace {
// Where the companion goes after a few seconds with the player standing still.
Vec2 companion_after_a_while(World& w) {
  for (int t = 0; t < 4 * rules::kTicksPerSecond; ++t) step(w, Input{});
  return {w.creatures[0].body.pos.x, w.creatures[0].body.pos.z};
}

// How well (the cosine) the companion stands from the player towards `to`.
float towards(const World& w, Vec2 at, Vec2 to) {
  const Vec2 p{w.player.pos.x, w.player.pos.z};
  const float ax = at.x - p.x, az = at.z - p.z, tx = to.x - p.x, tz = to.z - p.z;
  return (ax * tx + az * tz) / (std::hypot(ax, az) * std::hypot(tx, tz) + 1e-6f);
}
}  // namespace

TEST_CASE("the companion helps: it stands towards the nearest lost lamb, the nearest bundle, the way out of the cave") {
  World w = cached_game(9);
  if (w.active < 0) step(w, command(Command::Summon, 0));
  REQUIRE(w.active >= 0);
  // No quest: it follows behind.
  Vec2 goal;
  CHECK_FALSE(companion_guide(w, goal));

  // The shepherd's lambs: from the pen, towards the nearest one.
  quest(w, QuestId::Shepherd).stage = QuestStage::Active;
  put_player(w, w.west.pasture + Vec2{0, 12});
  const Vec2 p{w.player.pos.x, w.player.pos.z};
  const auto spots = lamb_spots(w);
  Vec2 nearest = spots[0];
  for (const Vec2 s : spots) {
    if (flat(s, p) < flat(nearest, p)) nearest = s;
  }
  REQUIRE(companion_guide(w, goal));
  CHECK(flat(goal, nearest) < 10.0f);  // where it waits, or where it wandered off to
  // Far from the pen (at the start, by the village): it follows as usual.
  {
    World away = w;
    put_player(away, {0, 0});
    CHECK_FALSE(companion_guide(away, goal));
    REQUIRE(companion_guide(w, goal));
  }
  const Vec2 at = companion_after_a_while(w);
  REQUIRE(companion_guide(w, goal));  // the lambs around came out meanwhile: maybe another one, nearer
  CHECK(towards(w, at, goal) > 0.9f);
  CHECK(std::fabs(flat(at, {w.player.pos.x, w.player.pos.z}) - rules::kCompanionGuideDistance) < 1.0f);
  // All back in the pen: nothing more to show.
  quest(w, QuestId::Shepherd).progress = kLambsHome;
  CHECK_FALSE(companion_guide(w, goal));
  quest(w, QuestId::Shepherd).stage = QuestStage::Done;

  // The merchant's bundles: the nearest one left; all found, his cart.
  quest(w, QuestId::Merchant).stage = QuestStage::Active;
  put_player(w, w.west.cart + Vec2{3, 3});
  const auto goods = goods_spots(w);
  quest(w, QuestId::Merchant).progress = 1;  // the first one found
  REQUIRE(companion_guide(w, goal));
  const Vec2 q{w.player.pos.x, w.player.pos.z};
  CHECK(flat(goal, flat(goods[1], q) < flat(goods[2], q) ? goods[1] : goods[2]) < 0.01f);
  const Vec2 by_cart = companion_after_a_while(w);
  REQUIRE(companion_guide(w, goal));
  CHECK(towards(w, by_cart, goal) > 0.9f);
  quest(w, QuestId::Merchant).progress = kGoodsFound;
  REQUIRE(companion_guide(w, goal));
  CHECK(flat(goal, w.west.cart) < 0.01f);
  {
    World away = w;
    put_player(away, w.west.cart + Vec2{rules::kGoodsGuideRange + 5, 0});
    CHECK_FALSE(companion_guide(away, goal));
  }
  quest(w, QuestId::Merchant).stage = QuestStage::Done;
  CHECK_FALSE(companion_guide(w, goal));

  // In the crystal cave: ahead along the way out, round its walls.
  const int mouth = kind_index(w, ObjectKind::CaveMouth);
  REQUIRE(mouth >= 0);
  REQUIRE(enter_place(w, {PlaceKind::Cave, static_cast<uint32_t>(mouth)}));
  const RoomPiece* far = nullptr;
  float farthest = 0;
  for (const RoomPiece& r : w.interior.pieces) {
    const float d = std::hypot(r.a.x, r.a.z);
    if (r.a == r.b && d > farthest) {
      farthest = d;
      far = &r;
    }
  }
  REQUIRE(far != nullptr);
  w.player.pos = {far->a.x, 0, far->a.z};
  w.creatures.resize(1);  // no Cristi in the way
  w.creatures[0].body.pos = w.player.pos;
  REQUIRE(companion_guide(w, goal));
  Vec2 dir;
  REQUIRE(way_out(w, dir));
  CHECK(flat(goal, {w.player.pos.x, w.player.pos.z}) > 1.5f);
  CHECK(flat(goal, {w.player.pos.x, w.player.pos.z}) < rules::kCompanionGuideDistance + 1.0f);
  const Vec2 in_cave = companion_after_a_while(w);
  CHECK(towards(w, in_cave, goal) > 0.8f);
  CHECK(wall_distance(w.interior, in_cave.x, in_cave.z) > 0.0f);
  // In a house: it follows as usual.
  w = cached_game(9);
  for (size_t k = 0; k < w.objects.size(); ++k) {
    if (w.objects[k].kind != ObjectKind::HouseCottage) continue;
    REQUIRE(enter_place(w, {PlaceKind::House, static_cast<uint32_t>(k)}));
    break;
  }
  REQUIRE(inside(w));
  CHECK_FALSE(companion_guide(w, goal));
}

TEST_CASE("the lost lambs stay close enough to the pen, the bundles to the cart, for the companion to show them") {
  for (uint64_t seed = 0; seed < kManySeeds; ++seed) {
    CAPTURE(seed);
    const World& w = cached_game(seed);
    REQUIRE(w.west.valid());
    // A lost lamb wanders around where it waits (rules::kWanderRadius, a square).
    for (const Vec2 s : lamb_spots(w)) CHECK(flat(s, w.west.pasture) + rules::kWanderRadius * 1.5f < rules::kLambGuideRange);
    for (const Vec2 s : goods_spots(w)) CHECK(flat(s, w.west.cart) + 5.0f < rules::kGoodsGuideRange);
  }
}
