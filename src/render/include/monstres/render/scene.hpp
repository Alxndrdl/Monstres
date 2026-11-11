#pragma once

#include "monstres/core/world.hpp"
#include "monstres/render/models.hpp"
#include "monstres/render/renderer.hpp"

namespace monstres {

namespace palette {
inline constexpr Color kSkyZenith{0.30f, 0.53f, 0.86f, 1};
inline constexpr Color kSkyHorizon{0.76f, 0.85f, 0.93f, 1};
inline constexpr Color kPlayer{0.95f, 0.55f, 0.20f, 1};
inline constexpr Color kPlayerNose{0.25f, 0.20f, 0.30f, 1};
}  // namespace palette

// Third-person camera orbiting around the player.
struct Camera {
  float yaw = 0;        // radians; 0 looks towards -Z
  float pitch = 0.28f;  // radians above the horizon (looking down at the player)
  float distance = 6.0f;
};

inline constexpr float kMinPitch = -0.15f;
inline constexpr float kMaxPitch = 1.35f;

Mat4 multiply(const Mat4& a, const Mat4& b);
Mat4 inverse(const Mat4& m);
Mat4 perspective(float fov_y_radians, float aspect, float near_z, float far_z);
Mat4 look_at(float eye_x, float eye_y, float eye_z, float target_x, float target_y, float target_z);

inline constexpr float kShadowHeight = 0.025f;  // above the path

// A soft round shadow on the ground (or on top of something at height y).
void add_shadow(DrawList& list, float x, float z, float rx, float rz, float yaw, float alpha = 1,
                float y = kShadowHeight);

// Which mesh draws an object.
MeshId mesh_for(const Object& object);

// Everything that never moves in a world: the ground, the path, the objects
// and their shadows, grass, flowers, the forest beyond the edges and the
// mountains. Build it once per world (new game, loaded save).
void build_landscape(const World& world, DrawList& list);

// What moves: the player, its shadow, the clouds. `time` in seconds.
void build_dynamic(const World& world, float time, DrawList& list);

// How far the camera can be from the target (tx, ty, tz) along the unit
// direction (dx, dy, dz) without an object in between, at most `wanted`.
float camera_distance(const World& world, float tx, float ty, float tz, float dx, float dy, float dz,
                      float wanted);

// Camera matrices, sky and lighting for one frame.
View build_view(const World& world, const Camera& camera, float aspect, float time);

}  // namespace monstres
