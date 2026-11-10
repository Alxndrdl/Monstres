#include "monstres/render/scene.hpp"

#include <cmath>

namespace monstres {

Mat4 multiply(const Mat4& a, const Mat4& b) {
  Mat4 r;
  for (int col = 0; col < 4; ++col) {
    for (int row = 0; row < 4; ++row) {
      float sum = 0;
      for (int k = 0; k < 4; ++k) sum += a.m[k * 4 + row] * b.m[col * 4 + k];
      r.m[col * 4 + row] = sum;
    }
  }
  return r;
}

Mat4 perspective(float fov_y, float aspect, float n, float f) {
  const float t = 1.0f / std::tan(fov_y / 2);
  Mat4 r;
  r.m[0] = t / aspect;
  r.m[5] = t;
  r.m[10] = (f + n) / (n - f);
  r.m[11] = -1;
  r.m[14] = 2 * f * n / (n - f);
  r.m[15] = 0;
  return r;
}

Mat4 look_at(float ex, float ey, float ez, float tx, float ty, float tz) {
  float fx = tx - ex, fy = ty - ey, fz = tz - ez;
  float len = std::sqrt(fx * fx + fy * fy + fz * fz);
  fx /= len; fy /= len; fz /= len;
  // side = forward x up(0,1,0)
  float sx = -fz, sy = 0, sz = fx;
  len = std::sqrt(sx * sx + sz * sz);
  sx /= len; sz /= len;
  // up = side x forward
  const float ux = sy * fz - sz * fy, uy = sz * fx - sx * fz, uz = sx * fy - sy * fx;
  Mat4 r;
  r.m[0] = sx;  r.m[4] = sy;  r.m[8] = sz;
  r.m[1] = ux;  r.m[5] = uy;  r.m[9] = uz;
  r.m[2] = -fx; r.m[6] = -fy; r.m[10] = -fz;
  r.m[12] = -(sx * ex + sy * ey + sz * ez);
  r.m[13] = -(ux * ex + uy * ey + uz * ez);
  r.m[14] = fx * ex + fy * ey + fz * ez;
  return r;
}

void build_scene(const World& w, std::vector<CubeInstance>& out) {
  out.clear();
  const float g = rules::kWorldHalfSize;
  out.push_back({0, -0.05f, 0, 0, g, 0.05f, g, 1, palette::kGround});

  constexpr size_t kColors = sizeof palette::kObjects / sizeof palette::kObjects[0];
  for (const Object& o : w.objects) {
    out.push_back({o.center.x, o.center.y, o.center.z, 0, o.half.x, o.half.y, o.half.z, 0,
                   palette::kObjects[o.color % kColors]});
  }

  const Player& p = w.player;
  const float h = rules::kPlayerHalfWidth;
  const float yaw = std::atan2(-p.facing_x, -p.facing_z);
  out.push_back({p.pos.x, p.pos.y + h, p.pos.z, yaw, h, h, h, 0, palette::kPlayer});
  // A small "nose" on the front face shows where the player is facing.
  out.push_back({p.pos.x + p.facing_x * h, p.pos.y + h * 1.3f, p.pos.z + p.facing_z * h, yaw,
                 0.12f, 0.08f, 0.06f, 0, palette::kPlayerNose});
}

View build_view(const World& w, const Camera& cam, float aspect) {
  const Player& p = w.player;
  const float tx = p.pos.x, ty = p.pos.y + 1.0f, tz = p.pos.z;

  // The camera sits behind the player, opposite to where it looks.
  const float cp = std::cos(cam.pitch), sp = std::sin(cam.pitch);
  const float fx = -std::sin(cam.yaw) * cp, fz = -std::cos(cam.yaw) * cp;
  float ex = tx - fx * cam.distance;
  float ey = ty + sp * cam.distance;
  float ez = tz - fz * cam.distance;
  if (ey < 0.2f) ey = 0.2f;  // never below the ground

  View v;
  v.view_projection = multiply(perspective(1.0f, aspect, 0.1f, 200.0f), look_at(ex, ey, ez, tx, ty, tz));
  v.camera_x = ex;
  v.camera_y = ey;
  v.camera_z = ez;
  const float lx = 0.4f, ly = 1.0f, lz = 0.25f;
  const float ll = std::sqrt(lx * lx + ly * ly + lz * lz);
  v.light_x = lx / ll;
  v.light_y = ly / ll;
  v.light_z = lz / ll;
  v.fog = palette::kSky;
  return v;
}

}  // namespace monstres
