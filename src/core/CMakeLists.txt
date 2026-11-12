# monstres_core: pure game logic. No browser, no OpenGL, no I/O besides bytes.
# Everything here must build and be testable natively and in WebAssembly.
add_library(monstres_core STATIC
  world.cpp
  terrain.cpp
  heightmap.cpp
  village.cpp
  save.cpp
  debug.cpp
  variants.cpp
  battle.cpp
  quests.cpp
)
target_include_directories(monstres_core PUBLIC include)
monstres_set_options(monstres_core)
