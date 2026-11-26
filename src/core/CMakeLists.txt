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
  continent.cpp
  weather.cpp
  interior.cpp
  west.cpp
  north.cpp
  roads.cpp
  landfix.cpp
  outer.cpp
  continent_map.cpp
  rivers.cpp
  routines.cpp
  sites.cpp
  water.cpp
)
target_include_directories(monstres_core PUBLIC include)
monstres_set_options(monstres_core)
