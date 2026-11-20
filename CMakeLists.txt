cmake_minimum_required(VERSION 3.24)
project(Monstres VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(MONSTRES_BUILD_TESTS "Build the test executables" ON)
option(MONSTRES_WERROR      "Treat warnings as errors"   OFF)
option(MONSTRES_SANITIZERS  "Native only: ASan + UBSan"   OFF)

# ccache, if installed: rebuilding what was already built once (another
# preset, a reconfigure, back to an older version) takes seconds. Native
# builds only (Emscripten keeps its own cache).
find_program(MONSTRES_CCACHE ccache)
if(MONSTRES_CCACHE AND NOT EMSCRIPTEN AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
  set(CMAKE_CXX_COMPILER_LAUNCHER "${MONSTRES_CCACHE}")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(MonstresOptions)
include(Dependencies)

set(MONSTRES_ASSETS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets")

add_subdirectory(src/core)
add_subdirectory(src/render)
add_subdirectory(src/audio)

if(EMSCRIPTEN)
  add_subdirectory(src/platform/web)
endif()

if(MONSTRES_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
