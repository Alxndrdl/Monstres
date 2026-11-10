# monstres_web: the browser build. Produces build/<preset>/site/, a folder
# ready to serve: index.html + index.js + index.wasm + the PWA files.
add_executable(monstres_web main.cpp)
target_link_libraries(monstres_web PRIVATE monstres_core monstres_render monstres_render_gles3)
monstres_set_options(monstres_web)

set(SITE_DIR "${CMAKE_BINARY_DIR}/site")
set(WEB_DIR  "${PROJECT_SOURCE_DIR}/web")

set_target_properties(monstres_web PROPERTIES
  OUTPUT_NAME index
  SUFFIX ".html"
  RUNTIME_OUTPUT_DIRECTORY "${SITE_DIR}"
  LINK_DEPENDS "${WEB_DIR}/index.html;${CMAKE_CURRENT_SOURCE_DIR}/pre.js;${CMAKE_CURRENT_SOURCE_DIR}/post.js"
)

target_link_options(monstres_web PRIVATE
  -sMIN_WEBGL_VERSION=2
  -sMAX_WEBGL_VERSION=2
  -sALLOW_MEMORY_GROWTH=1
  -sENVIRONMENT=web
  -sEXPORTED_FUNCTIONS=_main,_malloc,_free
  -sEXPORTED_RUNTIME_METHODS=UTF8ToString,FS
  -lidbfs.js
  -sMINIFY_HTML=0   # Debian's emscripten package lacks html-minifier-terser
  "--shell-file=${WEB_DIR}/index.html"
  "--pre-js=${CMAKE_CURRENT_SOURCE_DIR}/pre.js"
  "--post-js=${CMAKE_CURRENT_SOURCE_DIR}/post.js"
  $<$<CONFIG:Debug>:-sASSERTIONS=1>
)

# PWA files next to the game.
add_custom_command(TARGET monstres_web POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${WEB_DIR}/sw.js" "${WEB_DIR}/manifest.webmanifest" "${SITE_DIR}/"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${WEB_DIR}/icons" "${SITE_DIR}/icons"
  VERBATIM
)
