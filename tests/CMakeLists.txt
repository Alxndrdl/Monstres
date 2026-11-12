# Unit, functional and regression tests. They run natively (fast, with
# sanitizers in native-debug) and as WebAssembly under Node (web presets:
# emcmake sets CMAKE_CROSSCOMPILING_EMULATOR to node, so ctest just works).
#
#   ctest --preset native-debug             all tests (sanitizers: thorough, slower)
#   ctest --preset native-release           all tests, fast (no sanitizers)
#   ctest --preset native-debug -L unit     only one category
#   ctest --preset native-debug -R world    only the tests of one file (unit_world)

include(FetchContent)
FetchContent_Declare(doctest
  URL https://raw.githubusercontent.com/doctest/doctest/v2.4.11/doctest/doctest.h
  URL_HASH SHA256=44faa038e9c3f9728efbda143748d01124ea0a27f4bf78f35a15d8fab2e039fb
  DOWNLOAD_NO_EXTRACT TRUE
)
FetchContent_MakeAvailable(doctest)

add_library(monstres_test_main STATIC support/doctest_main.cpp)
target_include_directories(monstres_test_main PUBLIC "${doctest_SOURCE_DIR}" support)
target_link_libraries(monstres_test_main PUBLIC monstres_core monstres_render)
target_compile_definitions(monstres_test_main PUBLIC
  MONSTRES_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/regression/data"
  MONSTRES_ASSETS_DIR="${MONSTRES_ASSETS_DIR}")
monstres_set_options(monstres_test_main)

function(monstres_add_tests name label)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE monstres_test_main)
  monstres_set_options(${name})
  if(EMSCRIPTEN)
    # Run under Node with direct access to the real file system (test data).
    target_link_options(${name} PRIVATE
      -sENVIRONMENT=node -sNODERAWFS=1 -sEXIT_RUNTIME=1 -sALLOW_MEMORY_GROWTH=1)
  endif()
  # One ctest test per source file: ctest runs them side by side (-j), and a
  # failure names its file. doctest picks the file's cases (--source-file).
  foreach(source ${ARGN})
    get_filename_component(stem "${source}" NAME_WE)
    string(REPLACE "test_" "" stem "${stem}")
    add_test(NAME ${label}_${stem} COMMAND ${name} "--source-file=*${source}")
    set_tests_properties(${label}_${stem} PROPERTIES LABELS ${label})
  endforeach()
endfunction()

monstres_add_tests(monstres_unit_tests unit
  unit/test_rng.cpp
  unit/test_math.cpp
  unit/test_world.cpp
  unit/test_variants.cpp
  unit/test_battle.cpp
  unit/test_daycycle.cpp
  unit/test_save.cpp
  unit/test_debug.cpp
  unit/test_scene.cpp
  unit/test_models.cpp
  unit/test_terrain.cpp
  unit/test_creatures.cpp
  unit/test_characters.cpp
  unit/test_village.cpp
  unit/test_relief.cpp
  unit/test_ruins.cpp
  unit/test_population.cpp
)

monstres_add_tests(monstres_functional_tests functional
  functional/test_scenarios.cpp
)

monstres_add_tests(monstres_regression_tests regression
  regression/test_replays.cpp
  regression/test_saves.cpp
)

# The model files must match what tools/make_models.py generates.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  add_test(NAME monstres_models_up_to_date
           COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/tools/make_models.py" --check)
  set_tests_properties(monstres_models_up_to_date PROPERTIES LABELS regression)
endif()
