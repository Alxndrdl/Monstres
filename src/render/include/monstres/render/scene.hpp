#pragma once

#include "monstres/core/world.hpp"
#include "monstres/render/characters.hpp"
#include "monstres/render/landscape.hpp"
#include "monstres/render/models.hpp"
#include "monstres/render/renderer.hpp"

namespace monstres {

namespace palette {
inline constexpr Color kSkyZenith{0.30f, 0.53f, 0.86f, 1};
inline constexpr Color kSkyHorizon{0.76f, 0.85f, 0.93f, 1};
inline constexpr Color kPlayer{0.95f, 0.55f, 0.20f, 1};
inline constexpr Color kPlayerNose{0.25f, 0.20f, 0.30f, 1};
}  // namespace palette

// Who the camera follows: the player, or the companion (in a battle, and
// for a moment after a victory).
enum class CameraSubject : uint8_t { Magician, Companion };
inline constexpr uint64_t kCameraHoldTicks = 100;     // on the companion after a victory
inline constexpr float kCameraBlendSeconds = 0.9f;    // gliding from one subject to the other

// Third-person camera orbiting around the player.
struct Camera {
  float yaw = 0;        // radians; 0 looks towards -Z
  float pitch = 0.28f;  // radians above the horizon (looking down at the player)
  float distance = 6.0f;
  // Set by update_camera; without it, the camera jumps to its subject.
  bool smooth = false;
  CameraSubject subject = CameraSubject::Magician;
  CameraSubject previous = CameraSubject::Magician;
  float blend = 1;      // 0: on `previous`, 1: on `subject`
};

// Who the camera should follow now.
CameraSubject camera_subject(const World& world);

// Moves the camera on by `dt` seconds of the game: when its subject changes,
// it glides to the new one instead of jumping.
void update_camera(Camera& camera, const World& world, float dt);

inline constexpr float kMinPitch = -0.15f;
inline constexpr float kMaxPitch = 1.35f;

Mat4 multiply(const Mat4& a, const Mat4& b);
Mat4 inverse(const Mat4& m);
Mat4 perspective(float fov_y_radians, float aspect, float near_z, float far_z);
Mat4 look_at(float eye_x, float eye_y, float eye_z, float target_x, float target_y, float target_z);

inline constexpr float kShadowHeight = 0.045f;  // above the ground and the path

// A soft round shadow at height y (on top of something), tilted by `tilt`.
void add_shadow(DrawList& list, float x, float z, float rx, float rz, float yaw, float alpha = 1,
                float y = kShadowHeight, Quat tilt = {});

// A soft round shadow on the ground at (x, z), lying on the slope.
void add_ground_shadow(DrawList& list, const World& world, float x, float z, float rx, float rz, float yaw,
                       float alpha = 1);

// Rotation that turns "up" into the ground's normal at (x, z).
Quat ground_tilt(const World& world, float x, float z);

// Which mesh draws an object.
MeshId mesh_for(const Object& object);

// The continent beyond the playable area (a far list, see DrawList::far):
// its ground, waters and lava (meshes from far_terrain.hpp, set once per
// world); and what moves there: the active volcano's smoke.
void build_far_landscape(DrawList& list);
void build_far_dynamic(const World& world, float time, DrawList& list);

namespace view_range {
inline constexpr float kNearFar = 2000.0f;  // the near pass: up to here (the whole relief, from anywhere on it)
inline constexpr float kFarNear = 8.0f;    // the far pass: from here...
inline constexpr float kFarFar = 9500.0f;  // ...to the horizon
inline constexpr float kFogStart = 30.0f, kFogHalf = 1800.0f, kFogEnd = 9000.0f;
}  // namespace view_range

// What moves: the characters, their shadows, the effects (the clouds:
// weather_view.hpp). `time` in seconds.
// `selected`: index of a creature to mark with a ring at its feet (-1: none).
// With a view: the creatures out of it are left out (cull.hpp).
void build_dynamic(const World& world, float time, DrawList& list, int selected = -1, const Frustum* view = nullptr);

// How far the camera can be from the target (tx, ty, tz) along the unit
// direction (dx, dy, dz) without an object in between, at most `wanted`.
float camera_distance(const World& world, float tx, float ty, float tz, float dx, float dy, float dz,
                      float wanted);

// The day: colors of the sky and of the light at an hour (0..24), and how
// much daylight there is (0 at night, 1 by day).
struct SkyState {
  Color zenith, horizon;
  Color ambient_sky, ambient_ground;
  Color light;  // the sun's or the moon's
  float daylight;
};
SkyState sky_at(float hour);

// Where the sun and the moon are at an hour, and where surfaces are lit from.
struct SkyDirections {
  Vec3 sun, moon, light;  // unit vectors
};
SkyDirections sky_directions(float hour);

// 0 by day, 1 at night (lanterns lit, stars), in between at dawn and dusk.
float night_amount(const World& world);

// Camera matrices, sky and lighting (time of day, lanterns...) for one frame.
View build_view(const World& world, const Camera& camera, float aspect, float time);
// The same from any place, looking at `target` (the arrival's flight, intro.hpp).
View build_view_from(const World& world, Vec3 eye, Vec3 target, float aspect, float time);

// A portrait for the menus: one figure (see add_portrait_figure), framed
// from a little above, lit from the front, without sky (draw it with
// Renderer::draw_offscreen).
struct Portrait {
  DrawList list;
  View view;
};
Portrait build_portrait(const PortraitSubject& subject, float aspect);

// The same figure, seen from wherever the player turns, zooms and moves the
// camera (the character creation): `yaw` turns around it (0: from the
// front, +Z), `pitch` from above, `zoom` closer (1: the whole figure),
// `pan` moves its middle (meters). `time`: its breathing.
struct PreviewCamera {
  float yaw = 0.3f, pitch = 0.15f, zoom = 1, pan_x = 0, pan_y = 0;
};
Portrait build_preview(const PortraitSubject& subject, float aspect, const PreviewCamera& camera, float time);

// The creature under a point of the screen, given in normalized device
// coordinates (-1..1, y up), or -1. Creatures are a little easier to hit
// than their size; an object in front (a tree trunk, a house) hides them.
int pick_creature(const World& world, const View& view, float ndc_x, float ndc_y);

// The signpost or teleporter under a point of the screen (objects index),
// or -1: only within kReadDistance of the player, not hidden by another object.
inline constexpr float kReadDistance = 25.0f;
int pick_object(const World& world, const View& view, float ndc_x, float ndc_y);

// Where the middle of creatures[index] is on screen (normalized device
// coordinates); index -1: the magician. False if there is no such creature
// or it is behind the camera.
bool creature_on_screen(const World& world, const View& view, int index, float& ndc_x, float& ndc_y);

}  // namespace monstres
