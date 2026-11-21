// The seasons (for the eyes only for now): the landscape bare in winter,
// fallen leaves in autumn, more flowers in spring; leaves blown by the wind;
// the falls and the mill's wheel still in the ice; the models' leaves marked
// for the shaders.

#include "monstres/core/weather.hpp"
#include "monstres/core/west.hpp"
#include "monstres/render/landscape.hpp"
#include "monstres/render/models.hpp"
#include "monstres/render/scene.hpp"
#include "monstres/render/weather_view.hpp"
#include "test_helpers.hpp"

#include <string>

using namespace monstres;

namespace {

size_t count(const DrawList& list, MeshId id) {
  const auto i = static_cast<size_t>(id);
  return i < list.batches.size() ? list.batches[i].size() : 0;
}

DrawList landscape_in(Season season) {
  DrawList list;
  build_landscape(cached_game(42), list, season);
  return list;
}

size_t flowers(const DrawList& list) { return count(list, MeshId::Flower) + count(list, MeshId::FlowerFar); }
size_t grass(const DrawList& list) {
  return count(list, MeshId::GrassShort) + count(list, MeshId::GrassTall) + count(list, MeshId::GrassShortFar) +
         count(list, MeshId::GrassTallFar);
}

}  // namespace

TEST_CASE("the landscape in its season: bare trees in winter, leaves on the ground in autumn, flowers in spring") {
  const DrawList summer = landscape_in(Season::Summer);
  const DrawList winter = landscape_in(Season::Winter);
  const DrawList autumn = landscape_in(Season::Autumn);
  const DrawList spring = landscape_in(Season::Spring);
  CHECK(summer.seasonal);
  // Summer: as it always was.
  CHECK(count(summer, MeshId::Tree) + count(summer, MeshId::TreeMid) + count(summer, MeshId::TreeFar) > 50);
  CHECK(count(summer, MeshId::TreeBare) + count(summer, MeshId::TreeBareFar) == 0);
  CHECK(count(summer, MeshId::LeafLitter) == 0);
  // Winter: every leafy tree bare (the pines keep their needles), no flowers, less grass.
  CHECK(count(winter, MeshId::Tree) + count(winter, MeshId::TreeMid) + count(winter, MeshId::TreeFar) == 0);
  CHECK(count(winter, MeshId::FruitTree) == 0);
  CHECK(count(winter, MeshId::TreeBare) + count(winter, MeshId::TreeBareFar) ==
        count(summer, MeshId::Tree) + count(summer, MeshId::TreeMid) + count(summer, MeshId::TreeFar) +
            count(summer, MeshId::FruitTree));
  CHECK(count(winter, MeshId::Pine) == count(summer, MeshId::Pine));
  CHECK(flowers(winter) == 0);
  CHECK(grass(winter) < grass(summer) / 2);
  // Autumn: fallen leaves under the trees, fewer flowers; spring: more.
  CHECK(count(autumn, MeshId::LeafLitter) > 50);
  CHECK(flowers(autumn) < flowers(summer) / 2);
  CHECK(flowers(spring) > flowers(summer) * 3 / 2);
  CHECK(count(spring, MeshId::Tree) == count(summer, MeshId::Tree));
}

TEST_CASE("leaves are blown about in autumn, the more the wind blows; petals in spring") {
  World w = cached_game(42);
  w.clock = 12 * rules::kTicksPerHour;
  Camera camera;
  WeatherField field;
  field.seed = w.seed;
  field.tick = 30ULL * 60 * rules::kTicksPerSecond;
  field.forced = weather_of(WeatherKind::Clear);
  field.forced_amount = 1;
  const auto leaves = [&](Season season, float wind) {
    View view = build_view(w, camera, 16.0f / 9.0f, 0);
    apply_weather(view, field);
    view.season.spring = season == Season::Spring ? 1.0f : 0.0f;
    view.season.autumn = season == Season::Autumn ? 1.0f : 0.0f;
    view.wind = wind;
    DrawList far_list, near_list;
    build_weather(field, view, 0, 3.0f, far_list, near_list);
    return count(near_list, MeshId::Leaves);
  };
  CHECK(leaves(Season::Summer, 1) == 0);
  CHECK(leaves(Season::Autumn, 0) > 0);
  CHECK(leaves(Season::Autumn, 1) > 3 * leaves(Season::Autumn, 0));
  CHECK(leaves(Season::Spring, 0) == 0);
  CHECK(leaves(Season::Spring, 1) > 0);
}

TEST_CASE("snow falling hides the distance more than rain") {
  const World& w = cached_game(42);
  Camera camera;
  const auto haze = [&](WeatherKind kind) {
    WeatherField f;
    f.seed = w.seed;
    f.tick = 30ULL * 60 * rules::kTicksPerSecond;
    f.forced = weather_of(kind);
    f.forced_amount = 1;
    View view = build_view(w, camera, 16.0f / 9.0f, 0);
    apply_weather(view, f);
    return view;
  };
  const View snow = haze(WeatherKind::Snow), rain = haze(WeatherKind::Rain), clear = haze(WeatherKind::Clear);
  CHECK(snow.fog_half < rain.fog_half);
  CHECK(snow.fog_end < 600);
  CHECK(snow.fog_start < clear.fog_start);
}

TEST_CASE("in the ice: the falls stand still, without foam, and the mill's wheel stops") {
  World w = cached_game(42);
  REQUIRE(w.west.valid());
  w.player.pos = {w.west.cove.x, w.ground.height(w.west.cove.x, w.west.cove.z), w.west.cove.z};
  View summer;
  View winter;
  winter.season.winter = winter.season.ice = winter.season.snow = 1;
  DrawList flowing, frozen;
  build_dynamic(w, 3.0f, flowing, -1, nullptr, &summer);
  build_dynamic(w, 3.0f, frozen, -1, nullptr, &winter);
  REQUIRE(count(flowing, MeshId::FallsBand) > 0);
  REQUIRE(count(frozen, MeshId::FallsBand) == count(flowing, MeshId::FallsBand));
  for (const Instance& band : frozen.batches[static_cast<size_t>(MeshId::FallsBand)]) CHECK(band.tint.b == 1.0f);
  CHECK(count(frozen, MeshId::Smoke) < count(flowing, MeshId::Smoke));  // no foam
  // The same a moment later: nothing moves.
  DrawList later;
  build_dynamic(w, 4.0f, later, -1, nullptr, &winter);
  const auto& a = frozen.batches[static_cast<size_t>(MeshId::FallsBand)];
  const auto& b = later.batches[static_cast<size_t>(MeshId::FallsBand)];
  for (size_t i = 0; i < a.size(); ++i) CHECK(a[i].tint.a == b[i].tint.a);
}

TEST_CASE("only the leaves of trees and bushes are marked for the seasons") {
  std::vector<MeshData> meshes;
  std::string error;
  REQUIRE(load_models(std::string(MONSTRES_ASSETS_DIR) + "/models", meshes, &error));
  const auto marks = [&](MeshId id, float alpha) {
    const MeshData& m = meshes[static_cast<size_t>(id)];
    size_t n = 0;
    for (size_t i = 9; i < m.vertices.size(); i += MeshData::kFloatsPerVertex) n += std::fabs(m.vertices[i] - alpha) < 0.003f;
    return n;
  };
  for (const MeshId id : {MeshId::Tree, MeshId::TreeMid, MeshId::TreeFar, MeshId::Bush, MeshId::BushFar, MeshId::HollowTree,
                          MeshId::Larch, MeshId::LarchFar}) {
    CHECK(marks(id, kLeavesAlpha) > 0);
  }
  CHECK(marks(MeshId::FruitTree, kBlossomAlpha) > 0);
  CHECK(marks(MeshId::FruitTree, kFruitAlpha) > 0);
  CHECK(marks(MeshId::TreeBare, kLeavesAlpha) == 0);
  CHECK(marks(MeshId::Pine, kLeavesAlpha) == 0);
  // Nothing else opaque carries those alphas (the shader would take it for leaves).
  for (size_t id = 0; id < kMeshCount; ++id) {
    const MeshData& m = meshes[id];
    const auto mesh = static_cast<MeshId>(id);
    if (m.transparent || mesh == MeshId::Tree || mesh == MeshId::TreeMid || mesh == MeshId::TreeFar ||
        mesh == MeshId::Bush || mesh == MeshId::BushFar || mesh == MeshId::HollowTree || mesh == MeshId::FruitTree ||
        mesh == MeshId::Larch || mesh == MeshId::LarchFar) {  // the larch's needles turn gold too
      continue;
    }
    CAPTURE(model_name(mesh));
    size_t n = 0;
    for (size_t i = 9; i < m.vertices.size(); i += MeshData::kFloatsPerVertex) n += m.vertices[i] > 0.93f && m.vertices[i] < 0.98f;
    CHECK(n == 0);
  }
}
