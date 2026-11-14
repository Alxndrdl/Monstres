// Drawing only what can be seen, and building it ahead of time: culling by
// parts, the landscape by chunks, the ground by tiles.

#include "monstres/render/cull.hpp"
#include "monstres/render/far_terrain.hpp"
#include "monstres/render/scene.hpp"
#include "monstres/render/terrain_mesh.hpp"
#include "test_helpers.hpp"

#include <algorithm>
#include <cmath>
#include <map>

using namespace monstres;
using doctest::Approx;

namespace {

// A view from (x, y, z) towards a yaw, as the game's camera looks.
View view_at(const World& w, float yaw, float pitch = 0.28f) {
  Camera cam;
  cam.yaw = yaw;
  cam.pitch = pitch;
  return build_view(w, cam, 844.0f / 390.0f, 0);
}

bool covered(const std::vector<DrawRange>& ranges, uint32_t i) {
  return std::any_of(ranges.begin(), ranges.end(), [&](const DrawRange& r) { return i >= r.first && i < r.first + r.count; });
}

void load_bounds() {
  std::vector<MeshData> meshes;
  std::string error;
  REQUIRE(load_models(std::string(MONSTRES_ASSETS_DIR) + "/models", meshes, &error));
  set_mesh_bounds(meshes);
}

}  // namespace

TEST_CASE("the frustum keeps what is in front of the camera, not what is behind") {
  const World& w = cached_game(3);
  const View v = view_at(w, 0);  // looking towards -Z
  const Frustum f = frustum_of(v.view_projection);
  const Vec3 p = w.player.pos;
  const auto box = [&](float dz) { return Aabb{p.x - 1, p.y, p.z + dz - 1, p.x + 1, p.y + 2, p.z + dz + 1}; };
  CHECK(in_view(f, box(-20)));   // ahead
  CHECK(in_view(f, box(0)));     // the player
  CHECK_FALSE(in_view(f, box(40)));  // well behind the camera
  CHECK_FALSE(in_view(f, Aabb{p.x + 300, p.y, p.z - 5, p.x + 301, p.y + 2, p.z - 4}));  // far to the side
  CHECK_FALSE(in_view(f, box(-3000)));  // beyond the far plane
  // Merging: neighbors in view make one range, not over a hidden part.
  std::vector<Part> parts = {{{0, 10}, box(-20)}, {{10, 5}, box(40)}, {{15, 10}, box(-25)}, {{25, 4}, box(-30), true},
                             {{29, 3}, box(-35)}};
  std::vector<DrawRange> ranges;
  visible_ranges(parts, f, 8, ranges);
  REQUIRE(ranges.size() == 2);
  CHECK(ranges[0] == DrawRange{0, 25});
  CHECK(ranges[1] == DrawRange{29, 3});
}

TEST_CASE("culling the landscape leaves out only what is out of view, and much of it") {
  load_bounds();
  const World& w = cached_game(4);
  DrawList list;
  build_landscape(w, list);
  REQUIRE(list.parts.size() == kMeshCount);
  for (float yaw : {0.0f, 1.3f, 3.0f, -2.0f}) {
    CAPTURE(yaw);
    const View v = view_at(w, yaw);
    const Frustum f = frustum_of(v.view_projection);
    cull_list(list, f);
    REQUIRE(list.visible.size() == kMeshCount);
    size_t all = 0, drawn = 0;
    for (size_t m = 0; m < kMeshCount; ++m) {
      const auto& batch = list.batches[m];
      const DrawList::Visible& vis = list.visible[m];
      all += batch.size();
      if (vis.all) {
        drawn += batch.size();
        continue;
      }
      for (const DrawRange& r : vis.instances) drawn += r.count;
      for (uint32_t i = 0; i < batch.size(); ++i) {
        // Every instance that may show is drawn.
        if (in_view(f, instance_bounds(static_cast<MeshId>(m), batch[i]))) REQUIRE(covered(vis.instances, i));
      }
    }
    CHECK(drawn < all * 6 / 10);
    CHECK(drawn > all / 10);
  }
}

TEST_CASE("the landscape's parts: by sectors, covering each batch, around what they hold") {
  load_bounds();
  const World& w = cached_game(5);
  DrawList list;
  build_landscape(w, list);
  for (size_t m = 0; m < kMeshCount; ++m) {
    uint32_t next = 0;
    for (const Part& p : list.parts[m]) {
      CHECK(p.range.first == next);
      next += p.range.count;
      for (uint32_t i = p.range.first; i < p.range.first + p.range.count; ++i) {
        const Instance& in = list.batches[m][i];
        CHECK((in.x >= p.bounds.min_x && in.x <= p.bounds.max_x && in.z >= p.bounds.min_z && in.z <= p.bounds.max_z));
      }
    }
    CHECK(next == list.batches[m].size());
    CHECK(list.parts[m].size() <= static_cast<size_t>(landscape::kBins));
  }
}

TEST_CASE("the landscape built from the cache is the one built from scratch, chunks computed ahead of time") {
  const World& w = cached_game(6);
  const Vec2 center = landscape_center(w.player.pos.x, w.player.pos.z);
  DrawList scratch;
  build_landscape(w, scratch, center);
  LandscapeCache cache;
  CHECK_FALSE(cache.ready(w, center));
  int computed = 0, n = 0, frames = 0;
  while ((n = cache.prepare(w, {w.player.pos.x, w.player.pos.z}, 0.5)) > 0) {  // a little each frame
    computed += n;
    REQUIRE(++frames < 2000);
  }
  CHECK(frames > 3);
  CHECK(cache.ready(w, center));  // with some to spare: the next centers' too
  CHECK(cache.ready(w, {center.x + landscape::kStep, center.z - landscape::kStep}));
  CHECK(computed > 50);
  DrawList cached;
  build_landscape(w, cached, center, cache);
  for (size_t m = 0; m < kMeshCount; ++m) {
    REQUIRE(cached.batches[m].size() == scratch.batches[m].size());
    for (size_t i = 0; i < cached.batches[m].size(); ++i) CHECK(cached.batches[m][i].x == scratch.batches[m][i].x);
  }
  // Another step: built again from the same chunks (and a few more).
  DrawList moved;
  build_landscape(w, moved, {center.x + landscape::kStep, center.z}, cache);
  CHECK(moved.version == 1);
  CHECK(moved.batches[static_cast<size_t>(MeshId::GrassShort)].size() > 1000);
  // Grass thins out with the distance.
  const auto& grass = scratch.batches[static_cast<size_t>(MeshId::GrassShort)];
  size_t near = 0, far = 0;
  for (const Instance& g : grass) {
    const float d = std::hypot(g.x - center.x, g.z - center.z);
    near += d < 15;
    far += d > 50 && d < 52.2f;  // the same area as within 15 m
  }
  CHECK(far < near * 8 / 10);
  CHECK(far > near / 4);
}

TEST_CASE("the detailed tiles around the player: 2 m cells on and around its tile, then 4 m; edges stitched to coarser ones") {
  const std::vector<TileKey> keys = detailed_tiles(10, 10);
  CHECK(keys.size() == 49);
  const int pi = tile_of(10), pj = tile_of(10);
  std::map<std::pair<int, int>, int> lod;
  for (const TileKey& k : keys) lod[{k.i, k.j}] = k.lod;
  const auto lod_at = [&](int i, int j) {
    const auto it = lod.find({i, j});
    return it == lod.end() ? 2 : it->second;
  };
  for (const TileKey& k : keys) {
    const int d = std::max(std::abs(k.i - pi), std::abs(k.j - pj));
    CHECK(k.lod == (d <= 1 ? 0 : 1));
    const int sides[4][2] = {{k.i - 1, k.j}, {k.i + 1, k.j}, {k.i, k.j - 1}, {k.i, k.j + 1}};
    for (int s = 0; s < 4; ++s) CHECK(((k.stitch >> s) & 1) == (lod_at(sides[s][0], sides[s][1]) > k.lod ? 1 : 0));
  }
  // At the edge of the relief: fewer, none outside.
  for (const TileKey& k : detailed_tiles(-630, 630)) {
    CHECK(k.i >= 0);
    CHECK(k.j < terrain::kTiles);
  }
  CHECK(detailed_tiles(-630, 630).size() == 16);
}

TEST_CASE("no gap between a detailed tile and a coarser neighbor") {
  const World& w = cached_game(7);
  GroundColors colors;
  const MeshData coarse = build_coarse_ground(w, colors);
  REQUIRE(coarse.parts.size() == static_cast<size_t>(terrain::kTiles * terrain::kTiles));
  // Height of a mesh's edge along x = const (or z = const) at t, from its vertices on that line.
  const auto edge_height = [](const MeshData& m, bool along_z, float at, float t, float& out) {
    float lo_t = -1e9f, hi_t = 1e9f, lo_y = 0, hi_y = 0;
    for (size_t v = 0; v < m.vertices.size(); v += MeshData::kFloatsPerVertex) {
      const float a = along_z ? m.vertices[v] : m.vertices[v + 2], b = along_z ? m.vertices[v + 2] : m.vertices[v];
      if (std::fabs(a - at) > 1e-3f) continue;
      if (b <= t && b > lo_t) lo_t = b, lo_y = m.vertices[v + 1];
      if (b >= t && b < hi_t) hi_t = b, hi_y = m.vertices[v + 1];
    }
    if (lo_t < -1e8f || hi_t > 1e8f) return false;
    out = hi_t - lo_t < 1e-4f ? lo_y : lo_y + (hi_y - lo_y) * (t - lo_t) / (hi_t - lo_t);
    return true;
  };
  for (const TileKey& k : detailed_tiles(w.player.pos.x, w.player.pos.z)) {
    if (k.stitch == 0) continue;
    const MeshData tile = build_ground_tile(w, colors, k);
    const TileKey coarser_side[4] = {{static_cast<int8_t>(k.i - 1), k.j, static_cast<uint8_t>(k.lod + 1), 0},
                                     {static_cast<int8_t>(k.i + 1), k.j, static_cast<uint8_t>(k.lod + 1), 0},
                                     {k.i, static_cast<int8_t>(k.j - 1), static_cast<uint8_t>(k.lod + 1), 0},
                                     {k.i, static_cast<int8_t>(k.j + 1), static_cast<uint8_t>(k.lod + 1), 0}};
    for (int s = 0; s < 4; ++s) {
      if (!((k.stitch >> s) & 1)) continue;
      const TileKey n = coarser_side[s];
      const MeshData neighbor = n.lod >= terrain::kLods - 1 ? coarse : build_ground_tile(w, colors, n);
      const bool along_z = s < 2;  // the shared edge is x = const
      const float at = along_z ? tile_start(s == 0 ? k.i : k.i + 1) : tile_start(s == 2 ? k.j : k.j + 1);
      const float from = tile_start(along_z ? k.j : k.i);
      for (float t = from; t <= from + terrain::kTile; t += 0.5f) {
        float a = 0, b = 0;
        REQUIRE(edge_height(tile, along_z, at, t, a));
        REQUIRE(edge_height(neighbor, along_z, at, t, b));
        CHECK(a == Approx(b).epsilon(1e-4));
      }
    }
  }
}

namespace {
struct Uploads {
  int count = 0;
  size_t max_slot = 0;
};
void count_upload(void* user, size_t slot, const MeshData& mesh) {
  auto* u = static_cast<Uploads*>(user);
  ++u->count;
  u->max_slot = std::max(u->max_slot, slot);
  CHECK(mesh.vertex_count() > 0);
}
}  // namespace

TEST_CASE("the detailed tiles are built ahead, a few at a time, and swapped in all at once") {
  const World& w = cached_game(8);
  GroundColors colors;
  GroundTiles tiles;
  Uploads uploads;
  const Vec2 at{w.player.pos.x, w.player.pos.z};
  CHECK(tiles.update(w, colors, at, 0.0, count_upload, &uploads));  // nothing drawn yet: all now
  CHECK(tiles.drawn().size() == detailed_tiles(at.x, at.z).size());
  CHECK(uploads.count == static_cast<int>(tiles.drawn().size()));
  CHECK_FALSE(tiles.update(w, colors, at, 0.0, count_upload, &uploads));  // the same: nothing to do
  // One tile farther: built one by one (no time given), drawn when all are ready.
  const Vec2 next{at.x + terrain::kTile, at.z};
  const std::vector<int> before = tiles.drawn();
  int frames = 0;
  while (!tiles.update(w, colors, next, 0.0, count_upload, &uploads)) {
    CHECK(tiles.drawn() == before);  // meanwhile, the tiles before
    REQUIRE(++frames < 100);
  }
  CHECK(frames > 3);
  CHECK(tiles.drawn().size() == detailed_tiles(next.x, next.z).size());
  // Walking around for long: the slots are reused.
  for (int k = 0; k < 12; ++k) {
    const Vec2 p{at.x + terrain::kTile * static_cast<float>(k % 5), at.z + terrain::kTile * static_cast<float>(k / 3)};
    while (!tiles.update(w, colors, p, 1e9, count_upload, &uploads)) {}
  }
  CHECK(uploads.max_slot < terrain::kTileSlots);
  // The terrain list: the coarse ground but under the detailed tiles, the tiles.
  DrawList list;
  MeshData coarse = build_coarse_ground(w, colors);
  tiles.fill_list(list, coarse.parts, {});
  size_t hidden = 0;
  for (const Part& p : list.mesh_parts[static_cast<size_t>(MeshId::Ground)]) hidden += p.hidden;
  CHECK(hidden == tiles.drawn().size());
  size_t tile_batches = 0;
  for (size_t i = kMeshCount; i < list.batches.size(); ++i) tile_batches += list.batches[i].size();
  CHECK(tile_batches == tiles.drawn().size());
}

TEST_CASE("the continent is cut into parts by where it is, all of it kept") {
  const Continent k = make_continent(3);
  const MeshData ground = build_far_ground_mesh(k);
  REQUIRE(ground.parts.size() > 24);
  uint32_t next = 0;
  for (const Part& p : ground.parts) {
    CHECK(p.range.first == next);
    next += p.range.count;
    CHECK(p.range.count % 3 == 0);
  }
  CHECK(next == ground.vertex_count());
  // Looking from the playable area: much of it is out of view.
  const World& w = cached_game(3);
  const View v = view_at(w, 0.7f);
  std::vector<DrawRange> ranges;
  visible_ranges(ground.parts, frustum_of(v.far_view_projection), kMergeVertices, ranges);
  uint32_t drawn = 0;
  for (const DrawRange& r : ranges) drawn += r.count;
  CHECK(drawn < ground.vertex_count() * 6 / 10);
  CHECK(drawn > 0);
}
