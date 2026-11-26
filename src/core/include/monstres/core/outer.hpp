#pragma once

#include "monstres/core/continent.hpp"
#include "monstres/core/world.hpp"

#include <algorithm>
#include <vector>

namespace monstres {

// The continent beyond the relief (the ground: heightmap.hpp's tiles): its
// nature and its monsters, by tile, made when asked and the same whatever
// was asked before, two tiles meeting without a seam.
//
// The nature: on the world's cells of 5 m (not the tile's), a draw from the
// cell's place alone puts something there or not, anywhere in it (never in
// rows). How much grows: the region's (read a little aside, through a warped
// field: the regions' borders wind), by big patches (woods and clearings),
// then by clumps. What grows: the region's palette (pines and rocks up the
// mountains, few up high or on the volcano, sparse rocks and bushes in the
// desert, woods thick in the forest, bushes by the marsh), in sizes that
// vary. Nothing in the water (but seaweed and rocks on its bottom), nor on
// slopes too steep for it. The same within the playable world (world_nature:
// drawn once, World::objects) as beyond it, but in its valley, its places,
// its forest and woods, on its ways. Two things in each other's way: the one drawn
// first by their cells' hash stays (its neighbours worked out the same way:
// no order).
//
// The monsters: drawn by tile (and the times it was visited), by region,
// stronger the further from the valley; they live in the world when the
// player comes near their tile (update_population), beyond the playable
// ground (its squares' monsters within). Not saved: drawn again.

namespace rules {
inline constexpr float kOuterCell = 5.0f;       // one try for something to grow per square of 5 m
inline constexpr size_t kOuterTilesKept = 160;  // tiles of nature kept at most
inline constexpr uint32_t kOuterMonsterTries = 8;  // a tile's monsters: so many tries
}  // namespace rules

// The region at (x, z), read through a warped field: borders that wind.
Region outer_region(uint64_t seed, float x, float z);

// How likely a square of 5 m at (x, z) is to have a tree, a rock... (the
// map draws the continent's woods with it).
float outer_nature(const World& world, float x, float z);

// The map's picture (continent_map.cpp), one painter for all of it, the
// playable world as the rest of the continent: kCell meters a pixel over the
// continent's grid, drawn from the ground as the player walks on it (its
// folds, the sea, the lakes, the rivers along their smooth course, the ways,
// its nature: woods, rocks, snow). RGBA, rows j0 to j1; alpha: 1 the sea
// (and the ice, the lava: no season's tint), 2 a lake or a river, else 3 +
// 252 times how green it is (the seasons' tints, the map's).
namespace continent_map {
inline constexpr float kCell = 4.0f;
inline constexpr float kDetailSize = rules::kTileCells * 2.0f;  // a tile of the ground (128 m)...
inline constexpr int kDetail = 128;                            // ...a meter a pixel
inline constexpr int kWidth = static_cast<int>((Continent::kMaxX - Continent::kMinX) / kCell);
inline constexpr int kHeight = static_cast<int>((Continent::kMaxZ - Continent::kMinZ) / kCell);
}  // namespace continent_map
void continent_map_rows(const World& world, const Continent& continent, int j0, int j1, std::vector<uint8_t>& rgba);
// Zoomed in: a tile of the ground (ti, tj: as outer_tile_of; anywhere) a
// meter a pixel, in the same colors (its things: the world's objects and
// outer_objects, drawn over it). kDetail x kDetail pixels, rows from the north.
void map_detail(const World& world, int ti, int tj, std::vector<uint8_t>& rgba);

// The playable world's own nature (beyond its valley, its places, its
// forest and woods: World::objects, generate_wilds), drawn as the
// continent's, cell by cell, the same way on both sides of its edge.
std::vector<Object> world_nature(const World& world);

// The tile (heightmap.hpp's lattice) that (x, z) is in.
void outer_tile_of(float x, float z, int& ti, int& tj);
// What grows on a tile (empty all within the world: its own).
const std::vector<Object>& outer_objects(const World& world, int ti, int tj);
// Whether `area` may touch something of the continent's (it grows 3 m
// beyond the world at least, the biggest a few meters wide).
inline bool reaches_outer(const Box& area) {
  return std::min({rules::world_inset(area.min.x, area.min.z), rules::world_inset(area.max.x, area.min.z),
                   rules::world_inset(area.min.x, area.max.z), rules::world_inset(area.max.x, area.max.z)}) < 10.0f;
}
// Each of them whose box may touch `area` (the tiles it covers made if need be).
template <typename F>
void for_outer_objects_near(const World& world, const Box& area, F f) {
  int ti0, tj0, ti1, tj1;
  outer_tile_of(area.min.x, area.min.z, ti0, tj0);
  outer_tile_of(area.max.x, area.max.z, ti1, tj1);
  for (int ti = ti0; ti <= ti1; ++ti) {
    for (int tj = tj0; tj <= tj1; ++tj) {
      for (const Object& o : outer_objects(world, ti, tj)) {
        const Box b = object_box(o);
        if (b.max.x >= area.min.x && b.min.x <= area.max.x && b.max.z >= area.min.z && b.min.z <= area.max.z) f(o, b);
      }
    }
  }
}

// Every solid box that may touch `area`: the world's objects and, outside
// (World::ground not empty) beyond the relief, the continent's.
template <typename F>
void for_solid_near(const World& world, const Box& area, F f) {
  for_objects_near(world, area, [&](uint32_t, const Box& b) { f(b); });
  if (world.ground.empty() || !reaches_outer(area)) return;
  for_outer_objects_near(world, area, [&](const Object&, const Box& b) { f(b); });
}

// The monsters of a tile, drawn anew for its `visit` (not added to the world).
std::vector<Creature> outer_creatures(const World& world, int ti, int tj, uint32_t visit);
// Which species live in a region (none: the open sea, the capital), and the
// zone their variants are drawn from.
std::vector<Species> outer_species(Region region);
Zone outer_zone(Region region);
// How strong the monsters are at (x, z): further from the valley, stronger.
uint8_t outer_level(float x, float z);

}  // namespace monstres
