// The day and the night: the clock of the world, the sky and the lights.

#include "monstres/core/save.hpp"
#include "monstres/render/characters.hpp"
#include "monstres/render/scene.hpp"
#include "test_helpers.hpp"

#include <algorithm>

using namespace monstres;

TEST_CASE("an hour lasts a minute, a day 24 minutes, and a new game starts in the morning") {
  CHECK(rules::kTicksPerHour == 60u * 60u);
  CHECK(rules::kTicksPerDay == 24u * 60u * 60u);
  World w = new_game(3);
  CHECK(hour_of_day(w) == doctest::Approx(8.0f));
  run(w, Input{}, rules::kTicksPerHour);  // one minute
  CHECK(hour_of_day(w) == doctest::Approx(9.0f));
  w.clock = rules::kTicksPerDay - 1;
  step(w, Input{});
  CHECK(w.clock == 0u);  // midnight
}

TEST_CASE("the time of day is saved") {
  World w = new_game(3);
  w.clock = 22 * rules::kTicksPerHour + 5;
  const auto loaded = deserialize(serialize(w));
  REQUIRE(loaded.has_value());
  CHECK(loaded->clock == w.clock);
  std::vector<uint8_t> bytes = serialize(w);
  bytes[bytes.size() - 2] = 0xff;  // past the end of the day (the clock, before the active member)
  CHECK_FALSE(deserialize(bytes).has_value());
}

TEST_CASE("the sky: bright by day, dark at night, the sun up by day and the moon at night") {
  const SkyState noon = sky_at(12), midnight = sky_at(0), dusk = sky_at(18.5f);
  CHECK(noon.daylight == 1.0f);
  CHECK(midnight.daylight == 0.0f);
  CHECK(dusk.daylight > 0.0f);
  CHECK(dusk.daylight < 1.0f);
  CHECK(midnight.zenith.b < noon.zenith.b);
  CHECK(midnight.light.r < noon.light.r);
  CHECK(sky_at(24.0f).daylight == sky_at(0).daylight);
  CHECK(sky_directions(12).sun.y > 0.7f);
  CHECK(sky_directions(0).sun.y < -0.7f);
  CHECK(sky_directions(0).moon.y > 0.3f);  // up, but low enough to be seen
  CHECK(sky_directions(7).sun.y > 0);
  CHECK(sky_directions(5).sun.y < 0);
  for (float h = 0; h < 24; h += 0.5f) CHECK(sky_directions(h).light.y > 0.3f);  // never from below
}

TEST_CASE("at night the lanterns, the staff and glowing monsters give light") {
  World w = new_game(42);
  PointLight lights[kMaxPointLights];
  CHECK(world_lights(w, 0, 0, 0, 0, lights) == 0);  // by day: none
  const bool has_lamp = std::any_of(w.objects.begin(), w.objects.end(), [](const Object& o) { return o.kind == ObjectKind::Lamp; });
  REQUIRE(has_lamp);
  const size_t n = world_lights(w, w.village.center.x, w.village.center.z, 1, 0, lights);
  CHECK(n >= 3);  // the staff and the village lanterns at least
  // The staff first, in the magician's hand.
  CHECK(std::hypot(lights[0].x - w.player.pos.x, lights[0].z - w.player.pos.z) < 0.5f);
  CHECK(lights[0].y > w.player.pos.y + 1);
  CHECK(lights[0].color.b > lights[0].color.r);  // bluish
  // The others are the nearest to where they are looked for.
  for (size_t i = 2; i < n; ++i) {
    const float a = std::hypot(lights[i - 1].x - w.village.center.x, lights[i - 1].z - w.village.center.z);
    const float b = std::hypot(lights[i].x - w.village.center.x, lights[i].z - w.village.center.z);
    CHECK(a <= b);
  }

  // A monster with glowing antennae lights up its surroundings.
  World one = empty_world();
  Creature slime;
  slime.species = Species::Slime;
  slime.genes.allele[3] = 2;  // antennae
  slime.body.pos = {3, 0, 3};
  one.creatures.push_back(slime);
  CHECK(world_lights(one, 0, 0, 1, 0, lights) == 2);  // the staff, the slime
  one.creatures[0].genes.allele[3] = 0;
  CHECK(world_lights(one, 0, 0, 1, 0, lights) == 1);
}
