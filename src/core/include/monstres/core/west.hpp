#pragma once

#include "monstres/core/world.hpp"

#include <array>

namespace monstres {

// The west: between the valley and the sea. A road leaves the path near the
// start and goes west to the lighthouse on the coast; a river comes down
// from the Forêt Profonde in the east (the continent's higher ground), crosses
// the path under a bridge north of the cave's hill, runs along the hill's foot
// (a cove there, a waterfall down the cliff, and behind it a hidden way into
// the crystal cave), then west, under the road's bridge, and on to the sea.
// Along them (WestLayout, derived from the seed, see make_west_layout):
//
// - the merchant's cart, overturned by the road: Anselme lost his goods
//   (QuestId::Merchant, three bundles lying around); once they are back, the
//   cart stands again and he sells (Command::Buy);
// - the farm: Capucine's house, her barn, a paddock where the monsters left
//   with her graze, an orchard and fields; Violette, the farm's healer. The
//   strange stone Isidore sends there (QuestId::Delivery) is a transfer
//   stone: from then on, a monster caught with a full team goes to the farm
//   (World::farm); Capucine swaps the team's monsters with the farm's
//   (Command::FarmSwap; QuestId::Transfer: try it). Then raiders come for
//   the orchard (QuestId::Orchard): aggressive Chipous (Tag::Raider);
// - the mill on its knoll: its sails stopped (QuestId::Mill): an air
//   monster's throw at them, in training, starts them again;
// - the shepherd's pen and his flock of Laini; three lambs went astray
//   (QuestId::Shepherd): found, they follow the player back to the pen;
// - the burrow (inside: tunnels, and the Taupis who dug them);
// - the woods: the woodcutter's cabin, Romane's camp (a trainer), and the
//   giant hollow tree, whose crown the Hulous perch on;
// - the lighthouse; the circle of standing stones (a teleporter in its
//   middle); the crater where a meteorite fell.
//
// Aggressive monsters (Tag::Raider) force a battle when the player comes
// close (rules::kAggroDistance); losing it, or fleeing it, sends the
// magician back to the farm (rescue): the team stays as it is (Violette
// heals it). Once the orchard's raiders are all beaten, other aggressive
// monsters roam the land now and then (roaming_raider): a few at most, never
// close to the farm, the village or a teleporter (no battle again and again
// on coming back).

namespace rules {
inline constexpr float kRoadHalfWidth = 1.35f;
inline constexpr float kForkZ = -6.0f;            // the road leaves the path here
inline constexpr float kStreamWater = 2.6f;       // a usual half width of the river's water (Stream::width: each point's)
inline constexpr float kStreamChannel = 7.0f;     // a usual half width of its bed and banks
inline constexpr float kBridgeHalf = 8.4f;        // half length of a bridge over it
inline constexpr float kWaterMillUpstream = 110.0f;  // the water mill: this far up the river from the path's bridge
inline constexpr float kTerraceHeight = 8.5f;     // the terrace inland, above the valley (more or less)
inline constexpr float kFarmRadius = 36.0f;       // the farm's flat ground
inline constexpr float kMillRadius = 7.0f, kMillKnoll = 22.0f, kMillRise = 3.5f;
inline constexpr float kPastureRadius = 16.0f;
inline constexpr float kPenHalf = 5.0f;           // the pen: a square of fences
inline constexpr float kBurrowRadius = 8.0f;
inline constexpr float kWoodsRadiusX = 44.0f, kWoodsRadiusZ = 52.0f;
inline constexpr float kCampRadius = 9.0f, kHollowClearing = 9.5f, kCabinClearing = 7.0f;
inline constexpr float kLighthouseRadius = 8.0f, kLighthouseRise = 3.0f;
inline constexpr float kMenhirsRadius = 13.0f, kMenhirRing = 9.0f;
inline constexpr float kCraterRadius = 15.0f, kCraterRim = 23.0f, kCraterDepth = 4.0f;
inline constexpr float kCartFromFork = 62.0f;      // along the road
inline constexpr float kHollowTreeTop = 12.0f;     // its crown, where one stands
inline constexpr float kLighthouseGallery = 12.4f; // its gallery, around the lantern
inline constexpr float kMillHub = 7.0f;            // the sails' hub, above the knoll
inline constexpr float kSailReach = 5.2f;
inline constexpr float kAggroDistance = 6.0f;      // an aggressive monster attacks from this close
inline constexpr uint32_t kRoamingRaiders = 3;     // at most, at once (after the orchard)
inline constexpr uint32_t kRoamingChance = 5;      // percent of the new wild monsters
inline constexpr float kRaiderSafeDistance = 45.0f;  // from the farm's rescue spot, the village, a teleporter
// Trainers passing by (World::wanderers, world.cpp): once Bastien taught the player to fight.
inline constexpr uint32_t kWandererChance = 4;       // percent, each time a square is lived in again
inline constexpr uint32_t kWanderersAtOnce = 2;
inline constexpr uint32_t kAggressiveWanderers = 35;  // percent of them
inline constexpr float kGoodsReach = 1.4f;         // the merchant's bundles are picked up from this close
inline constexpr float kLambFollow = 3.0f;         // a lost lamb follows the player from this close
inline constexpr uint32_t kGoods = 3, kLambs = 3, kFlock = 5, kRaiders = 5, kBoardersShown = 8;
inline constexpr uint16_t kPotionMana = 15;        // a mana potion gives back
inline constexpr uint32_t kPotionPrice = 15;       // coins, at the merchant's
inline constexpr size_t kMaxFarm = 500;            // monsters kept at the farm
inline constexpr uint16_t kFarmEmpty = 8191, kTeamNone = 7;  // Command::FarmSwap's "none"
inline constexpr uint8_t kRaiderLevel = 6, kRaiderLeaderLevel = 8, kLambLevel = 3, kFlockLevel = 5;
// The water mill's wheel (QuestId::WaterWheel): aggressive Ploufis jammed it.
inline constexpr uint32_t kWheelRaiders = 3;
inline constexpr uint8_t kWheelRaiderLevel = 7, kWheelLeaderLevel = 9;
}  // namespace rules

// QuestState::progress of the west's quests.
inline constexpr uint8_t kGoodsFound = 7;       // Merchant: a bit per bundle found
inline constexpr uint8_t kCartUp = 8;           // and the cart stands again (the quest is done)
inline constexpr uint8_t kSentToFarm = 1;       // Transfer: a monster went to the farm
inline constexpr uint8_t kTookFromFarm = 2;     // and one came back from it
inline constexpr uint8_t kLambsHome = 7;        // Shepherd: a bit per lamb back in the pen
inline constexpr uint8_t kMillTurning = 1;      // Mill: the sails turn again
// Orchard: how many raiders were beaten or caught (its progress, up to rules::kRaiders);
// WaterWheel: the same, of the wheel's (up to rules::kWheelRaiders).

// Where the places of the west are (World::west), from the seed, the path and
// the valley's places; empty when the world has no relief.
WestLayout make_west_layout(uint64_t seed, const Path& path, const VillageLayout& village, const RuinsLayout& ruins,
                            const ForestLayout& forest, const CaveLayout& cave, const CastleLayout& castle);

// Distances on the ground to the road's middle, and to the stream's (its
// water's level there too); `limit` when farther than that.
float distance_to_road(const Road& road, float x, float z, float limit = 1e9f);
float distance_to_stream(const Stream& stream, float x, float z, float limit = 1e9f, float* level = nullptr);

bool in_farm(const WestLayout& west, float x, float z, float margin = 0);
bool in_woods(const WestLayout& west, float x, float z, float margin = 0);
float woods_distance(const WestLayout& west, float x, float z);  // 1 at its edge
// The woods' clearings (the camp, the hollow tree, the cabin): no trees.
bool in_woods_clearing(const WestLayout& west, float x, float z, float margin = 0);
bool in_crater(const WestLayout& west, float x, float z, float margin = 0);
// Whether (x, z) belongs to one of the west's places, its road or its stream
// (no trees or rocks drawn at random there).
bool in_west_place(const WestLayout& west, float x, float z, float margin = 0);

// The farm's parts, from its middle and front (u: along its front, to the
// right seen from the road; v: towards the road).
Vec2 farm_point(const WestLayout& west, float u, float v);
Vec2 paddock_center(const WestLayout& west);
Vec2 orchard_center(const WestLayout& west);
// Where the magician is sent back to, at the farm (by Violette).
Vec2 farm_rescue_spot(const WestLayout& west);

// The objects of the west, appended to World::objects (before the wilds'
// trees and rocks, see generate_wilds): its buildings, fences, the bridge,
// the orchard, the woods' clearings, the circle, the crater... and the
// teleporters of the farm and of the circle. Not saved (drawn again at each
// load). The valley's own trees and rocks in their way go first. Returns
// where the west's objects start in World::objects (its size if none).
size_t build_west(World& world);

// The villagers of the west (npc 10 to 17), each at its place, if not there
// yet (new games, and saves from before them).
void add_west_npcs(World& world);

// Where the merchant's bundles lie (rules::kGoods).
std::array<Vec2, rules::kGoods> goods_spots(const World& world);
// Where the lost lambs wait (rules::kLambs).
std::array<Vec2, rules::kLambs> lamb_spots(const World& world);
// The mill's sails: their hub (world space), the way they face (unit, on the
// ground), and whether they turn.
bool mill_sails(const World& world, Vec3& hub, Vec2& facing);
bool mill_turning(const World& world);
// The orchard's raiders still to beat (0 once the quest is done, or not taken).
uint32_t raiders_left(const World& world);
// The terrace's edge (WestLayout::scarp): signed distance to it, positive on
// the valley's side, negative on the terrace.
float scarp_side(const WestLayout& west, float x, float z);
// The river's nearest segment to (x, z) (its first point, and how far along
// it), and the distance to it (`limit` if farther).
float stream_segment(const Stream& stream, float x, float z, float limit, size_t& seg, float& t);
// The water mill: where the aggressive Ploufis that jammed its wheel stay (in
// the river, by the wheel), how many are left, whether the wheel turns again.
Vec2 wheel_spot(const WestLayout& west);
uint32_t wheel_raiders_left(const World& world);
bool wheel_turning(const World& world);
// The transfer stone is at the farm: caught with a full team, a monster goes there.
bool farm_open(const World& world);
// After the orchard: may a new wild monster at (x, z) be an aggressive one
// (`roll`: 0..99, drawn for it)? Not too many, never close to a safe place.
bool roaming_raider(const World& world, float x, float z, uint32_t roll);
// Far enough from where the magician comes back, or arrives (the farm's
// rescue spot, the village, the teleporters) for an aggressive one.
bool far_from_safety(const World& world, float x, float z);

// Creatures kept alive by the west, whenever their square is lived in (from
// update_population): the shepherd's flock, his lost lambs, the orchard's
// raiders, the monsters grazing in the farm's paddock, Romane's monster.
void keep_west(World& world);

// Steps of a tick (from step()): the bundles picked up, the lambs following
// the player into the pen, the aggressive monsters attacking.
void west_step(World& world);
// The battle with an aggressive monster was lost, or fled: the magician goes
// back to the farm, the companion back into the crystal (battle.cpp).
void rescue(World& world);
// A wind monster's throw (training) at (p): the mill's sails turn again if it
// hits them (battle.cpp, the projectiles).
bool push_sails(World& world, Vec3 p);
// A raider beaten or caught: one less for the orchard's quest (battle.cpp).
void raider_down(World& world, const Creature& creature);
// A monster caught while the team is full: to the farm (battle.cpp).
void send_to_farm(World& world, const Monster& monster);
// The monsters kept at the farm are cared for: their hit points full (as
// they arrive, are swapped, are loaded).
void heal_farm(World& world);
// The bag's commands (UseItem, Discard, Buy) and the farm's (FarmSwap).
void west_command(World& world, const Input& input);

// Owls' perches: the hollow tree's crown, the cabin's roof (see crow_perches).
std::vector<Vec3> owl_perches(const World& world);

// An item: whether it belongs to a quest (never thrown away), and its price at the merchant's (0: not sold).
struct ItemInfo {
  const char* id;  // names in web/lore.js
  bool quest;
  uint32_t price;
};
const ItemInfo& item_info(Item item);

}  // namespace monstres
