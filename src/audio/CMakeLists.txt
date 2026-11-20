# monstres_audio: what the game sounds like, from the world (sounds.hpp):
# the events since the last look, the ambience, the sources around. No
# browser audio here (web/audio.js plays them), so it is testable natively.
add_library(monstres_audio STATIC sounds.cpp)
target_include_directories(monstres_audio PUBLIC include)
target_link_libraries(monstres_audio PUBLIC monstres_core)
monstres_set_options(monstres_audio)
