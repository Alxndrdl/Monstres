// Drawing only what can be seen, and building it ahead of time: culling by
// parts, the landscape by chunks, the ground by tiles.

#include "monstres/render/cull.hpp"
#include "monstres/render/draw_plan.hpp"
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

// Where the tiles test walks last.
Vec2 p_last(Vec2 at) { return {at.x + terrain::kTile * 1, at.z + terrain::kTile * 3}; }

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
  CHECK(moved.batches[static_cast<size_t>(MeshId::GrassShort)].size() +
            moved.batches[static_cast<size_t>(MeshId::GrassShortFar)].size() > 1000);
  // Grass thins out with the distance, simpler beyond kGrassFullRadius.
  for (const Instance& g : scratch.batches[static_cast<size_t>(MeshId::GrassShort)]) {
    CHECK(std::hypot(g.x - center.x, g.z - center.z) <= landscape::kGrassFullRadius + 1e-3f);
  }
  std::vector<Instance> grass = scratch.batches[static_cast<size_t>(MeshId::GrassShort)];
  const auto& simpler = scratch.batches[static_cast<size_t>(MeshId::GrassShortFar)];
  grass.insert(grass.end(), simpler.begin(), simpler.end());
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

TEST_CASE("the detailed tiles are built ahead, a few at a time, and swapped in all at once, in few draw calls") {
  const World& w = cached_game(8);
  GroundColors colors;
  const MeshData coarse = build_coarse_ground(w, colors);
  GroundTiles tiles;
  tiles.reset(coarse);
  const Vec2 at{w.player.pos.x, w.player.pos.z};
  CHECK(tiles.update(w, colors, at, 0.0));  // nothing drawn yet: all now
  CHECK(tiles.drawn() == detailed_tiles(at.x, at.z));
  CHECK_FALSE(tiles.update(w, colors, at, 0.0));  // the same: nothing to do
  // The drawn tiles in one mesh, a part each; the coarse ground's others, all of its triangles but theirs.
  const MeshData& detail = tiles.detail();
  CHECK(detail.parts.size() == tiles.drawn().size());
  size_t expected = 0;
  for (const TileKey& k : tiles.drawn()) expected += build_ground_tile(w, colors, k).indices.size();
  CHECK(detail.indices.size() == expected);
  for (uint32_t i : detail.indices) REQUIRE(i < detail.vertex_count());
  const size_t per_coarse_tile = coarse.indices.size() / coarse.parts.size();
  CHECK(tiles.coarse_indices().size() == coarse.indices.size() - tiles.drawn().size() * per_coarse_tile);
  // One tile farther: built one by one (no time given), drawn when all are ready.
  const Vec2 next{at.x + terrain::kTile, at.z};
  const std::vector<TileKey> before = tiles.drawn();
  int frames = 0;
  while (!tiles.update(w, colors, next, 0.0)) {
    CHECK(tiles.drawn() == before);  // meanwhile, the tiles before
    REQUIRE(++frames < 100);
  }
  CHECK(frames > 3);
  CHECK(tiles.drawn() == detailed_tiles(next.x, next.z));
  // Far away at once (a teleporter): all of them right away.
  CHECK(tiles.update(w, colors, {at.x - 400, at.z + 300}, 0.0));
  CHECK(tiles.drawn() == detailed_tiles(at.x - 400, at.z + 300));
  // Walking around for long: the built tiles kept stay within bounds.
  for (int k = 0; k < 12; ++k) {
    const Vec2 p{at.x + terrain::kTile * static_cast<float>(k % 5), at.z + terrain::kTile * static_cast<float>(k / 3)};
    while (!tiles.update(w, colors, p, 1e9)) {}
  }
  CHECK(tiles.cached() <= terrain::kTileCache);
  // Few draw calls: sorted around the player, the parts in view are mostly next to one another.
  DrawList list;
  tiles.fill_list(list, {}, {});
  for (float yaw : {0.0f, 1.6f, -2.4f}) {
    World look = w;
    look.player.pos = {p_last(at).x, w.ground.height(p_last(at).x, p_last(at).z), p_last(at).z};
    const Frustum f = frustum_of(view_at(look, yaw).view_projection);
    cull_list(list, f);
    CHECK(list.visible[terrain::kDetailMesh].vertices.size() <= 4);
    CHECK(list.visible[static_cast<size_t>(MeshId::Ground)].vertices.size() <= 6);
    CHECK_FALSE(list.visible[static_cast<size_t>(MeshId::Ground)].vertices.empty());
  }
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
  CHECK(next == ground.draw_count());
  // Looking from the playable area: much of it is out of view.
  const World& w = cached_game(3);
  const View v = view_at(w, 0.7f);
  std::vector<DrawRange> ranges;
  visible_ranges(ground.parts, frustum_of(v.far_view_projection), kMergeVertices, ranges);
  uint32_t drawn = 0;
  for (const DrawRange& r : ranges) drawn += r.count;
  CHECK(drawn < ground.draw_count() * 6 / 10);
  CHECK(drawn > 0);
}

TEST_CASE("creatures out of view are not drawn; the player always is") {
  World w = cached_game(3);
  const View v = view_at(w, 0);  // looking towards -Z
  const Frustum f = frustum_of(v.view_projection);
  // Two slimes: one in front, one far behind the camera.
  w.creatures.resize(1);
  w.active = -1;
  for (float dz : {-12.0f, 60.0f}) {
    Creature c;
    c.species = Species::Slime;
    c.body.pos = {w.player.pos.x, w.ground.height(w.player.pos.x, w.player.pos.z + dz), w.player.pos.z + dz};
    w.creatures.push_back(c);
  }
  DrawList all, seen;
  build_dynamic(w, 0, all);
  build_dynamic(w, 0, seen, -1, &f);
  const auto bodies = [](const DrawList& l) { return l.batches[static_cast<size_t>(MeshId::SlimeBody)].size(); };
  CHECK(bodies(all) == 2);
  CHECK(bodies(seen) == 1);
  CHECK(seen.batches[static_cast<size_t>(MeshId::PlayerHead)].size() == 1);
}

TEST_CASE("walking on, the landscape keeps up: never so far behind that grass would be missing ahead") {
  // A run, and much faster than any run (the chunks then cannot keep up):
  // on the slowest device, one chunk computed per frame at most.
  for (const float speed : {6.5f, 40.0f}) {
    CAPTURE(speed);
    World w = cached_game(6);
    LandscapeCache cache;
    DrawList list;
    Vec2 built{1e9f, 1e9f};
    CHECK(follow_player(w, list, cache, built, 0.0, true));
    int rebuilds = 0, waits = 0;
    float worst = 0;
    for (int frame = 0; frame < 8 * 60; ++frame) {
      w.player.pos.x += speed / 60;
      w.player.pos.z += 0.3f * speed / 60;
      const Vec2 before = built;
      rebuilds += follow_player(w, list, cache, built, 0.0);
      waits += built == before && landscape_center(w.player.pos.x, w.player.pos.z) != built;
      worst = std::max(worst, std::hypot(w.player.pos.x - built.x, w.player.pos.z - built.z));
    }
    CHECK(rebuilds > 2);
    CHECK(waits >= 0);  // it may wait for its chunks...
    CHECK(worst < landscape::kGrassRadius - landscape::kGrassFadeEnd);  // ...but never too long
    // And what it built is what building from scratch gives.
    DrawList scratch;
    build_landscape(w, scratch, built);
    for (size_t m = 0; m < kMeshCount; ++m) CHECK(list.batches[m].size() == scratch.batches[m].size());
  }
}

// ---- The plan of the draw calls (draw_plan.hpp) ----------------------------------

namespace {

// Three meshes of a 1 m cube (the second transparent), and a list with
// cubes along +X, 10 m apart: mesh 0 near to far, mesh 2 far to near.
struct PlanScene {
  std::vector<PlanMesh> meshes;
  DrawList list;
  View view;
  PlanScene() {
    for (int i = 0; i < 3; ++i) {
      PlanMesh m;
      m.vertex_count = 36;
      m.transparent = i == 1;
      m.bounds = {-0.5f, 0, -0.5f, 0.5f, 1, 0.5f};
      meshes.push_back(m);
    }
    list.batches.resize(3);
    for (int k = 0; k < 4; ++k) {
      list.batches[0].push_back(make_instance(10.0f * static_cast<float>(k), 0, 0, 0, 1, 1, 1));
      list.batches[1].push_back(make_instance(10.0f * static_cast<float>(k), 0, 5, 0, 1, 1, 1));
      list.batches[2].push_back(make_instance(10.0f * static_cast<float>(3 - k), 0, -5, 0, 1, 1, 1));
    }
    // Each instance its own part: one draw each (culled list, all in view).
    list.parts.resize(3);
    list.visible.resize(3);
    for (size_t i = 0; i < 3; ++i) {
      list.visible[i].all = false;
      for (uint32_t k = 0; k < 4; ++k) {
        const Instance& in = list.batches[i][k];
        list.parts[i].push_back({{k, 1}, {in.x - 0.5f, in.y, in.z - 0.5f, in.x + 0.5f, in.y + 1, in.z + 0.5f}});
        list.visible[i].instances.push_back({k, 1});
      }
    }
    view.camera_x = -5;
    view.camera_y = 1;
    view.camera_z = 0;
  }
  std::vector<DrawItem> plan(bool transparent = false) const {
    const DrawList* lists[] = {&list};
    std::vector<DrawItem> out;
    plan_draws(lists, meshes, false, transparent, view, out);
    return out;
  }
};

}  // namespace

TEST_CASE("draw plan: the draws in mesh order, then by visible range") {
  PlanScene s;
  const auto items = s.plan();
  REQUIRE(items.size() == 8);  // meshes 0 and 2 (1 is transparent), 4 draws each
  for (size_t k = 0; k < items.size(); ++k) {
    CHECK(items[k].mesh == (k < 4 ? 0u : 2u));
    CHECK(items[k].first_instance == k % 4);
    CHECK(items[k].instance_count == 1);
    CHECK(items[k].vertex_count == 36);
    CHECK(items[k].bounded);
  }
  const auto transparent = s.plan(true);
  REQUIRE(transparent.size() == 4);
  CHECK(transparent[0].mesh == 1);
}

TEST_CASE("draw plan: only what is near the camera's line may discard; never the transparent") {
  PlanScene s;
  // From the camera to a point between the first two cubes of mesh 0 (only the first is near).
  s.view.fade_from_x = -5;
  s.view.fade_from_y = 1;
  s.view.fade_to_x = 5;
  s.view.fade_to_y = 1;
  s.view.fade_radius = 1.5f;
  for (const DrawItem& it : s.plan()) CHECK(it.fade == (it.mesh == 0 && it.first_instance == 0));
  for (const DrawItem& it : s.plan(true)) CHECK_FALSE(it.fade);
  s.view.fade_radius = 0;  // nothing fades (portraits)
  for (const DrawItem& it : s.plan()) CHECK_FALSE(it.fade);
}

TEST_CASE("draw plan: each draw gets only the lights that reach it") {
  PlanScene s;
  s.view.light_count = 2;
  s.view.lights[0] = {20, 1, 0, 3, {1, 1, 1, 1}};  // on the third cube of mesh 0
  s.view.lights[1] = {0, 1, -5, 2, {1, 1, 1, 1}};  // on the last cube of mesh 2 (at x 0)
  for (const DrawItem& it : s.plan()) {
    const uint16_t want = it.mesh == 0 && it.first_instance == 2 ? 0b01 : it.mesh == 2 && it.first_instance == 3 ? 0b10 : 0;
    CHECK(it.lights == want);
  }
}

TEST_CASE("draw plan: unculled batches are bounded by their instances; too many, not at all") {
  PlanScene s;
  s.list.parts.clear();
  s.list.visible.clear();
  s.view.light_count = 1;
  s.view.lights[0] = {100, 0, 0, 2, {1, 1, 1, 1}};  // reaches nothing
  s.view.fade_radius = 1;
  auto items = s.plan();
  REQUIRE(items.size() == 2);  // one draw per batch
  CHECK(items[0].bounded);
  CHECK(items[0].bounds.max_x >= 30.5f);
  CHECK(items[0].lights == 0);
  s.list.batches[0].assign(kMaxBoundedInstances + 1, make_instance(0, 0, 0, 0, 1, 1, 1));
  items = s.plan();
  CHECK_FALSE(items[0].bounded);
  CHECK(items[0].lights == 1);  // unknown: every light, and the program that discards
  CHECK(items[0].fade);
}

TEST_CASE("draw plan: a light's rectangle on the screen") {
  World w = cached_game(3);
  const View v = view_at(w, 0.0f);
  const int width = 844, height = 390;
  // In front of the camera, some way off: a small rectangle within the screen.
  const float fx = v.camera_x + (w.player.pos.x - v.camera_x) * 3, fz = v.camera_z + (w.player.pos.z - v.camera_z) * 3;
  PointLight ahead{fx, w.player.pos.y + 1, fz, 2, {1, 1, 1, 1}};
  PixelRect r;
  REQUIRE(light_rect(v.view_projection, ahead, width, height, r));
  CHECK(r.w > 0);
  CHECK(r.w < width / 2);
  CHECK(r.x >= 0);
  CHECK(r.x + r.w <= width);
  CHECK(r.y + r.h <= height);
  // Around the camera: the whole screen.
  PointLight around{v.camera_x, v.camera_y, v.camera_z, 5, {1, 1, 1, 1}};
  REQUIRE(light_rect(v.view_projection, around, width, height, r));
  CHECK(r.w == width);
  CHECK(r.h == height);
  // Behind the camera, small and far: nothing.
  PointLight behind{v.camera_x - (fx - v.camera_x), v.camera_y, v.camera_z - (fz - v.camera_z), 1, {1, 1, 1, 1}};
  CHECK_FALSE(light_rect(v.view_projection, behind, width, height, r));
}

TEST_CASE("render options from the page's bits") {
  CHECK(render_options_from_bits(0) == RenderOptions{});
  const RenderOptions all = render_options_from_bits(63);
  CHECK((all.deferred_shading && all.lighter_grass && all.half_res_lights && all.compact_gbuffer && all.light_rects &&
         all.compact_instances));
  CHECK(render_options_from_bits(1).deferred_shading);
  CHECK(render_options_from_bits(2).lighter_grass);
  CHECK(render_options_from_bits(4).half_res_lights);
  CHECK(render_options_from_bits(8).compact_gbuffer);
  CHECK(render_options_from_bits(16).light_rects);
  CHECK(render_options_from_bits(32).compact_instances);
  CHECK_FALSE(render_options_from_bits(32).deferred_shading);
}

TEST_CASE("draw plan, lighter grass: the parts already shrunk away are left out, never the others") {
  PlanScene s;
  s.list.cached = true;
  // Mesh 0: grass (it shrinks away within 15 m of the origin); mesh 2: its
  // last part mixes grass and something that never shrinks.
  for (Instance& in : s.list.batches[0]) in.material = kFadeNear;
  for (Instance& in : s.list.batches[2]) in.material = kFadeNear;
  s.list.batches[2][0].material = kLit;  // at x 30
  s.view.fade_x = 0;
  s.view.fade_z = 0;
  s.view.fade_near_start = 10;
  s.view.fade_near_end = 15;
  const DrawList* lists[] = {&s.list};
  std::vector<DrawItem> plain, lighter;
  ShrinkCache cache;
  plan_draws(lists, s.meshes, false, false, s.view, plain);
  plan_draws(lists, s.meshes, false, false, s.view, lighter, &cache);
  CHECK(plain.size() == 8);
  // Mesh 0: the cubes at 0 and 10 m stay (one run); mesh 2: the one at 30 m
  // (not shrinking), then those at 10 and 0 m (one run); those at 20 m go.
  const auto drawn = [&](uint32_t mesh) {
    std::vector<uint32_t> out;
    for (const DrawItem& it : lighter) {
      if (it.mesh != mesh) continue;
      for (uint32_t k = 0; k < it.instance_count; ++k) out.push_back(it.first_instance + k);
    }
    return out;
  };
  CHECK(drawn(0) == std::vector<uint32_t>{0, 1});
  CHECK(drawn(2) == std::vector<uint32_t>{0, 2, 3});
  // Not cached: drawn as usual.
  s.list.cached = false;
  plan_draws(lists, s.meshes, false, false, s.view, lighter, &cache);
  CHECK(lighter.size() == 8);
}

TEST_CASE("compact instances: half floats and packed instances keep what the shader needs") {
  for (float v : {0.0f, 1.0f, -1.0f, 0.5f, 3.0f, 5.0f, -1.0f, 1.234f, 0.0001f, 1000.0f}) {
    CHECK(from_half(to_half(v)) == Approx(v).epsilon(0.001));
  }
  Instance in = make_instance(123.456f, -7.25f, 640.5f, 1.1f, 1.5f, 0.75f, 2.0f, {0.2f, 0.4f, 0.9f, 0.985f}, 0.8f);
  in.material = kFadeTree;
  const PackedInstance p = pack(in);
  CHECK(p.x == in.x);  // positions stay exact
  CHECK(p.z == in.z);
  CHECK(p.wind == in.wind);
  CHECK(p.rotation[1] / 32767.0f == Approx(in.rotation.y).epsilon(0.001));
  CHECK(p.rotation[3] / 32767.0f == Approx(in.rotation.w).epsilon(0.001));
  CHECK(from_half(p.scale[0]) == Approx(1.5f));
  CHECK(from_half(p.scale[3]) == kFadeTree);  // the material, exactly
  CHECK(from_half(p.tint[3]) == Approx(0.985f).epsilon(0.0005));  // the windows' mark survives
  std::vector<PackedInstance> out;
  const Instance many[] = {in, in};
  pack(many, out);
  CHECK(out.size() == 2);
}
