#pragma once

#include "monstres/core/world.hpp"
#include "monstres/render/meshes.hpp"
#include "monstres/render/renderer.hpp"

namespace monstres {

namespace palette {
inline constexpr Color kSky{0.62f, 0.78f, 0.92f, 1};
inline constexpr Color kGround{0.42f, 0.62f, 0.38f, 1};
inline constexpr Color kPlayer{0.95f, 0.55f, 0.20f, 1};
inline constexpr Color kPlayerNose{0.25f, 0.20f, 0.30f, 1};
}  // namespace palette

// Third-person camera orbiting around the player.
struct Camera {
  float yaw = 0;        // radians; 0 looks towards -Z
  float pitch = 0.35f;  // radians above the horizon (looking down at the player)
  float distance = 6.0f;
};

inline constexpr float kMinPitch = -0.15f;
inline constexpr float kMaxPitch = 1.35f;

Mat4 multiply(const Mat4& a, const Mat4& b);
Mat4 perspective(float fov_y_radians, float aspect, float near_z, float far_z);
Mat4 look_at(float eye_x, float eye_y, float eye_z, float target_x, float target_y, float target_z);

// Which mesh draws an object.
MeshId mesh_for(const Object& object);

// Fills `list` (cleared first, one batch per MeshId) with the ground, the
// objects and the player.
void build_scene(const World& world, DrawList& list);

// Camera matrices and lighting for one frame.
View build_view(const World& world, const Camera& camera, float aspect);

}  // namespace monstres
