#include "monstres/render/models.hpp"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <cmath>
#include <memory>

namespace monstres {

namespace {

constexpr const char* kNames[kMeshCount] = {
    "cube", "crate", "tree", "pine", "rock0", "rock1", "rock2", "rock3", "log", "stump", "bush",
    "grass_short", "grass_tall", "flower", "cloud0", "cloud1", "cloud2", "mountain0", "mountain1",
    "eye", "player_leg", "player_torso", "player_arm", "player_head", "player_staff", "player_crystal",
    "companion_body", "companion_leg", "companion_head", "companion_ear", "companion_tail",
    "slime_body", "mushroom_stem", "mushroom_cap", "mushroom_leg", "bird_body", "bird_wing", "bird_leg",
    "house_cottage", "house_chimney", "house_hut", "fence", "sign", "well", "barrel", "lamp",
    "villager_torso", "villager_leg", "villager_arm", "villager_hand", "villager_head",
    "hair_short", "hair_long", "hair_bun", "hat_cap", "hat_straw", "hat_beanie",
    "crow_body", "crow_wing", "crow_leg", "fish_body", "fish_tail", "fish_fin",
    "tower", "ruin_wall_low", "ruin_wall_high", "pillar", "fallen_pillar", "rubble", "rune_stone",
    nullptr, nullptr, nullptr, "shadow", "ripple", "smoke", "sparkle",
};

struct DataDeleter {
  void operator()(cgltf_data* d) const { cgltf_free(d); }
};

void transform_point(const float m[16], const float in[3], float out[3]) {
  for (int r = 0; r < 3; ++r) out[r] = m[r] * in[0] + m[4 + r] * in[1] + m[8 + r] * in[2] + m[12 + r];
}

void transform_normal(const float m[16], const float in[3], float out[3]) {
  for (int r = 0; r < 3; ++r) out[r] = m[r] * in[0] + m[4 + r] * in[1] + m[8 + r] * in[2];
  const float len = std::sqrt(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
  if (len > 0) {
    for (int r = 0; r < 3; ++r) out[r] /= len;
  }
}

bool fail(std::string* error, const std::string& message) {
  if (error) *error = message;
  return false;
}

bool append_primitive(const cgltf_primitive& prim, const float world[16], MeshData& mesh, std::string* error) {
  if (prim.type != cgltf_primitive_type_triangles) return true;  // points or lines: ignored
  const cgltf_accessor* position = nullptr;
  const cgltf_accessor* normal = nullptr;
  const cgltf_accessor* color = nullptr;
  for (cgltf_size i = 0; i < prim.attributes_count; ++i) {
    const cgltf_attribute& a = prim.attributes[i];
    if (a.type == cgltf_attribute_type_position) position = a.data;
    if (a.type == cgltf_attribute_type_normal) normal = a.data;
    if (a.type == cgltf_attribute_type_color && a.index == 0) color = a.data;
  }
  if (!position) return fail(error, "primitive without POSITION");

  if (prim.material) {
    if (prim.material->alpha_mode == cgltf_alpha_mode_blend) mesh.transparent = true;
    if (prim.material->double_sided) mesh.double_sided = true;
  }

  const cgltf_size count = prim.indices ? prim.indices->count : position->count;
  if (count % 3 != 0) return fail(error, "triangle count is not a multiple of 3");
  const size_t first = mesh.vertices.size();
  for (cgltf_size k = 0; k < count; ++k) {
    const cgltf_size i = prim.indices ? cgltf_accessor_read_index(prim.indices, k) : k;
    float p[3] = {}, n[3] = {0, 1, 0}, c[4] = {1, 1, 1, 1};
    float wp[3], wn[3];
    if (!cgltf_accessor_read_float(position, i, p, 3)) return fail(error, "cannot read POSITION");
    if (normal) cgltf_accessor_read_float(normal, i, n, 3);
    if (color) cgltf_accessor_read_float(color, i, c, color->type == cgltf_type_vec3 ? 3 : 4);
    transform_point(world, p, wp);
    transform_normal(world, n, wn);
    mesh.vertices.insert(mesh.vertices.end(), {wp[0], wp[1], wp[2], wn[0], wn[1], wn[2], c[0], c[1], c[2], c[3]});
  }

  if (!normal) {  // flat normals from the triangles
    constexpr size_t k = MeshData::kFloatsPerVertex;
    for (size_t t = first; t < mesh.vertices.size(); t += 3 * k) {
      const float* a = &mesh.vertices[t];
      const float* b = &mesh.vertices[t + k];
      const float* c = &mesh.vertices[t + 2 * k];
      const float u[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
      const float v[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
      float nn[3] = {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
      const float len = std::sqrt(nn[0] * nn[0] + nn[1] * nn[1] + nn[2] * nn[2]);
      for (float& x : nn) x = len > 0 ? x / len : 0;
      if (len == 0) nn[1] = 1;
      for (size_t vtx = 0; vtx < 3; ++vtx) {
        for (size_t j = 0; j < 3; ++j) mesh.vertices[t + vtx * k + 3 + j] = nn[j];
      }
    }
  }
  return true;
}

}  // namespace

const char* model_name(MeshId id) {
  const auto i = static_cast<size_t>(id);
  return i < kMeshCount ? kNames[i] : nullptr;
}

std::optional<MeshData> load_model(const std::string& path, std::string* error) {
  cgltf_options options = {};
  cgltf_data* raw = nullptr;
  if (cgltf_parse_file(&options, path.c_str(), &raw) != cgltf_result_success) {
    fail(error, path + ": not a readable glTF file");
    return std::nullopt;
  }
  std::unique_ptr<cgltf_data, DataDeleter> data(raw);
  if (cgltf_load_buffers(&options, data.get(), path.c_str()) != cgltf_result_success ||
      cgltf_validate(data.get()) != cgltf_result_success) {
    fail(error, path + ": invalid glTF data");
    return std::nullopt;
  }

  MeshData mesh;
  for (cgltf_size n = 0; n < data->nodes_count; ++n) {
    const cgltf_node& node = data->nodes[n];
    if (!node.mesh) continue;
    float world[16];
    cgltf_node_transform_world(&node, world);
    for (cgltf_size p = 0; p < node.mesh->primitives_count; ++p) {
      std::string message;
      if (!append_primitive(node.mesh->primitives[p], world, mesh, &message)) {
        fail(error, path + ": " + message);
        return std::nullopt;
      }
    }
  }
  if (mesh.vertex_count() == 0) {
    fail(error, path + ": no triangles");
    return std::nullopt;
  }
  return mesh;
}

bool load_models(const std::string& dir, std::vector<MeshData>& meshes, std::string* error) {
  meshes.assign(kMeshCount, MeshData{});
  for (size_t i = 0; i < kMeshCount; ++i) {
    const char* name = kNames[i];
    if (!name) continue;
    auto mesh = load_model(dir + "/" + name + ".glb", error);
    if (!mesh) return false;
    meshes[i] = std::move(*mesh);
  }
  return true;
}

}  // namespace monstres
