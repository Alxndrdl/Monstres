# monstres_render: turns the world into lists of things to draw, loads the
# model files, builds the terrain meshes. No OpenGL here, so it is testable
# natively.
add_library(monstres_render STATIC scene.cpp models.cpp terrain_mesh.cpp characters.cpp)
target_include_directories(monstres_render PUBLIC include)
target_include_directories(monstres_render SYSTEM PRIVATE "${cgltf_SOURCE_DIR}")
target_link_libraries(monstres_render PUBLIC monstres_core)
monstres_set_options(monstres_render)

# monstres_render_gles3: the OpenGL ES 3.0 backend (WebGL2 in the browser).
# A WebGPU backend can be added later next to it, behind the same Renderer interface.
if(EMSCRIPTEN)
  add_library(monstres_render_gles3 STATIC gles3/gles3_renderer.cpp)
  target_include_directories(monstres_render_gles3 PUBLIC gles3)
  target_link_libraries(monstres_render_gles3 PUBLIC monstres_render)
  monstres_set_options(monstres_render_gles3)
endif()
