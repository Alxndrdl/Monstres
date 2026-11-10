# Shared compile settings, applied with monstres_set_options(<target>).

function(monstres_set_options target)
  target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
  if(MONSTRES_WERROR)
    target_compile_options(${target} PRIVATE -Werror)
  endif()
  if(MONSTRES_SANITIZERS AND NOT EMSCRIPTEN)
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
  endif()
endfunction()
